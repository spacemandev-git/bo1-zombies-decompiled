/*
 * d3d9.h - Direct3D 9 interfaces for the BO1 web port's D3D9 -> WebGL2 shim (src/web/d3d9).
 *
 * Written for this project. Interfaces are C++ abstract classes with the real method names, parameter lists and
 * vtable order (so engine code calling dx.device->SetRenderState(...) compiles unchanged). The implementation lives
 * in src/web/d3d9/ (d3d9_*.cpp); Direct3DCreate9() is the entry point. See src/web/d3d9/README.md.
 */
#ifndef D3D9SHIM_D3D9_H
#define D3D9SHIM_D3D9_H

#ifndef DIRECT3D_VERSION
#define DIRECT3D_VERSION 0x0900
#endif

#include "d3d9shim_win32.h"
#include "d3d9types.h"
#include "d3d9caps.h"

#define D3D_SDK_VERSION 32
#define D3D9b_SDK_VERSION 31

/* ------------------------------------------------------------------------------------------------ result codes */
#define _FACD3D 0x876
#define MAKE_D3DHRESULT(code) MAKE_HRESULT(1, _FACD3D, code)
#define MAKE_D3DSTATUS(code) MAKE_HRESULT(0, _FACD3D, code)

#define D3D_OK S_OK
#define D3DERR_WRONGTEXTUREFORMAT MAKE_D3DHRESULT(2072)
#define D3DERR_UNSUPPORTEDCOLOROPERATION MAKE_D3DHRESULT(2073)
#define D3DERR_UNSUPPORTEDCOLORARG MAKE_D3DHRESULT(2074)
#define D3DERR_UNSUPPORTEDALPHAOPERATION MAKE_D3DHRESULT(2075)
#define D3DERR_UNSUPPORTEDALPHAARG MAKE_D3DHRESULT(2076)
#define D3DERR_TOOMANYOPERATIONS MAKE_D3DHRESULT(2077)
#define D3DERR_CONFLICTINGTEXTUREFILTER MAKE_D3DHRESULT(2078)
#define D3DERR_UNSUPPORTEDFACTORVALUE MAKE_D3DHRESULT(2079)
#define D3DERR_CONFLICTINGRENDERSTATE MAKE_D3DHRESULT(2081)
#define D3DERR_UNSUPPORTEDTEXTUREFILTER MAKE_D3DHRESULT(2082)
#define D3DERR_CONFLICTINGTEXTUREPALETTE MAKE_D3DHRESULT(2086)
#define D3DERR_DRIVERINTERNALERROR MAKE_D3DHRESULT(2087)
#define D3DERR_NOTFOUND MAKE_D3DHRESULT(2150)
#define D3DERR_MOREDATA MAKE_D3DHRESULT(2151)
#define D3DERR_DEVICELOST MAKE_D3DHRESULT(2152)
#define D3DERR_DEVICENOTRESET MAKE_D3DHRESULT(2153)
#define D3DERR_NOTAVAILABLE MAKE_D3DHRESULT(2154)
#define D3DERR_OUTOFVIDEOMEMORY MAKE_D3DHRESULT(380)
#define D3DERR_INVALIDDEVICE MAKE_D3DHRESULT(2155)
#define D3DERR_INVALIDCALL MAKE_D3DHRESULT(2156)
#define D3DERR_DRIVERINVALIDCALL MAKE_D3DHRESULT(2157)
#define D3DERR_WASSTILLDRAWING MAKE_D3DHRESULT(540)
#define D3DOK_NOAUTOGEN MAKE_D3DSTATUS(2159)
#define D3DERR_DEVICEREMOVED MAKE_D3DHRESULT(2160)
#define S_NOT_RESIDENT MAKE_D3DSTATUS(2165)
#define S_RESIDENT_IN_SHARED_MEMORY MAKE_D3DSTATUS(2166)
#define S_PRESENT_MODE_CHANGED MAKE_D3DSTATUS(2167)
#define S_PRESENT_OCCLUDED MAKE_D3DSTATUS(2168)
#define D3DERR_DEVICEHUNG MAKE_D3DHRESULT(2164)
#define D3DERR_UNSUPPORTEDOVERLAY MAKE_D3DHRESULT(2171)
#define D3DERR_UNSUPPORTEDOVERLAYFORMAT MAKE_D3DHRESULT(2172)
#define D3DERR_CANNOTPROTECTCONTENT MAKE_D3DHRESULT(2173)
#define D3DERR_UNSUPPORTEDCRYPTO MAKE_D3DHRESULT(2174)
#define D3DERR_PRESENT_STATISTICS_DISJOINT MAKE_D3DHRESULT(2180)

