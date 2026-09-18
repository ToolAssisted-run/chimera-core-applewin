/* The debugger's GDI display: 4000 lines of drawing upstream, none of it
 * reachable without a window. The globals the command engine touches remain. */
#include "StdAfx.h"
#include "Debug.h"

// NOTE: Keep in sync ConsoleColors_e g_anConsoleColor !
COLORREF g_anConsoleColor[NUM_CONSOLE_COLORS] = {
    RGB(0, 0, 0),
    RGB(255, 32, 32),
    RGB(0, 255, 0),
    RGB(255, 255, 0),
    RGB(64, 64, 255),
    RGB(255, 0, 255),
    RGB(0, 255, 255),
    RGB(255, 255, 255),
    RGB(255, 128, 0),
    RGB(128, 128, 128),
    RGB(80, 192, 255),
};

VideoScannerDisplayInfo g_videoScannerDisplayInfo;

char g_aDebuggerVirtualTextScreen[DEBUG_VIRTUAL_TEXT_HEIGHT][DEBUG_VIRTUAL_TEXT_WIDTH];

void DrawConsoleCursor() {}
HDC GetDebuggerMemDC(void) { return nullptr; }
void UpdateDisplay(Update_t) {}
void StretchBltMemToFrameDC(void) {}
void ReleaseDebuggerMemDC(void) {}
void ReleaseConsoleFontDC(void) {}
HDC GetConsoleFontDC(void) { return nullptr; }
void DrawConsoleInput() {}
