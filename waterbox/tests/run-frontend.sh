#!/bin/bash
# The frontend half of the gate: load the AppleWin package in Chimera (under
# Mono, on a private Xvfb display), run the machine for a fixed number of
# frames with nothing pressed, and require its Main RAM - all 64K - to be
# byte-identical to the native reference. Then prove a machine-shaping
# setting arrives (model=apple2plus builds a ][+ that still matches ITS
# native reference), and that the package's keybinds become the frontend's
# defaults.
#
# Those three need no disk: every slot declares min 0, so a project with
# nothing in its drives is a machine Chimera will open, and the ROMs come from
# the submodule. They are what a public runner runs. With a disk staged in
# tests/roms-local/disk1.dsk (commercial, gitignored) the same claims are made
# again over a booted disk opened as a bare image, and with disk2.dsk beside
# it a project with two disks in one drive; without them those legs say SKIP,
# and a SKIP is not a failure.
#
# Usage: ./run-frontend.sh [--chimera-root <path>] [--frames N] [--no-disk]
#   --no-disk   run as a public runner does, whatever is in tests/roms-local
set -u

here="$(cd "$(dirname "$0")" && pwd)"
wb="$(cd "$here/.." && pwd)"
root="$(cd "$wb/.." && pwd)"
frames=300
chimera_root=""
no_disk=0
while [ $# -gt 0 ]; do
	case "$1" in
		--chimera-root) chimera_root="$2"; shift ;;
		--frames) frames="$2"; shift ;;
		--no-disk) no_disk=1 ;;
		-*) echo "unknown option: $1" >&2; exit 2 ;;
		*) break ;;
	esac
	shift
done

if [ -z "$chimera_root" ]; then
	for candidate in "$root/../chimera" "$HOME/chimera"; do
		[ -d "$candidate" ] && { chimera_root="$candidate"; break; }
	done
fi
[ -n "$chimera_root" ] && [ -d "$chimera_root" ] || {
	echo "chimera checkout not found; pass --chimera-root <path>" >&2; exit 1; }
chimera_root="$(cd "$chimera_root" && pwd)"

