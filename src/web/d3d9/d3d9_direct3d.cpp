// d3d9_direct3d.cpp - IDirect3D9(Ex): adapter info, display modes, caps, format support, device creation.
#include "d3d9_internal.h"

#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

namespace d3d9shim {

// ------------------------------------------------------------------------------------------------ GL capability probe

static GLCaps g_liveCaps;
static bool g_haveLiveCaps = false;
static GLCaps g_probeCaps;
static bool g_probed = false;

#ifdef __EMSCRIPTEN__
// Creates a throwaway WebGL2 context on an OffscreenCanvas (works in workers and on the main thread) to learn the
// limits before the real device exists. ints: [ok, maxTex, maxCube, max3D, maxRB, maxAttribs, maxUnits, maxVSVec,
// maxFSVec, s3tc, cbf, cbhf, tfl, aniso, norm16, floatBlend]; floats: [maxAniso, maxPointSize].
EM_JS(int, d3d9shim_probe_webgl2, (int *ints, float *floats, char *renderer, int rendererLen), {
    try {
        var canvas = null;
        if (typeof OffscreenCanvas !== 'undefined')
            canvas = new OffscreenCanvas(1, 1);
        else if (typeof document !== 'undefined')
            canvas = document.createElement('canvas');
        if (!canvas)
            return 0;
        var gl = canvas.getContext('webgl2');
        if (!gl)
            return 0;
        var ext = function(n) { return gl.getExtension(n) ? 1 : 0; };
        var i = ints >> 2;
        HEAP32[i + 0] = 1;
        HEAP32[i + 1] = gl.getParameter(gl.MAX_TEXTURE_SIZE);
        HEAP32[i + 2] = gl.getParameter(gl.MAX_CUBE_MAP_TEXTURE_SIZE);
        HEAP32[i + 3] = gl.getParameter(gl.MAX_3D_TEXTURE_SIZE);
        HEAP32[i + 4] = gl.getParameter(gl.MAX_RENDERBUFFER_SIZE);
        HEAP32[i + 5] = gl.getParameter(gl.MAX_VERTEX_ATTRIBS);
        HEAP32[i + 6] = gl.getParameter(gl.MAX_TEXTURE_IMAGE_UNITS);
        HEAP32[i + 7] = gl.getParameter(gl.MAX_VERTEX_UNIFORM_VECTORS);
        HEAP32[i + 8] = gl.getParameter(gl.MAX_FRAGMENT_UNIFORM_VECTORS);
        HEAP32[i + 9] = ext('WEBGL_compressed_texture_s3tc');
        HEAP32[i + 10] = ext('EXT_color_buffer_float');
        HEAP32[i + 11] = ext('EXT_color_buffer_half_float');
        HEAP32[i + 12] = ext('OES_texture_float_linear');
        var aniso = gl.getExtension('EXT_texture_filter_anisotropic');
        HEAP32[i + 13] = aniso ? 1 : 0;
        HEAP32[i + 14] = ext('EXT_texture_norm16');
        HEAP32[i + 15] = ext('EXT_float_blend');
        HEAPF32[(floats >> 2) + 0] = aniso ? gl.getParameter(aniso.MAX_TEXTURE_MAX_ANISOTROPY_EXT) : 1;
        var ps = gl.getParameter(gl.ALIASED_POINT_SIZE_RANGE);
        HEAPF32[(floats >> 2) + 1] = ps ? ps[1] : 1;
        var name = 'WebGL2';
        var dbg = gl.getExtension('WEBGL_debug_renderer_info');
        if (dbg)
            name = String(gl.getParameter(dbg.UNMASKED_RENDERER_WEBGL));
        else
            name = String(gl.getParameter(gl.RENDERER));
        var n = Math.min(name.length, rendererLen - 1);
        for (var k = 0; k < n; ++k)
            HEAPU8[renderer + k] = name.charCodeAt(k) & 0x7f;
        HEAPU8[renderer + n] = 0;
        var lose = gl.getExtension('WEBGL_lose_context');
        if (lose)
            lose.loseContext();
        return 1;
    } catch (e) {
        return 0;
    }
});
#endif

static void probeOnce()
{
    if (g_probed)
        return;
    g_probed = true;
#ifdef __EMSCRIPTEN__
    int ints[16] = {0};
    float floats[2] = {0, 0};
    char renderer[128] = {0};
    if (d3d9shim_probe_webgl2(ints, floats, renderer, (int)sizeof(renderer)) && ints[0]) {
        GLCaps c;
        c.valid = true;
        c.maxTextureSize = ints[1];
        c.maxCubeMapSize = ints[2];
        c.max3DTextureSize = ints[3];
        c.maxRenderbufferSize = ints[4];
        c.maxVertexAttribs = ints[5];
        c.maxTextureUnits = ints[6];
        c.maxVertexUniformVectors = ints[7];
        c.maxFragmentUniformVectors = ints[8];
        c.s3tc = ints[9] != 0;
        c.colorBufferFloat = ints[10] != 0;
        c.colorBufferHalfFloat = ints[11] != 0;
        c.textureFloatLinear = ints[12] != 0;
        c.anisotropic = ints[13] != 0;
        c.norm16 = ints[14] != 0;
        c.floatBlend = ints[15] != 0;
        c.maxAnisotropy = floats[0] > 1 ? floats[0] : 1;
        c.maxPointSize = floats[1] > 1 ? floats[1] : 1;
        strncpy(c.renderer, renderer, sizeof(c.renderer) - 1);
        g_probeCaps = c;
        D3D9_INFO("WebGL2 probe: %s, max texture %d, s3tc %d, color_buffer_float %d, aniso %.0f", c.renderer,
                  c.maxTextureSize, c.s3tc, c.colorBufferFloat, c.maxAnisotropy);
    }
#endif
}

const GLCaps &currentGLCaps()
{
    if (g_haveLiveCaps)
        return g_liveCaps;
    probeOnce();
    return g_probeCaps;
}

void setLiveGLCaps(const GLCaps &caps)
{
    g_liveCaps = caps;
    g_haveLiveCaps = true;
}

// ------------------------------------------------------------------------------------------------ caps

void fillD3DCaps(D3DCAPS9 *c, const GLCaps &gl)
{
    memset(c, 0, sizeof(*c));
    c->DeviceType = D3DDEVTYPE_HAL;
    c->AdapterOrdinal = 0;
    c->Caps = D3DCAPS_READ_SCANLINE;
    c->Caps2 = D3DCAPS2_DYNAMICTEXTURES | D3DCAPS2_FULLSCREENGAMMA | D3DCAPS2_CANAUTOGENMIPMAP;
    c->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD | D3DCAPS3_COPY_TO_VIDMEM | D3DCAPS3_COPY_TO_SYSTEMMEM;
    c->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE;
    c->CursorCaps = 0;
    c->DevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_EXECUTEVIDEOMEMORY | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY |
                 D3DDEVCAPS_TLVERTEXVIDEOMEMORY | D3DDEVCAPS_TEXTURESYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
                 D3DDEVCAPS_DRAWPRIMTLVERTEX | D3DDEVCAPS_CANRENDERAFTERFLIP | D3DDEVCAPS_TEXTURENONLOCALVIDMEM |
                 D3DDEVCAPS_DRAWPRIMITIVES2 | D3DDEVCAPS_DRAWPRIMITIVES2EX | D3DDEVCAPS_HWTRANSFORMANDLIGHT |
                 D3DDEVCAPS_CANBLTSYSTONONLOCAL | D3DDEVCAPS_HWRASTERIZATION | D3DDEVCAPS_PUREDEVICE;
    c->PrimitiveMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                           D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_CLIPPLANESCALEDPOINTS |
                           D3DPMISCCAPS_CLIPTLVERTS | D3DPMISCCAPS_TSSARGTEMP | D3DPMISCCAPS_BLENDOP |
                           D3DPMISCCAPS_PERSTAGECONSTANT | D3DPMISCCAPS_FOGANDSPECULARALPHA |
                           D3DPMISCCAPS_SEPARATEALPHABLEND | D3DPMISCCAPS_MRTPOSTPIXELSHADERBLENDING |
                           D3DPMISCCAPS_FOGVERTEXCLAMPED;
    c->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX |
                    D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_COLORPERSPECTIVE | D3DPRASTERCAPS_SCISSORTEST |
                    D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS | D3DPRASTERCAPS_DEPTHBIAS |
                    (gl.anisotropic ? D3DPRASTERCAPS_ANISOTROPY : 0);
    const DWORD allCmp = 0xFF;
    c->ZCmpCaps = allCmp;
    c->AlphaCmpCaps = allCmp;
    const DWORD blendCaps = D3DPBLENDCAPS_ZERO | D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_SRCCOLOR |
                            D3DPBLENDCAPS_INVSRCCOLOR | D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_INVSRCALPHA |
                            D3DPBLENDCAPS_DESTALPHA | D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_DESTCOLOR |
                            D3DPBLENDCAPS_INVDESTCOLOR | D3DPBLENDCAPS_SRCALPHASAT | D3DPBLENDCAPS_BOTHSRCALPHA |
                            D3DPBLENDCAPS_BOTHINVSRCALPHA | D3DPBLENDCAPS_BLENDFACTOR;
    c->SrcBlendCaps = blendCaps;
    c->DestBlendCaps = blendCaps;
    c->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB | D3DPSHADECAPS_ALPHAGOURAUDBLEND |
                   D3DPSHADECAPS_FOGGOURAUD;
    // Full non-power-of-2 support: neither POW2 nor NONPOW2CONDITIONAL.
    c->TextureCaps = D3DPTEXTURECAPS_PERSPECTIVE | D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_PROJECTED |
                     D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_VOLUMEMAP | D3DPTEXTURECAPS_MIPMAP |
                     D3DPTEXTURECAPS_MIPVOLUMEMAP | D3DPTEXTURECAPS_MIPCUBEMAP;
    const DWORD filt = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MIPFPOINT |
                       D3DPTFILTERCAPS_MIPFLINEAR | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR;
    const DWORD aniso = gl.anisotropic ? (D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MAGFANISOTROPIC) : 0;
    c->TextureFilterCaps = filt | aniso;
    c->CubeTextureFilterCaps = filt | aniso;
    c->VolumeTextureFilterCaps = filt;
    c->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
                            D3DPTADDRESSCAPS_INDEPENDENTUV;
    c->VolumeTextureAddressCaps = c->TextureAddressCaps;
    c->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
    c->MaxTextureWidth = (DWORD)gl.maxTextureSize;
    c->MaxTextureHeight = (DWORD)gl.maxTextureSize;
    c->MaxVolumeExtent = (DWORD)gl.max3DTextureSize;
    c->MaxTextureRepeat = 8192;
    c->MaxTextureAspectRatio = (DWORD)gl.maxTextureSize;
    c->MaxAnisotropy = gl.anisotropic ? (DWORD)gl.maxAnisotropy : 1;
    c->MaxVertexW = 1e10f;
    c->GuardBandLeft = -1e8f;
    c->GuardBandTop = -1e8f;
    c->GuardBandRight = 1e8f;
    c->GuardBandBottom = 1e8f;
    c->ExtentsAdjust = 0.f;
    c->StencilCaps = 0x1FF;
    c->FVFCaps = 8 | D3DFVFCAPS_PSIZE;
    c->TextureOpCaps = 0x03FFFFFF;
    c->MaxTextureBlendStages = 8;
    c->MaxSimultaneousTextures = 8;
    c->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_DIRECTIONALLIGHTS |
                              D3DVTXPCAPS_POSITIONALLIGHTS | D3DVTXPCAPS_LOCALVIEWER | D3DVTXPCAPS_TWEENING;
    c->MaxActiveLights = 8;
    c->MaxUserClipPlanes = 0; // WebGL2 has no clip distances; the engine only records the value
    c->MaxVertexBlendMatrices = 4;
    c->MaxVertexBlendMatrixIndex = 0;
    c->MaxPointSize = gl.maxPointSize;
    c->MaxPrimitiveCount = 0x555555;
    c->MaxVertexIndex = 0xFFFFFF;
    c->MaxStreams = 16;
    c->MaxStreamStride = 255; // WebGL's vertexAttribPointer stride limit
    c->VertexShaderVersion = D3DVS_VERSION(3, 0);
    c->MaxVertexShaderConst = 256;
    c->PixelShaderVersion = D3DPS_VERSION(3, 0);
    c->PixelShader1xMaxValue = 65504.f;
    c->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET | D3DDEVCAPS2_VERTEXELEMENTSCANSHARESTREAMOFFSET |
                  D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES;
    c->MaxNpatchTessellationLevel = 0.f;
    c->MasterAdapterOrdinal = 0;
    c->AdapterOrdinalInGroup = 0;
    c->NumberOfAdaptersInGroup = 1;
    c->DeclTypes = D3DDTCAPS_UBYTE4 | D3DDTCAPS_UBYTE4N | D3DDTCAPS_SHORT2N | D3DDTCAPS_SHORT4N |
                   D3DDTCAPS_USHORT2N | D3DDTCAPS_USHORT4N | D3DDTCAPS_UDEC3 | D3DDTCAPS_DEC3N |
                   D3DDTCAPS_FLOAT16_2 | D3DDTCAPS_FLOAT16_4;
    c->NumSimultaneousRTs = 1;
    c->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
                               D3DPTFILTERCAPS_MAGFLINEAR;
    c->VS20Caps.Caps = D3DVS20CAPS_PREDICATION;
    c->VS20Caps.DynamicFlowControlDepth = 24;
    c->VS20Caps.NumTemps = 32;
    c->VS20Caps.StaticFlowControlDepth = 4;
    c->PS20Caps.Caps = D3DPS20CAPS_ARBITRARYSWIZZLE | D3DPS20CAPS_GRADIENTINSTRUCTIONS | D3DPS20CAPS_PREDICATION |
                       D3DPS20CAPS_NODEPENDENTREADLIMIT | D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT;
    c->PS20Caps.DynamicFlowControlDepth = 24;
    c->PS20Caps.NumTemps = 32;
    c->PS20Caps.StaticFlowControlDepth = 4;
    c->PS20Caps.NumInstructionSlots = 512;
    c->VertexTextureFilterCaps = 0;
    c->MaxVShaderInstructionsExecuted = 65535;
    c->MaxPShaderInstructionsExecuted = 65535;
    c->MaxVertexShader30InstructionSlots = 32768;
    c->MaxPixelShader30InstructionSlots = 32768;
}

HRESULT checkDeviceFormat(const GLCaps &gl, DWORD usage, D3DRESOURCETYPE rtype, D3DFORMAT fmt)
{
    switch ((DWORD)fmt) {
    case D3D9SHIM_FOURCC_INTZ:
    case D3D9SHIM_FOURCC_RESZ:
    case D3D9SHIM_FOURCC_NULL:
    case D3D9SHIM_FOURCC_ATOC:
    case D3D9SHIM_FOURCC_SSAA:
    case D3D9SHIM_FOURCC_DF24:
    case D3D9SHIM_FOURCC_DF16:
    case D3D9SHIM_FOURCC_RAWZ:
        return D3DERR_NOTAVAILABLE; // vendor hacks: the engine has fallbacks for all of them
    case D3DFMT_D24FS8:
        return D3DERR_NOTAVAILABLE; // prefer D24S8: exact D3DRS_DEPTHBIAS -> glPolygonOffset conversion
    default:
        break;
    }
    if (rtype == D3DRTYPE_VERTEXBUFFER || rtype == D3DRTYPE_INDEXBUFFER)
        return (fmt == D3DFMT_VERTEXDATA || fmt == D3DFMT_INDEX16 || fmt == D3DFMT_INDEX32) ? D3D_OK
                                                                                            : D3DERR_NOTAVAILABLE;
    const FormatInfo fi = resolveFormat(fmt, gl);
    if (!(fi.flags & FMT_SUPPORTED))
        return D3DERR_NOTAVAILABLE;
    const bool depth = (fi.flags & FMT_DEPTH) != 0;
    if (usage & (D3DUSAGE_QUERY_SRGBREAD | D3DUSAGE_QUERY_SRGBWRITE | D3DUSAGE_QUERY_VERTEXTEXTURE |
                 D3DUSAGE_QUERY_LEGACYBUMPMAP | D3DUSAGE_DMAP))
        return D3DERR_NOTAVAILABLE;
    if (usage & D3DUSAGE_DEPTHSTENCIL) {
        if (!depth)
            return D3DERR_NOTAVAILABLE;
        // Sampleable depth (hardware shadow maps / INTZ-like) is not supported in v1: report surfaces only.
        if (rtype != D3DRTYPE_SURFACE)
            return D3DERR_NOTAVAILABLE;
        return D3D_OK;
    }
    if (depth)
        return rtype == D3DRTYPE_SURFACE ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (usage & D3DUSAGE_RENDERTARGET) {
        if (!(fi.flags & FMT_RENDERABLE) || rtype == D3DRTYPE_VOLUMETEXTURE)
            return D3DERR_NOTAVAILABLE;
    }
    if ((usage & D3DUSAGE_QUERY_FILTER) && !(fi.flags & FMT_FILTERABLE))
        return D3DERR_NOTAVAILABLE;
    if ((usage & D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING) && !(fi.flags & FMT_BLENDABLE))
        return D3DERR_NOTAVAILABLE;
    if (usage & D3DUSAGE_AUTOGENMIPMAP) {
        if (!(fi.flags & FMT_RENDERABLE) || !(fi.flags & FMT_FILTERABLE))
            return D3DOK_NOAUTOGEN;
    }
    return D3D_OK;
}

std::vector<D3DDISPLAYMODE> displayModeList()
{
    static const UINT kModes[][2] = {{640, 480},   {800, 600},   {1024, 768},  {1152, 864},  {1280, 720},
                                     {1280, 768},  {1280, 800},  {1280, 960},  {1280, 1024}, {1360, 768},
                                     {1366, 768},  {1440, 900},  {1600, 900},  {1600, 1200}, {1680, 1050},
                                     {1920, 1080}, {1920, 1200}, {2048, 1152}, {2560, 1080}, {2560, 1440},
                                     {2560, 1600}, {3440, 1440}, {3840, 2160}};
    std::vector<D3DDISPLAYMODE> modes;
    const UINT dw = (UINT)g_displayWidth, dh = (UINT)g_displayHeight;
    bool haveDesktop = false;
    for (auto &m : kModes) {
        if (m[0] > dw || m[1] > dh)
            continue;
        if (m[0] == dw && m[1] == dh)
            haveDesktop = true;
        modes.push_back({m[0], m[1], 60, D3DFMT_X8R8G8B8});
    }
    if (!haveDesktop)
        modes.push_back({dw, dh, 60, D3DFMT_X8R8G8B8});
    return modes;
}

// ------------------------------------------------------------------------------------------------ Direct3D9

HRESULT Direct3D9::GetAdapterIdentifier(UINT Adapter, DWORD, D3DADAPTER_IDENTIFIER9 *id)
{
    if (Adapter != 0 || !id)
        return D3DERR_INVALIDCALL;
    memset(id, 0, sizeof(*id));
    const GLCaps &gl = currentGLCaps();
    strncpy(id->Driver, "webgl2", sizeof(id->Driver) - 1);
    snprintf(id->Description, sizeof(id->Description), "WebGL2 (%s)", gl.renderer);
    strncpy(id->DeviceName, "\\\\.\\DISPLAY1", sizeof(id->DeviceName) - 1);
    // VendorId 0: none of the engine's NVIDIA (0x10DE) / AMD (0x1002) / Intel (0x8086) vendor paths apply.
    id->VendorId = 0;
    id->DeviceId = 0;
    id->SubSysId = 0;
    id->Revision = 0;
    id->DriverVersion.QuadPart = 0;
    id->WHQLLevel = 1;
    return D3D_OK;
}

UINT Direct3D9::GetAdapterModeCount(UINT Adapter, D3DFORMAT Format)
{
    if (Adapter != 0 || (Format != D3DFMT_X8R8G8B8 && Format != D3DFMT_A8R8G8B8))
        return 0;
    return (UINT)displayModeList().size();
}

HRESULT Direct3D9::EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode)
{
    if (Adapter != 0 || !pMode)
        return D3DERR_INVALIDCALL;
    if (Format != D3DFMT_X8R8G8B8 && Format != D3DFMT_A8R8G8B8)
        return D3DERR_NOTAVAILABLE;
    auto modes = displayModeList();
    if (Mode >= modes.size())
        return D3DERR_INVALIDCALL;
    *pMode = modes[Mode];
    pMode->Format = Format;
    return D3D_OK;
}

