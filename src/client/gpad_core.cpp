// mod (gpad): the platform-neutral game pad core. The functions below are retail win32/win_gamepad.cpp (and
// IN_GamepadsMove + buttonList from win32/win_input.cpp), moved here unchanged except where marked "mod (gpad)":
// they read a GPadRawState from the backend (client/gpad_backend.h) instead of calling XInput. No <XInput.h> here;
// the XInput backend is win32/win_gamepad.cpp, the web backend src/web/web_gamepad.cpp. See docs/controllers.md.

#include "gpad_core.h"
#include "cl_gamepad.h"
#include <win32/win_shared.h>
#include <universal/assertive.h>
#include <universal/com_math.h>
#include <qcommon/common.h>
#include <qcommon/com_clients.h>
#include <cmath>
#include <cstring>

const dvar_t *gpad_debug;
const dvar_t *gpad_button_lstick_deflect_max;
const dvar_t *gpad_button_rstick_deflect_max;
const dvar_t *gpad_button_deadzone;
const dvar_t *gpad_stick_deadzone_min;
const dvar_t *gpad_stick_deadzone_max;
const dvar_t *gpad_stick_pressed;
const dvar_t *gpad_stick_pressed_hysteresis;
const dvar_t *gpad_rumble;
const dvar_t *gpad_menu_scroll_delay_first;
const dvar_t *gpad_menu_scroll_delay_rest;
const dvar_t *gpad_buttonsConfig;
const dvar_t *gpad_sticksConfig;
const dvar_t *gpad_enabled;
const dvar_t *gpad_present;
const dvar_t *gpad_autoenable;   // mod (gpad)
const dvar_t *gpad_background;   // mod (gpad)
const dvar_t *gpad_rumble_scale; // mod (gpad)

GamePad s_gamePads[1];
int inputCounter;
bool hasInput[1];
GPadRawState delayedInputState[1]; // mod (gpad): retail _XINPUT_STATE
void(__cdecl *s_removedCB)(int);
void(__cdecl *s_insertedCB)(int);

// mod (gpad): retail win_input.cpp buttonList (pairs: pad button, key code), moved with IN_GamepadsMove.
GamePadButton buttonList[32] =
{
  (GamePadButton)GPAD_X,
  (GamePadButton)3,
  (GamePadButton)GPAD_A,
  (GamePadButton)1,
  (GamePadButton)GPAD_B,
  (GamePadButton)2,
  (GamePadButton)GPAD_Y,
  (GamePadButton)4,
  (GamePadButton)GPAD_L_TRIG,
  (GamePadButton)18,
  (GamePadButton)GPAD_R_TRIG,
  (GamePadButton)19,
  (GamePadButton)GPAD_L_SHLDR,
  (GamePadButton)5,
  (GamePadButton)GPAD_R_SHLDR,
  (GamePadButton)6,
  (GamePadButton)GPAD_START,
  (GamePadButton)14,
  (GamePadButton)GPAD_BACK,
  (GamePadButton)15,
  (GamePadButton)GPAD_L3,
  (GamePadButton)16,
  (GamePadButton)GPAD_R3,
  (GamePadButton)17,
  (GamePadButton)GPAD_UP,
  (GamePadButton)20,
  (GamePadButton)GPAD_DOWN,
  (GamePadButton)21,
  (GamePadButton)GPAD_LEFT,
  (GamePadButton)22,
  (GamePadButton)GPAD_RIGHT,
  (GamePadButton)23
};

static bool s_padDispatched[1];      // mod (gpad): the last dispatch sent this pad's live state (a release is owed)
static int s_autoEnablePresent = -1; // mod (gpad): pad presence gpad_autoenable last acted on (-1 = not yet)

