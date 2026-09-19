/* No threads, no waits, no sleeps: the sandbox is one thread and the frame is
 * the only clock. Upstream only reaches these from paths a headless machine
 * does not take (the serial card's COM thread, the sound-buffer restore loop,
 * the speaker benchmark). */
#include "winbase.h"

#include <errno.h>
#include <iostream>
#include <unistd.h>
#include <sstream>
#include <stdexcept>

DWORD WINAPI GetLastError(void)
{
    return errno;
}

void DeleteCriticalSection(CRITICAL_SECTION *) {}
void InitializeCriticalSection(CRITICAL_SECTION *) {}
void EnterCriticalSection(CRITICAL_SECTION *) {}
void LeaveCriticalSection(CRITICAL_SECTION *) {}

BOOL SetCurrentDirectory(LPCSTR)
{
    /* the sandbox has one flat directory: every path is already there */
    return TRUE;
}

void OutputDebugString(const char *str)
{
    std::cerr << str;
}

void ExitProcess(int status)
{
    std::ostringstream buffer;
    buffer << "ExitProcess: " << status;
    throw std::runtime_error(buffer.str());
}

DWORD WINAPI WaitForMultipleObjects(DWORD, const HANDLE *, BOOL, DWORD)
{
    return WAIT_FAILED;
}

DWORD WINAPI WaitForSingleObject(const HANDLE, DWORD)
{
    return WAIT_FAILED;
}

BOOL WINAPI GetExitCodeThread(HANDLE, LPDWORD code)
{
    *code = 0;
    return TRUE;
}

BOOL WINAPI SetEvent(HANDLE)
{
    return TRUE;
}

VOID WINAPI Sleep(DWORD)
{
    /* nothing to wait for: time only moves when the machine runs */
}

BOOL WINAPI SetThreadPriority(HANDLE, INT)
{
    return TRUE;
}

HANDLE WINAPI CreateEvent(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR)
{
    return INVALID_HANDLE_VALUE;
}

HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, LPDWORD)
{
    return INVALID_HANDLE_VALUE;
}

BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *counter)
{
    counter->QuadPart = chimera_emulated_ms();
    return TRUE;
}

HANDLE CreateSemaphore(LPSECURITY_ATTRIBUTES, LONG, LONG, LPCSTR)
{
    return INVALID_HANDLE_VALUE;
}