HRESULT Direct3D9::GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode)
{
    if (Adapter != 0 || !pMode)
        return D3DERR_INVALIDCALL;
    pMode->Width = (UINT)g_displayWidth;
    pMode->Height = (UINT)g_displayHeight;
    pMode->RefreshRate = 60;
    pMode->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

HRESULT Direct3D9::CheckDeviceType(UINT Adapter, D3DDEVTYPE, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat,
                                   BOOL)
{
    if (Adapter != 0)
        return D3DERR_INVALIDCALL;
    if (AdapterFormat != D3DFMT_X8R8G8B8 && AdapterFormat != D3DFMT_A8R8G8B8 && AdapterFormat != D3DFMT_UNKNOWN)
        return D3DERR_NOTAVAILABLE;
    if (BackBufferFormat != D3DFMT_X8R8G8B8 && BackBufferFormat != D3DFMT_A8R8G8B8 &&
        BackBufferFormat != D3DFMT_UNKNOWN)
        return D3DERR_NOTAVAILABLE;
    return D3D_OK;
}

HRESULT Direct3D9::CheckDeviceFormat(UINT Adapter, D3DDEVTYPE, D3DFORMAT, DWORD Usage, D3DRESOURCETYPE RType,
                                     D3DFORMAT CheckFormat)
{
    if (Adapter != 0)
        return D3DERR_INVALIDCALL;
    HRESULT hr = checkDeviceFormat(currentGLCaps(), Usage, RType, CheckFormat);
    D3D9_DEBUG("CheckDeviceFormat(usage 0x%lx, rtype %d, %s) -> %s", (unsigned long)Usage, (int)RType,
               formatName(CheckFormat), FAILED(hr) ? "NOTAVAILABLE" : "OK");
    return hr;
}

HRESULT Direct3D9::CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE, D3DFORMAT, BOOL,
                                              D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels)
{
    if (Adapter != 0)
        return D3DERR_INVALIDCALL;
    if (MultiSampleType == D3DMULTISAMPLE_NONE) {
        if (pQualityLevels)
            *pQualityLevels = 1;
        return D3D_OK;
    }
    // v1: no MSAA (multisampled renderbuffers can't be flipped by blitFramebuffer; see README).
    if (pQualityLevels)
        *pQualityLevels = 0;
    return D3DERR_NOTAVAILABLE;
}

