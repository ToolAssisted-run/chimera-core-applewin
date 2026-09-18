#pragma once

#include "winhandles.h"

typedef void *HCURSOR;
typedef void *HGLOBAL;

#define IDC_WAIT "IDC_WAIT"
#define CB_ERR (-1)
#define CB_ADDSTRING 0x0143
#define CB_RESETCONTENT 0x014b
#define CB_SETCURSEL 0x014e

typedef struct tagPOINT
{
    LONG x;
    LONG y;
} POINT, *LPPOINT;

HCURSOR LoadCursor(HINSTANCE hInstance, LPCSTR lpCursorName);
HCURSOR SetCursor(HCURSOR hCursor);

typedef VOID(CALLBACK *TIMERPROC)(HWND, UINT, UINT_PTR, DWORD);

UINT_PTR SetTimer(HWND, UINT_PTR, UINT, TIMERPROC);
BOOL KillTimer(HWND hWnd, UINT_PTR uIDEvent);
HWND WINAPI GetDlgItem(HWND, INT);
LRESULT WINAPI SendMessage(HWND, UINT, WPARAM, LPARAM);
void WINAPI PostQuitMessage(INT);

/* The keyboard as the emulator sees it: the level of a virtual key, from the
 * chimera keyboard controller (driver: chimera_key_state). Negative = down,
 * as the Win32 call reports it; the low bit is the toggle (Caps Lock). */
SHORT WINAPI GetKeyState(int nVirtKey);
SHORT WINAPI GetAsyncKeyState(int nVirtKey);
extern "C" int chimera_key_state(int vk);

/* the clipboard: there is none in a sandbox */
BOOL IsClipboardFormatAvailable(UINT format);
BOOL OpenClipboard(HWND);
BOOL CloseClipboard(void);
HGLOBAL GetClipboardData(UINT format);
LPVOID GlobalLock(HGLOBAL);
BOOL GlobalUnlock(HGLOBAL);
#define CF_TEXT 1

#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106

#define VK_CANCEL 0x03
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_EXECUTE 0x2B
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA      // ';:' for US
#define VK_OEM_PLUS 0xBB   // '+' any country
#define VK_OEM_COMMA 0xBC  // ',' any country
#define VK_OEM_MINUS 0xBD  // '-' any country
#define VK_OEM_PERIOD 0xBE // '.' any country
#define VK_OEM_2 0xBF      // '/?' for US
#define VK_OEM_3 0xC0      // '`~' for US
#define VK_OEM_4 0xDB      // '[{' for US
#define VK_OEM_5 0xDC      // '\|' for US
#define VK_OEM_6 0xDD      // ']}' for US
#define VK_OEM_7 0xDE      // ''"' for US
#define VK_OEM_8 0xDF
#define VK_OEM_102 0xE2    // '<>' or '\|' on RT 102-key kbd

#define KF_EXTENDED 0x0100
