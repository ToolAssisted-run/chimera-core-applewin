# chimera-core-applewin

[AppleWin](https://github.com/AppleWin/AppleWin) as a
[Chimera](https://github.com/ToolAssisted-run/chimera) core: an Apple II in
miniBox's sandbox, packaged as `applewin.chimeraCore`.

**Built on upstream, unmodified.** AppleWin's own `StdAfx.h` compiles against
a local `windows.h` when `_WIN32` is not defined, and its `FrameBase`,
`SoundBuffer` and `Registry` seams are the front-end interface upstream
itself defines. This repository provides that `windows.h`
(`waterbox/libwindows`, from audetto's Linux fork, with every host-touching
part rewritten for the sandbox), whole-file replacements for the few upstream
files that are Win32 through and through (`waterbox/shims`), and a driver
that runs the machine one video frame of 6502 cycles at a time with no clock
but the 6502's. The patch set to upstream is one hook, for lag detection.

## What it is

- **The Apple ][, ][+, //e, enhanced //e** and AppleWin's clones (Pravets,
  TK3000, Base64A), chosen by a setting.
- **Disk II drives 1 and 2** (.dsk, .do, .po, .nib, .woz, .2mg) and a hard
  disk controller in slot 7 (.hdv, .2mg, .po). Images are read-only files;
  the machine's writes go to an overlay that savestates carry.
- **The keyboard as a controller**, key by key, with the IIe's auto-repeat;
  **two joysticks** (analog axes, the Apple's pushbuttons); Mockingboard in
  slot 4 (and 5) if wanted.
- **Main RAM and Aux RAM** as memory domains.
- **The ROMs are yours to supply.** The package carries nothing of Apple's
  (nor the clones', nor the card firmware): a project brings the machine's
  ROM, its video ROM, the Disk II boot PROM and, while the default printer
  and serial cards are in, their firmware - each declared under the file
  name AppleWin gives it (`Apple2e_Enhanced.rom`, `DISK2.rom`, ...), which
  is how AppleWin's own source tree ships them under `resource/`. Only
  AppleWin's hard-disk controller firmware, which AppleWin wrote, is
  embedded.

## Building

```
git submodule update --init --recursive
meson setup build/meson-native && ninja -C build/meson-native       # the reference
sh waterbox/setup-guest.sh && ninja -C build/meson-guest core.wbx   # the guest
./waterbox/build-package.sh                                          # applewin.chimeraCore
```

## Gates

```
./waterbox/run-gate.sh                  # native == sandbox == savestate round-trip == new host
```

Disk images for the gate go in `tests/roms-local` (gitignored); without
one, the gate runs the machine from its ROM alone. The ROMs themselves the
gate takes from the submodule, so it runs wherever the checkout does.

Status and plan: `docs/PLAN.md`.