/* ------------------------------------------------------------------------------------------------ flags */
#define D3DCREATE_FPU_PRESERVE 0x00000002L
#define D3DCREATE_MULTITHREADED 0x00000004L
#define D3DCREATE_PUREDEVICE 0x00000010L
#define D3DCREATE_SOFTWARE_VERTEXPROCESSING 0x00000020L
#define D3DCREATE_HARDWARE_VERTEXPROCESSING 0x00000040L
#define D3DCREATE_MIXED_VERTEXPROCESSING 0x00000080L
#define D3DCREATE_DISABLE_DRIVER_MANAGEMENT 0x00000100L
#define D3DCREATE_ADAPTERGROUP_DEVICE 0x00000200L
#define D3DCREATE_DISABLE_DRIVER_MANAGEMENT_EX 0x00000400L
#define D3DCREATE_NOWINDOWCHANGES 0x00000800L
#define D3DCREATE_DISABLE_PSGP_THREADING 0x00002000L
#define D3DCREATE_ENABLE_PRESENTSTATS 0x00004000L
#define D3DCREATE_DISABLE_PRINTSCREEN 0x00008000L
#define D3DCREATE_SCREENSAVER 0x10000000L

#define D3DADAPTER_DEFAULT 0
#define D3DENUM_WHQL_LEVEL 0x00000002L
#define D3DENUM_NO_DRIVERVERSION 0x00000004L
#define D3DPRESENT_BACK_BUFFERS_MAX 3L
#define D3DPRESENT_BACK_BUFFERS_MAX_EX 30L
#define D3DSGR_NO_CALIBRATION 0x00000000L
#define D3DSGR_CALIBRATE 0x00000001L
#define D3DCURSOR_IMMEDIATE_UPDATE 0x00000001L
#define D3DPRESENT_DONOTWAIT 0x00000001L
#define D3DPRESENT_LINEAR_CONTENT 0x00000002L
#define D3DPRESENT_DONOTFLIP 0x00000004L
#define D3DPRESENT_FLIPRESTART 0x00000008L
#define D3DPRESENT_VIDEO_RESTRICT_TO_MONITOR 0x00000010L
#define D3DPRESENT_UPDATEOVERLAYONLY 0x00000020L
#define D3DPRESENT_HIDEOVERLAY 0x00000040L
#define D3DPRESENT_UPDATECOLORKEY 0x00000080L
#define D3DPRESENT_FORCEIMMEDIATE 0x00000100L

/* ------------------------------------------------------------------------------------------------ interfaces */
#ifdef __cplusplus

struct IDirect3D9;
struct IDirect3D9Ex;
struct IDirect3DDevice9;
struct IDirect3DDevice9Ex;
struct IDirect3DStateBlock9;
struct IDirect3DSwapChain9;
struct IDirect3DResource9;
struct IDirect3DVertexDeclaration9;
struct IDirect3DVertexShader9;
struct IDirect3DPixelShader9;
struct IDirect3DBaseTexture9;
struct IDirect3DTexture9;
struct IDirect3DVolumeTexture9;
struct IDirect3DCubeTexture9;
struct IDirect3DVertexBuffer9;
struct IDirect3DIndexBuffer9;
struct IDirect3DSurface9;
struct IDirect3DVolume9;
struct IDirect3DQuery9;

