#pragma once

// mod (gpad): the platform-neutral half of retail win32/win_gamepad.h (the GPad_* API and its dvars). The XInput types
// that were in the retail header (_XINPUT_GAMEPAD parameters, _XINPUT_VIBRATION / _XINPUT_CAPABILITIES members) moved
// behind the backend interface (client/gpad_backend.h). win32/win_gamepad.h now only includes this file.
// See docs/controllers.md.

#include <universal/dvar.h>
#include <client/cl_gamepad.h>
#include <client/gpad_backend.h>

struct GamePad // retail sizeof=0x7C (with the XInput members); mod (gpad): those replaced by the last three fields
{                                       // XREF: .data:s_gamePads/r
    bool enabled;
    bool keyboardEnabled;
    __int16 digitals;
    __int16 lastDigitals;
    // padding byte
    // padding byte
    float analogs[2];
    float lastAnalogs[2];
    float sticks[4];
    float lastSticks[4];
    bool stickDown[4][2];
    bool stickDownLast[4][2];
    float lowRumble;
    float highRumble;
    // mod (gpad): retail ended with feedback (_XINPUT_VIBRATION), caps and keyboardCaps (_XINPUT_CAPABILITIES).
    int backendPort;                    // backend port feeding this pad, -1 = none bound yet
    float sentLowRumble;                // motor values last passed to GPad_Backend_SetRumble (-1 = unknown, resend)
    float sentHighRumble;
};

void GPad_InitAll();
void __cdecl GPad_UpdateAll();
bool __cdecl GPad_Check(int portIndex, GPadRawState *raw);
void __cdecl GPad_UpdateDigitals(int portIndex, const GPadRawState *raw);
void __cdecl GPad_UpdateAnalogs(int portIndex, const GPadRawState *raw);
void __cdecl GPad_UpdateSticks(int portIndex, const GPadRawState *raw);
void __cdecl GPad_ConvertStickToFloat(float inX, float inY, float *outX, float *outY);
void __cdecl GPad_UpdateSticksDown(GamePad *gPad);
void __cdecl GPad_ResetState(int portIndex);
bool __cdecl GPad_IsActive(int portIndex);
double __cdecl GPad_GetButton(int portIndex, GamePadButton button);
bool __cdecl GPad_ButtonRequiresUpdates(int portIndex, GamePadButton button);
bool __cdecl GPad_IsButtonPressed(int portIndex, GamePadButton button);
bool __cdecl GPad_IsButtonReleased(int portIndex, GamePadButton button);
double __cdecl GPad_GetStick(int portIndex, GamePadStick stick);
bool __cdecl GPad_IsStickPressed(int portIndex, GamePadStick stick, GamePadStickDir stickDir);
bool __cdecl GPad_GetStickChangedToPressedState(
                int portIndex,
                GamePadStick stick,
                GamePadStickDir stickDir,
                bool pressedState);
bool __cdecl GPad_IsStickReleased(int portIndex, GamePadStick stick, GamePadStickDir stickDir);

// mod (gpad): rumble. Motor strengths 0..1; gpad_rumble 0 forces 0. Sent to the backend only when they change.
void __cdecl GPad_SetRumble(int portIndex, float low, float high);
void __cdecl GPad_StopRumbles(int portIndex);
int __cdecl GPad_GetBackendPort(int portIndex);

// mod (gpad): the per-frame poll (retail IN_GamepadsMove, moved from win32/win_input.cpp so every platform shares it).
// The platform input frame calls IN_GamepadsMove while the game window has focus (or gpad_background is 1) and
// IN_GamepadsIdle otherwise: the idle call releases held buttons and zeroes the axes once, so nothing stays held while
// the window is in the background.
void IN_GamepadsMove();
void IN_GamepadsIdle();

// mod (gpad): named rumbles from script (gpad_rumble.cpp). The engine has no rumble asset type on PC (retail rumble
// builtins were empty), so a name maps to a built-in intensity / duration envelope. Thread-safe: the server script
// builtins may run on the server thread; requests are resolved on the main thread by GPad_Rumble_Frame.
void GPad_Rumble_Play(const char *name, bool loop);                              // on the local player
void GPad_Rumble_PlayForClient(const char *name, int clientNum, bool loop);      // only if clientNum is the local player
void GPad_Rumble_PlayAtPosition(const char *name, const float *origin, bool loop); // scaled by distance to the local player
void GPad_Rumble_Stop(const char *name); // 0 or "" = every rumble
void GPad_Rumble_Frame(int controllerIndex); // main thread, once per input frame: resolves, mixes, drives the motors
void GPad_Rumble_RegisterCommands(); // gpad_rumbletest

extern const dvar_t *gpad_debug;
extern const dvar_t *gpad_button_lstick_deflect_max;
extern const dvar_t *gpad_button_rstick_deflect_max;
extern const dvar_t *gpad_button_deadzone;
extern const dvar_t *gpad_stick_deadzone_min;
extern const dvar_t *gpad_stick_deadzone_max;
extern const dvar_t *gpad_stick_pressed;
extern const dvar_t *gpad_stick_pressed_hysteresis;
extern const dvar_t *gpad_rumble;
extern const dvar_t *gpad_menu_scroll_delay_first;
extern const dvar_t *gpad_menu_scroll_delay_rest;
extern const dvar_t *gpad_buttonsConfig;
extern const dvar_t *gpad_sticksConfig;
extern const dvar_t *gpad_enabled;
extern const dvar_t *gpad_present;
extern const dvar_t *gpad_autoenable;  // mod (gpad)
extern const dvar_t *gpad_background;  // mod (gpad)
extern const dvar_t *gpad_rumble_scale; // mod (gpad)
