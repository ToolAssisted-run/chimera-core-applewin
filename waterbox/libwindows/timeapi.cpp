/* Every clock AppleWin can see is the machine's own. Upstream reads
 * GetTickCount() to rate-limit joystick polls (Joystick.cpp), to time the
 * speaker's benchmark, and the SSI263/Mockingboard debug traces; the No-Slot
 * Clock reads GetLocalTime() for the date ProDOS stamps files with. A host
 * clock in any of those makes the machine a function of the wall, so they all
 * derive from the cycle count the driver publishes. */
#include "timeapi.h"

#include <cstring>
#include <cstdio>

errno_t ctime_s(char *buf, size_t size, const time_t *time)
{
    const char *t = asctime(gmtime(time));
    strncpy(buf, t, size);
    if (size) buf[size - 1] = 0;
    return 0;
}

DWORD timeGetTime()
{
    return chimera_emulated_ms();
}

DWORD GetTickCount()
{
    return chimera_emulated_ms();
}

void GetLocalTime(SYSTEMTIME *t)
{
    const time_t seconds = (time_t)chimera_emulated_epoch_seconds();
    struct tm parts;
    gmtime_r(&seconds, &parts);
    t->wMilliseconds = (WORD)(chimera_emulated_ms() % 1000);
    t->wSecond = parts.tm_sec;
    t->wMinute = parts.tm_min;
    t->wHour = parts.tm_hour;
    t->wDayOfWeek = parts.tm_wday;
    t->wDay = parts.tm_mday;
    t->wMonth = parts.tm_mon + 1;
    t->wYear = parts.tm_year + 1900;
}

int GetDateFormat(LCID, DWORD, CONST SYSTEMTIME *, LPCSTR, LPSTR lpDateStr, int cchDate)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    snprintf(lpDateStr, cchDate, "%04u-%02u-%02u", now.wYear, now.wMonth, now.wDay);
    return cchDate;
}

int GetTimeFormat(LCID, DWORD, CONST SYSTEMTIME *, LPCSTR, LPSTR lpTimeStr, int cchTime)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    snprintf(lpTimeStr, cchTime, "%02u:%02u:%02u", now.wHour, now.wMinute, now.wSecond);
    return cchTime;
}