struct IDirect3D9 : public IUnknown {
    virtual HRESULT RegisterSoftwareDevice(void *pInitializeFunction) = 0;
    virtual UINT GetAdapterCount() = 0;
    virtual HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier) = 0;
    virtual UINT GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) = 0;
    virtual HRESULT EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat,
                                    D3DFORMAT BackBufferFormat, BOOL bWindowed) = 0;
    virtual HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage,
                                      D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) = 0;
    virtual HRESULT CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat,
                                               BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType,
                                               DWORD *pQualityLevels) = 0;
    virtual HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
                                           D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) = 0;
    virtual HRESULT CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat,
                                                D3DFORMAT TargetFormat) = 0;
    virtual HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps) = 0;
    virtual HMONITOR GetAdapterMonitor(UINT Adapter) = 0;
    virtual HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                                 D3DPRESENT_PARAMETERS *pPresentationParameters,
                                 IDirect3DDevice9 **ppReturnedDeviceInterface) = 0;
};

struct IDirect3D9Ex : public IDirect3D9 {
    virtual UINT GetAdapterModeCountEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter) = 0;
    virtual HRESULT EnumAdapterModesEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter, UINT Mode,
                                       D3DDISPLAYMODEEX *pMode) = 0;
    virtual HRESULT GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX *pMode,
                                            D3DDISPLAYROTATION *pRotation) = 0;
    virtual HRESULT CreateDeviceEx(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                                   D3DPRESENT_PARAMETERS *pPresentationParameters,
                                   D3DDISPLAYMODEEX *pFullscreenDisplayMode,
                                   IDirect3DDevice9Ex **ppReturnedDeviceInterface) = 0;
    virtual HRESULT GetAdapterLUID(UINT Adapter, struct _LUID *pLUID) = 0;
};

