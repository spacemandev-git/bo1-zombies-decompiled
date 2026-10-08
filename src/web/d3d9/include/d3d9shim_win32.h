/*
 * d3d9shim_win32.h - Win32/COM base types the D3D9 shim headers need.
 *
 * Part of the BO1 web port's Direct3D 9 -> WebGL2 shim (src/web/d3d9). Not a Microsoft header.
 *
 * Two modes:
 *   1. Engine build: the web compat layer (src/web/compat) puts its replacement <windows.h> on the include path.
 *      We include it (plus <objbase.h> / <unknwn.h> if present, like the real d3d9.h does) and use ITS types.
 *      The compat layer must provide: BYTE WORD DWORD UINT INT LONG ULONG BOOL HRESULT HANDLE HWND(HWND__*)
 *      HMONITOR(HMONITOR__*) RECT POINT LARGE_INTEGER GUID(struct _GUID) IID REFIID REFGUID IUnknown
 *      (QueryInterface/AddRef/Release) S_OK S_FALSE E_FAIL E_NOTIMPL E_OUTOFMEMORY E_INVALIDARG E_NOINTERFACE
 *      E_POINTER SUCCEEDED FAILED MAKE_HRESULT. Everything else (RGNDATA, PALETTEENTRY, HDC, LUID) is referenced
 *      only through struct tags (struct _RGNDATA, struct tagPALETTEENTRY, struct HDC__, struct _LUID) so it does
 *      not have to exist.
 *   2. Standalone (shim unit tests, smoke test; no <windows.h> anywhere on the include path): everything below
 *      is defined here with MSVC-compatible definitions (DWORD = unsigned long, 32-bit on wasm32).
 *
 * Force a mode with -DD3D9SHIM_USE_WINDOWS_H=1 or =0.
 */
#ifndef D3D9SHIM_WIN32_H
#define D3D9SHIM_WIN32_H

#ifndef D3D9SHIM_USE_WINDOWS_H
#  if defined(_WINDOWS_) || defined(_WINDOWS_H)
#    define D3D9SHIM_USE_WINDOWS_H 1
#  elif defined(__has_include)
#    if __has_include(<windows.h>)
#      define D3D9SHIM_USE_WINDOWS_H 1
#    else
#      define D3D9SHIM_USE_WINDOWS_H 0
#    endif
#  else
#    define D3D9SHIM_USE_WINDOWS_H 0
#  endif
#endif

#if D3D9SHIM_USE_WINDOWS_H
#  include <windows.h>
#  if defined(__has_include)
#    if __has_include(<objbase.h>)
#      include <objbase.h>
#    elif __has_include(<unknwn.h>)
#      include <unknwn.h>
#    endif
#  endif
#else /* standalone fallback */

#include <stddef.h>
#include <stdint.h>

typedef unsigned char BYTE;
typedef unsigned short WORD;
#if defined(__SIZEOF_LONG__) && __SIZEOF_LONG__ == 8
/* LP64 host (native unit-test builds): keep the Win32 32-bit widths, like Wine does. */
typedef unsigned int DWORD;
typedef int LONG;
typedef unsigned int ULONG;
#else
typedef unsigned long DWORD; /* MSVC / wasm32: long is 32-bit */
typedef long LONG;
typedef unsigned long ULONG;
#endif
typedef unsigned int UINT;
typedef unsigned int UINT32;
typedef int INT;
typedef int BOOL;
typedef float FLOAT;
typedef char CHAR;
typedef LONG HRESULT;
typedef void *HANDLE;
typedef void *LPVOID;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef uintptr_t ULONG_PTR;
typedef uintptr_t UINT_PTR;
typedef intptr_t INT_PTR;
typedef uintptr_t DWORD_PTR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#ifndef CONST
#define CONST const
#endif
#ifndef WINAPI
#define WINAPI
#endif

struct HWND__ { int unused; };
typedef struct HWND__ *HWND;
#ifndef HMONITOR_DECLARED
#define HMONITOR_DECLARED
struct HMONITOR__ { int unused; };
typedef struct HMONITOR__ *HMONITOR;
#endif
struct HINSTANCE__ { int unused; };
typedef struct HINSTANCE__ *HINSTANCE;
typedef HINSTANCE HMODULE;

typedef struct tagRECT { LONG left, top, right, bottom; } RECT, *PRECT, *LPRECT;
typedef const RECT *LPCRECT;
typedef struct tagPOINT { LONG x, y; } POINT, *PPOINT, *LPPOINT;
typedef struct tagSIZE { LONG cx, cy; } SIZE;

typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG HighPart; } u;
    struct { DWORD LowPart; LONG HighPart; };
    long long QuadPart;
} LARGE_INTEGER;

#ifndef GUID_DEFINED
#define GUID_DEFINED
typedef struct _GUID {
    DWORD Data1;
    unsigned short Data2;
    unsigned short Data3;
    unsigned char Data4[8];
} GUID;
#endif
typedef GUID IID;
typedef GUID CLSID;
#ifdef __cplusplus
#ifndef _REFIID_DEFINED
#define _REFIID_DEFINED
typedef const IID &REFIID;
#endif
#ifndef _REFGUID_DEFINED
#define _REFGUID_DEFINED
typedef const GUID &REFGUID;
#endif
#ifndef _REFCLSID_DEFINED
#define _REFCLSID_DEFINED
typedef const GUID &REFCLSID;
#endif
inline bool IsEqualGUID(REFGUID a, REFGUID b)
{
    const unsigned char *x = (const unsigned char *)&a, *y = (const unsigned char *)&b;
    for (unsigned i = 0; i < sizeof(GUID); ++i)
        if (x[i] != y[i])
            return false;
    return true;
}
#define IsEqualIID(a, b) IsEqualGUID(a, b)
#endif

#define S_OK ((HRESULT)0L)
#define S_FALSE ((HRESULT)1L)
#define E_NOTIMPL ((HRESULT)0x80004001L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_FAIL ((HRESULT)0x80004005L)
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)
#define MAKE_HRESULT(sev, fac, code) \
    ((HRESULT)(((DWORD)(sev) << 31) | ((DWORD)(fac) << 16) | ((DWORD)(code))))

#ifdef __cplusplus
#ifndef __IUnknown_INTERFACE_DEFINED__
#define __IUnknown_INTERFACE_DEFINED__
struct IUnknown {
    virtual HRESULT QueryInterface(REFIID riid, void **ppvObject) = 0;
    virtual ULONG AddRef() = 0;
    virtual ULONG Release() = 0;
};
#endif
#endif

#endif /* standalone fallback */

/* Things neither mode is guaranteed to have. */
#ifndef CONST
#define CONST const
#endif
#ifndef WINAPI
#define WINAPI
#endif
#ifndef MAKEFOURCC
#define MAKEFOURCC(ch0, ch1, ch2, ch3) \
    ((DWORD)(BYTE)(ch0) | ((DWORD)(BYTE)(ch1) << 8) | ((DWORD)(BYTE)(ch2) << 16) | ((DWORD)(BYTE)(ch3) << 24))
#endif

/* GDI/kernel structs referenced only by pointer from D3D9 signatures (never dereferenced by the engine). */
struct _RGNDATA;
struct tagPALETTEENTRY;
struct HDC__;
struct _LUID;

#endif /* D3D9SHIM_WIN32_H */
