// mod (gpad): named script rumbles (NOT part of the original game).
//
// Console builds play rumble assets (an intensity graph per motor, a duration, a falloff range) loaded by
// precacheRumble. The PC exe has no rumble asset type: precacherumble, playrumbleonentity, playrumblelooponentity,
// stoprumble, playrumbleonposition, playrumblelooponposition and stopallrumbles were empty builtins on both script
// VMs (game_mp/g_scr_main_mp.cpp, cgame/cg_scr_main.cpp). Here a rumble name picks a built-in envelope by substring
// (first match in s_rumbleProfiles): low / high motor strength, a duration over which a one-shot fades linearly to 0,
// and a falloff radius for the positional builtins. Loops hold their strength until stopped by name. All active
// rumbles mix by taking the strongest value per motor.
//
// Callers: the server script builtins (possibly on the server thread) and client script builtins push requests under
// a spin lock: for the local player, for a client number (server scripts: played only when it is this process's
// player, so on a listen server the host feels its own rumbles and remote co-op players feel none), or at a position
// (scaled by 1 - distance / radius from the local player, resolved once when the request is picked up).
// GPad_Rumble_Frame (main thread, from IN_GamepadsMove) resolves the requests, mixes and drives the pad through
// GPad_SetRumble (gpad_rumble 0 = off, gpad_rumble_scale = overall strength). Rumble is muted while a menu other than
// the scoreboard has the keys (pause menu, front end) and every rumble is dropped while the local client is not in a
// game (map change, quit).
// See docs/controllers.md.

#include "gpad_core.h"
#include "client.h"
#include "cl_keys.h"
#include <ui/ui_main.h>
#include <cgame_mp/cg_main_mp.h>
#include <win32/win_shared.h>
#include <universal/q_shared.h>
#include <qcommon/cmd.h>
#include <qcommon/common.h>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

struct GPadRumbleProfile
{
    const char *match;  // substring of the rumble name (lower case)
    float low;          // left (low-frequency) motor 0..1
    float high;         // right (high-frequency) motor 0..1
    int durationMs;     // one-shot length; the envelope fades linearly to 0 over it
    float radius;       // positional falloff radius (world units)
};

// Keyed on the naming of the IW / Treyarch rumble assets (damage_heavy, damage_light, explosion_generic,
// grenade_rumble, artillery_rumble, tank_rumble, *_fire, reload_*, melee_*, ...). The retail scripts are not in the
// repo, so the names a map actually uses are not verified; anything unmatched gets s_rumbleDefault. Values are
// hand-tuned, not measured from console. Order matters: first match wins.
static const GPadRumbleProfile s_rumbleProfiles[] =
{
    { "damage_heavy", 0.90f, 0.70f, 350, 512.0f },
    { "damage_light", 0.45f, 0.35f, 200, 512.0f },
    { "damage",       0.65f, 0.50f, 250, 512.0f },
    { "explosion",    1.00f, 0.80f, 600, 1000.0f },
    { "artillery",    1.00f, 0.80f, 700, 1500.0f },
    { "grenade",      0.90f, 0.70f, 500, 800.0f },
    { "bomb",         1.00f, 0.80f, 600, 1000.0f },
    { "quake",        0.60f, 0.40f, 1000, 2000.0f },
    { "tank",         0.80f, 0.60f, 300, 1000.0f },
    { "minigun",      0.50f, 0.60f, 120, 400.0f },
    { "lmg",          0.40f, 0.55f, 110, 400.0f },
    { "shotgun",      0.60f, 0.80f, 160, 400.0f },
    { "sniper",       0.55f, 0.80f, 160, 400.0f },
    { "pistol",       0.15f, 0.45f, 90, 300.0f },
    { "fire",         0.30f, 0.55f, 110, 400.0f },
    { "melee",        0.55f, 0.50f, 160, 300.0f },
    { "knife",        0.55f, 0.50f, 160, 300.0f },
    { "impact",       0.55f, 0.50f, 160, 400.0f },
    { "reload",       0.10f, 0.30f, 90, 200.0f },
    { "slide",        0.30f, 0.20f, 300, 300.0f },
};
static const GPadRumbleProfile s_rumbleDefault = { "", 0.50f, 0.50f, 250, 750.0f };

#define GPAD_RUMBLE_MAX 16

enum GPadRumbleTarget
{
    GPAD_RUMBLE_TARGET_LOCAL,     // the local player
    GPAD_RUMBLE_TARGET_CLIENT,    // a client number, played only when it is the local player
    GPAD_RUMBLE_TARGET_POSITION,  // a world position, scaled by distance to the local player
};