struct IDirect3DDevice9 : public IUnknown {
    virtual HRESULT TestCooperativeLevel() = 0;
    virtual UINT GetAvailableTextureMem() = 0;
    virtual HRESULT EvictManagedResources() = 0;
    virtual HRESULT GetDirect3D(IDirect3D9 **ppD3D9) = 0;
    virtual HRESULT GetDeviceCaps(D3DCAPS9 *pCaps) = 0;
    virtual HRESULT GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) = 0;
    virtual HRESULT SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9 *pCursorBitmap) = 0;
    virtual void SetCursorPosition(int X, int Y, DWORD Flags) = 0;
    virtual BOOL ShowCursor(BOOL bShow) = 0;
    virtual HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pPresentationParameters,
                                              IDirect3DSwapChain9 **pSwapChain) = 0;
    virtual HRESULT GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9 **pSwapChain) = 0;
    virtual UINT GetNumberOfSwapChains() = 0;
    virtual HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) = 0;
    virtual HRESULT Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride,
                            const struct _RGNDATA *pDirtyRegion) = 0;
    virtual HRESULT GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type,
                                  IDirect3DSurface9 **ppBackBuffer) = 0;
    virtual HRESULT GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS *pRasterStatus) = 0;
    virtual HRESULT SetDialogBoxMode(BOOL bEnableDialogs) = 0;
    virtual void SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP *pRamp) = 0;
    virtual void GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP *pRamp) = 0;
    virtual HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage,
                                        D3DFORMAT Format, D3DPOOL Pool, IDirect3DVolumeTexture9 **ppVolumeTexture,
                                        HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                      IDirect3DCubeTexture9 **ppCubeTexture, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                                       IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                      IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                       DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9 **ppSurface,
                                       HANDLE *pSharedHandle) = 0;
    virtual HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format,
                                              D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                              BOOL Discard, IDirect3DSurface9 **ppSurface,
                                              HANDLE *pSharedHandle) = 0;
    virtual HRESULT UpdateSurface(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect,
                                  IDirect3DSurface9 *pDestinationSurface, const POINT *pDestPoint) = 0;
    virtual HRESULT UpdateTexture(IDirect3DBaseTexture9 *pSourceTexture,
                                  IDirect3DBaseTexture9 *pDestinationTexture) = 0;
    virtual HRESULT GetRenderTargetData(IDirect3DSurface9 *pRenderTarget, IDirect3DSurface9 *pDestSurface) = 0;
    virtual HRESULT GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9 *pDestSurface) = 0;
    virtual HRESULT StretchRect(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect,
                                IDirect3DSurface9 *pDestSurface, const RECT *pDestRect,
                                D3DTEXTUREFILTERTYPE Filter) = 0;
    virtual HRESULT ColorFill(IDirect3DSurface9 *pSurface, const RECT *pRect, D3DCOLOR color) = 0;
    virtual HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
                                                IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle) = 0;
    virtual HRESULT SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget) = 0;
    virtual HRESULT GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 **ppRenderTarget) = 0;
    virtual HRESULT SetDepthStencilSurface(IDirect3DSurface9 *pNewZStencil) = 0;
    virtual HRESULT GetDepthStencilSurface(IDirect3DSurface9 **ppZStencilSurface) = 0;
    virtual HRESULT BeginScene() = 0;
    virtual HRESULT EndScene() = 0;
    virtual HRESULT Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z,
                          DWORD Stencil) = 0;
    virtual HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) = 0;
    virtual HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) = 0;
    virtual HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) = 0;
    virtual HRESULT SetViewport(const D3DVIEWPORT9 *pViewport) = 0;
    virtual HRESULT GetViewport(D3DVIEWPORT9 *pViewport) = 0;
    virtual HRESULT SetMaterial(const D3DMATERIAL9 *pMaterial) = 0;
    virtual HRESULT GetMaterial(D3DMATERIAL9 *pMaterial) = 0;
    virtual HRESULT SetLight(DWORD Index, const D3DLIGHT9 *pLight) = 0;
    virtual HRESULT GetLight(DWORD Index, D3DLIGHT9 *pLight) = 0;
    virtual HRESULT LightEnable(DWORD Index, BOOL Enable) = 0;
    virtual HRESULT GetLightEnable(DWORD Index, BOOL *pEnable) = 0;
    virtual HRESULT SetClipPlane(DWORD Index, const float *pPlane) = 0;
    virtual HRESULT GetClipPlane(DWORD Index, float *pPlane) = 0;
    virtual HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) = 0;
    virtual HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) = 0;
    virtual HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9 **ppSB) = 0;
    virtual HRESULT BeginStateBlock() = 0;
    virtual HRESULT EndStateBlock(IDirect3DStateBlock9 **ppSB) = 0;
    virtual HRESULT SetClipStatus(const D3DCLIPSTATUS9 *pClipStatus) = 0;
    virtual HRESULT GetClipStatus(D3DCLIPSTATUS9 *pClipStatus) = 0;
    virtual HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture9 **ppTexture) = 0;
    virtual HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture9 *pTexture) = 0;
    virtual HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) = 0;
    virtual HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD *pValue) = 0;
    virtual HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) = 0;
    virtual HRESULT ValidateDevice(DWORD *pNumPasses) = 0;
    virtual HRESULT SetPaletteEntries(UINT PaletteNumber, const struct tagPALETTEENTRY *pEntries) = 0;
    virtual HRESULT GetPaletteEntries(UINT PaletteNumber, struct tagPALETTEENTRY *pEntries) = 0;
    virtual HRESULT SetCurrentTexturePalette(UINT PaletteNumber) = 0;
    virtual HRESULT GetCurrentTexturePalette(UINT *PaletteNumber) = 0;
    virtual HRESULT SetScissorRect(const RECT *pRect) = 0;
    virtual HRESULT GetScissorRect(RECT *pRect) = 0;
    virtual HRESULT SetSoftwareVertexProcessing(BOOL bSoftware) = 0;
    virtual BOOL GetSoftwareVertexProcessing() = 0;
    virtual HRESULT SetNPatchMode(float nSegments) = 0;
    virtual float GetNPatchMode() = 0;
    virtual HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) = 0;
    virtual HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex,
                                         UINT NumVertices, UINT startIndex, UINT primCount) = 0;
    virtual HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount,
                                    const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices,
                                           UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                           const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) = 0;
    virtual HRESULT ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount,
                                    IDirect3DVertexBuffer9 *pDestBuffer, IDirect3DVertexDeclaration9 *pVertexDecl,
                                    DWORD Flags) = 0;
    virtual HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *pVertexElements,
                                            IDirect3DVertexDeclaration9 **ppDecl) = 0;
    virtual HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *pDecl) = 0;
    virtual HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9 **ppDecl) = 0;
    virtual HRESULT SetFVF(DWORD FVF) = 0;
    virtual HRESULT GetFVF(DWORD *pFVF) = 0;
    virtual HRESULT CreateVertexShader(const DWORD *pFunction, IDirect3DVertexShader9 **ppShader) = 0;
    virtual HRESULT SetVertexShader(IDirect3DVertexShader9 *pShader) = 0;
    virtual HRESULT GetVertexShader(IDirect3DVertexShader9 **ppShader) = 0;
    virtual HRESULT SetVertexShaderConstantF(UINT StartRegister, const float *pConstantData,
                                             UINT Vector4fCount) = 0;
    virtual HRESULT GetVertexShaderConstantF(UINT StartRegister, float *pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT SetVertexShaderConstantI(UINT StartRegister, const int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT GetVertexShaderConstantI(UINT StartRegister, int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT SetVertexShaderConstantB(UINT StartRegister, const BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT GetVertexShaderConstantB(UINT StartRegister, BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes,
                                    UINT Stride) = 0;
    virtual HRESULT GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 **ppStreamData, UINT *pOffsetInBytes,
                                    UINT *pStride) = 0;
    virtual HRESULT SetStreamSourceFreq(UINT StreamNumber, UINT Setting) = 0;
    virtual HRESULT GetStreamSourceFreq(UINT StreamNumber, UINT *pSetting) = 0;
    virtual HRESULT SetIndices(IDirect3DIndexBuffer9 *pIndexData) = 0;
    virtual HRESULT GetIndices(IDirect3DIndexBuffer9 **ppIndexData) = 0;
    virtual HRESULT CreatePixelShader(const DWORD *pFunction, IDirect3DPixelShader9 **ppShader) = 0;
    virtual HRESULT SetPixelShader(IDirect3DPixelShader9 *pShader) = 0;
    virtual HRESULT GetPixelShader(IDirect3DPixelShader9 **ppShader) = 0;
    virtual HRESULT SetPixelShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT GetPixelShaderConstantF(UINT StartRegister, float *pConstantData, UINT Vector4fCount) = 0;
    virtual HRESULT SetPixelShaderConstantI(UINT StartRegister, const int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT GetPixelShaderConstantI(UINT StartRegister, int *pConstantData, UINT Vector4iCount) = 0;
    virtual HRESULT SetPixelShaderConstantB(UINT StartRegister, const BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT GetPixelShaderConstantB(UINT StartRegister, BOOL *pConstantData, UINT BoolCount) = 0;
    virtual HRESULT DrawRectPatch(UINT Handle, const float *pNumSegs, const D3DRECTPATCH_INFO *pRectPatchInfo) = 0;
    virtual HRESULT DrawTriPatch(UINT Handle, const float *pNumSegs, const D3DTRIPATCH_INFO *pTriPatchInfo) = 0;
    virtual HRESULT DeletePatch(UINT Handle) = 0;
    virtual HRESULT CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9 **ppQuery) = 0;
};

struct IDirect3DDevice9Ex : public IDirect3DDevice9 {
    virtual HRESULT SetConvolutionMonoKernel(UINT width, UINT height, float *rows, float *columns) = 0;
    virtual HRESULT ComposeRects(IDirect3DSurface9 *pSrc, IDirect3DSurface9 *pDst,
                                 IDirect3DVertexBuffer9 *pSrcRectDescs, UINT NumRects,
                                 IDirect3DVertexBuffer9 *pDstRectDescs, D3DCOMPOSERECTSOP Operation, int Xoffset,
                                 int Yoffset) = 0;
    virtual HRESULT PresentEx(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride,
                              const struct _RGNDATA *pDirtyRegion, DWORD dwFlags) = 0;
    virtual HRESULT GetGPUThreadPriority(INT *pPriority) = 0;
    virtual HRESULT SetGPUThreadPriority(INT Priority) = 0;
    virtual HRESULT WaitForVBlank(UINT iSwapChain) = 0;
    virtual HRESULT CheckResourceResidency(IDirect3DResource9 **pResourceArray, UINT NumResources) = 0;
    virtual HRESULT SetMaximumFrameLatency(UINT MaxLatency) = 0;
    virtual HRESULT GetMaximumFrameLatency(UINT *pMaxLatency) = 0;
    virtual HRESULT CheckDeviceState(HWND hDestinationWindow) = 0;
    virtual HRESULT CreateRenderTargetEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                         DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9 **ppSurface,
                                         HANDLE *pSharedHandle, DWORD Usage) = 0;
    virtual HRESULT CreateOffscreenPlainSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
                                                  IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle,
                                                  DWORD Usage) = 0;
    virtual HRESULT CreateDepthStencilSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format,
                                                D3DMULTISAMPLE_TYPE MultiSample, DWORD MultisampleQuality,
                                                BOOL Discard, IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle,
                                                DWORD Usage) = 0;
    virtual HRESULT ResetEx(D3DPRESENT_PARAMETERS *pPresentationParameters,
                            D3DDISPLAYMODEEX *pFullscreenDisplayMode) = 0;
    virtual HRESULT GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) = 0;
};