void GPad_InitAll()
{
    GamePad *gPad; // [esp+14h] [ebp-8h]
    signed int portIndex; // [esp+18h] [ebp-4h]

    gpad_debug = _Dvar_RegisterInt("gpad_debug", 0, 0x80000000, 0x7FFFFFFF, 0, "coder use only");
    gpad_button_lstick_deflect_max = _Dvar_RegisterFloat(
                                                                         "gpad_button_lstick_deflect_max",
                                                                         1.0,
                                                                         0.0,
                                                                         1.0,
                                                                         0,
                                                                         "Game pad maximum pad stick pressed value");
    gpad_button_rstick_deflect_max = _Dvar_RegisterFloat(
                                                                         "gpad_button_rstick_deflect_max",
                                                                         1.0,
                                                                         0.0,
                                                                         1.0,
                                                                         0,
                                                                         "Game pad maximum pad stick pressed value");
    gpad_button_deadzone = _Dvar_RegisterFloat(
                                                     "gpad_button_deadzone",
                                                     0.13,
                                                     0.0,
                                                     1.0,
                                                     0x80u,
                                                     "Game pad button deadzone threshhold");
    gpad_stick_deadzone_min = _Dvar_RegisterFloat(
                                                            "gpad_stick_deadzone_min",
                                                            0.2,
                                                            0.0,
                                                            1.0,
                                                            0x80u,
                                                            "Game pad minimum stick deadzone");
    gpad_stick_deadzone_max = _Dvar_RegisterFloat(
                                                            "gpad_stick_deadzone_max",
                                                            0.0099999998,
                                                            0.0,
                                                            1.0,
                                                            0x80u,
                                                            "Game pad maximum stick deadzone");
    gpad_stick_pressed = _Dvar_RegisterFloat(
                                                 "gpad_stick_pressed",
                                                 0.40000001,
                                                 0.0,
                                                 1.0,
                                                 0x80u,
                                                 "Game pad stick pressed threshhold");
    gpad_stick_pressed_hysteresis = _Dvar_RegisterFloat(
                                                                        "gpad_stick_pressed_hysteresis",
                                                                        0.1,
                                                                        0.0,
                                                                        1.0,
                                                                        0x80u,
                                                                        "Game pad stick pressed no-change-zone around gpad_stick_pressed to prevent bouncing");
    gpad_rumble = _Dvar_RegisterBool("gpad_rumble", 1, 1u, "Enable game pad rumble");
    gpad_menu_scroll_delay_first = _Dvar_RegisterInt(
                                                                     "gpad_menu_scroll_delay_first",
                                                                     420,
                                                                     0,
                                                                     1000,
                                                                     1u,
                                                                     "Menu scroll key-repeat delay, for the first repeat, in milliseconds");
    gpad_menu_scroll_delay_rest = _Dvar_RegisterInt(
                                                                    "gpad_menu_scroll_delay_rest",
                                                                    210,
                                                                    0,
                                                                    1000,
                                                                    1u,
                                                                    "Menu scroll key-repeat delay, for repeats after the first, in milliseconds");
    gpad_buttonsConfig = _Dvar_RegisterString(
                                                 "gpad_buttonsConfig",
                                                 "buttons_default",
                                                 1u,
                                                 "Game pad button configuration");
    gpad_sticksConfig = _Dvar_RegisterString(
                                                "gpad_sticksConfig",
                                                "thumbstick_default",
                                                1u,
                                                "Game pad stick configuration");
    gpad_enabled = _Dvar_RegisterBool("gpad_enabled", 0, 1u, "Game pad enabled");
    gpad_present = _Dvar_RegisterBool("gpad_present", 0, 0x40u, "Game pad present");
    // mod (gpad): retail PC left gpad_enabled 0 until the player turned the pad on in the options (and the profile
    // load overwrote it). gpad_autoenable 1 makes gpad_enabled follow the pad: set to 1 when one is connected (or at
    // the first poll with one connected) and to 0 when the last one is unplugged. 0 = retail: gpad_enabled is manual.
    gpad_autoenable = _Dvar_RegisterBool(
        "gpad_autoenable",
        1,
        1u,
        "mod: gpad_enabled follows whether a game pad is connected (0 = retail: gpad_enabled is set by hand / the options)");
    // mod (gpad): retail read the pads only while the game window had focus; 1 also reads them in the background.
    gpad_background = _Dvar_RegisterBool(
        "gpad_background",
        0,
        1u,
        "mod: read the game pad while the game window is in the background (0 = retail: only with focus)");
    gpad_rumble_scale = _Dvar_RegisterFloat(
        "gpad_rumble_scale",
        1.0f,
        0.0f,
        1.0f,
        1u,
        "mod: strength of script rumbles (0..1); gpad_rumble 0 turns rumble off");
    GPad_Rumble_RegisterCommands(); // mod (gpad)
    for ( portIndex = 0; portIndex < 1; ++portIndex )
    {
        gPad = &s_gamePads[portIndex];
        // mod (gpad): retail zeroed the XInput vibration and called XInputSetState(portIndex) here (not when
        // headless). The motors are now set to 0 when a port is bound (GPad_Check), so an empty port is never touched.
        gPad->lowRumble = 0.0f;
        gPad->highRumble = 0.0f;
        gPad->backendPort = -1;
        gPad->sentLowRumble = -1.0f;
        gPad->sentHighRumble = -1.0f;
    }
}

