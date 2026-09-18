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

## Open

- Lag detection needs one upstream hook (a call from `KeybReadData`/
  `JoyReadButton`/`JoyReadPosition`): patch 0001.
- The guest build, the gate (native == sandbox == rerecord == session), the
  package, the frontend.
- The ROMs are embedded from the submodule's `resource/` directory at
  build time (`waterbox/gen-resources.py`), exactly as AppleWin's own
  binary embeds them. They are Apple's; AppleWin has shipped them since
  1994. Whether a chimera package may do the same is the user's call - the
  alternative is firmware slots (one ROM per model, plus the Disk II and
  peripheral card ROMs).
- The No-Slot Clock's date is the sandbox epoch (2017-05-27) plus emulated
  time; a setting for the boot date would let a project pick one.
- Settings not yet exposed: CPU type override, Saturn/RamWorks memory,
  the printer and clock cards, VidHD, the mouse card.
