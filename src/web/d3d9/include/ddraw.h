/*
 * ddraw.h - the DirectDraw 7 subset gfx_d3d/r_texturemem.cpp references (video memory queries), for the BO1 web
 * port's D3D9 -> WebGL2 shim (src/web/d3d9). Written for this project.
 *
 * r_texturemem.cpp loads ddraw.dll with LoadLibrary/GetProcAddress; on the web those fail (compat layer), so nothing
 * here is ever called and no implementation exists beyond IID_IDirectDraw7 (d3d9_guids.cpp). The engine then falls
 * back to IDirect3DDevice9::GetAvailableTextureMem().
 */
#ifndef D3D9SHIM_DDRAW_H
#define D3D9SHIM_DDRAW_H

#include "d3d9shim_win32.h"

#define DD_OK S_OK
#define DDSCAPS_TEXTURE 0x00001000l
#define DDSCAPS_VIDEOMEMORY 0x00004000l
#define DDSCAPS_LOCALVIDMEM 0x10000000l
#define DDSCAPS_NONLOCALVIDMEM 0x20000000l
#define DDENUM_ATTACHEDSECONDARYDEVICES 0x00000001L
#define DDENUM_DETACHEDSECONDARYDEVICES 0x00000002L
#define DDENUM_NONDISPLAYDEVICES 0x00000004L
#define DDSCL_NORMAL 0x00000008l

typedef struct _DDSCAPS2 {
    DWORD dwCaps;
    DWORD dwCaps2;
    DWORD dwCaps3;
    union {
        DWORD dwCaps4;
        DWORD dwVolumeDepth;
    };
} DDSCAPS2, *LPDDSCAPS2;

#ifdef __cplusplus
struct IDirectDraw7 : public IUnknown {
    virtual HRESULT SetCooperativeLevel(HWND hWnd, DWORD dwFlags) = 0;
    virtual HRESULT GetAvailableVidMem(LPDDSCAPS2 lpDDSCaps2, DWORD *lpdwTotal, DWORD *lpdwFree) = 0;
};
typedef IDirectDraw7 *LPDIRECTDRAW7;
extern "C" const GUID IID_IDirectDraw7;
#endif

typedef BOOL(WINAPI *LPDDENUMCALLBACKEXA)(GUID *, LPSTR, LPSTR, LPVOID, HMONITOR);

#endif /* D3D9SHIM_DDRAW_H */