void __cdecl GPad_UpdateAll()
{
    GPadRawState inputState; // [esp+0h] [ebp-14h] BYREF (mod (gpad): retail _XINPUT_STATE)
    int portIndex; // [esp+10h] [ebp-4h]

    if ( gpad_debug->current.integer >= 0 )
    {
        for ( portIndex = 0; portIndex < 1; ++portIndex )
        {
            // mod (gpad): retail GPad_RefreshAll (XInputGetCapabilities) then XInputGetState; one backend read now
            if ( GPad_Check(portIndex, &inputState) )
            {
                GPad_UpdateSticks(portIndex, &inputState);
                GPad_UpdateDigitals(portIndex, &inputState);
                GPad_UpdateAnalogs(portIndex, &inputState);
            }
        }
        inputCounter = 0;
    }
    else
    {
        if ( !inputCounter )
        {
            for ( portIndex = 0; portIndex < 1; ++portIndex )
            {
                if ( hasInput[portIndex] )
                {
                    GPad_UpdateSticks(portIndex, &delayedInputState[portIndex]);
                    GPad_UpdateDigitals(portIndex, &delayedInputState[portIndex]);
                    GPad_UpdateAnalogs(portIndex, &delayedInputState[portIndex]);
                }
            }
        }
        if ( --inputCounter <= gpad_debug->current.integer )
        {
            for ( portIndex = 0; portIndex < 1; ++portIndex )
                hasInput[portIndex] = GPad_Check(portIndex, &delayedInputState[portIndex]); // mod (gpad)
            inputCounter = 0;
        }
    }
}

// mod (gpad): retail GPad_Check(portIndex) asked XInputGetCapabilities(portIndex) every frame for XInput port 0 only
// (a known stall when nothing is plugged in) and set enabled from the answer. Now controller portIndex is fed by the
// first connected backend port: the bound port is read every frame; when none is bound (or it was unplugged) every
// backend port is tried in order (the XInput backend probes an empty port at most once per second). Returns true with
// *raw filled when the pad is connected; the retail inserted / removed callbacks fire on the transitions.
bool __cdecl GPad_Check(int portIndex, GPadRawState *raw)
{
    GamePad *gPad; // [esp+0h] [ebp-8h]
    bool wasEnabled; // [esp+7h] [ebp-1h]
    int port;
    int maxPorts;

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    100,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    wasEnabled = gPad->enabled;
    memset(raw, 0, sizeof(*raw));
    if ( gPad->backendPort >= 0 && !GPad_Backend_Poll(gPad->backendPort, raw) )
    {
        Com_Printf(16, "Game pad %i disconnected (port %i).\n", portIndex, gPad->backendPort);
        gPad->backendPort = -1;
    }
    if ( gPad->backendPort < 0 )
    {
        maxPorts = GPad_Backend_MaxPorts();
        if ( maxPorts > GPAD_BACKEND_MAX_PORTS )
            maxPorts = GPAD_BACKEND_MAX_PORTS;
        for ( port = 0; port < maxPorts; ++port )
        {
            if ( GPad_Backend_Poll(port, raw) )
            {
                gPad->backendPort = port;
                gPad->sentLowRumble = -1.0f; // resend: stops any rumble left over on this pad
                gPad->sentHighRumble = -1.0f;
                Com_Printf(16, "Game pad %i connected (port %i).\n", portIndex, port);
                break;
            }
        }
    }
    gPad->enabled = gPad->backendPort >= 0;
    gPad->keyboardEnabled = 0;
    if ( !gPad->enabled )
        memset(raw, 0, sizeof(*raw));
    if ( wasEnabled && !gPad->enabled )
    {
        // mod (gpad): retail reset only when a removed callback was set; reset always so nothing stays held
        GPad_ResetState(portIndex);
        if ( s_removedCB )
            s_removedCB(portIndex);
    }
    else if ( s_insertedCB && !wasEnabled && gPad->enabled )
    {
        s_insertedCB(portIndex);
    }
    return gPad->enabled;
}

