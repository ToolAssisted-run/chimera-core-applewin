#pragma once

#include "wincompat.h"

#include <ctime>

#define _tzset tzset
errno_t ctime_s(char *buf, size_t size, const time_t *time);

#define LOCALE_SYSTEM_DEFAULT 0x0800

typedef struct _SYSTEMTIME
{
    WORD wYear;
    WORD wMonth;
    WORD wDayOfWeek;
    WORD wDay;
    WORD wHour;
    WORD wMinute;
    WORD wSecond;
    WORD wMilliseconds;
} SYSTEMTIME;

int GetDateFormat(LCID Locale, DWORD dwFlags, CONST SYSTEMTIME *lpDate, LPCSTR lpFormat, LPSTR lpDateStr, int cchDate);
int GetTimeFormat(LCID Locale, DWORD dwFlags, CONST SYSTEMTIME *lpTime, LPCSTR lpFormat, LPSTR lpTimeStr, int cchTime);

DWORD timeGetTime();
DWORD GetTickCount();
void GetLocalTime(SYSTEMTIME *t);

/* The emulated clock, provided by the driver: milliseconds and seconds of
 * 6502 time since the machine was built. Every Win32 clock above is this. */
extern "C" uint32_t chimera_emulated_ms(void);
extern "C" int64_t chimera_emulated_epoch_seconds(void);
