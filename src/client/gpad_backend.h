#pragma once

// mod (gpad): platform-neutral game pad backend (NOT part of the original game).
//
// Retail win32/win_gamepad.cpp called XInput directly from the shared pad code. The pad code is now split:
//   - src/client/gpad_core.cpp: dvars, deadzones, stick / trigger / button edge logic, the GPad_* query API
//     (devgui, demo playback, ui_screenshot, cl_input_mp), the per-frame poll that turns pad state into key and axis
//     events (IN_GamepadsMove), menu auto-repeat and rumble mixing. It never includes <XInput.h>.
//   - a backend per platform, implementing the three functions below:
//       Windows: src/win32/win_gamepad.cpp (XInput, ports 0-3).
//       Web:     src/web/web_gamepad.cpp reads the page-written bo1_gamepad array of docs/web-engine-interface.md
//                section 4. Its fields map 1:1 onto GPadRawState: connected, buttons (XInput bit layout), lt, rt,
//                lx, ly, rx, ry (Y positive = up); the engine writes rumble_low / rumble_high from
//                GPad_Backend_SetRumble.
//
// Ports are backend slots (XInput user index, web gamepad slot). The core binds the first connected port to
// controller 0 (MAX_GPAD_COUNT / MAX_LOCAL_CLIENTS is 1) and rebinds when that port disconnects.
// See docs/controllers.md.

#define GPAD_BACKEND_MAX_PORTS 4 // the most any backend reports (XUSER_MAX_COUNT, BO1_MAX_GAMEPADS)

// XInput XINPUT_GAMEPAD_* button bits; the low 16 bits of the GamePadButton GPAD_* digital values (cl_gamepad.h).
#define GPAD_RAW_DPAD_UP        0x0001
#define GPAD_RAW_DPAD_DOWN      0x0002
#define GPAD_RAW_DPAD_LEFT      0x0004
#define GPAD_RAW_DPAD_RIGHT     0x0008
#define GPAD_RAW_START          0x0010
#define GPAD_RAW_BACK           0x0020
#define GPAD_RAW_LEFT_THUMB     0x0040
#define GPAD_RAW_RIGHT_THUMB    0x0080
#define GPAD_RAW_LEFT_SHOULDER  0x0100
#define GPAD_RAW_RIGHT_SHOULDER 0x0200
#define GPAD_RAW_A              0x1000
#define GPAD_RAW_B              0x2000
#define GPAD_RAW_X              0x4000
#define GPAD_RAW_Y              0x8000

struct GPadRawState
{
    bool connected;
    unsigned int buttons;   // XInput XINPUT_GAMEPAD_* bit layout (GPAD_RAW_* above)
    float lt, rt;           // triggers 0..1, no deadzone applied (the core applies gpad_button_deadzone)
    float lx, ly, rx, ry;   // sticks -1..1, Y positive = up, no deadzone applied (the core applies gpad_stick_deadzone_*)
};

// Number of ports the backend can report (XInput: 4). The core scans 0 .. min(this, GPAD_BACKEND_MAX_PORTS) - 1.
int GPad_Backend_MaxPorts();

// Reads one port. false (and *out zeroed, connected = false) = nothing connected in that port. Must be cheap to call
// every frame, also for empty ports: the XInput backend probes an empty port at most once per second, because
// XInputGetState on an empty port can stall for milliseconds.
bool GPad_Backend_Poll(int port, GPadRawState *out);

// Motor strengths 0..1 (low = left, low-frequency motor; high = right, high-frequency motor). 0, 0 stops. The core
// calls it only when the values change and only for a connected port; gpad_rumble 0 is applied by the core.
void GPad_Backend_SetRumble(int port, float low, float high);