void __cdecl GPad_UpdateDigitals(int portIndex, const GPadRawState *xpad)
{
    GamePad *gPad; // [esp+10h] [ebp-10h]
    float rightDeflect; // [esp+14h] [ebp-Ch]
    float leftDeflect; // [esp+1Ch] [ebp-4h]

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    146,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    if ( !xpad && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp", 147, 0, "%s", "xpad") )
        __debugbreak();
    gPad = &s_gamePads[portIndex];
    gPad->lastDigitals = gPad->digitals;
    gPad->digitals = (__int16)xpad->buttons; // mod (gpad): retail xpad->wButtons
    rightDeflect = Dvar_GetFloat("gpad_button_rstick_deflect_max");
    if ( fabs(gPad->sticks[2]) > rightDeflect )
        gPad->digitals &= ~0x80u;
    if ( fabs(gPad->sticks[3]) > rightDeflect )
        gPad->digitals &= ~0x80u;
    leftDeflect = Dvar_GetFloat("gpad_button_lstick_deflect_max");
    if ( fabs(gPad->sticks[0]) > leftDeflect )
        gPad->digitals &= ~0x40u;
    if ( fabs(gPad->sticks[1]) > leftDeflect )
        gPad->digitals &= ~0x40u;
}

void __cdecl GPad_UpdateAnalogs(int portIndex, const GPadRawState *xpad)
{
    float v2; // [esp+4h] [ebp-1Ch]
    float v3; // [esp+8h] [ebp-18h]
    GamePad *gPad; // [esp+14h] [ebp-Ch]

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    187,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    if ( !xpad && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp", 188, 0, "%s", "xpad") )
        __debugbreak();
    gPad = &s_gamePads[portIndex];
    // mod (gpad): retail (float)xpad->bLeftTrigger / 255.0 and bRightTrigger; the backend delivers 0..1
    gPad->lastAnalogs[0] = gPad->analogs[0];
    if ( (float)(0.0
                         - (float)((float)(xpad->lt - gpad_button_deadzone->current.value)
                                         / (float)(1.0 - gpad_button_deadzone->current.value))) < 0.0 )
        v3 = (float)(xpad->lt - gpad_button_deadzone->current.value)
             / (float)(1.0 - gpad_button_deadzone->current.value);
    else
        v3 = 0.0f;
    gPad->analogs[0] = v3;
    gPad->lastAnalogs[1] = gPad->analogs[1];
    if ( (float)(0.0
                         - (float)((float)(xpad->rt - gpad_button_deadzone->current.value)
                                         / (float)(1.0 - gpad_button_deadzone->current.value))) < 0.0 )
        v2 = (float)(xpad->rt - gpad_button_deadzone->current.value)
             / (float)(1.0 - gpad_button_deadzone->current.value);
    else
        v2 = 0.0f;
    gPad->analogs[1] = v2;
}

void __cdecl GPad_UpdateSticks(int portIndex, const GPadRawState *xpad)
{
    float rVec[2]; // [esp+4h] [ebp-18h] BYREF
    GamePad *gPad; // [esp+Ch] [ebp-10h]
    int stickIndex; // [esp+10h] [ebp-Ch]
    float lVec[2]; // [esp+14h] [ebp-8h] BYREF

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    273,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    if ( !xpad && !Assert_MyHandler("C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp", 274, 0, "%s", "xpad") )
        __debugbreak();
    gPad = &s_gamePads[portIndex];
    GPad_ConvertStickToFloat(xpad->lx, xpad->ly, lVec, &lVec[1]); // mod (gpad): retail sThumbLX / sThumbLY
    GPad_ConvertStickToFloat(xpad->rx, xpad->ry, rVec, &rVec[1]);
    stickIndex = 0;
    gPad->lastSticks[0] = gPad->sticks[0];
    gPad->sticks[stickIndex] = lVec[0];
    stickIndex = 1;
    gPad->lastSticks[1] = gPad->sticks[1];
    gPad->sticks[stickIndex] = lVec[1];
    stickIndex = 2;
    gPad->lastSticks[2] = gPad->sticks[2];
    gPad->sticks[stickIndex] = rVec[0];
    stickIndex = 3;
    gPad->lastSticks[3] = gPad->sticks[3];
    gPad->sticks[stickIndex] = rVec[1];
    GPad_UpdateSticksDown(gPad);
}

// mod (gpad): retail took the XInput shorts and divided by 32767.0; the backend delivers -1..1 (same scale)
void __cdecl GPad_ConvertStickToFloat(float inX, float inY, float *outX, float *outY)
{
    float stickVec[2]; // [esp+Ch] [ebp-10h] BYREF
    float len; // [esp+14h] [ebp-8h]
    float deadZoneTotal; // [esp+18h] [ebp-4h]

    stickVec[0] = inX;
    stickVec[1] = inY;
    deadZoneTotal = gpad_stick_deadzone_min->current.value + gpad_stick_deadzone_max->current.value;
    len = Vec2Normalize(stickVec);
    if ( gpad_stick_deadzone_min->current.value <= len )
    {
        if ( len <= (float)(1.0 - gpad_stick_deadzone_max->current.value) )
            len = (float)(len - gpad_stick_deadzone_min->current.value) / (float)(1.0 - deadZoneTotal);
        else
            len = 1.0f;
    }
    else
    {
        len = 0.0f;
    }
    *outX = stickVec[0] * len;
    *outY = stickVec[1] * len;
}

void __cdecl GPad_UpdateSticksDown(GamePad *gPad)
{
    int dir; // [esp+Ch] [ebp-Ch]
    float threshold; // [esp+10h] [ebp-8h]
    float thresholda; // [esp+10h] [ebp-8h]
    int stickIter; // [esp+14h] [ebp-4h]

    for ( stickIter = 0; stickIter != 4; ++stickIter )
    {
        for ( dir = 0; dir != 2; ++dir )
        {
            threshold = gpad_stick_pressed->current.value;
            gPad->stickDownLast[stickIter][dir] = gPad->stickDown[stickIter][dir];
            if ( gPad->stickDownLast[stickIter][dir] )
                thresholda = threshold - gpad_stick_pressed_hysteresis->current.value;
            else
                thresholda = threshold + gpad_stick_pressed_hysteresis->current.value;
            if ( dir )
            {
                if ( dir != 1
                    && !Assert_MyHandler(
                                "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                                258,
                                0,
                                "%s",
                                "dir == GPAD_STICK_NEG") )
                {
                    __debugbreak();
                }
                //gPad->stickDown[stickIter][dir] = COERCE_FLOAT(LODWORD(thresholda) ^ _mask__NegFloat_) > gPad->sticks[stickIter];
                gPad->stickDown[stickIter][dir] = -thresholda > gPad->sticks[stickIter];
            }
            else
            {
                gPad->stickDown[stickIter][0] = gPad->sticks[stickIter] > thresholda;
            }
        }
    }
}

void __cdecl GPad_ResetState(int portIndex)
{
    GPadRawState inputStateGamepad; // [esp+0h] [ebp-Ch] BYREF (mod (gpad): retail _XINPUT_GAMEPAD)

    memset(&inputStateGamepad, 0, sizeof(inputStateGamepad));
    GPad_UpdateSticks(portIndex, &inputStateGamepad);
    GPad_UpdateDigitals(portIndex, &inputStateGamepad);
    GPad_UpdateAnalogs(portIndex, &inputStateGamepad);
}

bool __cdecl GPad_IsActive(int portIndex)
{
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    469,
                    0,
                    "%s",
                    "(portIndex >= 0) && (portIndex < MAX_GPAD_COUNT)") )
    {
        __debugbreak();
    }
    return s_gamePads[portIndex].enabled;
}

