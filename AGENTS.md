# AGENTS.md - AppleWin core for Chimera

This repository builds AppleWin's Apple II emulator as a core for Chimera
(https://github.com/ToolAssisted-run/chimera), a frontend for tool-assisted
speedruns. It produces one file, `applewin.chimeraCore`: the emulator as a
sandboxed guest (`core.wbx`) plus the declarations Chimera reads. Upstream
AppleWin is a submodule; everything else here is the driver, the Win32
stand-ins, the build and the gates.

`<chimera>` is a Chimera checkout and `<miniBox>` is its submodule
`<chimera>/extern/chimera-common-minibox`.

## Layout

- `extern/AppleWin/` - upstream AppleWin, a pinned git submodule.
- `patches/` - the numbered patch series over the submodule (one patch: the
  input-read hook for lag detection).
- `meson.build` - both builds: the native reference and, as a cross build,
  the guest. It applies the patches when it configures.
- `waterbox/applewin-driver.{cpp,h}` - the machine, stepped one video frame
  at a time. Compiled into both builds.
- `waterbox/wbx-entry.cpp` - the guest ABI over the driver.
- `waterbox/libwindows/` - the `windows.h` AppleWin compiles against off
  Windows; clocks, files, keyboard and joystick answer from the sandbox.
- `waterbox/shims/` - whole-file replacements for the Win32-only upstream
  files.
- `waterbox/gen-resources.py` - embeds AppleWin's own hard-disk firmware.
- `waterbox/run-native.c`, `run-wbx.c`, `gate-harness.h` - the two gate
  drivers and the schedule they share.
- `waterbox/waterbox.config` - what Chimera is told: settings, inputs,
  firmware. `file_slots.json` - the disk slots. `default_keybinds.json`.
- `waterbox/package-licenses.json` - the licences that travel in the package.
- `waterbox/setup-guest.sh`, `build-package.sh`, `apply-patches.sh` - build.
- `waterbox/run-gate.sh`, `waterbox/tests/run-frontend.sh` - the gates.
- `tests/roms-local/` - your own disk images for the gates. Gitignored.
- `docs/PLAN.md` - decisions, milestones, sharp edges.

## Set up the build environment

    sudo apt-get update
    sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3

    git submodule update --init
    git clone https://github.com/ToolAssisted-run/chimera.git <chimera>
    git -C <chimera> submodule update --init extern/chimera-common-minibox

    mb=<miniBox>
    [ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
    meson compile -C "$mb/build/meson-linux"
    [ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
    meson compile -C "$mb/build/meson-cpp"

The `meson-cpp` build downloads the GCC source matching the host `gcc`,
once. If a Chimera checkout already exists, use it and skip the clone.

## Build

    meson setup build/meson-native -Dminibox_dir=<miniBox>
    ninja -C build/meson-native
    ./waterbox/build-package.sh -m <miniBox> -r <chimera>

The first two lines build the native reference (`run-native`) and the
sandbox driver (`run-wbx`). The third configures and builds the guest
(`build/meson-guest/core.wbx`), checks it with miniBox's `check-wbx.sh` and
writes `<chimera>/build/Cores/applewin.chimeraCore`. To build the guest
without packaging:

    MINIBOX_DIR=<miniBox> sh waterbox/setup-guest.sh -- -Dminibox_dir=<miniBox>
    ninja -C build/meson-guest

A hand-built package stamps `<commit>+local` (`-dirty` with changes in the
tree). It is for testing. CI stamps the commit through `CORE_VERSION`.

## Install the core into Chimera

Chimera ships no cores and downloads none. `build-package.sh -r <chimera>`
writes the package into `<chimera>/build/Cores/`, which is the cores folder
of a source checkout. For a release bundle, copy `applewin.chimeraCore`
into the `Cores` folder beside `Chimera.exe` (or the folder set in File >
Core Manager > Change folder...). File > Core Manager lists the folder;
Refresh List rescans it. The same file works on Linux and on Windows.

## Test before you commit

    ./waterbox/run-gate.sh

It must end `<n> ok, 0 failed`. It needs no file from you: the ROMs come
from the submodule. It compares the native reference with the sandbox,
checks a savestate around every frame, a state finished in a new host,
turbo, and that settings and input reach the machine. With
`tests/roms-local/disk1.dsk` staged it also boots that disk.

The frontend gate runs the package inside a built Chimera:

    ./waterbox/tests/run-frontend.sh --chimera-root <chimera>

It needs `tests/roms-local/disk1.dsk` and `disk2.dsk`, Chimera built,
`mono` and `Xvfb`. Without `disk1.dsk` it prints that it is skipping and
exits 0: that is not a pass. A public runner has no disk image, so CI
skips it every time; it only really runs on a machine that has the disks.
`docs/BUILDING.md` has the Chimera build commands.

Both gate scripts are bash: run them directly, not through `sh`.

## Rules of this repository

- **Upstream is not edited in place.** `extern/AppleWin` is a submodule
  pinned to upstream. A change to it is a numbered patch in `patches/`.
  `meson.build` runs `waterbox/apply-patches.sh` on every configure; the
  script accepts a pristine tree or one that carries the whole series, and
  stops on anything in between. Never commit inside the submodule.
  `git status` shows it modified once the patches are applied; that is
  expected.
- **Win32 is replaced here, not patched there.** What AppleWin asks of
  Windows is answered in `waterbox/libwindows` and `waterbox/shims`. The
  patch series is one hook.
- **Determinism is the product.** The guest must not read host time, host
  randomness or anything else that differs between runs. Every clock here
  derives from the 6502's cycle count and `rand()` is one pinned generator
  seeded from a setting. A savestate must round-trip. The gate checks all
  of it; a change that breaks it is a bug.
- **Both builds compile the same driver** and the same AppleWin sources.
  Apart from the runner, only `native-shim/emulibc.h` (native) and
  `guest-syscalls.cpp` (guest) differ. That is what makes the comparison a
  proof; keep it so.
- **The wire is the button order.** `waterbox.config` and the `AwButton` /
  `AwAxis` enums in `applewin-driver.h` must list the same controls in the
  same order; `tests/check-wire.py` fails the gate otherwise.
- **Run the gate before committing.** A new check needs a negative control:
  show that it fails when the thing it checks is broken, and say so in the
  commit.
- **Never commit ROMs, disk images or firmware.** The package carries none
  of Apple's ROMs; a project brings them as firmware. Never add network
  access.
- **Shell scripts stay executable** (git mode 100755).
- **Documentation prose is plain ASCII.**
- **Commit messages.** The subject is a sentence that states what is now
  true ("The package carries no ROM but AppleWin's own; ..."), sometimes
  with a `type(scope):` prefix. The body gives the cause, the change and
  what the gates said. A fix for a reported problem cites
  `ToolAssisted-run/chimera#N`: issues are filed in the Chimera repository.
- **Do not edit `.github/workflows`** unless the task is the workflow.

## Where to read more

- `docs/BUILDING.md` - every build step, option and error message.
- `docs/PLAN.md` - why the core is built this way, and the sharp edges.
- `.github/workflows/chimera.yml` - the recipe CI runs; it is authoritative.
- `waterbox/libwindows/README.md`, `waterbox/shims/README.md`.
- In Chimera: `docs/porting-a-core.md`, `docs/gates.md`,
  `docs/core-manager.md`.
