# AppleWin as a Chimera core: plan and log

## What this is

An Apple II (the ][, ][+, //e and the enhanced //e, and AppleWin's clones)
for chimera, built from upstream `AppleWin/AppleWin` (submodule, pinned,
unmodified) with a headless driver of our own in `waterbox/`.

## The decision that shaped it (2026-09-18)

Two ways in were on the table: upstream AppleWin plus our own shims, or
audetto's Linux fork (`audetto/AppleWin`), which already builds headless on
Linux and carries a libretro core.

Reading both settled it. The fork's `source/` - the emulator itself - is
byte for byte upstream's, bar four files (`StdAfx.h`'s speech define, two
Uthernet headers, one volume default): everything that made AppleWin
portable was upstreamed years ago. Upstream's `StdAfx.h` has a `!_WIN32`
branch that includes a local `windows.h` instead of the real one;
`FrameBase` (the window), `SoundBuffer` (DirectSound), `Interface.h` and the
`Registry` calls are the seams upstream itself defines for a front end. What
the fork adds is (a) `source/linux/libwindows`, the `windows.h` that branch
expects, (b) whole-file replacements for the handful of upstream files that
are Win32 through and through, and (c) its own front ends and a `common2`
layer of speed regulation, config files and program options that a chimera
core has no use for.

So: **upstream is the submodule**, and the core takes from the fork only
its `libwindows` (vendored under `waterbox/libwindows`, GPL-2.0-or-later
like AppleWin, credited there and in `package-licenses.json`; every part of
it that touched the host - clocks, files, threads, keyboard, joystick - is
rewritten for the sandbox). The replacements for the Win32-only files are
our own, in `waterbox/shims/` (registry, SSC, debugger display, pcap), and
they are smaller than the fork's because two of the fork's replacements
(`Keyboard.cpp`, `Joystick.cpp`) turned out to be unnecessary: upstream's
own compile unmodified once `windows.h` reports the chimera keyboard through
`GetKeyState` and the chimera joysticks through `joyGetPos`. That keeps
upstream's paddle timing (GH#985, GH#1128), its joyport, and the 4Play and
SNES MAX cards for free.

The patch set to upstream is ONE hook (lag detection), see below.

## How the machine runs

`waterbox/applewin-driver.cpp` does what upstream's `WinMain`, frame window
and `ContinueExecution()` do, with the 6502 as the only clock:

- The chimera settings are written into an in-memory registry
  (`waterbox/shims/Registry.cpp`) BEFORE `LoadConfiguration()`, so upstream
  builds the project's machine as it would a user's.
- A frame is `NTSC_GetCyclesPerFrame()` cycles - 17030 at 60 Hz, 20280 at
  the PAL 50 Hz - executed in upstream's own 1 ms batches with
  `CpuExecute`, `CardManager::Update` and `SpkrUpdate`. No speed
  regulation, no full-speed mode, no speaker feedback into the cycle count.
  The frame rate is declared as the exact fraction (the 6502 clock over the
  cycles in a frame): 1640625/27379 = 59.9227 Hz for NTSC.
- Every clock upstream can see (`GetTickCount`, `timeGetTime`,
  `QueryPerformanceCounter`, `GetLocalTime`) is derived from
  `g_nCumulativeCycles`; `rand()` is one pinned generator seeded from a
  setting (`libwindows/wincompat.h`); `Sleep`, timers and threads are inert.
- Disk images are the project's read-only files; upstream's
  `CreateFile`/`ReadFile`/`WriteFile` go through `libwindows/fileapi.cpp`,
  where every written 64 KiB chunk lives in a guest-memory overlay - the
  chimera-core-dosbox-x hard disk design applied at the file API, which
  covers .dsk, .woz, .nib, .2mg and .hdv alike without touching upstream.
- Video is the NTSC renderer's own framebuffer (560x384 plus a border, a
  bottom-up DIB), cropped and flipped into BGRA. Audio is the speaker's
  ring buffer (upstream produces a sample every 23 cycles, 44369 Hz)
  resampled by a fixed-ratio linear interpolator to 44100, mixed with the
  Mockingboard's 44100 ring read at exactly the frame's rate (its own
  feedback regulates it against the play cursor, which here moves only
  when the driver reads).
