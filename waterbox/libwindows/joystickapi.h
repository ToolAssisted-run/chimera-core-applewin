#pragma once

#include "wincompat.h"

/* The winmm joystick API, as far as AppleWin's Joystick.cpp, FourPlay.cpp and
 * SNESMAX.cpp use it. The devices behind it are the chimera controller's two
 * joysticks: the driver publishes them through chimera_joystick_state(). */

typedef UINT MMRESULT;

#define JOYERR_NOERROR 0
#define JOYERR_PARMS 165
#define JOYERR_UNPLUGGED 167

#define JOY_BUTTON1 0x0001
#define JOY_BUTTON2 0x0002
#define JOY_BUTTON3 0x0004
#define JOY_BUTTON4 0x0008
#define JOY_BUTTON5 0x00000010
#define JOY_BUTTON6 0x00000020
#define JOY_BUTTON7 0x00000040
#define JOY_BUTTON8 0x00000080
#define JOY_BUTTON9 0x00000100
#define JOY_BUTTON10 0x00000200
#define JOY_BUTTON11 0x00000400
#define JOY_BUTTON12 0x00000800
#define JOY_BUTTON13 0x00001000
#define JOY_BUTTON14 0x00002000
#define JOY_BUTTON15 0x00004000
#define JOY_BUTTON16 0x00008000

#define JOY_RETURNX 0x00000001
#define JOY_RETURNY 0x00000002
#define JOY_RETURNZ 0x00000004
#define JOY_RETURNR 0x00000008
#define JOY_RETURNU 0x00000010
#define JOY_RETURNV 0x00000020
#define JOY_RETURNPOV 0x00000040
#define JOY_RETURNBUTTONS 0x00000080
#define JOY_RETURNALL (JOY_RETURNX | JOY_RETURNY | JOY_RETURNZ | JOY_RETURNR | JOY_RETURNU | JOY_RETURNV | JOY_RETURNPOV | JOY_RETURNBUTTONS)

#define JOYSTICKID1 0
#define JOYSTICKID2 1

#define JOY_POVCENTERED 0xFFFF
#define JOY_POVFORWARD 0
#define JOY_POVRIGHT 9000
#define JOY_POVBACKWARD 18000
#define JOY_POVLEFT 27000

typedef struct joyinfo_tag
{
    UINT wXpos;
    UINT wYpos;
    UINT wZpos;
    UINT wButtons;
} JOYINFO, *LPJOYINFO;

typedef struct joyinfoex_tag
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwXpos;
    DWORD dwYpos;
    DWORD dwZpos;
    DWORD dwRpos;
    DWORD dwUpos;
    DWORD dwVpos;
    DWORD dwButtons;
    DWORD dwButtonNumber;
    DWORD dwPOV;
    DWORD dwReserved1;
    DWORD dwReserved2;
} JOYINFOEX, *LPJOYINFOEX;

typedef struct tagJOYCAPS
{
    WORD wMid;
    WORD wPid;
    CHAR szPname[32];
    UINT wXmin;
    UINT wXmax;
    UINT wYmin;
    UINT wYmax;
    UINT wZmin;
    UINT wZmax;
    UINT wNumButtons;
    UINT wPeriodMin;
    UINT wPeriodMax;
    UINT wRmin;
    UINT wRmax;
    UINT wUmin;
    UINT wUmax;
    UINT wVmin;
    UINT wVmax;
    UINT wCaps;
    UINT wMaxAxes;
    UINT wNumAxes;
    UINT wMaxButtons;
    CHAR szRegKey[32];
    CHAR szOEMVxD[260];
} JOYCAPS, *LPJOYCAPS;

UINT joyGetNumDevs(void);
MMRESULT joyGetDevCaps(UINT uJoyID, LPJOYCAPS pjc, UINT cbjc);
MMRESULT joyGetPos(UINT uJoyID, LPJOYINFO pji);
MMRESULT joyGetPosEx(UINT uJoyID, LPJOYINFOEX pji);

/* what the driver publishes: one joystick's axes (0..255, 127 centred) and
 * its buttons as a JOY_BUTTONn mask; returns 0 when that joystick is not
 * plugged into this machine */
extern "C" int chimera_joystick_state(unsigned index, unsigned *x, unsigned *y, unsigned *buttons);
