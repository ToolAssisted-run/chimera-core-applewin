# libwindows

The `windows.h` upstream AppleWin's `StdAfx.h` includes when `_WIN32` is not
defined: the Win32 types, constants and the few functions the emulator core
still calls by their Windows names.

The headers and most of the small function bodies are taken from audetto's
Linux fork of AppleWin (`source/linux/libwindows`, GPL-2.0-or-later, the same
licence as AppleWin), which is the header set upstream's `!_WIN32` branch was
written against. What is changed here is everything that touched the host:

- `timeapi.cpp`: `GetTickCount`, `timeGetTime`, `GetLocalTime` and
  `QueryPerformanceCounter` read the EMULATED clock (6502 cycles), not the
  host's. AppleWin polls its joysticks and stamps its clock cards from these;
  a movie needs them to be a function of the frame.
- `winbase.cpp`: no `Sleep`, no threads, no events.
- `fileapi.cpp`: `CreateFile`/`ReadFile`/`WriteFile` over stdio with a sparse
  write overlay, so a disk image mounted read-only in the sandbox is still a
  writable disk to the machine and every write it takes is guest memory that a
  savestate carries (the chimera-core-dosbox-x hard disk design).
- `joystickapi.cpp` (new): `joyGetPos`/`joyGetPosEx` report the chimera
  controller, which is what lets upstream's own `Joystick.cpp`, `FourPlay.cpp`
  and `SNESMAX.cpp` compile unmodified.
- `winuser.cpp`: `GetKeyState` reports the chimera keyboard; the clipboard is
  empty; timers never fire.
- `wincompat.h`: `rand`/`srand` are pinned to one generator, so glibc (the
  native reference) and musl (the guest) agree on every "random" bit
  AppleWin's disk emulation produces.