HRESULT Direct3D9::CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE, D3DFORMAT, D3DFORMAT,
                                          D3DFORMAT DepthStencilFormat)
{
    if (Adapter != 0)
        return D3DERR_INVALIDCALL;
    const FormatInfo fi = resolveFormat(DepthStencilFormat, currentGLCaps());
    return (fi.flags & FMT_DEPTH) ? D3D_OK : D3DERR_NOTAVAILABLE;
}

HRESULT Direct3D9::CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE, D3DFORMAT SourceFormat,
                                               D3DFORMAT TargetFormat)
{
    if (Adapter != 0)
        return D3DERR_INVALIDCALL;
    const GLCaps &gl = currentGLCaps();
    const FormatInfo s = resolveFormat(SourceFormat, gl), t = resolveFormat(TargetFormat, gl);
    if (!(s.flags & FMT_SUPPORTED) || !(t.flags & FMT_RENDERABLE) || (s.flags & FMT_DEPTH))
        return D3DERR_NOTAVAILABLE;
    return D3D_OK;
}

HRESULT Direct3D9::GetDeviceCaps(UINT Adapter, D3DDEVTYPE, D3DCAPS9 *pCaps)
{
    if (Adapter != 0 || !pCaps)
        return D3DERR_INVALIDCALL;
    fillD3DCaps(pCaps, currentGLCaps());
    return D3D_OK;
}

