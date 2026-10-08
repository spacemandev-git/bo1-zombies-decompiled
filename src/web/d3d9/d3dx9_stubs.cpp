// d3dx9_stubs.cpp - the D3DX 9 subset the engine references (see include/d3dx9.h).
#include "d3d9_internal.h"

#include <d3dx9.h>
#include <stdio.h>

namespace d3d9shim {

class XBuffer final : public Unknown<ID3DXBuffer> {
public:
    explicit XBuffer(size_t n) : m_data(n, 0) {}
    LPVOID GetBufferPointer() override { return m_data.data(); }
    DWORD GetBufferSize() override { return (DWORD)m_data.size(); }
    std::vector<uint8_t> m_data;
};

class XConstantTable final : public Unknown<ID3DXConstantTable> {
public:
    XConstantTable(const uint8_t *p, size_t n) : m_data(p, p + n) {}
    LPVOID GetBufferPointer() override { return m_data.data(); }
    DWORD GetBufferSize() override { return (DWORD)m_data.size(); }
    std::vector<uint8_t> m_data;
};

} // namespace d3d9shim

using namespace d3d9shim;

extern "C" {

HRESULT WINAPI D3DXCreateBuffer(DWORD NumBytes, LPD3DXBUFFER *ppBuffer)
{
    if (!ppBuffer)
        return D3DERR_INVALIDCALL;
    *ppBuffer = new XBuffer(NumBytes);
    return D3D_OK;
}

HRESULT WINAPI D3DXCompileShader(LPCSTR, UINT, const D3DXMACRO *, LPD3DXINCLUDE, LPCSTR pFunctionName, LPCSTR pProfile,
                                 DWORD, LPD3DXBUFFER *ppShader, LPD3DXBUFFER *ppErrorMsgs,
                                 LPD3DXCONSTANTTABLE *ppConstantTable)
{
    static const char msg[] = "D3DXCompileShader: no HLSL compiler in the web build (load precompiled shaders "
                              "from fastfiles)";
    D3D9_WARN_ONCE("%s (%s %s)", msg, pFunctionName ? pFunctionName : "?", pProfile ? pProfile : "?");
    if (ppShader)
        *ppShader = nullptr;
    if (ppConstantTable)
        *ppConstantTable = nullptr;
    if (ppErrorMsgs) {
        XBuffer *b = new XBuffer(sizeof(msg));
        memcpy(b->m_data.data(), msg, sizeof(msg));
        *ppErrorMsgs = b;
    }
    return E_NOTIMPL;
}

HRESULT WINAPI D3DXGetShaderConstantTable(const DWORD *pFunction, LPD3DXCONSTANTTABLE *ppConstantTable)
{
    if (!pFunction || !ppConstantTable)
        return D3DERR_INVALIDCALL;
    *ppConstantTable = nullptr;
    const size_t bytes = shaderByteLength(pFunction, 0);
    for (size_t i = 1; i < bytes / 4;) {
        const DWORD t = pFunction[i];
        if ((t & 0xFFFF) == D3DSIO_COMMENT) {
            const DWORD n = (t & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT;
            if (n >= 1 && pFunction[i + 1] == MAKEFOURCC('C', 'T', 'A', 'B')) {
                *ppConstantTable = new XConstantTable((const uint8_t *)&pFunction[i + 2], (size_t)(n - 1) * 4);
                return D3D_OK;
            }
            i += 1 + n;
            continue;
        }
        if (t == D3DVS_END())
            break;
        i += 1 + ((t & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT);
    }
    return D3DXERR_INVALIDDATA;
}

static HRESULT semantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount, bool outputs)
{
    if (!pFunction || !pCount)
        return D3DERR_INVALIDCALL;
    TranslatedShader tr = translateShader(pFunction, 0, nullptr);
    if (!tr.ok)
        return D3DXERR_INVALIDDATA;
    UINT n = 0;
    for (const ShaderSemantic &s : outputs ? tr.outputs : tr.inputs) {
        if (s.usage == 255)
            continue;
        if (pSemantics) {
            pSemantics[n].Usage = s.usage;
            pSemantics[n].UsageIndex = s.usageIndex;
        }
        ++n;
    }
    *pCount = n;
    return D3D_OK;
}

HRESULT WINAPI D3DXGetShaderInputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount)
{
    return semantics(pFunction, pSemantics, pCount, false);
}

HRESULT WINAPI D3DXGetShaderOutputSemantics(const DWORD *pFunction, D3DXSEMANTIC *pSemantics, UINT *pCount)
{
    return semantics(pFunction, pSemantics, pCount, true);
}

UINT WINAPI D3DXGetShaderSize(const DWORD *pFunction) { return (UINT)shaderByteLength(pFunction, 0); }

DWORD WINAPI D3DXGetShaderVersion(const DWORD *pFunction) { return pFunction ? pFunction[0] : 0; }

HRESULT WINAPI D3DXSaveSurfaceToFileA(LPCSTR pDestFile, D3DXIMAGE_FILEFORMAT DestFormat, IDirect3DSurface9 *pSrcSurface,
                                      const struct tagPALETTEENTRY *, const RECT *pSrcRect)
{
    if (!pDestFile || !pSrcSurface)
        return D3DERR_INVALIDCALL;
    if (DestFormat != D3DXIFF_TGA && DestFormat != D3DXIFF_BMP) {
        D3D9_WARN_ONCE("D3DXSaveSurfaceToFile: only TGA and BMP are implemented (asked for format %d)", (int)DestFormat);
        return E_NOTIMPL;
    }
    D3DSURFACE_DESC desc;
    pSrcSurface->GetDesc(&desc);
    if (desc.Format != D3DFMT_A8R8G8B8 && desc.Format != D3DFMT_X8R8G8B8) {
        D3D9_WARN_ONCE("D3DXSaveSurfaceToFile: only A8R8G8B8/X8R8G8B8 surfaces are supported");
        return E_NOTIMPL;
    }
    RECT r = pSrcRect ? *pSrcRect : RECT{0, 0, (LONG)desc.Width, (LONG)desc.Height};
    const UINT w = (UINT)(r.right - r.left), h = (UINT)(r.bottom - r.top);
    D3DLOCKED_RECT lr;
    if (FAILED(pSrcSurface->LockRect(&lr, &r, D3DLOCK_READONLY)))
        return D3DERR_INVALIDCALL;
    FILE *f = fopen(pDestFile, "wb");
    if (!f) {
        pSrcSurface->UnlockRect();
        return E_FAIL;
    }
    std::vector<uint8_t> row((size_t)w * 4);
    if (DestFormat == D3DXIFF_TGA) {
        uint8_t hdr[18] = {0};
        hdr[2] = 2; // uncompressed true color
        hdr[12] = (uint8_t)(w & 0xff), hdr[13] = (uint8_t)(w >> 8);
        hdr[14] = (uint8_t)(h & 0xff), hdr[15] = (uint8_t)(h >> 8);
        hdr[16] = 32;
        hdr[17] = 0x28; // top-left origin, 8 alpha bits
        fwrite(hdr, 1, sizeof(hdr), f);
        for (UINT y = 0; y < h; ++y) {
            memcpy(row.data(), (const uint8_t *)lr.pBits + (size_t)y * lr.Pitch, row.size());
            if (desc.Format == D3DFMT_X8R8G8B8)
                for (UINT x = 0; x < w; ++x)
                    row[x * 4 + 3] = 255;
            fwrite(row.data(), 1, row.size(), f);
        }
    } else {
        const uint32_t dataSize = w * h * 4, fileSize = 54 + dataSize;
        uint8_t hdr[54] = {'B', 'M'};
        memcpy(hdr + 2, &fileSize, 4);
        const uint32_t off = 54, dib = 40, planesBpp = 1 | (32u << 16);
        const int32_t sw = (int32_t)w, sh = -(int32_t)h; // top-down
        memcpy(hdr + 10, &off, 4);
        memcpy(hdr + 14, &dib, 4);
        memcpy(hdr + 18, &sw, 4);
        memcpy(hdr + 22, &sh, 4);
        memcpy(hdr + 26, &planesBpp, 4);
        memcpy(hdr + 34, &dataSize, 4);
        fwrite(hdr, 1, sizeof(hdr), f);
        for (UINT y = 0; y < h; ++y)
            fwrite((const uint8_t *)lr.pBits + (size_t)y * lr.Pitch, 1, row.size(), f);
    }
    fclose(f);
    pSrcSurface->UnlockRect();
    return D3D_OK;
}

} // extern "C"
