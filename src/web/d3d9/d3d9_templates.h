// d3d9_templates.h - member definitions of the Resource<I> / TextureBase<I> templates (included by
// d3d9_internal.h after the Device declaration).
#pragma once

namespace d3d9shim {

template <class I>
Resource<I>::Resource(Device *dev, bool holdDeviceRef) : m_device(dev), m_holdsDeviceRef(holdDeviceRef), m_id(nextObjectId())
{
    if (m_holdsDeviceRef && m_device)
        m_device->AddRef();
}

template <class I> Resource<I>::~Resource()
{
    if (m_holdsDeviceRef && m_device)
        m_device->Release();
}

template <class I> void Resource<I>::finalRelease()
{
    if (!m_device || onDeviceThread()) {
        delete this;
        return;
    }
    Resource<I> *self = this;
    m_device->deferDestroy([self] { delete self; });
}

template <class I> HRESULT Resource<I>::GetDevice(IDirect3DDevice9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_device;
    if (m_device)
        m_device->AddRef();
    return D3D_OK;
}

template <class I> HRESULT Resource<I>::SetPrivateData(REFGUID g, const void *data, DWORD size, DWORD flags)
{
    if (flags & 1 /* D3DSPD_IUNKNOWN */) {
        D3D9_WARN_ONCE("SetPrivateData with D3DSPD_IUNKNOWN is not supported");
        return D3DERR_INVALIDCALL;
    }
    FreePrivateData(g);
    std::vector<uint8_t> v((const uint8_t *)data, (const uint8_t *)data + size);
    m_private.emplace_back(g, std::move(v));
    return D3D_OK;
}

template <class I> HRESULT Resource<I>::GetPrivateData(REFGUID g, void *data, DWORD *size)
{
    for (auto &p : m_private) {
        if (memcmp(&p.first, &g, sizeof(GUID)) == 0) {
            if (!size)
                return D3DERR_INVALIDCALL;
            if (!data || *size < p.second.size()) {
                *size = (DWORD)p.second.size();
                return data ? D3DERR_MOREDATA : D3D_OK;
            }
            memcpy(data, p.second.data(), p.second.size());
            *size = (DWORD)p.second.size();
            return D3D_OK;
        }
    }
    return D3DERR_NOTFOUND;
}

template <class I> HRESULT Resource<I>::FreePrivateData(REFGUID g)
{
    for (size_t i = 0; i < m_private.size(); ++i) {
        if (memcmp(&m_private[i].first, &g, sizeof(GUID)) == 0) {
            m_private.erase(m_private.begin() + (long)i);
            return D3D_OK;
        }
    }
    return D3DERR_NOTFOUND;
}

template <class I> TextureBase<I>::~TextureBase()
{
    if (this->m_device) {
        this->m_device->onTextureStorageDestroyed(&m_storage);
        m_storage.destroyGL(this->m_device);
    }
}

template <class I> DWORD TextureBase<I>::SetLOD(DWORD lod)
{
    DWORD old = m_storage.lod;
    if (m_storage.pool == D3DPOOL_MANAGED)
        m_storage.lod = lod < m_storage.levels ? lod : m_storage.levels - 1;
    return old;
}

template <class I> void TextureBase<I>::GenerateMipSubLevels()
{
    if (!this->m_device || !onDeviceThread() || m_storage.cpuOnly)
        return;
    m_storage.ensureGL(this->m_device);
    m_storage.flushUploads(this->m_device);
    if ((m_storage.fi.flags & FMT_FILTERABLE) && (m_storage.fi.flags & FMT_RENDERABLE) && m_storage.levels > 1) {
        this->m_device->bindTextureForEdit(m_storage.target, m_storage.tex);
        glGenerateMipmap(m_storage.target);
    }
}

template <class I> void TextureBase<I>::PreLoad()
{
    if (!this->m_device || !onDeviceThread() || m_storage.cpuOnly)
        return;
    m_storage.ensureGL(this->m_device);
    m_storage.flushUploads(this->m_device);
}

} // namespace d3d9shim