struct IDirect3DStateBlock9 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT Capture() = 0;
    virtual HRESULT Apply() = 0;
};

struct IDirect3DSwapChain9 : public IUnknown {
    virtual HRESULT Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride,
                            const struct _RGNDATA *pDirtyRegion, DWORD dwFlags) = 0;
    virtual HRESULT GetFrontBufferData(IDirect3DSurface9 *pDestSurface) = 0;
    virtual HRESULT GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer) = 0;
    virtual HRESULT GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) = 0;
    virtual HRESULT GetDisplayMode(D3DDISPLAYMODE *pMode) = 0;
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters) = 0;
};

struct IDirect3DResource9 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT SetPrivateData(REFGUID refguid, const void *pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData) = 0;
    virtual HRESULT FreePrivateData(REFGUID refguid) = 0;
    virtual DWORD SetPriority(DWORD PriorityNew) = 0;
    virtual DWORD GetPriority() = 0;
    virtual void PreLoad() = 0;
    virtual D3DRESOURCETYPE GetType() = 0;
};

struct IDirect3DVertexDeclaration9 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetDeclaration(D3DVERTEXELEMENT9 *pElement, UINT *pNumElements) = 0;
};

struct IDirect3DVertexShader9 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetFunction(void *pData, UINT *pSizeOfData) = 0;
};