- Input: a 67-button controller (the IIe keyboard key by key, the
  joystick buttons, two disk-swap buttons) and four axes (two joysticks).
  Keys are typed the way the Win32 window did it - WM_KEYDOWN for the
  any-key-down flag, WM_CHAR with the layout's character - with the IIe's
  auto-repeat (half a second, then 15 Hz). Caps lock is a toggle, on at
  boot, as AppleWin's is.
- Memory domains: Main RAM and Aux RAM, 64K each, handed out as upstream's
  `memmain`/`memaux` after a flush of the CPU's dirty pages; pokes are
  copied back before the next frame.

## Milestones

- **M1 DONE** (2026-09-18): the native reference builds from upstream
  unmodified, boots an enhanced //e to the Applesoft prompt, types
  `PRINT 1+1` and gets `2`, and boots a real Disk II image. ~740 fps on
  this machine.
- **M2 DONE** (2026-09-18): the guest builds from the same sources (first
  try; no TLS, no threads to remove), and the gates are green.
  - `waterbox/run-gate.sh`: 34/34. The wire check (config == driver enum),
    the file-overlay unit test, and six machines - the ROM alone typing a
    two-line Applesoft program and reading 24 lines of `2` back off the
    text page, the ROM under a random key/joystick exercise, a ][+, a PAL
    machine, no Mockingboard with two joysticks, and a commercial Disk II
    image with a swap at frame 900 (staged in `tests/roms-local`) - each
    proving native == sandbox over 300-1500 frames, that the input shaped
    the machine, that turbo is the same machine and the same second-half
    pictures, that a savestate round-trip around every frame is lossless,
    and that a NEW host finishes the run from a state saved half way; plus
    the settings leg (NTSC 1640625/27379 vs PAL 2375075/47424 with more
    cycles, and a ][+ is another machine). ~35 s.
  - `waterbox/tests/run-frontend.sh`: 4/4 with the disk staged. Chimera
    headless builds the same Main RAM (64K identical to the native
    reference) from a bare image, `model=apple2plus` reaches the guest
    through the frontend's config and is another machine, a hand-written
    `.chimeraProject` with two disks in the `floppy1` slot opens headless
    and matches its native reference, and the package's 67 bindings and 4
    analog bindings become the frontend's defaults.
  - The package: `waterbox/build-package.sh` ->
    `build/Cores/applewin.chimeraCore` (core.wbx 4.5 MB, of which ~1 MB is
    the embedded ROM table). A savestate is 2.4 MB (the 600x420 framebuffer
    is the biggest part; the machine itself is 128K).
  - A ProDOS hard disk image in slot 7 boots (a IIgs System 6 .2mg gets as
    far as "GS/OS REQUIRES APPLE IIGS HARDWARE", which is the controller
    and its firmware working).

## Sharp edges hit

- **The //e's Up arrow is Ctrl-K and Down is Ctrl-J (2026-09-19, user
  report).** The keyboard emits $0B and $0A for them, and Prince of Persia
  (like Lode Runner) takes Ctrl-K/Ctrl-J as "keyboard mode"/"joystick
  mode". A player who binds the PC arrows to the joystick axes while the
  default binding still puts them on `Key Up`/`Key Down` sends both at
  once, and the first jump switches the game to keyboard mode: the
  joystick then "has no effect" though the frontend shows it at 0/255.
  Proven with the game's own disks through chimera-run and through the
  headless frontend (movie playback and live Lua input alike): joystick X
  at 255 sends the prince out of the room; after one Up-arrow press it no
  longer does. The remedy is a binding, not code: arrows on either the
  //e keys or the axes, not both. Documented in the README and the keybind
  notes. Found on the way: chimera-run's "idle" input past the source
  movie held every axis at 0 - a paddle hard up-left - rather than the
  declared neutral (chimera fix, `ce_session_axis_neutral`).

- **A //e with no disk boots forever.** The Disk II boot ROM spins waiting
  for a disk; the ROM-only machine needs Ctrl-Reset (the `Reset` button)
  to reach Applesoft. The gate presses it at frame 60.
