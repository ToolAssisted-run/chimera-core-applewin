# chimera-core-applewin: AppleWin in miniBox's sandbox, as a Chimera core.
#
# Native reference build (run-native + run-wbx under the build dir):
#     meson setup build/meson-native && ninja -C build/meson-native
# Guest (core.wbx) build via the cross file waterbox/setup-guest.sh writes
# (needs miniBox's C++ guest toolchain, build/meson-cpp):
#     sh waterbox/setup-guest.sh && ninja -C build/meson-guest core.wbx
#
# THE EMULATOR IS UPSTREAM APPLEWIN, UNMODIFIED. Upstream's StdAfx.h already
# has a !_WIN32 branch that includes a local "windows.h" instead of the real
# one; waterbox/libwindows is that header set (see its README), and the handful
# of upstream files that are Win32 through and through - the registry, the
# serial card's COM port, the debugger's GDI window, the property sheets - are
# replaced whole by waterbox/shims/. Both flavors compile the SAME driver; only
# emulibc.h differs (waterbox/native-shim for the reference build).
project('chimera-core-applewin', ['c', 'cpp'], version: '0.1',
  default_options: ['c_std=gnu11', 'cpp_std=gnu++17', 'optimization=2', 'debug=false', 'warning_level=0'])

aw = 'extern/AppleWin'
src = aw / 'source'

# the submodule pin is pristine upstream; overlay the chimera patch set
# (idempotent; a series that is empty is fine - see docs/PLAN.md)
run_command(files('waterbox/apply-patches.sh'), check: true)

mb = get_option('minibox_dir')
if mb == ''
  mb_candidate = meson.current_source_dir() / '../chimera/extern/chimera-common-minibox'
  if import('fs').is_dir(mb_candidate)
    mb = mb_candidate
  else
    error('pass -Dminibox_dir=<miniBox checkout> (or keep one at ../chimera/extern/chimera-common-minibox)')
  endif
endif

# AppleWin is a Windows project: it asserts with _ASSERT under _DEBUG, so a
# release build defines neither. -Wno-multichar for its four-char constants.
defs = [
  '-DNDEBUG',
  '-DUSE_FILE32API',
]
quiet = [
  '-fno-strict-aliasing',
  '-Wno-multichar',
  '-Wno-switch',
  '-Wno-psabi',
  '-Werror=return-type',
]
add_project_arguments(defs + quiet, language: ['c', 'cpp'])

inc = include_directories(
  'waterbox',
  'waterbox/libwindows',
  src,
  src / 'Debugger',
  aw,
  aw / 'libyaml/include',
  aw / 'zlib',
)

# ---- the emulator's own third-party libraries, as the tree carries them ----
yaml_sources = files(
  aw / 'libyaml/src/api.c',
  aw / 'libyaml/src/dumper.c',
  aw / 'libyaml/src/emitter.c',
  aw / 'libyaml/src/loader.c',
  aw / 'libyaml/src/parser.c',
  aw / 'libyaml/src/reader.c',
  aw / 'libyaml/src/scanner.c',
  aw / 'libyaml/src/writer.c',
)
zlib_sources = files(
  aw / 'zlib/adler32.c',
  aw / 'zlib/compress.c',
  aw / 'zlib/crc32.c',
  aw / 'zlib/deflate.c',
  aw / 'zlib/gzclose.c',
  aw / 'zlib/gzlib.c',
  aw / 'zlib/gzread.c',
  aw / 'zlib/gzwrite.c',
  aw / 'zlib/infback.c',
  aw / 'zlib/inffast.c',
  aw / 'zlib/inflate.c',
  aw / 'zlib/inftrees.c',
  aw / 'zlib/trees.c',
  aw / 'zlib/uncompr.c',
  aw / 'zlib/zutil.c',
)
minizip_sources = files(
  aw / 'minizip/ioapi.c',
  aw / 'minizip/mztools.c',
  aw / 'minizip/unzip.c',
  aw / 'minizip/zip.c',
)
thirdparty_c_args = ['-DHAVE_CONFIG_H', '-I' + meson.current_source_dir() / aw / 'libyaml/win32', '-Wno-implicit-function-declaration']