struct IDirect3DPixelShader9 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT GetFunction(void *pData, UINT *pSizeOfData) = 0;
};

struct IDirect3DBaseTexture9 : public IDirect3DResource9 {
    virtual DWORD SetLOD(DWORD LODNew) = 0;
    virtual DWORD GetLOD() = 0;
    virtual DWORD GetLevelCount() = 0;
    virtual HRESULT SetAutoGenFilterType(D3DTEXTUREFILTERTYPE FilterType) = 0;
    virtual D3DTEXTUREFILTERTYPE GetAutoGenFilterType() = 0;
    virtual void GenerateMipSubLevels() = 0;
};

struct IDirect3DTexture9 : public IDirect3DBaseTexture9 {
    virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) = 0;
    virtual HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) = 0;
    virtual HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect(UINT Level) = 0;
    virtual HRESULT AddDirtyRect(const RECT *pDirtyRect) = 0;
};

struct IDirect3DVolumeTexture9 : public IDirect3DBaseTexture9 {
    virtual HRESULT GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) = 0;
    virtual HRESULT GetVolumeLevel(UINT Level, IDirect3DVolume9 **ppVolumeLevel) = 0;
    virtual HRESULT LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) = 0;
    virtual HRESULT UnlockBox(UINT Level) = 0;
    virtual HRESULT AddDirtyBox(const D3DBOX *pDirtyBox) = 0;
};