emu_exe="$chimera_root/build/Chimera.exe"
package="$chimera_root/build/Cores/applewin.chimeraCore"
rn="$root/build/meson-native/run-native"
# the ROMs a project brings as firmware, from the submodule where AppleWin
# keeps them (the package carries none); the frontend takes them by id
roms="$root/extern/AppleWin/resource"
roms_into() { cp "$roms"/*.rom "$roms"/*.ROM "$roms"/CHARSET82.bmp "$roms"/CHARSET8M.bmp "$roms"/CHARSET8C.bmp "$1/"; }
firmware_args=()
for f in "$roms"/*.rom "$roms"/*.ROM "$roms"/CHARSET82.bmp "$roms"/CHARSET8M.bmp "$roms"/CHARSET8C.bmp; do
	firmware_args+=("--firmware=$(basename "$f")=$f")
done
rom="$root/tests/roms-local/disk1.dsk"
have_disk=0
[ "$no_disk" = 0 ] && [ -f "$rom" ] && have_disk=1
[ -f "$emu_exe" ] || { echo "Chimera not built: $emu_exe" >&2; exit 1; }
[ -f "$package" ] || { echo "package not installed: $package (run ../build-package.sh)" >&2; exit 1; }
[ -x "$rn" ] || { echo "native reference not built" >&2; exit 1; }

work="$here/work"
mkdir -p "$work"

export LD_LIBRARY_PATH="$chimera_root/build/dll:$chimera_root/build:/usr/lib/x86_64-linux-gnu"
export MONO_CRASH_NOFILE=1 MONO_WINFORMS_XIM_STYLE=disabled ALSOFT_DRIVERS=null
xvfb_pid=""
cleanup() { [ -n "$xvfb_pid" ] && kill "$xvfb_pid" 2>/dev/null; }
trap cleanup EXIT
if [ -z "${DISPLAY:-}" ]; then
	command -v Xvfb >/dev/null || { echo "Xvfb not found (apt install xvfb)" >&2; exit 1; }
	for n in 90 91 92 93 94 95 96; do
		if [ ! -e "/tmp/.X11-unix/X$n" ]; then
			Xvfb ":$n" -screen 0 640x480x24 -nolisten tcp & xvfb_pid=$!
			export DISPLAY=":$n"; break
		fi
	done
	sleep 1
fi

config="$work/config.ini"
if [ ! -f "$config" ]; then
	( cd "$chimera_root" && timeout 120 mono "$emu_exe" --headless "--config=$config" \
		"--lua=$here/exit.lua" ) > "$work/bootstrap.log" 2>&1
	[ -f "$config" ] || { echo "config bootstrap failed (see $work/bootstrap.log)" >&2; exit 1; }
fi
sed -i 's/"DispMethod": [0-9]/"DispMethod": 1/' "$config"

ok=0
failed=0
skipped=0
report() { printf "%-28s %-9s %s\n" "$1" "$2" "$3"; case "$2" in PASS) ok=$((ok+1)) ;; SKIP) skipped=$((skipped+1)) ;; *) failed=$((failed+1)) ;; esac; }
printf "%-28s %-9s %s\n" "Check" "Result" "Detail"
printf "%-28s %-9s %s\n" "-----" "------" "------"

run_frontend() {
	local tag="$1" cfg="$2" nframes="$3" shot="${4:-}" pkg="${5:-$package}" therom="${6:-$rom}"
	local job="$work/job.$tag.txt"
	{
		echo "frames=$nframes"
		echo "out=$work/$tag.ram.bin"
		echo "meta=$work/$tag.meta.txt"
		echo "shot=$shot"
	} > "$job"
	rm -f "$work/$tag.ram.bin" "$work/$tag.meta.txt"
	[ -n "$shot" ] && rm -f "$shot"
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 mono "$emu_exe" --headless \
		"--config=$cfg" "--core=$pkg" "${firmware_args[@]}" \
		"--lua=$here/frontend-ram.lua" "$therom" ) > "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}

# the same through a hand-written project: $4 onward goes to make-project.py
# (settings and slot files; none at all is the machine with empty drives)
run_project() {
	local tag="$1" cfg="$2" nframes="$3"; shift 3
	local job="$work/job.$tag.txt"
	printf 'frames=%s\nout=%s/%s.ram.bin\nmeta=%s/%s.meta.txt\nshot=\n' "$nframes" "$work" "$tag" "$work" "$tag" > "$job"
	rm -f "$work/$tag.ram.bin" "$work/$tag.meta.txt"
	python3 "$here/make-project.py" "$package" "$work/$tag.chimeraProject" "$nframes" "$@" > "$work/$tag.log" 2>&1 || return 1
	( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 mono "$emu_exe" --headless \
		"--config=$cfg" "--project=$work/$tag.chimeraProject" "${firmware_args[@]}" \
		"--lua=$here/frontend-ram.lua" ) >> "$work/$tag.log" 2>&1
	[ -f "$work/$tag.meta.txt" ] && grep -q "^status=OK" "$work/$tag.meta.txt"
}

# a native reference run: same idle schedule, settings JSON in $2; "disk" in
# $3 puts the disk in, as the frontend does when it opens a bare image
native_ram() {
	local tag="$1" settings="$2" media="${3:-}"
	local wd="$work/native.$tag"
	rm -rf "$wd"
	mkdir -p "$wd"
	# the frontend opens a bare image: it arrives as the plain "rom" mount
	[ "$media" = disk ] && cp "$rom" "$wd/rom"
	roms_into "$wd"
	[ -n "$settings" ] && printf '%s' "$settings" > "$wd/settings"
	"$rn" "$wd" --frames "$frames" --dump-domain "Main RAM" "$work/native.$tag.ram.bin" \
		> "$work/native.$tag.txt" 2>&1
}

settings_config() { python3 "$here/settings-config.py" "$config" "$1" "$2"; }

settings_config "$work/config.base.ini" '{}'

# --- the machine the frontend builds must be the one the gate signed off on ---
# Nothing in the drives: the ROM waits for a disk that is not coming, and
# sixty-four kilobytes of that wait are compared.
if ! native_ram "rom" ""; then
	report "rom:frontend" FAIL "native runner error (see tests/work/native.rom.txt)"
elif ! run_project "rom" "$work/config.base.ini" "$frames"; then
	report "rom:frontend" FAIL "no OK meta (see tests/work/rom.log)"
elif cmp -s "$work/native.rom.ram.bin" "$work/rom.ram.bin"; then
	report "rom:frontend" PASS "$frames frames with empty drives, Main RAM identical to the native reference"
else
	report "rom:frontend" FAIL "Main RAM differs from the native reference"
fi

# --- a machine-shaping setting must reach the guest through a project ---
if ! native_ram "romplus" '{"model":"apple2plus"}'; then
	report "rom:settings" FAIL "native runner error (see tests/work/native.romplus.txt)"
elif ! run_project "romplus" "$work/config.base.ini" "$frames" --set=model=apple2plus; then
	report "rom:settings" FAIL "run did not report OK (see tests/work/romplus.log)"
elif ! cmp -s "$work/native.romplus.ram.bin" "$work/romplus.ram.bin"; then
	report "rom:settings" FAIL "][+ Main RAM differs from its native reference"
elif cmp -s "$work/native.romplus.ram.bin" "$work/native.rom.ram.bin"; then
	report "rom:settings" FAIL "a ][+ built the same RAM as a //e"
else
	report "rom:settings" PASS "a project's model=apple2plus matches its native reference and is another machine than the //e"
fi

# --- the same two claims over a booted disk, opened as a bare image ---
if [ "$have_disk" = 1 ]; then
	if ! native_ram "base" "" disk; then
		report "disk:frontend" FAIL "native runner error (see tests/work/native.base.txt)"
	elif ! run_frontend "base" "$work/config.base.ini" "$frames" "$work/base.png"; then
		report "disk:frontend" FAIL "no OK meta (see tests/work/base.log)"
	elif cmp -s "$work/native.base.ram.bin" "$work/base.ram.bin"; then
		report "disk:frontend" PASS "$frames frames, Main RAM identical to the native reference"
	else
		report "disk:frontend" FAIL "Main RAM differs from the native reference"
	fi

	# --- a machine-shaping setting must reach the guest through the frontend ---
	settings_config "$work/config.plus.ini" '{"model": "apple2plus"}'
	if ! native_ram "plus" '{"model":"apple2plus"}' disk; then
		report "settings:model" FAIL "native runner error (see tests/work/native.plus.txt)"
	elif ! run_frontend "plus" "$work/config.plus.ini" "$frames"; then
		report "settings:model" FAIL "run did not report OK (see tests/work/plus.log)"
	elif ! cmp -s "$work/native.plus.ram.bin" "$work/plus.ram.bin"; then
		report "settings:model" FAIL "][+ Main RAM differs from its native reference"
	elif cmp -s "$work/native.plus.ram.bin" "$work/native.base.ram.bin"; then
		report "settings:model" FAIL "a ][+ built the same RAM as a //e"
	else
		report "settings:model" PASS "model=apple2plus matches its native reference and is another machine than the //e"
	fi

	# --- a PROJECT: the disks arrive through the slots file, in swap order ---
	disk2="$root/tests/roms-local/disk2.dsk"
	if [ -f "$disk2" ]; then
		pframes=$frames
		wd="$work/native.project"
		rm -rf "$wd"; mkdir -p "$wd"
		cp "$rom" "$wd/disk1.dsk"; cp "$disk2" "$wd/disk2.dsk"; roms_into "$wd"
		printf '{"floppy1":["disk1.dsk","disk2.dsk"]}' > "$wd/slots"
		"$rn" "$wd" --frames "$pframes" --dump-domain "Main RAM" "$work/native.project.ram.bin" > "$work/native.project.txt" 2>&1
		# the project resolves its files beside itself
		ln -sf "$rom" "$work/disk1.dsk"; ln -sf "$disk2" "$work/disk2.dsk"
		python3 "$here/make-project.py" "$package" "$work/gate.chimeraProject" "$pframes" "floppy1=$rom" "floppy1=$disk2"
		job="$work/job.project.txt"
		printf 'frames=%s\nout=%s/project.ram.bin\nmeta=%s/project.meta.txt\nshot=%s/project.png\n' "$pframes" "$work" "$work" "$work" > "$job"
		rm -f "$work/project.ram.bin" "$work/project.meta.txt"
		( cd "$chimera_root" && MINIHAWK_JOB="$job" timeout 900 mono "$emu_exe" --headless \
			"--config=$work/config.base.ini" "--project=$work/gate.chimeraProject" "${firmware_args[@]}" \
			"--lua=$here/frontend-ram.lua" ) > "$work/project.log" 2>&1
		if [ ! -f "$work/project.meta.txt" ] || ! grep -q "^status=OK" "$work/project.meta.txt"; then
			report "project:frontend" FAIL "no OK meta (see tests/work/project.log)"
		elif cmp -s "$work/native.project.ram.bin" "$work/project.ram.bin"; then
			report "project:frontend" PASS "a hand-written project with two disks in drive 1: Main RAM identical to the native reference"
		else
			report "project:frontend" FAIL "Main RAM differs from the native reference"
		fi
	else
		report "project:frontend" SKIP "needs tests/roms-local/disk2.dsk"
	fi
else
	for leg in disk:frontend settings:model project:frontend; do
		report "$leg" SKIP "needs tests/roms-local/disk1.dsk"
	done
fi

# --- the bindings the package ships must become the frontend's defaults ---
python3 "$here/forget-controller.py" "$work/config.base.ini" "$work/config.keys.ini" "Apple II Keyboard and Joysticks"
if run_project "keys" "$work/config.keys.ini" 1; then
	if python3 "$here/check-keybinds.py" "$work/config.keys.ini" \
		"$wb/default_keybinds.json" "Apple II Keyboard and Joysticks" > "$work/keys.txt" 2>&1; then
		report "keybinds" PASS "$(cat "$work/keys.txt")"
	else
		report "keybinds" FAIL "$(head -1 "$work/keys.txt")"
	fi
else
	report "keybinds" FAIL "run did not report OK (see tests/work/keys.log)"
fi

echo
echo "$ok ok, $failed failed, $skipped skipped"
[ "$failed" -eq 0 ] && [ "$ok" -gt 0 ]