double __cdecl GPad_GetButton(int portIndex, GamePadButton button)
{
    GamePad *gPad; // [esp+4h] [ebp-Ch]
    float value; // [esp+Ch] [ebp-4h]

    value = 0.0f;
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    501,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (button & 0x30000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    502,
                    0,
                    "%s",
                    "button & ( GPAD_DIGITAL_MASK | GPAD_ANALOG_MASK )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    if ( (button & 0x10000000) != 0 )
    {
        if ( (button & 0xEFFFFFFF & gPad->digitals) != 0 )
            return 1.0f;
        else
            return 0.0f;
    }
    else if ( (button & 0x20000000) != 0 )
    {
        return gPad->analogs[button & 0xDFFFFFFF];
    }
    return value;
}

bool __cdecl GPad_ButtonRequiresUpdates(int portIndex, GamePadButton button)
{
    return (button & 0x20000000) != 0 && GPad_GetButton(portIndex, button) > 0.0;
}

bool __cdecl GPad_IsButtonPressed(int portIndex, GamePadButton button)
{
    GamePad *gPad; // [esp+Ch] [ebp-Ch]
    unsigned __int32 but; // [esp+10h] [ebp-8h]
    bool lastDown; // [esp+16h] [ebp-2h]
    bool down; // [esp+17h] [ebp-1h]

    down = 0;
    lastDown = 0;
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    539,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (button & 0x30000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    540,
                    0,
                    "%s",
                    "button & ( GPAD_DIGITAL_MASK | GPAD_ANALOG_MASK )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    if ( (button & 0x10000000) != 0 )
    {
        but = button & 0xEFFFFFFF;
        if ( (button & 0xF) != 0 && (but & gPad->digitals) != 0 && (gPad->digitals & 0xF) != (button & 0xF) )
        {
            down = 0;
            lastDown = 0;
        }
        else
        {
            down = (but & gPad->digitals) != 0;
            lastDown = ((button & 0xF) == 0 || (but & gPad->lastDigitals) == 0 || (gPad->lastDigitals & 0xF) == (button & 0xF))
                            && (but & gPad->lastDigitals) != 0;
        }
    }
    else if ( (button & 0x20000000) != 0 )
    {
        down = gPad->analogs[button & 0xDFFFFFFF] > 0.0;
        lastDown = gPad->lastAnalogs[button & 0xDFFFFFFF] > 0.0;
    }
    return down && !lastDown;
}

bool __cdecl GPad_IsButtonReleased(int portIndex, GamePadButton button)
{
    GamePad *gPad; // [esp+Ch] [ebp-Ch]
    bool lastDown; // [esp+16h] [ebp-2h]
    bool down; // [esp+17h] [ebp-1h]

    down = 0;
    lastDown = 0;
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    585,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (button & 0x30000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    586,
                    0,
                    "%s",
                    "button & ( GPAD_DIGITAL_MASK | GPAD_ANALOG_MASK )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    if ( (button & 0x10000000) != 0 )
    {
        down = (button & 0xEFFFFFFF & gPad->digitals) != 0;
        lastDown = (button & 0xEFFFFFFF & gPad->lastDigitals) != 0;
    }
    else if ( (button & 0x20000000) != 0 )
    {
        down = gPad->analogs[button & 0xDFFFFFFF] > 0.0;
        lastDown = gPad->lastAnalogs[button & 0xDFFFFFFF] > 0.0;
    }
    return !down && lastDown;
}

double __cdecl GPad_GetStick(int portIndex, GamePadStick stick)
{
    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    615,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (stick & 0x40000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    616,
                    0,
                    "%s",
                    "stick & GPAD_STICK_MASK") )
    {
        __debugbreak();
    }
    // mod (gpad): retail indexed sticks[stick] with the 0x40000000 tag still set; index * 4 wraps to the right address
    // only in 32-bit pointer arithmetic. Masking the tag gives the same element on every target.
    return s_gamePads[portIndex].sticks[stick & 0xBFFFFFFF];
}

bool __cdecl GPad_IsStickPressed(int portIndex, GamePadStick stick, GamePadStickDir stickDir)
{
    return GPad_GetStickChangedToPressedState(portIndex, stick, stickDir, 1);
}

bool __cdecl GPad_GetStickChangedToPressedState(
                int portIndex,
                GamePadStick stick,
                GamePadStickDir stickDir,
                bool pressedState)
{
    GamePad *gPad; // [esp+4h] [ebp-10h]
    unsigned __int32 stickIndex; // [esp+Ch] [ebp-8h]
    bool down; // [esp+13h] [ebp-1h]

    if ( portIndex
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    663,
                    0,
                    "%s",
                    "( portIndex >= 0 ) && ( portIndex < MAX_GPAD_COUNT )") )
    {
        __debugbreak();
    }
    if ( (stick & 0x40000000) == 0
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    664,
                    0,
                    "%s",
                    "stick & GPAD_STICK_MASK") )
    {
        __debugbreak();
    }
    if ( (unsigned int)stickDir >= GPAD_STICK_DIRCOUNT
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    665,
                    0,
                    "%s",
                    "( stickDir == GPAD_STICK_POS ) || ( stickDir == GPAD_STICK_NEG )") )
    {
        __debugbreak();
    }
    gPad = &s_gamePads[portIndex];
    stickIndex = stick & 0xBFFFFFFF;
    if ( (stick & 0xBFFFFFFF) >= 4
        && !Assert_MyHandler(
                    "C:\\projects_pc\\cod\\codsrc\\src\\win32\\win_gamepad.cpp",
                    669,
                    0,
                    "%s",
                    "(unsigned)stickIndex < GPAD_STICK_COUNT") )
    {
        __debugbreak();
    }
    down = gPad->stickDown[stickIndex][stickDir];
    return down != gPad->stickDownLast[stickIndex][stickDir] && down == pressedState;
}

