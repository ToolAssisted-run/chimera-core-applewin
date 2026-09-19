#!/bin/bash
# The core-level equivalence gate: for every test the sandboxed core must
# produce byte-identical video, audio, lag, cycle-count and memory-domain
# digests to the native reference build (the same driver compiled natively),
# survive a whole-machine savestate round-trip around every frame, and finish
# a run in a NEW host after a savestate (the reopened-project case).
#
# The machine needs no disk to be proved: the ROM boots to Applesoft, and a
# typed program is a program. Disk images are commercial and belong in
# tests/roms-local (gitignored); when one named there is present the gate
# boots it too.
#
# Usage: ./run-gate.sh [-n <native build dir>] [-g <guest build dir>]
set -u

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
nat="$root/build/meson-native"
gst="$root/build/meson-guest"
while getopts "n:g:" opt; do
	case "$opt" in
		n) nat="$OPTARG" ;;
		g) gst="$OPTARG" ;;
		*) exit 2 ;;
	esac
done

[ -x "$nat/run-native" ] && [ -x "$nat/run-wbx" ] || {
	echo "native build missing: meson setup build/meson-native && ninja -C build/meson-native" >&2; exit 1; }
[ -f "$gst/core.wbx" ] || {
	echo "guest build missing: sh waterbox/setup-guest.sh && ninja -C build/meson-guest" >&2; exit 1; }

work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT

