/*
 * d3dx9.h - the D3DX 9 subset the engine references, for the BO1 web port's D3D9 -> WebGL2 shim (src/web/d3d9).
 *
 * Written for this project. Users in the engine: gfx_d3d/r_material_load_obj.cpp (runtime HLSL material compiler -
 * a dev path; fastfile shaders are precompiled bytecode) and gfx_d3d/r_screenshot.cpp. Implementation:
 * src/web/d3d9/d3dx9_stubs.cpp - D3DXCreateBuffer, D3DXGetShaderConstantTable (copies the CTAB comment block),
 * D3DXGetShaderInput/OutputSemantics (parse dcl tokens) and D3DXSaveSurfaceToFileA (TGA only) work;
 * D3DXCompileShader returns E_NOTIMPL (no HLSL compiler in the browser).
 */
#ifndef D3D9SHIM_D3DX9_H
#define D3D9SHIM_D3DX9_H

#include "d3d9.h"
#include <float.h>

#define D3DX_DEFAULT ((UINT)-1)
#define D3DX_DEFAULT_NONPOW2 ((UINT)-2)
#define D3DX_DEFAULT_FLOAT FLT_MAX
#define D3DX_FROM_FILE ((UINT)-3)
#define D3DFMT_FROM_FILE ((D3DFORMAT)-3)

#define _FACDD 0x876
#define MAKE_DDHRESULT(code) MAKE_HRESULT(1, _FACDD, code)
#define D3DXERR_CANNOTMODIFYINDEXBUFFER MAKE_DDHRESULT(2900)
#define D3DXERR_INVALIDMESH MAKE_DDHRESULT(2901)
#define D3DXERR_CANNOTATTRSORT MAKE_DDHRESULT(2902)
#define D3DXERR_SKINNINGNOTSUPPORTED MAKE_DDHRESULT(2903)
#define D3DXERR_TOOMANYINFLUENCES MAKE_DDHRESULT(2904)
#define D3DXERR_INVALIDDATA MAKE_DDHRESULT(2905)
#define D3DXERR_LOADEDMESHASNODATA MAKE_DDHRESULT(2906)
#define D3DXERR_DUPLICATENAMEDFRAGMENT MAKE_DDHRESULT(2907)
#define D3DXERR_CANNOTREMOVELASTITEM MAKE_DDHRESULT(2908)

#define D3DXSHADER_DEBUG (1 << 0)
#define D3DXSHADER_SKIPVALIDATION (1 << 1)
#define D3DXSHADER_SKIPOPTIMIZATION (1 << 2)
#define D3DXSHADER_PACKMATRIX_ROWMAJOR (1 << 3)
#define D3DXSHADER_PACKMATRIX_COLUMNMAJOR (1 << 4)
#define D3DXSHADER_PARTIALPRECISION (1 << 5)
#define D3DXSHADER_FORCE_VS_SOFTWARE_NOOPT (1 << 6)
#define D3DXSHADER_FORCE_PS_SOFTWARE_NOOPT (1 << 7)
#define D3DXSHADER_NO_PRESHADER (1 << 8)
#define D3DXSHADER_AVOID_FLOW_CONTROL (1 << 9)
#define D3DXSHADER_PREFER_FLOW_CONTROL (1 << 10)
#define D3DXSHADER_ENABLE_BACKWARDS_COMPATIBILITY (1 << 12)
#define D3DXSHADER_IEEE_STRICTNESS (1 << 13)
#define D3DXSHADER_USE_LEGACY_D3DX9_31_DLL (1 << 16)
#define D3DXSHADER_OPTIMIZATION_LEVEL0 (1 << 14)
#define D3DXSHADER_OPTIMIZATION_LEVEL1 0
#define D3DXSHADER_OPTIMIZATION_LEVEL2 ((1 << 14) | (1 << 15))
#define D3DXSHADER_OPTIMIZATION_LEVEL3 (1 << 15)

typedef enum _D3DXREGISTER_SET {
    D3DXRS_BOOL, D3DXRS_INT4, D3DXRS_FLOAT4, D3DXRS_SAMPLER, D3DXRS_FORCE_DWORD = 0x7fffffff
} D3DXREGISTER_SET;

typedef enum _D3DXPARAMETER_CLASS {
    D3DXPC_SCALAR, D3DXPC_VECTOR, D3DXPC_MATRIX_ROWS, D3DXPC_MATRIX_COLUMNS, D3DXPC_OBJECT, D3DXPC_STRUCT,
    D3DXPC_FORCE_DWORD = 0x7fffffff
} D3DXPARAMETER_CLASS;

