/* winmm joysticks, backed by the chimera controller. Upstream's Joystick.cpp
 * enumerates the devices at JoyInitialize() and polls joyGetPos() as the
 * program strobes the paddles; it scales what it reads by the range
 * joyGetDevCaps() reported, so the range here is the Apple's own 0..255 and
 * upstream's shift comes out as zero. */
#include "joystickapi.h"

#include <cstring>

UINT joyGetNumDevs(void)
{
    return 2;
}

MMRESULT joyGetDevCaps(UINT uJoyID, LPJOYCAPS pjc, UINT cbjc)
{
    unsigned x, y, buttons;
    if (uJoyID >= 2 || !chimera_joystick_state(uJoyID, &x, &y, &buttons))
        return JOYERR_UNPLUGGED;
    if (pjc == nullptr || cbjc < sizeof(JOYCAPS))
        return JOYERR_PARMS;
    memset(pjc, 0, sizeof(JOYCAPS));
    strcpy(pjc->szPname, uJoyID == 0 ? "Chimera joystick 1" : "Chimera joystick 2");
    pjc->wXmin = 0; pjc->wXmax = 255;
    pjc->wYmin = 0; pjc->wYmax = 255;
    pjc->wZmin = 0; pjc->wZmax = 255;
    pjc->wRmin = 0; pjc->wRmax = 255;
    pjc->wNumButtons = 2;
    pjc->wMaxButtons = 16;
    pjc->wNumAxes = 2;
    pjc->wMaxAxes = 4;
    return JOYERR_NOERROR;
}

MMRESULT joyGetPos(UINT uJoyID, LPJOYINFO pji)
{
    unsigned x, y, buttons;
    if (uJoyID >= 2 || !chimera_joystick_state(uJoyID, &x, &y, &buttons))
        return JOYERR_UNPLUGGED;
    if (pji == nullptr)
        return JOYERR_PARMS;
    pji->wXpos = x;
    pji->wYpos = y;
    pji->wZpos = 127;
    pji->wButtons = buttons;
    return JOYERR_NOERROR;
}

MMRESULT joyGetPosEx(UINT uJoyID, LPJOYINFOEX pji)
{
    unsigned x, y, buttons;
    if (uJoyID >= 2 || !chimera_joystick_state(uJoyID, &x, &y, &buttons))
        return JOYERR_UNPLUGGED;
    if (pji == nullptr || pji->dwSize < sizeof(JOYINFOEX))
        return JOYERR_PARMS;
    const DWORD flags = pji->dwFlags;
    memset(pji, 0, sizeof(JOYINFOEX));
    pji->dwSize = sizeof(JOYINFOEX);
    pji->dwFlags = flags;
    pji->dwXpos = x;
    pji->dwYpos = y;
    /* the second thumbstick (Z/R) of a modern pad is the second Apple
     * joystick's axes when upstream is told J1C_JOYSTICK1_THUMBSTICK2 */
    unsigned x2 = 127, y2 = 127, buttons2 = 0;
    if (uJoyID == 0 && chimera_joystick_state(1, &x2, &y2, &buttons2))
    {
        pji->dwZpos = x2;
        pji->dwRpos = y2;
    }
    else
    {
        pji->dwZpos = 127;
        pji->dwRpos = 127;
    }
    pji->dwUpos = 127;
    pji->dwVpos = 127;
    pji->dwButtons = buttons;
    pji->dwPOV = JOY_POVCENTERED;
    return JOYERR_NOERROR;
}