HMONITOR Direct3D9::GetAdapterMonitor(UINT)
{
    static int dummyMonitor;
    return (HMONITOR)(void *)&dummyMonitor;
}

HRESULT Direct3D9::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                                D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **ppDevice)
{
    if (!ppDevice || !pp)
        return D3DERR_INVALIDCALL;
    *ppDevice = nullptr;
    if (Adapter != 0)
        return D3DERR_INVALIDCALL;
    Device *dev = new Device(this, Adapter, DeviceType, hFocusWindow, BehaviorFlags);
    HRESULT hr = dev->init(pp);
    if (FAILED(hr)) {
        dev->Release();
        return hr;
    }
    *ppDevice = dev;
    return D3D_OK;
}

UINT Direct3D9::GetAdapterModeCountEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter)
{
    return GetAdapterModeCount(Adapter, pFilter ? pFilter->Format : D3DFMT_X8R8G8B8);
}

HRESULT Direct3D9::EnumAdapterModesEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter, UINT Mode,
                                      D3DDISPLAYMODEEX *pMode)
{
    if (!pMode)
        return D3DERR_INVALIDCALL;
    D3DDISPLAYMODE m;
    HRESULT hr = EnumAdapterModes(Adapter, pFilter ? pFilter->Format : D3DFMT_X8R8G8B8, Mode, &m);
    if (FAILED(hr))
        return hr;
    pMode->Size = sizeof(*pMode);
    pMode->Width = m.Width;
    pMode->Height = m.Height;
    pMode->RefreshRate = m.RefreshRate;
    pMode->Format = m.Format;
    pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    return D3D_OK;
}