# ---- upstream AppleWin: the same file list audetto's CMake names for a
# headless build (source/CMakeLists.txt there), with UPSTREAM paths ----------
applewin_sources = files(
  src / 'Tfe/tfesupp.cpp',
  src / 'Tfe/NetworkBackend.cpp',
  src / 'Tfe/PCapBackend.cpp',
  src / 'Tfe/IPRaw.cpp',
  src / 'Tfe/DNS.cpp',

  src / 'Debugger/BreakpointCard.cpp',
  src / 'Debugger/Debug.cpp',
  src / 'Debugger/Debugger_Help.cpp',
  src / 'Debugger/Debugger_Color.cpp',
  src / 'Debugger/Debugger_Disassembler.cpp',
  src / 'Debugger/Debugger_Symbols.cpp',
  src / 'Debugger/Debugger_DisassemblerData.cpp',
  src / 'Debugger/Debugger_Console.cpp',
  src / 'Debugger/Debugger_Assembler.cpp',
  src / 'Debugger/Debugger_Parser.cpp',
  src / 'Debugger/Debugger_Range.cpp',
  src / 'Debugger/Debugger_Commands.cpp',
  src / 'Debugger/Util_MemoryTextFile.cpp',

  src / 'CopyProtectionDongles.cpp',
  src / 'Uthernet1.cpp',
  src / 'Uthernet2.cpp',
  src / 'StrFormat.cpp',
  src / '6522.cpp',
  src / 'VidHD.cpp',
  src / 'SSI263.cpp',
  src / 'Speaker.cpp',
  src / 'SoundCore.cpp',
  src / 'AY8910.cpp',
  src / 'Mockingboard.cpp',
  src / 'MockingboardCardManager.cpp',
  src / 'Pravets.cpp',
  src / 'YamlHelper.cpp',
  src / 'Log.cpp',
  src / 'Disk.cpp',
  src / 'DiskFormatTrack.cpp',
  src / 'DiskImage.cpp',
  src / 'DiskImageHelper.cpp',
  src / 'Harddisk.cpp',
  src / 'Memory.cpp',
  src / 'CPU.cpp',
  src / '6821.cpp',
  src / 'NoSlotClock.cpp',
  src / 'SAM.cpp',
  src / 'z80emu.cpp',
  src / 'ParallelPrinter.cpp',
  src / 'ProDOS_Utils.cpp',
  src / 'MouseInterface.cpp',
  src / 'LanguageCard.cpp',
  src / 'RGBMonitor.cpp',
  src / 'NTSC.cpp',
  src / 'NTSC_CharSet.cpp',
  src / 'Card.cpp',
  src / 'CardManager.cpp',
  src / 'Disk2CardManager.cpp',
  src / 'Riff.cpp',
  src / 'SaveState.cpp',
  src / 'SynchronousEventManager.cpp',
  src / 'Video.cpp',
  src / 'Core.cpp',
  src / 'Utilities.cpp',
  src / 'FrameBase.cpp',
  src / 'CmdLine.cpp',
  src / 'Tape.cpp',

  # These four audetto's build replaces (they read Win32 joysticks and the
  # Win32 keyboard); here they compile from upstream against libwindows, whose
  # joyGetPos/GetKeyState are the chimera controller. The paddle timing, the
  # joyport, the 4Play and SNES MAX cards are then upstream's own.
  src / 'Joystick.cpp',
  src / 'Keyboard.cpp',
  src / 'FourPlay.cpp',
  src / 'SNESMAX.cpp',

  src / 'Z80VICE/z80.cpp',
  src / 'Z80VICE/z80mem.cpp',
  src / 'Z80VICE/daa.cpp',
)