struct IDirect3DCubeTexture9 : public IDirect3DBaseTexture9 {
    virtual HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) = 0;
    virtual HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level,
                                      IDirect3DSurface9 **ppCubeMapSurface) = 0;
    virtual HRESULT LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect,
                             DWORD Flags) = 0;
    virtual HRESULT UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) = 0;
    virtual HRESULT AddDirtyRect(D3DCUBEMAP_FACES FaceType, const RECT *pDirtyRect) = 0;
};

struct IDirect3DVertexBuffer9 : public IDirect3DResource9 {
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetDesc(D3DVERTEXBUFFER_DESC *pDesc) = 0;
};

struct IDirect3DIndexBuffer9 : public IDirect3DResource9 {
    virtual HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) = 0;
    virtual HRESULT Unlock() = 0;
    virtual HRESULT GetDesc(D3DINDEXBUFFER_DESC *pDesc) = 0;
};

struct IDirect3DSurface9 : public IDirect3DResource9 {
    virtual HRESULT GetContainer(REFIID riid, void **ppContainer) = 0;
    virtual HRESULT GetDesc(D3DSURFACE_DESC *pDesc) = 0;
    virtual HRESULT LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) = 0;
    virtual HRESULT UnlockRect() = 0;
    virtual HRESULT GetDC(struct HDC__ **phdc) = 0;
    virtual HRESULT ReleaseDC(struct HDC__ *hdc) = 0;
};

struct IDirect3DVolume9 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual HRESULT SetPrivateData(REFGUID refguid, const void *pData, DWORD SizeOfData, DWORD Flags) = 0;
    virtual HRESULT GetPrivateData(REFGUID refguid, void *pData, DWORD *pSizeOfData) = 0;
    virtual HRESULT FreePrivateData(REFGUID refguid) = 0;
    virtual HRESULT GetContainer(REFIID riid, void **ppContainer) = 0;
    virtual HRESULT GetDesc(D3DVOLUME_DESC *pDesc) = 0;
    virtual HRESULT LockBox(D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) = 0;
    virtual HRESULT UnlockBox() = 0;
};

struct IDirect3DQuery9 : public IUnknown {
    virtual HRESULT GetDevice(IDirect3DDevice9 **ppDevice) = 0;
    virtual D3DQUERYTYPE GetType() = 0;
    virtual DWORD GetDataSize() = 0;
    virtual HRESULT Issue(DWORD dwIssueFlags) = 0;
    virtual HRESULT GetData(void *pData, DWORD dwSize, DWORD dwGetDataFlags) = 0;
};