struct GPadRumble
{
    bool active;
    bool pending;                 // pushed, not resolved yet (target / origin still to check on the main thread)
    bool loop;
    char name[64];
    const GPadRumbleProfile *profile;
    float scale;
    unsigned int startTime;
    GPadRumbleTarget target;
    int clientNum;
    float origin[3];
};

static GPadRumble s_rumbles[GPAD_RUMBLE_MAX];
static std::atomic_flag s_rumbleLock = ATOMIC_FLAG_INIT;

static void GPad_Rumble_Lock()
{
    while ( s_rumbleLock.test_and_set(std::memory_order_acquire) )
        ;
}

static void GPad_Rumble_Unlock()
{
    s_rumbleLock.clear(std::memory_order_release);
}

static bool GPad_Rumble_NameContains(const char *name, const char *match)
{
    size_t matchLen = strlen(match);
    size_t i;

    for ( ; *name; ++name )
    {
        for ( i = 0; i < matchLen; ++i )
        {
            if ( !name[i] || tolower((unsigned char)name[i]) != match[i] )
                break;
        }
        if ( i == matchLen )
            return true;
    }
    return false;
}

static const GPadRumbleProfile *GPad_Rumble_FindProfile(const char *name)
{
    unsigned int i;

    if ( name )
    {
        for ( i = 0; i < sizeof(s_rumbleProfiles) / sizeof(s_rumbleProfiles[0]); ++i )
        {
            if ( GPad_Rumble_NameContains(name, s_rumbleProfiles[i].match) )
                return &s_rumbleProfiles[i];
        }
    }
    return &s_rumbleDefault;
}

static void GPad_Rumble_Push(const char *name, bool loop, GPadRumbleTarget target, int clientNum, const float *origin)
{
    const GPadRumbleProfile *profile;
    GPadRumble *slot;
    unsigned int now;
    int i;

    if ( !name )
        name = "";
    profile = GPad_Rumble_FindProfile(name);
    now = Sys_Milliseconds();
    GPad_Rumble_Lock();
    slot = 0;
    if ( loop )
    {
        for ( i = 0; i < GPAD_RUMBLE_MAX; ++i )
        {
            if ( s_rumbles[i].active && s_rumbles[i].loop && !I_stricmp(s_rumbles[i].name, name) )
            {
                slot = &s_rumbles[i];
                break;
            }
        }
    }
    if ( !slot )
    {
        for ( i = 0; i < GPAD_RUMBLE_MAX; ++i )
        {
            if ( !s_rumbles[i].active )
            {
                slot = &s_rumbles[i];
                break;
            }
        }
    }
    if ( !slot )
    {
        // full: replace the oldest one-shot (loops stay until stopped)
        for ( i = 0; i < GPAD_RUMBLE_MAX; ++i )
        {
            if ( !s_rumbles[i].loop && (!slot || (int)(s_rumbles[i].startTime - slot->startTime) < 0) )
                slot = &s_rumbles[i];
        }
    }
    if ( slot )
    {
        slot->active = true;
        slot->pending = true;
        slot->loop = loop;
        I_strncpyz(slot->name, name, sizeof(slot->name));
        slot->profile = profile;
        slot->scale = 1.0f;
        slot->startTime = now;
        slot->target = target;
        slot->clientNum = clientNum;
        slot->origin[0] = origin ? origin[0] : 0.0f;
        slot->origin[1] = origin ? origin[1] : 0.0f;
        slot->origin[2] = origin ? origin[2] : 0.0f;
    }
    GPad_Rumble_Unlock();
}

void GPad_Rumble_Play(const char *name, bool loop)
{
    GPad_Rumble_Push(name, loop, GPAD_RUMBLE_TARGET_LOCAL, -1, 0);
}

void GPad_Rumble_PlayForClient(const char *name, int clientNum, bool loop)
{
    GPad_Rumble_Push(name, loop, GPAD_RUMBLE_TARGET_CLIENT, clientNum, 0);
}

void GPad_Rumble_PlayAtPosition(const char *name, const float *origin, bool loop)
{
    GPad_Rumble_Push(name, loop, GPAD_RUMBLE_TARGET_POSITION, -1, origin);
}

