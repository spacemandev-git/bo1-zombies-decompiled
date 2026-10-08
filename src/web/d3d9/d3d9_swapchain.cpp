// d3d9_swapchain.cpp - the implicit swap chain: back buffer surface (an RGBA8 GL texture rendered top-down) and
// Present (flip-blit to the canvas' default framebuffer).
#include "d3d9_internal.h"

namespace d3d9shim {

SwapChain::SwapChain(Device *dev, const D3DPRESENT_PARAMETERS &pp) : m_device(dev), m_pp(pp)
{
    m_refs = 0; // device-owned; external references only
    D3DFORMAT fmt = pp.BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_A8R8G8B8 : pp.BackBufferFormat;
    // The canvas has no alpha, so X8R8G8B8 and A8R8G8B8 back buffers are both RGBA8 textures.
    if (fmt == D3DFMT_X8R8G8B8)
        fmt = D3DFMT_A8R8G8B8;
    m_bbStorage.init(D3DRTYPE_TEXTURE, pp.BackBufferWidth, pp.BackBufferHeight, 1, 1, D3DUSAGE_RENDERTARGET, fmt,
                     D3DPOOL_DEFAULT);
    // Container = this swap chain: AddRef/Release on the back buffer forward here (and never free it).
    m_backBuffer = new Surface(dev, static_cast<IDirect3DSwapChain9 *>(this), &m_bbStorage, 0, 0, Surface::BackBuffer);
}

SwapChain::~SwapChain() {}

void SwapChain::destroy()
{
    if (m_backBuffer) {
        m_backBuffer->destroyInternal();
        m_backBuffer = nullptr;
    }
    m_bbStorage.destroyGL(m_device);
    delete this;
}

ULONG SwapChain::Release()
{
    ULONG r = m_refs.load();
    if (r == 0)
        return 0;
    return --m_refs;
}

void SwapChain::reset(const D3DPRESENT_PARAMETERS &pp)
{
    m_pp = pp;
    m_backBuffer->resize(pp.BackBufferWidth, pp.BackBufferHeight);
}

HRESULT SwapChain::Present(const RECT *, const RECT *, HWND, const struct _RGNDATA *, DWORD)
{
    return m_device->Present(nullptr, nullptr, nullptr, nullptr);
}

HRESULT SwapChain::GetFrontBufferData(IDirect3DSurface9 *pDestSurface)
{
    return m_device->GetFrontBufferData(0, pDestSurface);
}

HRESULT SwapChain::GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **pp)
{
    if (!pp || iBackBuffer != 0)
        return D3DERR_INVALIDCALL;
    m_backBuffer->AddRef();
    *pp = m_backBuffer;
    return D3D_OK;
}

HRESULT SwapChain::GetRasterStatus(D3DRASTER_STATUS *p)
{
    if (!p)
        return D3DERR_INVALIDCALL;
    p->InVBlank = FALSE;
    p->ScanLine = 0;
    return D3D_OK;
}

HRESULT SwapChain::GetDisplayMode(D3DDISPLAYMODE *pMode) { return m_device->GetDisplayMode(0, pMode); }

HRESULT SwapChain::GetDevice(IDirect3DDevice9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT SwapChain::GetPresentParameters(D3DPRESENT_PARAMETERS *p)
{
    if (!p)
        return D3DERR_INVALIDCALL;
    *p = m_pp;
    return D3D_OK;
}

} // namespace d3d9shim
