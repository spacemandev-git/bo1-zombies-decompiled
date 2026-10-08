// dsound.h - DirectSound declarations for the web build. Only gfx_d3d/r_cinematic.cpp names it (Bink's sound
// output); DirectSoundCreate8 fails (web_cinematic.cpp), so nothing is ever called through these interfaces.
#pragma once
#include "windows.h"
#ifdef __cplusplus
struct IDirectSound8 : IUnknown
{
    virtual HRESULT SetCooperativeLevel(HWND hwnd, DWORD level) = 0;
};
struct IDirectSoundBuffer8 : IUnknown {};
typedef IDirectSound8 *LPDIRECTSOUND8, *LPDIRECTSOUND;
typedef IDirectSoundBuffer8 *LPDIRECTSOUNDBUFFER8, *LPDIRECTSOUNDBUFFER;
#endif
#define DS_OK S_OK
#define DSERR_NODRIVER MAKE_HRESULT(1, 0x878, 120)
#define DSSCL_PRIORITY 0x00000002
BO1_EXTERN_C_BEGIN
#ifdef __cplusplus
HRESULT DirectSoundCreate8(LPCGUID device, LPDIRECTSOUND8 *ds, LPUNKNOWN outer);   // web: DSERR_NODRIVER
#endif
BO1_EXTERN_C_END
