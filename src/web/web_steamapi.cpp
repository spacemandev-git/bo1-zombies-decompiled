// web_steamapi.cpp - the Steamworks C API for the web build: "Steam is not running".
//
// The engine's own no-Steam path (win32/win_steam.cpp, live/live_steam*.cpp, compiled unchanged) runs exactly as on a
// Windows machine without the Steam client: SteamAPI_Init reports k_ESteamAPIInitResult_NoSteamClient, every
// interface accessor returns null, callbacks never fire. Multiplayer identity and connections go through the page
// (docs/web-engine-interface.md), not Steam.
#include <steam/steam_api.h>
#include <steam/steam_gameserver.h>
#include <string.h>

extern "C" {

S_API ESteamAPIInitResult S_CALLTYPE SteamInternal_SteamAPI_Init(const char *pszInternalCheckInterfaceVersions, SteamErrMsg *pOutErrMsg)
{
    if (pOutErrMsg)
        strncpy(*pOutErrMsg, "Steam is not available in the browser", sizeof(SteamErrMsg) - 1);
    return k_ESteamAPIInitResult_NoSteamClient;
}

S_API void S_CALLTYPE SteamAPI_RunCallbacks()
{
}

S_API void S_CALLTYPE SteamAPI_RegisterCallback(class CCallbackBase *pCallback, int iCallback)
{
}

S_API void S_CALLTYPE SteamAPI_UnregisterCallback(class CCallbackBase *pCallback)
{
}

S_API HSteamUser S_CALLTYPE SteamAPI_GetHSteamUser()
{
    return 0;
}

S_API HSteamUser S_CALLTYPE SteamGameServer_GetHSteamUser()
{
    return 0;
}

S_API void S_CALLTYPE SteamGameServer_RunCallbacks()
{
}

// the accessor context of STEAM_DEFINE_INTERFACE_ACCESSOR: { init function, counter, interface pointer }. The init
// function runs once; it stores what FindOrCreate* returns (null here).
S_API void *S_CALLTYPE SteamInternal_ContextInit(void *pContextInitData)
{
    void **ctx = (void **)pContextInitData;
    if (!ctx[1])
    {
        ctx[1] = (void *)1;
        ((void (*)(void *))ctx[0])(&ctx[2]);
    }
    return &ctx[2];
}

S_API void *S_CALLTYPE SteamInternal_CreateInterface(const char *ver)
{
    return nullptr;
}

S_API void *S_CALLTYPE SteamInternal_FindOrCreateUserInterface(HSteamUser hSteamUser, const char *pszVersion)
{
    return nullptr;
}

S_API void *S_CALLTYPE SteamInternal_FindOrCreateGameServerInterface(HSteamUser hSteamUser, const char *pszVersion)
{
    return nullptr;
}

} // extern "C"