- **Turbo cannot skip the renderer.** The NTSC renderer's scanner clock
  (`g_nVideoClockVert/Horz`) is what a program reads on the floating bus
  and at the VBL; running the CPU with `bVideoUpdate=false` (upstream's
  full-speed mode) leaves it behind and upstream resyncs it from the cycle
  count, a different machine on purpose. Turbo here only skips handing the
  picture over; the machine digests stayed identical either way, but the
  second-half pictures did not until the renderer ran regardless.
- **Two rand()s.** glibc's and musl's differ; AppleWin draws Disk II
  read noise and formatted-track bits from `rand()`. One generator,
  defined by the driver and macro-pinned in `libwindows/wincompat.h`
  (every upstream file includes it through `StdAfx.h`).
- **The speaker's rate is not 44100.** Upstream rounds cycles-per-sample
  to the integer 23 (`SetClksPerSpkrSample`), 44369 Hz at the NTSC clock,
  and the Win32 build compensates by nudging the CPU's cycles per frame
  from the DirectSound buffer level. The cycle count is untouchable here,
  so the driver resamples instead (fixed-ratio linear, integer math).
  The Mockingboard regulates its own sample count against the play
  cursor, so its ring is read at exactly the frame's rate and it settles.
- **Files are bottom-up and Win32.** The framebuffer is a DIB (row 0 at
  the bottom); the disk images are read through `CreateFile`/`ReadFile`
  with `GetFileAttributes` deciding write protection - a mounted image is
  never reported read-only, or AppleWin would refuse the machine's writes.
- **`Peripheral_Clock_*.cpp`** in upstream's tree is not in upstream's own
  project file; it does not compile off Windows and nothing references it.

## Open

- **The ROMs: DECIDED 2026-09-19, the package carries none but AppleWin's
  own.** The rule is the one the user set for ares (2026-09-10): a core
  carries no firmware but the emulator's. The first package embedded the
  submodule's `resource/` directory whole, as AppleWin's binary has since
  1994; now `gen-resources.py` embeds only the hard-disk controller
  firmware (`firmware/HDD*`, AppleWin's own, GPL) and everything else is
  the project's firmware, declared in `waterbox.config` under the file
  name AppleWin gives it, mounted in the machine under that name, and
  read by `FindResource` in the driver on upstream's first ask - upstream
  is unchanged, it still calls `GetResource(id)`. Twenty declarations:
  ten machine ROMs (one per model, `requiredWhen` the model), four video
  ROMs and the three Pravets character-set bitmaps (by model; upstream
  loads every character set at start whatever the model, so one it was
  not given is a NULL it skips, and `GetBitmap` blanks a set the project
  did not bring), the Disk II boot PROM (always: every machine here has
  the card), and the SSC and printer firmware, needed while the new
  `serial`/`printer` settings keep AppleWin's default cards in slots 2 and
  1 - a user without those two ROMs takes the cards out instead. An
  enhanced //e with a disk therefore asks for five files. The 13-sector
  Disk II firmware, the mouse and clock cards, the custom F8 ROM and the
  system disks AppleWin formats new images with are not declared because
  nothing here reaches them. The gate and the frontend gate feed the ROMs
  from the submodule (`roms_into`, `--firmware id=path`), so CI still runs
  every leg while the package stays clean - the property ares' console
  BIOSes could not have.
- The No-Slot Clock's date is the sandbox epoch (2017-05-27) plus emulated
  time; a setting for the boot date would let a project pick one.
- Settings not yet exposed: CPU type override, Saturn/RamWorks memory,
  the printer and clock cards, VidHD, the mouse card, 13-sector Disk II
  firmware, the SSC, custom F8 ROM.
- Save data: the disk overlay lives in the savestate only; exporting a
  written disk image (the save-data channel) is not wired.
- Speed: ~740 fps native on this machine, unmeasured in the sandbox
  beyond the gate's timings (1200 frames in 1.7 s through run-wbx).
- `tests/roms-local` holds the commercial disks the gate and the frontend
  gate use (gitignored); a freely redistributable Apple II disk for the
  repository would let the CI run the disk legs.