HRESULT Direct3D9::GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation)
{
    if (pRotation)
        *pRotation = D3DDISPLAYROTATION_IDENTITY;
    if (!pMode)
        return D3D_OK;
    D3DDISPLAYMODE m;
    HRESULT hr = GetAdapterDisplayMode(Adapter, &m);
    if (FAILED(hr))
        return hr;
    pMode->Size = sizeof(*pMode);
    pMode->Width = m.Width;
    pMode->Height = m.Height;
    pMode->RefreshRate = m.RefreshRate;
    pMode->Format = m.Format;
    pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    return D3D_OK;
}

HRESULT Direct3D9::CreateDeviceEx(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                                  D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODEEX *, IDirect3DDevice9Ex **ppDevice)
{
    IDirect3DDevice9 *dev = nullptr;
    HRESULT hr = CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pp, &dev);
    if (ppDevice)
        *ppDevice = static_cast<IDirect3DDevice9Ex *>(static_cast<Device *>(dev));
    return hr;
}

HRESULT Direct3D9::GetAdapterLUID(UINT, struct _LUID *pLUID)
{
    if (pLUID)
        memset(pLUID, 0, 8);
    return D3D_OK;
}

} // namespace d3d9shim

using namespace d3d9shim;

extern "C" IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion)
{
    initLogFromEnv();
    if (SDKVersion != D3D_SDK_VERSION && SDKVersion != (D3D9b_SDK_VERSION | 0x80000000u) &&
        SDKVersion != D3D9b_SDK_VERSION)
        D3D9_WARN("Direct3DCreate9: unexpected SDK version %u", SDKVersion);
    return new Direct3D9(false);
}

extern "C" HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex **ppD3D)
{
    initLogFromEnv();
    (void)SDKVersion;
    if (!ppD3D)
        return D3DERR_INVALIDCALL;
    *ppD3D = new Direct3D9(true);
    return D3D_OK;
}