# ---- what replaces the Win32-only upstream files (waterbox/shims/README) ----
shim_sources = files(
  'waterbox/shims/Registry.cpp',
  'waterbox/shims/SerialComms.cpp',
  'waterbox/shims/Debugger_Display.cpp',
  'waterbox/shims/Debugger_Win32.cpp',
  'waterbox/shims/tfearch.cpp',
)

libwindows_sources = files(
  'waterbox/libwindows/dmusicc.cpp',
  'waterbox/libwindows/fileapi.cpp',
  'waterbox/libwindows/guiddef.cpp',
  'waterbox/libwindows/winhandles.cpp',
  'waterbox/libwindows/misc.cpp',
  'waterbox/libwindows/strsafe.cpp',
  'waterbox/libwindows/winstrings.cpp',
  'waterbox/libwindows/timeapi.cpp',
  'waterbox/libwindows/winbase.cpp',
  'waterbox/libwindows/winnls.cpp',
  'waterbox/libwindows/winuser.cpp',
  'waterbox/libwindows/joystickapi.cpp',
)

# the ROMs and firmware the Windows build carries as resources, as a table
python = import('python').find_installation('python3')
resources_cpp = custom_target('resources',
  output: 'applewin-resources.cpp',
  input: 'waterbox/gen-resources.py',
  command: [python, '@INPUT@', meson.current_source_dir() / aw, '@OUTPUT@'])

driver_sources = files(
  'waterbox/applewin-driver.cpp',
  'waterbox/wbx-entry.cpp',
)

core_sources = yaml_sources + zlib_sources + minizip_sources + applewin_sources + shim_sources + libwindows_sources + driver_sources

if meson.is_cross_build()
  # ---- the guest: core.wbx --------------------------------------------------
  mbdir = meson.get_external_property('minibox_dir')
  mbuild = meson.get_external_property('minibox_build')
  sr = meson.get_external_property('guest_sysroot')

  executable('core', core_sources, resources_cpp, files('waterbox/guest-syscalls.cpp'),
    name_suffix: 'wbx',
    include_directories: inc,
    c_args: thirdparty_c_args,
    link_args: [
      '-static', '-no-pie',
      '-Wl,--eh-frame-hdr', '-Wl,-O2', '-Wl,--no-relax', '-Wl,-z,stack-size=8388608',
      '-T', mbdir / 'source/guest/linkscript.T',
      '-Wl,-u,pthread_once', '-Wl,-u,pthread_cond_wait',
      '-Wl,-u,pthread_cond_broadcast', '-Wl,-u,pthread_key_create',
      mbuild / 'source/guest/cxxglue.c.o',
      mbuild / 'source/guest/emulibc.c.o',
      '-L' + sr / 'lib', '-lstdc++', '-lgcc', '-lgcc_eh', '-lc'])
else
  # ---- the native reference -------------------------------------------------
  m = meson.get_compiler('c').find_library('m', required: false)
  executable('run-native', core_sources, resources_cpp, files('waterbox/run-native.c'),
    include_directories: [include_directories('waterbox/native-shim'), inc],
    c_args: thirdparty_c_args + ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    cpp_args: ['-I' + mb / 'source/guest/include', '-I' + mb / 'extern/jsmn'],
    dependencies: [m])

  # the write overlay behind the Win32 file API, on its own (run-gate.sh runs it)
  executable('test-fileapi', files('waterbox/tests/test-fileapi.cpp',
      'waterbox/libwindows/fileapi.cpp', 'waterbox/libwindows/winhandles.cpp'),
    include_directories: include_directories('waterbox/libwindows'))

  mbhost = mb / 'build/meson-linux/source/host'
  executable('run-wbx', files('waterbox/run-wbx.c'),
    c_args: ['-I' + mb / 'source/host'],
    link_args: [mbhost / 'libminiboxhost.so', '-Wl,-rpath,' + mbhost])
endif
