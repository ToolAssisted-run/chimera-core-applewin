# Building the AppleWin core

This builds one file, `applewin.chimeraCore`: AppleWin's Apple II emulator
as a sandboxed guest (`core.wbx`) with its declarations, which Chimera
loads. The steps are the ones `.github/workflows/chimera.yml` runs from a
fresh clone on a public Ubuntu runner. That workflow is the reference: when
this page and the workflow disagree, the workflow is right.

`<chimera>` below is a checkout of
https://github.com/ToolAssisted-run/chimera, and `<miniBox>` is its
submodule `<chimera>/extern/chimera-common-minibox`.

## Requirements

- Linux, x86-64. CI builds on GitHub's `ubuntu-latest`. The build runs on
  Linux; the package it makes is the same file on Linux and on Windows.
- For the core, the package and the core gate (the workflow's `core-gate`
  job):

      sudo apt-get update
      sudo apt-get install -y --no-install-recommends meson ninja-build build-essential python3

  No compiler version is pinned: the build uses the `gcc` and `g++` that
  `build-essential` installs. `git` and `curl` are used too (by the scripts
  and by miniBox's build); the workflow installs neither, so they have to be
  there already.
- The network, once: miniBox's C++ guest toolchain downloads the GCC source
  that matches the host `gcc` (about 84 MB) and builds libstdc++ for the
  guest from it. Nothing in this repository downloads anything.
- For the frontend gate as well (the `frontend-gate` job), what a built
  Chimera needs:

      sudo apt-get install -y --no-install-recommends \
        meson ninja-build build-essential cmake pkg-config python3 \
        mono-complete xvfb \
        libgl1-mesa-dev libx11-dev libxext-dev libasound2-dev

  and the .NET SDK 8.0. The workflow gets it from `actions/setup-dotnet@v4`
  with `dotnet-version: '8.0'`; by hand, Chimera's README gives

      curl -sSL https://dot.net/v1/dotnet-install.sh | bash -s -- --channel 8.0

## Get the sources

This repository, with its one submodule (`extern/AppleWin`, upstream
AppleWin). The workflow uses `actions/checkout@v6` with `submodules: true`,
which is:

    git clone https://github.com/ToolAssisted-run/chimera-core-applewin.git
    cd chimera-core-applewin
    git submodule update --init

A Chimera checkout, for miniBox. The workflow checks out Chimera's `main`.
To build and to run the core gate, only the miniBox submodule is needed:

    git clone https://github.com/ToolAssisted-run/chimera.git <chimera>
    git -C <chimera> submodule update --init extern/chimera-common-minibox

For the frontend gate the workflow checks Chimera out with every submodule
(`submodules: recursive`), which is `git clone --recursive`.

Where the scripts look when they are not told:

| Script | Option | Default |
| --- | --- | --- |
| `meson setup` (`meson.build`) | `-Dminibox_dir=<miniBox>` | `../chimera/extern/chimera-common-minibox`, beside this repository; an error if it is not there |
| `waterbox/setup-guest.sh` | `-m <miniBox>` or `MINIBOX_DIR` | `$HOME/chimera/extern/chimera-common-minibox` |
| `waterbox/build-package.sh` | `-r <chimera>` | `../chimera` beside this repository, then `$HOME/chimera` |
| `waterbox/build-package.sh` | `-m <miniBox>` or `MINIBOX_DIR` | `<chimera>/extern/chimera-common-minibox` |
| `waterbox/tests/run-frontend.sh` | `--chimera-root <chimera>` | `../chimera` beside this repository, then `$HOME/chimera` |

The defaults are not all the same place. Pass the paths, as the workflow
does.

## Build miniBox

The sandbox host, and the guest toolchain with C++ (AppleWin is C++):

    mb=<miniBox>
    [ -f "$mb/build/meson-linux/build.ninja" ] || meson setup "$mb/build/meson-linux" "$mb"
    meson compile -C "$mb/build/meson-linux"
    [ -f "$mb/build/meson-cpp/build.ninja" ] || meson setup "$mb/build/meson-cpp" "$mb" -Dguest_cpp=true
    meson compile -C "$mb/build/meson-cpp"

`build/meson-linux` holds the host library the sandbox driver links
(`source/host/libminiboxhost.so`). `build/meson-cpp` holds the guest sysroot
(`guest-sysroot/`, musl and libstdc++) the core is compiled against. The
workflow caches these two directories with `actions/cache@v4`; on your own
machine they simply stay where they are.

## Build the core

**Patches.** `patches/` holds the series over `extern/AppleWin`: one patch,
a hook that marks a frame in which the machine read its input (lag
detection). There is no separate step: `meson.build` runs
`waterbox/apply-patches.sh` every time it configures, and the workflow never
calls the script by itself. The script judges the series as a whole:

- a pristine submodule gets every patch (`applied: <name>`);
- a submodule that already carries the whole series is left alone
  (`already applied: all N patches`);
- anything in between is an error that names the files, and so is a series
  that does not apply to the submodule's HEAD.

Once the patches are applied, `git status` shows `extern/AppleWin` as
modified. That is the patch set; it is not committed.

**The native reference and the sandbox driver.**

    meson setup build/meson-native -Dminibox_dir=<miniBox>
    ninja -C build/meson-native

This builds three programs in `build/meson-native`:

- `run-native`: the same AppleWin sources and the same driver as the core,
  built for the host with no sandbox. It is what the gates compare the core
  against, and where debugging is done.
- `run-wbx`: runs `core.wbx` through the miniBox host, with the same
  schedule and the same digests as `run-native`.
- `test-fileapi`: the unit test of the disk-image write overlay.

**The guest.**

    MINIBOX_DIR=<miniBox> sh waterbox/setup-guest.sh -- -Dminibox_dir=<miniBox>
    ninja -C build/meson-guest

`waterbox/setup-guest.sh [-m <miniBox dir>]` writes the meson cross file
`build/guest-cross.ini` (machine-local paths, not committed) and configures
`build/meson-guest`. Arguments after the options go to `meson setup`. It
stops if `<miniBox>/build/meson-cpp` has no guest sysroot. The result is
`build/meson-guest/core.wbx`.

## Build the package

    ./waterbox/build-package.sh -m <miniBox> -r <chimera>

Usage: `./build-package.sh [-m <miniBox dir>] [-r <chimera root>]`. There is
no output-directory option. The script:

1. configures the guest build if `build/meson-guest` is not configured, and
   runs `ninja -C build/meson-guest core.wbx`;
2. runs miniBox's `source/guest/check-wbx.sh` on `core.wbx` and stops if the
   guest is not sandbox-clean;
3. stages `core.wbx`, `waterbox.config`, `default_keybinds.json`,
   `file_slots.json`, a `licenses/` folder (the licence texts
   `waterbox/package-licenses.json` names) and a `build.json` (what built
   it) in `build/package-staging`;
4. stamps the version into the staged `waterbox.config`;
5. writes `<chimera>/build/Cores/applewin.chimeraCore`, packs it a second
   time and stops if the two are not byte-identical. It prints
   `package sha1 <hash>` and `packaged -> <path>`;
6. removes any `<chimera>/build/CoreCache/applewin-*` directory.

It needs a built miniBox. It does not need a built Chimera.

**The version** is the commit. CI passes `CORE_VERSION` (the commit SHA).
Without it the script stamps `<commit>+local`, where `<commit>` is twelve
characters, or `<commit>-dirty+local` when `git diff --quiet HEAD` reports a
change. The applied patch set counts as a change (the submodule reads as
modified), so a hand build normally says `-dirty`. `versionDate` is the
commit's date in UTC, never the build's. A hand-built package is for
testing: Chimera's publish step refuses a version with `+local` or `-dirty`.

CI publishes what passed both jobs: a rolling `dev` release on every green
push to `main`, and a dated `nightly-YYYY-MM-DD` release from the scheduled
run (cron `0 4 * * *`), only when `main` moved since the last one. Nothing
is published from a pull request. The asset is named
`applewin-<version>.chimeraCore`.

## Install it into Chimera

Chimera ships no cores and downloads none: it has no network code. A core
gets there as a file.

- **A Chimera source checkout.** The cores folder is `<chimera>/build/Cores/`
  and `build-package.sh -r <chimera>` has already written the package there.
  Start Chimera (`<chimera>/build/ChimeraMono.sh` on Linux).
- **A release bundle.** Copy `applewin.chimeraCore` into the `Cores` folder
  beside `Chimera.exe`, or into the folder chosen in File > Core Manager >
  Change folder... The same file serves a Linux and a Windows Chimera.

File > Core Manager lists the packages in that folder; Refresh List rescans
it. A hand-built version reads as its commit followed by `local`.

To use a published build instead, download `applewin-<version>.chimeraCore`
from https://github.com/ToolAssisted-run/chimera-core-applewin/releases and
put it in the same folder.

## Run the gates

Both scripts are bash. Run them as written, not through `sh`.

### The core gate

    ./waterbox/run-gate.sh

Usage: `./run-gate.sh [-n <native build dir>] [-g <guest build dir>]`
(defaults `build/meson-native` and `build/meson-guest`). It needs
`run-native`, `run-wbx` and `core.wbx` built, and `python3`. It needs no
file from you: the machine's ROMs are taken from the submodule
(`extern/AppleWin/resource`), where AppleWin keeps them.

What it proves, with nothing provided:

- `wire:config==driver`: the buttons and axes `waterbox.config` declares
  are the driver's, in the same order.
- `fileapi:overlay`: the disk-image write overlay reads back what was
  written.
- Five machines booted from ROM alone (`basic`, `basicExercise`,
  `apple2plus`, `pal`, `noMockingboard`). For each: the native reference
  and the sandbox give identical video, audio, lag, cycle-count and
  memory-domain digests; an idle run of the same length differs (the input
  reached the machine); turbo is the same machine and the same second-half
  pictures; a savestate round-trip around every frame changes nothing; a
  new host finishes the run from a state.
- `basic:program-ran`: a typed Applesoft program printed its output on the
  text page.
- `settings:reach-the-guest`: 60 Hz and 50 Hz machines report their own
  rates and cycle counts, and a ][+ is another machine.

That is 29 checks. A sixth machine, `disk`, joins the table only when
`tests/roms-local/disk1.dsk` exists: 34 checks. `disk2.dsk`, if it is
there, goes in the same drive, and the run swaps disks at frame 900.
Without `disk1.dsk` nothing is printed for that machine. `tests/roms-local`
is gitignored. The last line is `<n> ok, <n> failed`; the exit status is
non-zero on any failure.

### The frontend gate

    ./waterbox/tests/run-frontend.sh --chimera-root <chimera>

Usage: `./run-frontend.sh [--chimera-root <path>] [--frames N]` (300 frames
by default). It runs the installed package inside Chimera, headless, under
Mono.

It needs:

- `tests/roms-local/disk1.dsk`, a disk image of your own. Without it the
  script says there is nothing for the frontend to open, that it is
  skipping, and exits 0. That is a skip, not a pass.
- `tests/roms-local/disk2.dsk` for the project leg.
- A built Chimera at `<chimera>` (`build/Chimera.exe`), built as the
  workflow builds it:

      cd <chimera>
      meson setup build/meson-linux --prefix "$PWD/build" --libdir dll
      meson compile -C build/meson-linux
      meson install -C build/meson-linux
      dotnet build source/gui/Chimera.sln -c Release /nodeReuse:false -p:UseSharedCompilation=false

- The package at `<chimera>/build/Cores/applewin.chimeraCore`.
- `build/meson-native/run-native` (the workflow builds only that target in
  this job: `ninja -C build/meson-native run-native`).
- `mono`, `python3`, and `Xvfb` when `DISPLAY` is not set: the script then
  starts its own display. With `DISPLAY` set it uses that one.

What it proves: Chimera builds the same Main RAM as the native reference
from a bare disk image (`disk:frontend`); `model=apple2plus` reaches the
guest through the frontend's config and is another machine
(`settings:model`); a hand-written `.chimeraProject` with two disks in
drive 1 opens and matches (`project:frontend`); the package's default
keybinds become the frontend's (`keybinds`). It writes its work files to
`waterbox/tests/work/`.

On the public runner there is no disk image, so this gate reports itself
skipped there. The package is published on the core gate and on a package
build that installs into Chimera.

## Files the core needs at run time

None of these is in the repository or in the package. The user provides
them.

**The machine's ROMs, as project firmware.** `waterbox.config` declares
twenty files, each under the name AppleWin gives it and pinned by size and
SHA-1. AppleWin's source tree keeps them under `resource/`. Which ones a
project needs follows its settings:

| Firmware | Needed when |
| --- | --- |
| `Apple2e_Enhanced.rom`, `Apple2e.rom`, `Apple2_Plus.rom`, `Apple2.rom`, `Apple2_JPlus.rom`, `PRAVETS82.ROM`, `PRAVETS8M.ROM`, `PRAVETS8C.ROM`, `TK3000e.rom`, `Base64A.rom` | the Apple II Model setting names that machine (one ROM per model) |
| `Apple2e_Enhanced_Video.rom` | model is the enhanced //e, the //e or the TK3000 |
| `Apple2_Video.rom` | model is the ][ or the ][+ |
| `Apple2_JPlus_Video.rom` | model is the j-plus |
| `Base64A_German_Video.rom` | model is the Base 64A |
| `CHARSET82.bmp`, `CHARSET8M.bmp`, `CHARSET8C.bmp` | model is the Pravets 82, 8M or 8A |
| `DISK2.rom` | always |
| `SSC.rom` | the Serial card setting keeps the card in slot 2 (the default) |
| `Parallel.rom` | the Printer card setting keeps the card in slot 1 (the default) |

The default machine, an enhanced //e with its default cards, therefore asks
for five: `Apple2e_Enhanced.rom`, `Apple2e_Enhanced_Video.rom`,
`DISK2.rom`, `SSC.rom` and `Parallel.rom`. The package embeds only
AppleWin's own hard-disk controller firmware.

**Disk images**, in the slots `waterbox/file_slots.json` declares:

| Slot | Files | How many |
| --- | --- | --- |
| Disk II drive 1 | `.dsk` `.do` `.po` `.nib` `.woz` `.2mg` | any number; the order is the swap order |
| Disk II drive 2 | the same | any number |
| Hard disk | `.hdv` `.2mg` `.po` | up to two |

Images are read-only. What the machine writes goes to an overlay that
savestates carry.

## Troubleshooting

- **`pass -Dminibox_dir=<miniBox checkout>`** from `meson setup`: there is
  no Chimera checkout at `../chimera`. Pass the option.
- **`miniBox C++ guest toolchain missing under <miniBox>/build/meson-cpp`**
  from `setup-guest.sh`: miniBox was built without `-Dguest_cpp=true`. The
  message gives the command.
- **`chimera checkout not found; pass -r <path>`** from `build-package.sh`:
  pass `-r`.
- **`extern/AppleWin is not checked out`**: the submodule is empty. The
  message gives the command.
- **`extern/AppleWin is partly patched`**: some file the series touches is
  neither pristine nor as the whole series leaves it. The message names the
  files and gives the command that starts again from the submodule's HEAD.
  That command discards edits made in the tree: turn them into a patch
  first.
- **`the series does not apply to the submodule's HEAD at <patch>`**: the
  submodule was moved without rebasing the patches.
- **`native build missing` / `guest build missing`** from `run-gate.sh`:
  the message gives the build command.
- **`Syntax error: "(" unexpected`**: a gate script was run through `sh`.
  Run it directly.
- **The frontend gate fails with `project:frontend SKIP`**: `disk1.dsk` is
  staged and `disk2.dsk` is not. The script counts every result that is not
  PASS as failed. Stage both disks.
- **A moved miniBox.** `build/guest-cross.ini` holds absolute paths. Run
  `waterbox/setup-guest.sh` again after miniBox moves.
- **Arrow keys and the joystick.** On a //e, Up is Ctrl-K and Down is
  Ctrl-J. Bind the PC arrows to the //e's keys or to the joystick axes, not
  both: see `README.md`.