// Main thread, lock held: decides whether a pushed rumble reaches this player and how strong. false = drop it.
static bool GPad_Rumble_Resolve(GPadRumble *rumble)
{
    const clientActive_t *cl;
    float delta[3];
    float dist;

    rumble->pending = false;
    if ( rumble->target == GPAD_RUMBLE_TARGET_LOCAL )
        return true;
    cl = CL_GetLocalClientGlobals(0);
    if ( !cl )
        return false;
    if ( rumble->target == GPAD_RUMBLE_TARGET_CLIENT )
        return rumble->clientNum == CG_GetClientNumForLocalClient(0); // the player's own number (not a followed one)
    delta[0] = rumble->origin[0] - cl->snap.ps.origin[0];
    delta[1] = rumble->origin[1] - cl->snap.ps.origin[1];
    delta[2] = rumble->origin[2] - cl->snap.ps.origin[2];
    dist = sqrtf(delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]);
    if ( dist >= rumble->profile->radius )
        return false;
    rumble->scale = 1.0f - dist / rumble->profile->radius;
    return true;
}

void GPad_Rumble_Stop(const char *name)
{
    int i;

    GPad_Rumble_Lock();
    for ( i = 0; i < GPAD_RUMBLE_MAX; ++i )
    {
        if ( s_rumbles[i].active && (!name || !*name || !I_stricmp(s_rumbles[i].name, name)) )
            s_rumbles[i].active = false;
    }
    GPad_Rumble_Unlock();
}

void GPad_Rumble_Frame(int controllerIndex)
{
    const GPadRumbleProfile *profile;
    unsigned int now;
    unsigned int elapsed;
    float envelope;
    float low;
    float high;
    float strength;
    bool inGame;
    int i;

    now = Sys_Milliseconds();
    inGame = CL_GetLocalClientConnectionState(0) == CA_ACTIVE;
    low = 0.0f;
    high = 0.0f;
    GPad_Rumble_Lock();
    for ( i = 0; i < GPAD_RUMBLE_MAX; ++i )
    {
        if ( !s_rumbles[i].active )
            continue;
        profile = s_rumbles[i].profile;
        elapsed = now - s_rumbles[i].startTime;
        if ( !inGame || (!s_rumbles[i].loop && elapsed >= (unsigned int)profile->durationMs)
            || (s_rumbles[i].pending && !GPad_Rumble_Resolve(&s_rumbles[i])) )
        {
            s_rumbles[i].active = false;
            continue;
        }
        envelope = s_rumbles[i].loop ? 1.0f : 1.0f - (float)elapsed / (float)profile->durationMs;
        strength = s_rumbles[i].scale * envelope;
        if ( profile->low * strength > low )
            low = profile->low * strength;
        if ( profile->high * strength > high )
            high = profile->high * strength;
    }
    GPad_Rumble_Unlock();
    if ( Key_IsCatcherActive(0, 0x10) && UI_GetActiveMenu(0) != UIMENU_SCOREBOARD ) // a menu, not the held scoreboard
    {
        low = 0.0f;
        high = 0.0f;
    }
    if ( gpad_rumble_scale )
    {
        low *= gpad_rumble_scale->current.value;
        high *= gpad_rumble_scale->current.value;
    }
    GPad_SetRumble(controllerIndex, low, high);
}

// mod (gpad): "gpad_rumbletest [name] [loop]" plays a named rumble on the local player ("gpad_rumbletest stop" stops
// all), for checking the motors and the envelopes without a script. Plays only in a game with no menu up, as script
// rumbles do. Default name damage_heavy.
static void GPad_Rumble_Test_f()
{
    const char *name;
    bool loop;

    name = Cmd_Argc() > 1 ? Cmd_Argv(1) : "damage_heavy";
    if ( !I_stricmp(name, "stop") )
    {
        GPad_Rumble_Stop(0);
        Com_Printf(16, "gpad_rumbletest: stopped\n");
        return;
    }
    loop = Cmd_Argc() > 2 && atoi(Cmd_Argv(2)) != 0;
    GPad_Rumble_Play(name, loop);
    Com_Printf(16, "gpad_rumbletest: %s%s\n", name, loop ? " (loop)" : "");
}

static cmd_function_s GPad_Rumble_Test_f_VAR;

void GPad_Rumble_RegisterCommands()
{
    static bool registered;

    if ( registered ) // GPad_InitAll runs again on in_restart
        return;
    registered = true;
    Cmd_AddCommandInternal("gpad_rumbletest", GPad_Rumble_Test_f, &GPad_Rumble_Test_f_VAR);
}
