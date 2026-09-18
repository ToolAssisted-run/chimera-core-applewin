# shims

Whole-file replacements for the upstream AppleWin sources that are Win32
through and through and have no headless half: the registry (`Registry.cpp` -
the machine's configuration comes from the chimera settings instead), the
Super Serial Card's COM port and TCP socket (`SerialComms.cpp` - the card
exists, with nothing on the other end of the cable), the debugger's GDI
window and fonts (`Debugger_Display.cpp`, `Debugger_Win32.cpp`), and the
Uthernet card's packet capture (`tfearch.cpp` - no network). The property
sheet interface (`IPropertySheet`) is implemented in the driver itself.

Everything else in `extern/AppleWin/source` compiles as upstream wrote it,
against `waterbox/libwindows`.
