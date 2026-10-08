// web_bridge.h - the engine side of docs/web-engine-interface.md (the contract between the wasm engine and the page).
// Change the contract document first, then this header and the page (web/client/src/engine/bridge.ts) together.
#pragma once
#include <stdint.h>
#include <emscripten.h>

#ifdef __cplusplus
#include <atomic>
#define BO1_ATOMIC_U32 std::atomic<uint32_t>
extern "C" {
#else
#include <stdatomic.h>
#define BO1_ATOMIC_U32 _Atomic uint32_t
#endif

// ----- section 3: network rings -----
#define BO1_NET_MAX_PACKET 1400
#define BO1_NET_SLOT_SIZE 1408           // 8-byte header + BO1_NET_MAX_PACKET
#define BO1_NET_RING_CAPACITY 256        // slots per ring (a power of two)
#define BO1_NET_GAME_PORT 28960          // every player's port (web/shared/protocol.ts GAME_PORT)

typedef struct bo1_net_ring
{
    uint32_t capacity;                   // number of slots, a power of two (engine sets it: 256)
    uint32_t slot_size;                  // BO1_NET_SLOT_SIZE
    BO1_ATOMIC_U32 write_index;          // producer: write slot (write_index % capacity), then increment
    BO1_ATOMIC_U32 read_index;           // consumer: read slot (read_index % capacity), then increment
    // then capacity slots, each: uint32_t ip; uint16_t port; uint16_t length; uint8_t data[BO1_NET_MAX_PACKET];
} bo1_net_ring;                          // header is 16 bytes, slots start at offset 16

typedef struct bo1_net_slot
{
    uint32_t ip;                         // a | b<<8 | c<<16 | d<<24 for a.b.c.d (10.66.0.1 = 0x0100420A)
    uint16_t port;                       // host byte order
    uint16_t length;
    uint8_t data[BO1_NET_MAX_PACKET];
} bo1_net_slot;

typedef struct bo1_net_shared
{
    bo1_net_ring *to_page;               // engine produces (Sys_SendPacket), page consumes
    bo1_net_ring *from_page;             // page produces, engine consumes (Sys_GetPacket)
    uint32_t local_ip;                   // page sets before main(): this player's fake address
    uint32_t ready;                      // engine sets to 1 once the rings exist
} bo1_net_shared;

EMSCRIPTEN_KEEPALIVE bo1_net_shared *bo1_net_get_shared(void);

// ----- section 4: gamepads -----
#define BO1_MAX_GAMEPADS 4

typedef struct bo1_gamepad
{
    uint32_t connected;                  // page: 1 if a pad with mapping "standard" is in this slot
    uint32_t buttons;                    // page: XInput button bits
    float lt, rt;                        // page: triggers 0..1
    float lx, ly, rx, ry;                // page: sticks -1..1, Y positive = up
    float rumble_low, rumble_high;       // engine: 0..1 motor strengths; the page plays them
    uint32_t seq;                        // page: incremented after every write of this entry
    uint32_t reserved;                   // padding: entries are 48 bytes apart (the contract's size; the page's
                                         // GAMEPAD_STRIDE, web/client/src/input) - the fields above end at 44
} bo1_gamepad;                           // 48 bytes

EMSCRIPTEN_KEEPALIVE bo1_gamepad *bo1_input_gamepads(void);   // BO1_MAX_GAMEPADS entries

#ifdef __cplusplus
}
static_assert(sizeof(bo1_net_ring) == 16, "bo1_net_ring header: 16 bytes (docs/web-engine-interface.md)");
static_assert(sizeof(bo1_net_slot) == BO1_NET_SLOT_SIZE, "bo1_net_ring slot size");
static_assert(sizeof(bo1_net_shared) == 16, "bo1_net_shared layout");
static_assert(sizeof(bo1_gamepad) == 48, "bo1_gamepad: 48 bytes (docs/web-engine-interface.md)");
#endif

// ----- engine-internal web helpers (src/web/*.cpp, src/web/compat/*.cpp) -----
#ifdef __cplusplus
extern "C" {
#endif
int bo1_web_resolve_path(const char *in, char *out, int outSize);   // compat/web_path.cpp: 1 if it exists
void bo1_web_invalidate_path(const char *resolvedPath);
void bo1_web_report_error(const char *message);                    // compat/win_crt.cpp: Module.onEngineError
__attribute__((noreturn)) void bo1_web_exit(int code);              // Module.onEngineExit, then exit
void bo1_web_set_home_dir(const char *dir);                         // compat/win_file.cpp (SHGetFolderPathA)
void bo1_web_input_init(void);                                      // web_input.cpp
void bo1_web_input_pump(void);                                      // web_input.cpp: queued page input -> engine
#ifdef __cplusplus
}
#endif
