/* The debugger's Win32 half: fonts and the clipboard. The debugger's command
 * engine compiles (CPU.cpp needs its breakpoints), its window does not exist. */
#include "StdAfx.h"
#include "Debug.h"

Update_t CmdConfigFont(int) { return UPDATE_NOTHING; }
Update_t CmdConfigGetFont(int) { return UPDATE_NOTHING; }
void ProcessClipboardCommands() {}
void FontsDestroy() {}
void FontsInitialize() {}