bool __cdecl GPad_IsStickReleased(int portIndex, GamePadStick stick, GamePadStickDir stickDir)
{
    return GPad_GetStickChangedToPressedState(portIndex, stick, stickDir, 0);
}

// mod (gpad): retail kept lowRumble / highRumble in GamePad and the XInput vibration next to them; no PC caller set
// them (the script rumble builtins were empty stubs). Values are clamped to 0..1, forced to 0 by gpad_rumble 0, and
// passed to the backend only when they change (the first call after a pad is bound always goes through).
void __cdecl GPad_SetRumble(int portIndex, float low, float high)
{
    GamePad *gPad;

    if ( portIndex < 0 || portIndex >= 1 )
        return;
    gPad = &s_gamePads[portIndex];
    if ( !(low > 0.0f) )
        low = 0.0f;
    if ( !(high > 0.0f) )
        high = 0.0f;
    if ( low > 1.0f )
        low = 1.0f;
    if ( high > 1.0f )
        high = 1.0f;
    if ( !gpad_rumble || !gpad_rumble->current.enabled )
    {
        low = 0.0f;
        high = 0.0f;
    }
    // 64 steps: a motor cannot render finer changes, and a decaying envelope then does not call the driver every frame
    low = floorf(low * 64.0f + 0.5f) / 64.0f;
    high = floorf(high * 64.0f + 0.5f) / 64.0f;
    gPad->lowRumble = low;
    gPad->highRumble = high;
    if ( gPad->backendPort < 0 )
        return;
    if ( low == gPad->sentLowRumble && high == gPad->sentHighRumble )
        return;
    gPad->sentLowRumble = low;
    gPad->sentHighRumble = high;
    GPad_Backend_SetRumble(gPad->backendPort, low, high);
}