typedef enum _D3DXPARAMETER_TYPE {
    D3DXPT_VOID, D3DXPT_BOOL, D3DXPT_INT, D3DXPT_FLOAT, D3DXPT_STRING, D3DXPT_TEXTURE, D3DXPT_TEXTURE1D,
    D3DXPT_TEXTURE2D, D3DXPT_TEXTURE3D, D3DXPT_TEXTURECUBE, D3DXPT_SAMPLER, D3DXPT_SAMPLER1D, D3DXPT_SAMPLER2D,
    D3DXPT_SAMPLER3D, D3DXPT_SAMPLERCUBE, D3DXPT_PIXELSHADER, D3DXPT_VERTEXSHADER, D3DXPT_PIXELFRAGMENT,
    D3DXPT_VERTEXFRAGMENT, D3DXPT_UNSUPPORTED, D3DXPT_FORCE_DWORD = 0x7fffffff
} D3DXPARAMETER_TYPE;

/* CTAB comment block layout (offsets are relative to the start of the table). */
typedef struct _D3DXSHADER_CONSTANTTABLE {
    DWORD Size;
    DWORD Creator;
    DWORD Version;
    DWORD Constants;
    DWORD ConstantInfo;
    DWORD Flags;
    DWORD Target;
} D3DXSHADER_CONSTANTTABLE, *LPD3DXSHADER_CONSTANTTABLE;

typedef struct _D3DXSHADER_CONSTANTINFO {
    DWORD Name;
    WORD RegisterSet;
    WORD RegisterIndex;
    WORD RegisterCount;
    WORD Reserved;
    DWORD TypeInfo;
    DWORD DefaultValue;
} D3DXSHADER_CONSTANTINFO, *LPD3DXSHADER_CONSTANTINFO;

typedef struct _D3DXSHADER_TYPEINFO {
    WORD Class;
    WORD Type;
    WORD Rows;
    WORD Columns;
    WORD Elements;
    WORD StructMembers;
    DWORD StructMemberInfo;
} D3DXSHADER_TYPEINFO, *LPD3DXSHADER_TYPEINFO;

typedef struct _D3DXSEMANTIC {
    UINT Usage;
    UINT UsageIndex;
} D3DXSEMANTIC, *LPD3DXSEMANTIC;

typedef struct _D3DXMACRO {
    LPCSTR Name;
    LPCSTR Definition;
} D3DXMACRO, *LPD3DXMACRO;

typedef enum _D3DXIMAGE_FILEFORMAT {
    D3DXIFF_BMP = 0, D3DXIFF_JPG = 1, D3DXIFF_TGA = 2, D3DXIFF_PNG = 3, D3DXIFF_DDS = 4, D3DXIFF_PPM = 5,
    D3DXIFF_DIB = 6, D3DXIFF_HDR = 7, D3DXIFF_PFM = 8, D3DXIFF_FORCE_DWORD = 0x7fffffff
} D3DXIMAGE_FILEFORMAT;

#ifdef __cplusplus
struct ID3DXBuffer : public IUnknown {
    virtual LPVOID GetBufferPointer() = 0;
    virtual DWORD GetBufferSize() = 0;
};
typedef ID3DXBuffer *LPD3DXBUFFER;

/* Only the IUnknown + buffer accessors (the engine reads the raw CTAB through GetBufferPointer). */
struct ID3DXConstantTable : public IUnknown {
    virtual LPVOID GetBufferPointer() = 0;
    virtual DWORD GetBufferSize() = 0;
};
typedef ID3DXConstantTable *LPD3DXCONSTANTTABLE;

typedef enum _D3DXINCLUDE_TYPE { D3DXINC_LOCAL, D3DXINC_SYSTEM, D3DXINC_FORCE_DWORD = 0x7fffffff } D3DXINCLUDE_TYPE;
struct ID3DXInclude {
    virtual HRESULT Open(D3DXINCLUDE_TYPE IncludeType, LPCSTR pFileName, const void * pParentData, const void **ppData,
                         UINT *pBytes) = 0;
    virtual HRESULT Close(const void * pData) = 0;
};
typedef ID3DXInclude *LPD3DXINCLUDE;

extern "C" {
HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer);
HRESULT WINAPI D3DXCompileShader(LPCSTR pSrcData, UINT SrcDataLen, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude,
                                 LPCSTR pFunctionName, LPCSTR pProfile, DWORD Flags, LPD3DXBUFFER *ppShader,
                                 LPD3DXBUFFER *ppErrorMsgs, LPD3DXCONSTANTTABLE *ppConstantTable);
HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *pFunction, LPD3DXCONSTANTTABLE *ppConstantTable);
HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount);
HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount);
UINT WINAPI D3DXGetShaderSize(const DWORD *pFunction);
DWORD WINAPI D3DXGetShaderVersion(const DWORD *pFunction);
HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat,
                                      IDirect3DSurface9 *pSrcSurface, const struct tagPALETTEENTRY *pSrcPalette,
                                      const RECT *pSrcRect);
}
#define D3DXSaveSurfaceToFile D3DXSaveSurfaceToFileA
#endif /* __cplusplus */

#endif /* D3D9SHIM_D3DX9_H */
