// web_gamepad.cpp - the game pad backend (client/gpad_backend.h) of the web build.
//
// The Gamepad API exists only on the page's main thread: the page polls navigator.getGamepads() every animation frame
// and writes each pad into the bo1_gamepad array below (docs/web-engine-interface.md section 4). The fields are the
// backend's GPadRawState one for one; rumble goes the other way (rumble_low / rumble_high, played by the page).
#include <client/gpad_backend.h>

#include "web_bridge.h"

#include <atomic>

namespace
{
bo1_gamepad s_pads[BO1_MAX_GAMEPADS];
}

extern "C" EMSCRIPTEN_KEEPALIVE bo1_gamepad *bo1_input_gamepads(void)
{
    return s_pads;
}

int GPad_Backend_MaxPorts()
{
    return BO1_MAX_GAMEPADS;
}

bool GPad_Backend_Poll(int port, GPadRawState *out)
{
    *out = GPadRawState{};
    if (port < 0 || port >= BO1_MAX_GAMEPADS)
        return false;
    volatile bo1_gamepad &p = s_pads[port];
    // the page may be writing this entry right now: a copy is good when seq did not move during it
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before = __atomic_load_n(&s_pads[port].seq, __ATOMIC_ACQUIRE);
        GPadRawState s;
        s.connected = p.connected != 0;
        s.buttons = p.buttons & 0xFFFF;
        s.lt = p.lt;
        s.rt = p.rt;
        s.lx = p.lx;
        s.ly = p.ly;
        s.rx = p.rx;
        s.ry = p.ry;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        if (__atomic_load_n(&s_pads[port].seq, __ATOMIC_RELAXED) == before)
        {
            if (!s.connected)
                return false;
            *out = s;
            return true;
        }
    }
    return false;
}

void GPad_Backend_SetRumble(int port, float low, float high)
{
    if (port < 0 || port >= BO1_MAX_GAMEPADS)
        return;
    __atomic_store_n((uint32_t *)&s_pads[port].rumble_low, *(uint32_t *)&low, __ATOMIC_RELAXED);
    __atomic_store_n((uint32_t *)&s_pads[port].rumble_high, *(uint32_t *)&high, __ATOMIC_RELAXED);
}