void __cdecl GPad_StopRumbles(int portIndex)
{
    GPad_SetRumble(portIndex, 0.0f, 0.0f);
}

int __cdecl GPad_GetBackendPort(int portIndex)
{
    if ( portIndex < 0 || portIndex >= 1 )
        return -1;
    return s_gamePads[portIndex].backendPort;
}

// mod (gpad): gpad_autoenable 1: gpad_enabled follows pad presence. Acts only when presence changes (and once at the
// first poll), so the player can still turn gpad_enabled off by hand while a pad stays connected.
static void GPad_UpdateAutoEnable(bool present)
{
    if ( !gpad_autoenable || !gpad_autoenable->current.enabled || !gpad_enabled )
    {
        s_autoEnablePresent = -1;
        return;
    }
    if ( s_autoEnablePresent == (present ? 1 : 0) )
        return;
    s_autoEnablePresent = present ? 1 : 0;
    if ( gpad_enabled->current.enabled != present )
    {
        Dvar_SetBool((dvar_s *)gpad_enabled, present);
        Com_Printf(16, "gpad_enabled set to %i (gpad_autoenable 1: game pad %s).\n", present ? 1 : 0,
            present ? "connected" : "not connected");
    }
}

// retail IN_GamepadsMove (win32/win_input.cpp), per-controller part: axes to CL_GamepadEvent, button edges (and the
// analog triggers' per-frame updates) to CL_GamepadButtonEventForPort.
static void IN_GamepadDispatch(int controllerIndex, unsigned int time)
{
    int ry; // [esp+0h] [ebp-2Ch]
    unsigned int butIndex; // [esp+4h] [ebp-28h]
    int rightTrig; // [esp+Ch] [ebp-20h]
    int lx; // [esp+14h] [ebp-18h]
    int rx; // [esp+20h] [ebp-Ch]
    int leftTrig; // [esp+24h] [ebp-8h]
    int ly; // [esp+28h] [ebp-4h]

    lx = (int)(GPad_GetStick(controllerIndex, GPAD_LX) * 65535.0);
    ly = (int)(GPad_GetStick(controllerIndex, GPAD_LY) * 65535.0);
    rx = (int)(GPad_GetStick(controllerIndex, GPAD_RX) * 65535.0);
    ry = (int)(GPad_GetStick(controllerIndex, GPAD_RY) * 65535.0);
    leftTrig = (int)(GPad_GetButton(controllerIndex, GPAD_L_TRIG) * 65535.0);
    rightTrig = (int)(GPad_GetButton(controllerIndex, GPAD_R_TRIG) * 65535.0);
    CL_GamepadEvent(controllerIndex, 0, rx);
    CL_GamepadEvent(controllerIndex, 1u, ry);
    CL_GamepadEvent(controllerIndex, 2u, lx);
    CL_GamepadEvent(controllerIndex, 3u, ly);
    CL_GamepadEvent(controllerIndex, 5u, leftTrig);
    CL_GamepadEvent(controllerIndex, 4u, rightTrig);
    for (butIndex = 0; butIndex < 0x10; ++butIndex)
    {
        if (GPad_IsButtonPressed(controllerIndex, buttonList[2 * butIndex]))
        {
            CL_GamepadButtonEventForPort(controllerIndex, buttonList[2 * butIndex + 1], 1, time, buttonList[2 * butIndex]);
        }
        else if (GPad_ButtonRequiresUpdates(controllerIndex, buttonList[2 * butIndex]))
        {
            CL_GamepadButtonEventForPort(controllerIndex, buttonList[2 * butIndex + 1], 2, time, buttonList[2 * butIndex]);
        }
        else if (GPad_IsButtonReleased(controllerIndex, buttonList[2 * butIndex]))
        {
            CL_GamepadButtonEventForPort(controllerIndex, buttonList[2 * butIndex + 1], 0, time, buttonList[2 * butIndex]);
        }
    }
}