typedef IDirect3D9 *LPDIRECT3D9, *PDIRECT3D9;
typedef IDirect3D9Ex *LPDIRECT3D9EX, *PDIRECT3D9EX;
typedef IDirect3DDevice9 *LPDIRECT3DDEVICE9, *PDIRECT3DDEVICE9;
typedef IDirect3DDevice9Ex *LPDIRECT3DDEVICE9EX, *PDIRECT3DDEVICE9EX;
typedef IDirect3DStateBlock9 *LPDIRECT3DSTATEBLOCK9, *PDIRECT3DSTATEBLOCK9;
typedef IDirect3DSwapChain9 *LPDIRECT3DSWAPCHAIN9, *PDIRECT3DSWAPCHAIN9;
typedef IDirect3DResource9 *LPDIRECT3DRESOURCE9, *PDIRECT3DRESOURCE9;
typedef IDirect3DVertexDeclaration9 *LPDIRECT3DVERTEXDECLARATION9, *PDIRECT3DVERTEXDECLARATION9;
typedef IDirect3DVertexShader9 *LPDIRECT3DVERTEXSHADER9, *PDIRECT3DVERTEXSHADER9;
typedef IDirect3DPixelShader9 *LPDIRECT3DPIXELSHADER9, *PDIRECT3DPIXELSHADER9;
typedef IDirect3DBaseTexture9 *LPDIRECT3DBASETEXTURE9, *PDIRECT3DBASETEXTURE9;
typedef IDirect3DTexture9 *LPDIRECT3DTEXTURE9, *PDIRECT3DTEXTURE9;
typedef IDirect3DVolumeTexture9 *LPDIRECT3DVOLUMETEXTURE9, *PDIRECT3DVOLUMETEXTURE9;
typedef IDirect3DCubeTexture9 *LPDIRECT3DCUBETEXTURE9, *PDIRECT3DCUBETEXTURE9;
typedef IDirect3DVertexBuffer9 *LPDIRECT3DVERTEXBUFFER9, *PDIRECT3DVERTEXBUFFER9;
typedef IDirect3DIndexBuffer9 *LPDIRECT3DINDEXBUFFER9, *PDIRECT3DINDEXBUFFER9;
typedef IDirect3DSurface9 *LPDIRECT3DSURFACE9, *PDIRECT3DSURFACE9;
typedef IDirect3DVolume9 *LPDIRECT3DVOLUME9, *PDIRECT3DVOLUME9;
typedef IDirect3DQuery9 *LPDIRECT3DQUERY9, *PDIRECT3DQUERY9;

/* IIDs (values from the SDK). Only GetContainer/QueryInterface look at them. */
extern "C" const GUID IID_IDirect3D9;
extern "C" const GUID IID_IDirect3DDevice9;
extern "C" const GUID IID_IDirect3DResource9;
extern "C" const GUID IID_IDirect3DBaseTexture9;
extern "C" const GUID IID_IDirect3DTexture9;
extern "C" const GUID IID_IDirect3DCubeTexture9;
extern "C" const GUID IID_IDirect3DVolumeTexture9;
extern "C" const GUID IID_IDirect3DSurface9;
extern "C" const GUID IID_IDirect3DSwapChain9;

/* ------------------------------------------------------------------------------------------------ entry points */
extern "C" {
IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion);
HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex **ppD3D);

/* PIX markers: accepted and ignored (WebGL has no debug groups). Names are wchar_t (L"..." literals). */
int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, const wchar_t *wszName);
int WINAPI D3DPERF_EndEvent(void);
void WINAPI D3DPERF_SetMarker(D3DCOLOR col, const wchar_t *wszName);
void WINAPI D3DPERF_SetRegion(D3DCOLOR col, const wchar_t *wszName);
BOOL WINAPI D3DPERF_QueryRepeatFrame(void);
void WINAPI D3DPERF_SetOptions(DWORD dwOptions);
DWORD WINAPI D3DPERF_GetStatus(void);
}

#endif /* __cplusplus */

#endif /* D3D9SHIM_D3D9_H */
