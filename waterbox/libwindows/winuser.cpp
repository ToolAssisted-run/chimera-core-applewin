#include "winuser.h"

#include <sstream>
#include <stdexcept>

HCURSOR LoadCursor(HINSTANCE, LPCSTR)
{
    return nullptr;
}

HCURSOR SetCursor(HCURSOR)
{
    return nullptr;
}

UINT_PTR SetTimer(HWND, UINT_PTR, UINT, TIMERPROC)
{
    /* SoundCore arms a 1 ms timer to fade the volume when the emulator pauses;
     * a headless machine never pauses, and a timer that fired would be a host
     * clock inside the machine. It is granted and never fires. */
    return 1;
}

BOOL KillTimer(HWND, UINT_PTR)
{
    return TRUE;
}

HWND WINAPI GetDlgItem(HWND, INT)
{
    return nullptr;
}

LRESULT WINAPI SendMessage(HWND, UINT, WPARAM, LPARAM)
{
    return 0;
}

void WINAPI PostQuitMessage(INT status)
{
    std::ostringstream buffer("PostQuitMessage: ");
    buffer << status;
    throw std::runtime_error(buffer.str());
}

SHORT WINAPI GetKeyState(int nVirtKey)
{
    return (SHORT)chimera_key_state(nVirtKey);
}

SHORT WINAPI GetAsyncKeyState(int nVirtKey)
{
    return (SHORT)chimera_key_state(nVirtKey);
}

BOOL IsClipboardFormatAvailable(UINT)
{
    return FALSE;
}

BOOL OpenClipboard(HWND)
{
    return FALSE;
}

BOOL CloseClipboard(void)
{
    return TRUE;
}

HGLOBAL GetClipboardData(UINT)
{
    return nullptr;
}

LPVOID GlobalLock(HGLOBAL)
{
    return nullptr;
}

BOOL GlobalUnlock(HGLOBAL)
{
    return TRUE;
}