void IN_GamepadsMove()
{
    int controllerIndex; // [esp+8h] [ebp-24h]
    char gpadPresent; // [esp+1Bh] [ebp-11h]
    unsigned int time; // [esp+1Ch] [ebp-10h]
    int localClientNum;

    if ( !gpad_debug ) // mod (gpad): IN_Frame now polls without the mouse; GPad_InitAll registers the dvars first
        return;
    gpadPresent = 0;
    GPad_UpdateAll();
    time = Sys_Milliseconds();
    for (controllerIndex = 0; controllerIndex < 1; ++controllerIndex)
    {
        if (GPad_IsActive(controllerIndex))
            gpadPresent = 1;
    }
    if (gpad_present && gpad_present->current.color[0] != gpadPresent)
        Dvar_SetBool((dvar_s*)gpad_present, gpadPresent);
    GPad_UpdateAutoEnable(gpadPresent != 0); // mod (gpad)
    // mod (gpad): gpad_enabled 0 = the pad does nothing. Retail sent the pad buttons to their binds whatever gpad_enabled
    // said (only the sticks, CL_GamepadMove, checked it), which mattered little while PC had no pad binds by default.
    // The pad is still read (gpad_present, gpad_autoenable, devgui); held input is released once.
    if ( !gpad_enabled->current.enabled )
    {
        IN_GamepadsIdle();
        return;
    }
    for (controllerIndex = 0; controllerIndex < 1; ++controllerIndex)
    {
        if (GPad_IsActive(controllerIndex))
        {
            IN_GamepadDispatch(controllerIndex, time);
            s_padDispatched[controllerIndex] = true;
            // mod (gpad): held D-pad / left stick directions repeat in menus (gpad_menu_scroll_delay_first / _rest)
            localClientNum = Com_ControllerIndex_GetLocalClientNum(controllerIndex);
            if ( localClientNum >= 0 )
                CL_GamepadRepeatScrollingButtons(localClientNum, controllerIndex);
            GPad_Rumble_Frame(controllerIndex); // mod (gpad): script rumbles to the motors
        }
        else if ( s_padDispatched[controllerIndex] )
        {
            // mod (gpad): unplugged this frame. GPad_Check reset the pad's state, but retail dispatched only active
            // pads, so the releases never went out and a held button (fire, sprint, ...) stayed held. Send them once.
            IN_GamepadDispatch(controllerIndex, time);
            s_padDispatched[controllerIndex] = false;
        }
    }
}

// mod (gpad): the window lost focus (retail simply stopped polling, so a button or stick held at that moment stayed
// held: the player kept firing / walking / turning), or gpad_enabled is 0. Release everything once and stop the motors.
void IN_GamepadsIdle()
{
    int controllerIndex;
    unsigned int time;

    if ( !gpad_debug )
        return;
    time = Sys_Milliseconds();
    for ( controllerIndex = 0; controllerIndex < 1; ++controllerIndex )
    {
        if ( !s_padDispatched[controllerIndex] )
            continue;
        s_padDispatched[controllerIndex] = false;
        GPad_ResetState(controllerIndex);
        IN_GamepadDispatch(controllerIndex, time);
        GPad_StopRumbles(controllerIndex);
    }
}
