/*
 * dxerr.h - DirectX error strings for the BO1 web port's D3D9 -> WebGL2 shim (src/web/d3d9).
 * Written for this project; implemented in src/web/d3d9/d3d9_log.cpp. Covers D3DERR_*, D3DOK_*, S_* and E_* codes.
 */
#ifndef D3D9SHIM_DXERR_H
#define D3D9SHIM_DXERR_H

#include "d3d9.h"

#ifdef __cplusplus
extern "C" {
#endif
const char *WINAPI DXGetErrorStringA(HRESULT hr);
const char *WINAPI DXGetErrorDescriptionA(HRESULT hr);
#ifdef __cplusplus
}
#endif
#define DXGetErrorString DXGetErrorStringA
#define DXGetErrorDescription DXGetErrorDescriptionA

#endif /* D3D9SHIM_DXERR_H */