# The package carries none of Apple's ROMs (nor the clones', nor the card
# firmware): a project brings them as firmware, mounted under the file names
# AppleWin gives them. The gate takes them from the submodule, where AppleWin
# keeps them, so it runs anywhere the checkout does.
roms="$root/extern/AppleWin/resource"
roms_into() {
	cp "$roms"/*.rom "$roms"/*.ROM "$roms"/CHARSET82.bmp "$roms"/CHARSET8M.bmp "$roms"/CHARSET8C.bmp "$1/"
}
digests() { grep -E '^(frames|vsync|videoHash|audioHash|lagFrames|cycles|domain\[)'; }
# What a turbo run can be held to: everything except the whole-run video hash,
# which a run that skipped the first half cannot possibly match - the second
# half it did draw is compared instead.
turboDigests() { grep -E '^(frames|vsync|tailVideoHash|audioHash|lagFrames|cycles|domain\[)'; }

ok=0
failed=0
report() { printf "%-32s %-6s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-32s %-6s %s\n" "Check" "Result" "Detail"
printf "%-32s %-6s %s\n" "-----" "------" "------"

# the wire the driver declares must be the wire the package declares
if python3 "$here/tests/check-wire.py" "$root" >"$work/wire.txt" 2>&1; then
	report "wire:config==driver" PASS "$(cat "$work/wire.txt")"
else
	report "wire:config==driver" FAIL "$(head -1 "$work/wire.txt")"
fi

# the disk-image write overlay, on its own
if [ -x "$nat/test-fileapi" ] && "$nat/test-fileapi" "$work/fileapi.base" >"$work/fileapi.txt" 2>&1; then
	report "fileapi:overlay" PASS "$(cat "$work/fileapi.txt")"
else
	report "fileapi:overlay" FAIL "$(head -1 "$work/fileapi.txt" 2>/dev/null)"
fi

# name frames settings slots(json) extra-args...
# "basic": the ROM alone; Reset out of the disk-boot wait, then a program
# typed at the prompt, which the text-page check reads back.
tests=(
	"basic 420 {} {} --reset-at 60 --type-at 100 --type-every 3 --type \$'10 PRINT 1+1\\n20 GOTO 10\\nRUN\\n'"
	"basicExercise 600 {} {} --reset-at 60 --exercise"
	"apple2plus 300 {\"model\":\"apple2plus\"} {} --reset-at 60 --exercise"
	"pal 300 {\"refreshRate\":\"50\"} {} --reset-at 60 --exercise"
	"noMockingboard 300 {\"mockingboard\":\"none\",\"joystick2\":true} {} --reset-at 60 --exercise"
)
# a commercial disk, when the user has staged one
if [ -f "$root/tests/roms-local/disk1.dsk" ]; then
	slots='{"floppy1":["disk1.dsk"]}'
	[ -f "$root/tests/roms-local/disk2.dsk" ] && slots='{"floppy1":["disk1.dsk","disk2.dsk"]}'
	tests+=("disk 1500 {} $slots --exercise --swap-disk-at 900")
fi

for t in "${tests[@]}"; do
	read -r name frames settings slots extra <<< "$t"
	# the extra arguments are shell-quoted in the table (a typed text has spaces)
	eval "args=($extra)"

	wd="$work/$name"
	mkdir -p "$wd"
	roms_into "$wd"
	[ -d "$root/tests/roms-local" ] && cp "$root"/tests/roms-local/*.dsk "$wd/" 2>/dev/null
	printf '%s' "$slots" > "$wd/slots"
	printf '%s' "$settings" > "$wd/settings"
	args+=(--frames "$frames")

	if ! "$nat/run-native" "$wd" "${args[@]}" --text "$wd/native.txt" 2>"$work/nat.err" | digests > "$work/nat.txt"; then
		report "$name:equivalence" FAIL "native runner error: $(head -1 "$work/nat.err")"; continue
	fi
	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>"$work/box.err" | digests > "$work/box.txt"; then
		report "$name:equivalence" FAIL "waterbox runner error: $(head -1 "$work/box.err")"; continue
	fi
	nframes="$(sed -n 's/^frames=//p' "$work/box.txt")"
	if cmp -s "$work/nat.txt" "$work/box.txt"; then
		report "$name:equivalence" PASS "$nframes frames, native == waterboxed"
	else
		report "$name:equivalence" FAIL "$(diff "$work/nat.txt" "$work/box.txt" | tr '\n' ' ' | head -c 120)"
		continue
	fi

	# the ROM-only program: the screen must say what a running Applesoft
	# program says (the text page, read from the Main RAM domain)
	if [ "$name" = "basic" ]; then
		twos="$(grep -c '^2 *$' "$wd/native.txt")"
		if [ "$twos" -ge 20 ]; then
			report "$name:program-ran" PASS "the typed program had scrolled $twos lines of 2 up the screen"
		else
			report "$name:program-ran" FAIL "text page: $(tr -s ' \n' ' ' < "$wd/native.txt" | head -c 100)"
		fi
	fi

	# a hollow pass cannot sneak through: the input schedule must have shaped
	# the machine - an idle run of the same length must differ
	"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames "$nframes" 2>/dev/null | digests > "$work/idle.txt"
	if cmp -s "$work/box.txt" "$work/idle.txt"; then
		report "$name:input-shaped" FAIL "the input changed nothing"
	else
		report "$name:input-shaped" PASS "input visibly shaped the machine"
	fi

	# Turbo: the core's picture switched off for the first half of the run and
	# back on for the second. The machine, the sound, the lag count and every
	# picture of that second half must be what they would have been.
	"$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" 2>/dev/null | turboDigests > "$work/tnorm.txt"
	if "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" --turbo 2>/dev/null | turboDigests > "$work/turbo.txt"; then
		if cmp -s "$work/tnorm.txt" "$work/turbo.txt"; then
			report "$name:turbo" PASS "half the frames undrawn, same machine and same pictures"
		else
			report "$name:turbo" FAIL "$(diff "$work/tnorm.txt" "$work/turbo.txt" | tr '\n' ' ' | head -c 120)"
		fi
	else
		report "$name:turbo" FAIL "turbo runner error"
	fi

	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" --rerecord 2>/dev/null | digests > "$work/rr.txt"; then
		report "$name:savestate" FAIL "rerecord runner error"; continue
	fi
	if cmp -s "$work/box.txt" "$work/rr.txt"; then
		report "$name:savestate" PASS "per-frame round-trip is lossless"
	else
		report "$name:savestate" FAIL "$(diff "$work/box.txt" "$work/rr.txt" | tr '\n' ' ' | head -c 120)"
	fi

	if ! "$nat/run-wbx" "$gst/core.wbx" "$wd" "${args[@]}" --session 2>"$work/ss.err" | digests > "$work/ss.txt"; then
		report "$name:session" FAIL "session runner error: $(head -1 "$work/ss.err")"; continue
	fi
	if cmp -s "$work/box.txt" "$work/ss.txt"; then
		report "$name:session" PASS "a new host finished the run from a state"
	else
		report "$name:session" FAIL "$(diff "$work/box.txt" "$work/ss.txt" | tr '\n' ' ' | head -c 120)"
	fi
done

# ---- the settings are machine choices and have to REACH the guest: a PAL
# machine runs at 50 Hz with more cycles per frame; a ][+ is a different ROM.
ntscRate="$(sed -n 's/^vsync=//p' "$work/basicExercise/../box.txt" 2>/dev/null)"
wd="$work/rates"; mkdir -p "$wd"; printf '{}' > "$wd/slots"; roms_into "$wd"
printf '{"refreshRate":"60"}' > "$wd/settings"
"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 120 --reset-at 60 2>/dev/null | digests > "$work/ntsc.txt"
printf '{"refreshRate":"50"}' > "$wd/settings"
"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 120 --reset-at 60 2>/dev/null | digests > "$work/pal.txt"
printf '{"model":"apple2plus"}' > "$wd/settings"
"$nat/run-wbx" "$gst/core.wbx" "$wd" --frames 120 --reset-at 60 2>/dev/null | digests > "$work/plus.txt"
ntscRate="$(sed -n 's/^vsync=//p' "$work/ntsc.txt")"
palRate="$(sed -n 's/^vsync=//p' "$work/pal.txt")"
ntscCycles="$(sed -n 's/^cycles=//p' "$work/ntsc.txt")"
palCycles="$(sed -n 's/^cycles=//p' "$work/pal.txt")"
if [ "$ntscRate" = "1640625/27379" ] && [ "$palRate" = "2375075/47424" ] && [ "$palCycles" -gt "$ntscCycles" ] && ! cmp -s "$work/ntsc.txt" "$work/plus.txt"; then
	report "settings:reach-the-guest" PASS "NTSC $ntscRate ($ntscCycles cycles), PAL $palRate ($palCycles), and a ][+ is another machine"
else
	report "settings:reach-the-guest" FAIL "NTSC=$ntscRate/$ntscCycles PAL=$palRate/$palCycles"
fi

echo
echo "$ok ok, $failed failed"
[ "$failed" -eq 0 ]
