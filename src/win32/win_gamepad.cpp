// mod (gpad): the XInput backend of the game pad code (client/gpad_backend.h). Retail win_gamepad.cpp held the whole pad
// stack and called XInput from it; that code (dvars, deadzones, stick / trigger / button logic, the GPad_* API) moved
// unchanged to client/gpad_core.cpp, which now reads pads only through the three functions below. See
// docs/controllers.md.
//
// Retail read only XInput port 0 and asked XInputGetCapabilities for it every frame. XInputGetState /
// XInputGetCapabilities on an empty port can take milliseconds (the driver re-enumerates), so an empty port is probed
// here at most once per GPAD_XINPUT_PROBE_MS, and a connected port costs one XInputGetState per frame.

#include <Windows.h>
#include <XInput.h>

#include <client/gpad_backend.h>
#include "win_gamepad.h"
#include "win_main.h"
#include "win_shared.h"
#include <qcommon/common.h>
#include <cstring>

#define GPAD_XINPUT_PROBE_MS 1000

static bool s_xinputConnected[XUSER_MAX_COUNT];
static unsigned int s_xinputNextProbe[XUSER_MAX_COUNT]; // Sys_Milliseconds() after which an empty port is tried again
static bool s_xinputProbed[XUSER_MAX_COUNT];

int GPad_Backend_MaxPorts()
{
    return XUSER_MAX_COUNT;
}

static float GPad_XInput_Stick(SHORT value)
{
    // retail GPad_ConvertStickToFloat divided by 32767.0; -32768 gives slightly less than -1, which the deadzone code
    // maps to full deflection the same way
    return (float)value / 32767.0f;
}

bool GPad_Backend_Poll(int port, GPadRawState *out)
{
    XINPUT_STATE state;
    XINPUT_CAPABILITIES caps;
    unsigned int now;

    memset(out, 0, sizeof(*out));
    if ( port < 0 || port >= XUSER_MAX_COUNT )
        return false;
    now = Sys_Milliseconds();
    if ( !s_xinputConnected[port] && s_xinputProbed[port] && (int)(now - s_xinputNextProbe[port]) < 0 )
        return false;
    memset(&state, 0, sizeof(state));
    if ( XInputGetState(port, &state) != ERROR_SUCCESS )
    {
        // the first failure of each port is offset by a quarter period per port, so four empty ports are probed in
        // four different frames (one probe every GPAD_XINPUT_PROBE_MS / 4) instead of all in the same frame
        s_xinputNextProbe[port] = now + GPAD_XINPUT_PROBE_MS
            + (s_xinputProbed[port] ? 0 : port * (GPAD_XINPUT_PROBE_MS / XUSER_MAX_COUNT));
        s_xinputConnected[port] = false;
        s_xinputProbed[port] = true;
        return false;
    }
    if ( !s_xinputConnected[port] )
    {
        s_xinputConnected[port] = true;
        memset(&caps, 0, sizeof(caps));
        if ( XInputGetCapabilities(port, XINPUT_FLAG_GAMEPAD, &caps) == ERROR_SUCCESS )
            Com_Printf(16, "XInput port %i: type %i subtype %i.\n", port, caps.Type, caps.SubType);
    }
    out->connected = true;
    out->buttons = state.Gamepad.wButtons;
    out->lt = (float)state.Gamepad.bLeftTrigger / 255.0f;
    out->rt = (float)state.Gamepad.bRightTrigger / 255.0f;
    out->lx = GPad_XInput_Stick(state.Gamepad.sThumbLX);
    out->ly = GPad_XInput_Stick(state.Gamepad.sThumbLY);
    out->rx = GPad_XInput_Stick(state.Gamepad.sThumbRX);
    out->ry = GPad_XInput_Stick(state.Gamepad.sThumbRY);
    return true;
}

void GPad_Backend_SetRumble(int port, float low, float high)
{
    XINPUT_VIBRATION vibration;

    if ( port < 0 || port >= XUSER_MAX_COUNT || !s_xinputConnected[port] )
        return;
    if ( Sys_IsHeadless() ) // zombies: headless never writes to the user's game pad (rumble); retail GPad_InitAll check
        return;
    if ( !(low > 0.0f) )
        low = 0.0f;
    if ( !(high > 0.0f) )
        high = 0.0f;
    if ( low > 1.0f )
        low = 1.0f;
    if ( high > 1.0f )
        high = 1.0f;
    vibration.wLeftMotorSpeed = (WORD)(low * 65535.0f);
    vibration.wRightMotorSpeed = (WORD)(high * 65535.0f);
    XInputSetState(port, &vibration);
}
