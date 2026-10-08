// d3d9_resources.cpp - textures (2D/cube/volume), surfaces, volumes, vertex/index buffers, vertex declarations.
#include "d3d9_internal.h"

#include <algorithm>

namespace d3d9shim {

// ------------------------------------------------------------------------------------------------ TexStorage

static void forceSoftwareDecode(FormatInfo &fi)
{
    if (!(fi.flags & FMT_COMPRESSED) || fi.conv != Conv::None)
        return;
    switch ((DWORD)fi.d3d) {
    case D3DFMT_DXT1: fi.conv = Conv::DXT1_RGBA8; break;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3: fi.conv = Conv::DXT3_RGBA8; break;
    default: fi.conv = Conv::DXT5_RGBA8; break;
    }
    fi.internalFormat = GL_RGBA8;
    fi.format = GL_RGBA;
    fi.type = GL_UNSIGNED_BYTE;
    fi.glBytesPerPixel = 4;
    fi.flags |= FMT_EMULATED;
}

void TexStorage::init(D3DRESOURCETYPE t, UINT w, UINT h, UINT d, UINT lv, DWORD use, D3DFORMAT f, D3DPOOL p)
{
    type = t;
    width = w ? w : 1;
    height = h ? h : 1;
    depth = (t == D3DRTYPE_VOLUMETEXTURE) ? (d ? d : 1) : 1;
    const UINT maxLv = maxMipLevels(width, height, depth);
    levels = (lv == 0 || lv > maxLv) ? maxLv : lv;
    if (use & D3DUSAGE_AUTOGENMIPMAP)
        levels = maxLv;
    faces = t == D3DRTYPE_CUBETEXTURE ? 6 : 1;
    format = f;
    usage = use;
    pool = p;
    fi = resolveFormat(f, currentGLCaps());
    // WebGL has no compressed 3D textures and requires 4-aligned S3TC level 0: decode on the CPU instead.
    if ((fi.flags & FMT_COMPRESSED) && (t == D3DRTYPE_VOLUMETEXTURE || (width % 4) || (height % 4)))
        forceSoftwareDecode(fi);
    target = t == D3DRTYPE_CUBETEXTURE ? GL_TEXTURE_CUBE_MAP : (t == D3DRTYPE_VOLUMETEXTURE ? GL_TEXTURE_3D : GL_TEXTURE_2D);
    cpuOnly = p == D3DPOOL_SYSTEMMEM || p == D3DPOOL_SCRATCH;
    renderTarget = (use & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) != 0 && !cpuOnly;
    keepShadow = cpuOnly || (use & D3DUSAGE_DYNAMIC) || !(fi.flags & FMT_COMPRESSED);
    images.clear();
    images.resize((size_t)faces * levels);
}

HRESULT TexStorage::lock(UINT face, UINT level, const D3DBOX *box, DWORD flags, void **bits, INT *outPitch,
                         INT *outSlice)
{
    if (face >= faces || level >= levels || !bits)
        return D3DERR_INVALIDCALL;
    if (renderTarget) {
        D3D9_WARN_ONCE("LockRect on a render-target texture is not supported (use GetRenderTargetData)");
        return D3DERR_INVALIDCALL;
    }
    std::lock_guard<std::mutex> g(mtx);
    Image &im = image(face, level);
    const size_t bytes = levelBytes(level);
    if (im.shadow.size() != bytes) {
        if (!keepShadow && !im.shadow.size() && !(flags & D3DLOCK_DISCARD) && im.contentsLost)
            D3D9_WARN_ONCE("re-locking a compressed texture whose CPU copy was released after upload; contents "
                           "outside the written area are lost");
        im.shadow.assign(bytes, 0);
    }
    const UINT lw = levelWidth(level), lh = levelHeight(level), ld = levelDepth(level);
    UINT x0 = 0, y0 = 0, z0 = 0, x1 = lw, y1 = lh, z1 = ld;
    if (box) {
        x0 = box->Left;
        y0 = box->Top;
        z0 = box->Front;
        x1 = box->Right;
        y1 = box->Bottom;
        z1 = box->Back;
        if (x0 >= x1 || y0 >= y1 || z0 >= z1 || x1 > lw || y1 > lh || z1 > ld)
            return D3DERR_INVALIDCALL;
    }
    const UINT p = pitch(level), sp = slicePitch(level);
    size_t offset;
    if (fi.flags & FMT_COMPRESSED)
        offset = (size_t)z0 * sp + (size_t)(y0 / 4) * p + (size_t)(x0 / 4) * fi.bytesPerBlock;
    else
        offset = (size_t)z0 * sp + (size_t)y0 * p + (size_t)x0 * fi.bytesPerBlock;
    *bits = im.shadow.data() + offset;
    if (outPitch)
        *outPitch = (INT)p;
    if (outSlice)
        *outSlice = (INT)sp;
    if (!(flags & D3DLOCK_READONLY)) {
        if (!im.lockWrites) {
            im.lx0 = x0, im.ly0 = y0, im.lz0 = z0, im.lx1 = x1, im.ly1 = y1, im.lz1 = z1;
        } else {
            im.lx0 = std::min(im.lx0, x0), im.ly0 = std::min(im.ly0, y0), im.lz0 = std::min(im.lz0, z0);
            im.lx1 = std::max(im.lx1, x1), im.ly1 = std::max(im.ly1, y1), im.lz1 = std::max(im.lz1, z1);
        }
        im.lockWrites = true;
    }
    ++im.lockCount;
    return D3D_OK;
}

HRESULT TexStorage::unlock(UINT face, UINT level)
{
    if (face >= faces || level >= levels)
        return D3DERR_INVALIDCALL;
    std::lock_guard<std::mutex> g(mtx);
    Image &im = image(face, level);
    if (im.lockCount <= 0)
        return D3DERR_INVALIDCALL;
    if (--im.lockCount == 0 && im.lockWrites) {
        if (!im.dirty) {
            im.dx0 = im.lx0, im.dy0 = im.ly0, im.dz0 = im.lz0, im.dx1 = im.lx1, im.dy1 = im.ly1, im.dz1 = im.lz1;
        } else {
            im.dx0 = std::min(im.dx0, im.lx0), im.dy0 = std::min(im.dy0, im.ly0), im.dz0 = std::min(im.dz0, im.lz0);
            im.dx1 = std::max(im.dx1, im.lx1), im.dy1 = std::max(im.dy1, im.ly1), im.dz1 = std::max(im.dz1, im.lz1);
        }
        im.dirty = true;
        im.lockWrites = false;
        if (!cpuOnly)
            anyDirty = true;
    }
    return D3D_OK;
}

void TexStorage::ensureGL(Device *dev)
{
    if (cpuOnly || glAllocated)
        return;
    glGenTextures(1, &tex);
    dev->bindTextureForEdit(target, tex);
    if (fi.flags & FMT_UNSIZED) {
        for (UINT f = 0; f < faces; ++f)
            for (UINT l = 0; l < levels; ++l) {
                if (target == GL_TEXTURE_3D)
                    glTexImage3D(target, (GLint)l, (GLint)fi.internalFormat, (GLsizei)levelWidth(l),
                                 (GLsizei)levelHeight(l), (GLsizei)levelDepth(l), 0, fi.format, fi.type, nullptr);
                else
                    glTexImage2D(faceTarget(f), (GLint)l, (GLint)fi.internalFormat, (GLsizei)levelWidth(l),
                                 (GLsizei)levelHeight(l), 0, fi.format, fi.type, nullptr);
            }
    } else if (target == GL_TEXTURE_3D) {
        glTexStorage3D(target, (GLsizei)levels, fi.internalFormat, (GLsizei)width, (GLsizei)height, (GLsizei)depth);
    } else {
        glTexStorage2D(target, (GLsizei)levels, fi.internalFormat, (GLsizei)width, (GLsizei)height);
    }
    glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, (GLint)levels - 1);
    if (lod) {
        glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, (GLint)lod);
        lodApplied = lod;
    }
    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        D3D9_ERROR("allocating %ux%ux%u %s texture (%u levels) failed: GL error 0x%x", width, height, depth, fi.name,
                   levels, err);
    glAllocated = true;
    // Images written before the GL texture existed are already marked dirty.
}

void TexStorage::flushUploads(Device *dev)
{
    if (cpuOnly)
        return;
    if (lod != lodApplied && glAllocated) {
        dev->bindTextureForEdit(target, tex);
        glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, (GLint)lod);
        lodApplied = lod;
    }
    if (!anyDirty.load())
        return;
    ensureGL(dev);
    std::lock_guard<std::mutex> g(mtx);
    bool remaining = false;
    static thread_local std::vector<uint8_t> temp;
    for (UINT f = 0; f < faces; ++f) {
        for (UINT l = 0; l < levels; ++l) {
            Image &im = image(f, l);
            if (!im.dirty)
                continue;
            if (im.lockCount > 0) {
                remaining = true; // still being written (another thread); upload next time
                continue;
            }
            if (im.shadow.size() != levelBytes(l)) {
                im.dirty = false;
                continue;
            }
            dev->bindTextureForEdit(target, tex);
            const UINT lw = levelWidth(l), lh = levelHeight(l), ld = levelDepth(l);
            const GLenum ft = faceTarget(f);
            if ((fi.flags & FMT_COMPRESSED) && fi.conv == Conv::None) {
                // Native S3TC: whole level (WebGL sub-image rules want 4-aligned regions anyway).
                glCompressedTexSubImage2D(ft, (GLint)l, 0, 0, (GLsizei)lw, (GLsizei)lh, fi.internalFormat,
                                          (GLsizei)im.shadow.size(), im.shadow.data());
                g_stats.textureUploadBytes += im.shadow.size();
            } else {
                UINT x0 = im.dx0, y0 = im.dy0, z0 = im.dz0, x1 = std::min(im.dx1, lw), y1 = std::min(im.dy1, lh),
                     z1 = std::min(im.dz1, ld);
                if (fi.flags & FMT_COMPRESSED) { // software decode: whole blocks
                    x0 &= ~3u, y0 &= ~3u;
                    x1 = std::min((x1 + 3) & ~3u, lw);
                    y1 = std::min((y1 + 3) & ~3u, lh);
                }
                if (x0 >= x1 || y0 >= y1 || z0 >= z1) {
                    im.dirty = false;
                    continue;
                }
                const UINT rw = x1 - x0, rh = y1 - y0, rd = z1 - z0;
                const size_t dstPitch = uploadPitch(fi, rw);
                const size_t dstSlice = dstPitch * rh;
                temp.resize(dstSlice * rd);
                const UINT p = pitch(l), sp = slicePitch(l);
                for (UINT z = 0; z < rd; ++z) {
                    const uint8_t *src;
                    if (fi.flags & FMT_COMPRESSED)
                        src = im.shadow.data() + (size_t)(z0 + z) * sp + (size_t)(y0 / 4) * p +
                              (size_t)(x0 / 4) * fi.bytesPerBlock;
                    else
                        src = im.shadow.data() + (size_t)(z0 + z) * sp + (size_t)y0 * p +
                              (size_t)x0 * fi.bytesPerBlock;
                    convertForUpload(fi, src, p, temp.data() + z * dstSlice, dstPitch, rw, rh);
                }
                if (target == GL_TEXTURE_3D)
                    glTexSubImage3D(target, (GLint)l, (GLint)x0, (GLint)y0, (GLint)z0, (GLsizei)rw, (GLsizei)rh,
                                    (GLsizei)rd, fi.format, fi.type, temp.data());
                else
                    glTexSubImage2D(ft, (GLint)l, (GLint)x0, (GLint)y0, (GLsizei)rw, (GLsizei)rh, fi.format, fi.type,
                                    temp.data());
                g_stats.textureUploadBytes += temp.size();
            }
            im.dirty = false;
            if (!keepShadow) {
                std::vector<uint8_t>().swap(im.shadow);
                im.contentsLost = true;
            }
        }
    }
    anyDirty = remaining;
}

void TexStorage::destroyGL(Device *dev)
{
    if (tex) {
        if (onDeviceThread()) {
            glDeleteTextures(1, &tex);
            dev->cache().invalidate(); // texture unit bindings may reference the name
        } else {
            dev->queueGLDelete(GL_TEXTURE, tex);
        }
        tex = 0;
    }
    glAllocated = false;
}

void TexStorage::copyImageFrom(TexStorage &src, UINT srcFace, UINT srcLevel, UINT dstFace, UINT dstLevel,
                               const RECT *srcRect, const POINT *dstPoint)
{
    std::lock_guard<std::mutex> g(mtx);
    Image &si = src.image(srcFace, srcLevel);
    Image &di = image(dstFace, dstLevel);
    const size_t bytes = levelBytes(dstLevel);
    if (di.shadow.size() != bytes)
        di.shadow.assign(bytes, 0);
    if (si.shadow.empty())
        return;
    RECT r = {0, 0, (LONG)src.levelWidth(srcLevel), (LONG)src.levelHeight(srcLevel)};
    if (srcRect)
        r = *srcRect;
    const UINT dx = dstPoint ? (UINT)dstPoint->x : 0, dy = dstPoint ? (UINT)dstPoint->y : 0;
    UINT w = (UINT)(r.right - r.left), h = (UINT)(r.bottom - r.top);
    w = std::min(w, levelWidth(dstLevel) - std::min(dx, levelWidth(dstLevel)));
    h = std::min(h, levelHeight(dstLevel) - std::min(dy, levelHeight(dstLevel)));
    const bool comp = (fi.flags & FMT_COMPRESSED) != 0;
    const UINT rows = comp ? (h + 3) / 4 : h;
    const UINT rowBytes = comp ? ((w + 3) / 4) * fi.bytesPerBlock : w * fi.bytesPerBlock;
    const UINT sp = src.pitch(srcLevel), dp = pitch(dstLevel);
    for (UINT y = 0; y < rows; ++y) {
        const size_t so = comp ? (size_t)(r.top / 4 + y) * sp + (size_t)(r.left / 4) * fi.bytesPerBlock
                               : (size_t)(r.top + y) * sp + (size_t)r.left * fi.bytesPerBlock;
        const size_t doff = comp ? (size_t)(dy / 4 + y) * dp + (size_t)(dx / 4) * fi.bytesPerBlock
                                 : (size_t)(dy + y) * dp + (size_t)dx * fi.bytesPerBlock;
        if (so + rowBytes <= si.shadow.size() && doff + rowBytes <= di.shadow.size())
            memcpy(di.shadow.data() + doff, si.shadow.data() + so, rowBytes);
    }
    if (!di.dirty) {
        di.dx0 = dx, di.dy0 = dy, di.dz0 = 0, di.dx1 = dx + w, di.dy1 = dy + h, di.dz1 = 1;
    } else {
        di.dx0 = std::min(di.dx0, dx), di.dy0 = std::min(di.dy0, dy), di.dz0 = 0;
        di.dx1 = std::max(di.dx1, dx + w), di.dy1 = std::max(di.dy1, dy + h), di.dz1 = std::max(di.dz1, 1u);
    }
    di.dirty = true;
    if (!cpuOnly)
        anyDirty = true;
}

TexStorage *textureStorage(IDirect3DBaseTexture9 *t)
{
    if (!t)
        return nullptr;
    switch (t->GetType()) {
    case D3DRTYPE_TEXTURE: return &static_cast<Texture *>(static_cast<IDirect3DTexture9 *>(t))->storage();
    case D3DRTYPE_CUBETEXTURE: return &static_cast<CubeTexture *>(static_cast<IDirect3DCubeTexture9 *>(t))->storage();
    case D3DRTYPE_VOLUMETEXTURE:
        return &static_cast<VolumeTexture *>(static_cast<IDirect3DVolumeTexture9 *>(t))->storage();
    default: return nullptr;
    }
}

static void fillSurfaceDesc(const TexStorage &s, UINT level, D3DSURFACE_DESC *d)
{
    d->Format = s.format;
    d->Type = D3DRTYPE_SURFACE;
    d->Usage = s.usage;
    d->Pool = s.pool;
    d->MultiSampleType = D3DMULTISAMPLE_NONE;
    d->MultiSampleQuality = 0;
    d->Width = s.levelWidth(level);
    d->Height = s.levelHeight(level);
}

static D3DBOX rectToBox(const RECT *r)
{
    D3DBOX b = {(UINT)r->left, (UINT)r->top, (UINT)r->right, (UINT)r->bottom, 0, 1};
    return b;
}

// ------------------------------------------------------------------------------------------------ Texture

Texture::Texture(Device *dev) : TextureBase<IDirect3DTexture9>(dev) {}

Texture::~Texture()
{
    for (Surface *s : m_surfaces)
        delete s;
}

Surface *Texture::surface(UINT level)
{
    if (level >= m_storage.levels)
        return nullptr;
    if (m_surfaces.empty())
        m_surfaces.resize(m_storage.levels, nullptr);
    if (!m_surfaces[level])
        m_surfaces[level] = new Surface(m_device, static_cast<IDirect3DTexture9 *>(this), &m_storage, 0, level);
    return m_surfaces[level];
}

HRESULT Texture::GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc)
{
    if (Level >= m_storage.levels || !pDesc)
        return D3DERR_INVALIDCALL;
    fillSurfaceDesc(m_storage, Level, pDesc);
    return D3D_OK;
}

HRESULT Texture::GetSurfaceLevel(UINT Level, IDirect3DSurface9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    Surface *s = surface(Level);
    if (!s)
        return D3DERR_INVALIDCALL;
    s->AddRef();
    *pp = s;
    return D3D_OK;
}

HRESULT Texture::LockRect(UINT Level, D3DLOCKED_RECT *lr, const RECT *pRect, DWORD Flags)
{
    if (!lr)
        return D3DERR_INVALIDCALL;
    D3DBOX box;
    if (pRect)
        box = rectToBox(pRect);
    return m_storage.lock(0, Level, pRect ? &box : nullptr, Flags, &lr->pBits, &lr->Pitch, nullptr);
}

HRESULT Texture::UnlockRect(UINT Level) { return m_storage.unlock(0, Level); }

HRESULT Texture::AddDirtyRect(const RECT *)
{
    std::lock_guard<std::mutex> g(m_storage.mtx);
    TexStorage::Image &im = m_storage.image(0, 0);
    if (!im.shadow.empty()) {
        im.dx0 = im.dy0 = im.dz0 = 0;
        im.dx1 = m_storage.width, im.dy1 = m_storage.height, im.dz1 = 1;
        im.dirty = true;
        m_storage.anyDirty = true;
    }
    return D3D_OK;
}

// ------------------------------------------------------------------------------------------------ CubeTexture

CubeTexture::CubeTexture(Device *dev) : TextureBase<IDirect3DCubeTexture9>(dev) {}

CubeTexture::~CubeTexture()
{
    for (Surface *s : m_surfaces)
        delete s;
}

Surface *CubeTexture::surface(UINT face, UINT level)
{
    if (face >= 6 || level >= m_storage.levels)
        return nullptr;
    if (m_surfaces.empty())
        m_surfaces.resize(6 * m_storage.levels, nullptr);
    Surface *&s = m_surfaces[face * m_storage.levels + level];
    if (!s)
        s = new Surface(m_device, static_cast<IDirect3DCubeTexture9 *>(this), &m_storage, face, level);
    return s;
}

HRESULT CubeTexture::GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc)
{
    if (Level >= m_storage.levels || !pDesc)
        return D3DERR_INVALIDCALL;
    fillSurfaceDesc(m_storage, Level, pDesc);
    return D3D_OK;
}

HRESULT CubeTexture::GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    Surface *s = surface((UINT)FaceType, Level);
    if (!s)
        return D3DERR_INVALIDCALL;
    s->AddRef();
    *pp = s;
    return D3D_OK;
}

HRESULT CubeTexture::LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *lr, const RECT *pRect,
                              DWORD Flags)
{
    if (!lr)
        return D3DERR_INVALIDCALL;
    D3DBOX box;
    if (pRect)
        box = rectToBox(pRect);
    return m_storage.lock((UINT)FaceType, Level, pRect ? &box : nullptr, Flags, &lr->pBits, &lr->Pitch, nullptr);
}

HRESULT CubeTexture::UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level)
{
    return m_storage.unlock((UINT)FaceType, Level);
}

HRESULT CubeTexture::AddDirtyRect(D3DCUBEMAP_FACES, const RECT *) { return D3D_OK; }

// ------------------------------------------------------------------------------------------------ VolumeTexture

VolumeTexture::VolumeTexture(Device *dev) : TextureBase<IDirect3DVolumeTexture9>(dev) {}

VolumeTexture::~VolumeTexture()
{
    for (Volume *v : m_volumes)
        if (v)
            v->destroy();
}

HRESULT VolumeTexture::GetLevelDesc(UINT Level, D3DVOLUME_DESC *d)
{
    if (Level >= m_storage.levels || !d)
        return D3DERR_INVALIDCALL;
    d->Format = m_storage.format;
    d->Type = D3DRTYPE_VOLUME;
    d->Usage = m_storage.usage;
    d->Pool = m_storage.pool;
    d->Width = m_storage.levelWidth(Level);
    d->Height = m_storage.levelHeight(Level);
    d->Depth = m_storage.levelDepth(Level);
    return D3D_OK;
}

HRESULT VolumeTexture::GetVolumeLevel(UINT Level, IDirect3DVolume9 **pp)
{
    if (!pp || Level >= m_storage.levels)
        return D3DERR_INVALIDCALL;
    if (m_volumes.empty())
        m_volumes.resize(m_storage.levels, nullptr);
    if (!m_volumes[Level])
        m_volumes[Level] = new Volume(m_device, this, &m_storage, Level);
    m_volumes[Level]->AddRef();
    *pp = m_volumes[Level];
    return D3D_OK;
}

HRESULT VolumeTexture::LockBox(UINT Level, D3DLOCKED_BOX *lb, const D3DBOX *pBox, DWORD Flags)
{
    if (!lb)
        return D3DERR_INVALIDCALL;
    return m_storage.lock(0, Level, pBox, Flags, &lb->pBits, &lb->RowPitch, &lb->SlicePitch);
}

HRESULT VolumeTexture::UnlockBox(UINT Level) { return m_storage.unlock(0, Level); }

HRESULT VolumeTexture::AddDirtyBox(const D3DBOX *) { return D3D_OK; }

// ------------------------------------------------------------------------------------------------ Volume

Volume::Volume(Device *dev, VolumeTexture *container, TexStorage *storage, UINT level)
    : m_device(dev), m_container(container), m_storage(storage), m_level(level)
{
    m_refs = 0;
}

ULONG Volume::AddRef() { return m_container->AddRef(); }
ULONG Volume::Release() { return m_container->Release(); }

HRESULT Volume::GetDevice(IDirect3DDevice9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_device;
    m_device->AddRef();
    return D3D_OK;
}

HRESULT Volume::GetContainer(REFIID riid, void **pp) { return m_container->QueryInterface(riid, pp); }

HRESULT Volume::GetDesc(D3DVOLUME_DESC *d) { return m_container->GetLevelDesc(m_level, d); }

HRESULT Volume::LockBox(D3DLOCKED_BOX *lb, const D3DBOX *pBox, DWORD Flags)
{
    return m_container->LockBox(m_level, lb, pBox, Flags);
}

HRESULT Volume::UnlockBox() { return m_container->UnlockBox(m_level); }

// ------------------------------------------------------------------------------------------------ Surface

Surface::Surface(Device *dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, DWORD usage, bool lockable,
                 bool deviceInternal)
    : Resource(dev, !deviceInternal), m_kind(kind), m_lockable(lockable), m_deviceInternal(deviceInternal)
{
    m_desc.Format = fmt;
    m_desc.Type = D3DRTYPE_SURFACE;
    m_desc.Usage = usage;
    m_desc.Pool = pool;
    m_desc.MultiSampleType = D3DMULTISAMPLE_NONE;
    m_desc.MultiSampleQuality = 0;
    m_desc.Width = w;
    m_desc.Height = h;
    if (kind == DepthStencil) {
        m_fi = resolveFormat(fmt, currentGLCaps());
    } else {
        m_storage = new TexStorage();
        m_ownStorage = true;
        m_storage->init(D3DRTYPE_TEXTURE, w, h, 1, 1, usage, fmt, pool);
        if (kind == OffscreenPlain && !m_storage->cpuOnly)
            m_storage->renderTarget = false; // DEFAULT-pool offscreen plain: lockable and a StretchRect source
        m_fi = m_storage->fi;
    }
}

Surface::Surface(Device *dev, IUnknown *container, TexStorage *storage, UINT face, UINT level, Kind kind)
    : Resource(dev, false), m_kind(kind), m_container(container), m_storage(storage), m_face(face), m_level(level)
{
    m_refs = 0;
    fillSurfaceDesc(*storage, level, &m_desc);
    m_fi = storage->fi;
    m_lockable = !storage->renderTarget;
}

Surface::~Surface()
{
    if (m_device)
        m_device->onSurfaceDestroyed(this);
    if (m_rb) {
        if (onDeviceThread())
            glDeleteRenderbuffers(1, &m_rb);
        else
            m_device->queueGLDelete(GL_RENDERBUFFER, m_rb);
        m_rb = 0;
    }
    if (m_ownStorage) {
        m_storage->destroyGL(m_device);
        delete m_storage;
    }
}

ULONG Surface::AddRef()
{
    if (m_container)
        return m_container->AddRef();
    return Resource::AddRef();
}

ULONG Surface::Release()
{
    if (m_container)
        return m_container->Release();
    return Resource::Release();
}

void Surface::finalRelease()
{
    if (m_deviceInternal)
        return; // the device destroys it
    Resource::finalRelease();
}

HRESULT Surface::GetContainer(REFIID riid, void **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    if (m_container)
        return m_container->QueryInterface(riid, pp);
    return m_device->QueryInterface(riid, pp);
}

HRESULT Surface::GetDesc(D3DSURFACE_DESC *pDesc)
{
    if (!pDesc)
        return D3DERR_INVALIDCALL;
    *pDesc = m_desc;
    return D3D_OK;
}

HRESULT Surface::LockRect(D3DLOCKED_RECT *lr, const RECT *pRect, DWORD Flags)
{
    if (!lr)
        return D3DERR_INVALIDCALL;
    if (!m_storage) {
        D3D9_WARN_ONCE("LockRect on a depth-stencil surface is not supported");
        return D3DERR_INVALIDCALL;
    }
    D3DBOX box;
    if (pRect)
        box = rectToBox(pRect);
    return m_storage->lock(m_face, m_level, pRect ? &box : nullptr, Flags, &lr->pBits, &lr->Pitch, nullptr);
}

HRESULT Surface::UnlockRect()
{
    if (!m_storage)
        return D3DERR_INVALIDCALL;
    return m_storage->unlock(m_face, m_level);
}

HRESULT Surface::GetDC(struct HDC__ **) { return D3DERR_INVALIDCALL; }
HRESULT Surface::ReleaseDC(struct HDC__ *) { return D3DERR_INVALIDCALL; }

void Surface::ensureGL()
{
    if (m_storage) {
        m_storage->ensureGL(m_device);
        m_storage->flushUploads(m_device);
        return;
    }
    if (!m_rb && (m_fi.flags & FMT_DEPTH)) {
        glGenRenderbuffers(1, &m_rb);
        glBindRenderbuffer(GL_RENDERBUFFER, m_rb);
        glRenderbufferStorage(GL_RENDERBUFFER, m_fi.internalFormat, (GLsizei)m_desc.Width, (GLsizei)m_desc.Height);
        m_device->cache().renderbuffer = m_rb;
    }
}

void Surface::attach(GLenum attachmentPoint)
{
    ensureGL();
    if (m_storage) {
        if (m_storage->target == GL_TEXTURE_3D)
            glFramebufferTextureLayer(GL_DRAW_FRAMEBUFFER, attachmentPoint, m_storage->tex, (GLint)m_level,
                                      (GLint)m_face);
        else
            glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, attachmentPoint, m_storage->faceTarget(m_face),
                                   m_storage->tex, (GLint)m_level);
    } else {
        glFramebufferRenderbuffer(GL_DRAW_FRAMEBUFFER, attachmentPoint, GL_RENDERBUFFER, m_rb);
    }
}

void Surface::resize(UINT w, UINT h)
{
    m_device->purgeFbos(this);
    m_desc.Width = w;
    m_desc.Height = h;
    if (m_storage) {
        m_storage->destroyGL(m_device);
        m_storage->init(D3DRTYPE_TEXTURE, w, h, 1, 1, m_desc.Usage, m_desc.Format, m_desc.Pool);
    }
    if (m_rb) {
        glDeleteRenderbuffers(1, &m_rb);
        m_rb = 0;
        m_device->cache().renderbuffer = ~0u;
    }
}

// ------------------------------------------------------------------------------------------------ buffers

HRESULT BufferStorage::lock(UINT offset, UINT len, void **pp, DWORD flags)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    if (offset > size)
        return D3DERR_INVALIDCALL;
    if (len == 0)
        len = size - offset;
    if (offset + len > size)
        len = size - offset;
    std::lock_guard<std::mutex> g(mtx);
    *pp = shadow.data() + offset;
    if (!(flags & D3DLOCK_READONLY) && len) {
        if (lockEnd <= lockBegin) {
            lockBegin = offset;
            lockEnd = offset + len;
        } else {
            lockBegin = std::min(lockBegin, offset);
            lockEnd = std::max(lockEnd, offset + len);
        }
    }
    ++lockCount;
    return D3D_OK;
}

HRESULT BufferStorage::unlock()
{
    std::lock_guard<std::mutex> g(mtx);
    if (lockCount <= 0)
        return D3DERR_INVALIDCALL;
    if (--lockCount == 0 && lockEnd > lockBegin) {
        if (dirtyEnd <= dirtyBegin) {
            dirtyBegin = lockBegin;
            dirtyEnd = lockEnd;
        } else {
            dirtyBegin = std::min(dirtyBegin, lockBegin);
            dirtyEnd = std::max(dirtyEnd, lockEnd);
        }
        lockBegin = lockEnd = 0;
        dirty = true;
    }
    return D3D_OK;
}

void BufferStorage::flush(Device *dev)
{
    if (!dirty.load() && glAllocated)
        return;
    std::lock_guard<std::mutex> g(mtx);
    if (target == GL_ARRAY_BUFFER)
        dev->bindArrayBuffer(buf ? buf : (glGenBuffers(1, &buf), buf));
    else
        dev->bindElementBuffer(buf ? buf : (glGenBuffers(1, &buf), buf));
    if (!glAllocated) {
        glBufferData(target, (GLsizeiptr)size, shadow.data(),
                     (usage & D3DUSAGE_DYNAMIC) ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
        glAllocated = true;
        g_stats.bufferUploadBytes += size;
    } else if (dirtyEnd > dirtyBegin) {
        glBufferSubData(target, (GLintptr)dirtyBegin, (GLsizeiptr)(dirtyEnd - dirtyBegin), shadow.data() + dirtyBegin);
        g_stats.bufferUploadBytes += dirtyEnd - dirtyBegin;
    }
    dirtyBegin = dirtyEnd = 0;
    dirty = false;
}

void BufferStorage::destroyGL(Device *dev)
{
    if (buf) {
        if (onDeviceThread()) {
            glDeleteBuffers(1, &buf);
            GLStateCache &c = dev->cache();
            if (c.arrayBuffer == buf)
                c.arrayBuffer = ~0u;
            if (c.elementBuffer == buf)
                c.elementBuffer = ~0u;
            for (auto &a : c.attribs)
                if (a.buffer == buf)
                    a.valid = false;
        } else {
            dev->queueGLDelete(GL_ARRAY_BUFFER, buf);
        }
        buf = 0;
    }
    glAllocated = false;
}

VertexBuffer::VertexBuffer(Device *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool) : Resource(dev), m_fvf(fvf)
{
    m_buf.size = length;
    m_buf.usage = usage;
    m_buf.pool = pool;
    m_buf.target = GL_ARRAY_BUFFER;
    m_buf.shadow.assign(length, 0);
}

VertexBuffer::~VertexBuffer()
{
    m_device->onVertexBufferDestroyed(this);
    m_buf.destroyGL(m_device);
}

void VertexBuffer::PreLoad()
{
    if (onDeviceThread())
        m_buf.flush(m_device);
}

HRESULT VertexBuffer::GetDesc(D3DVERTEXBUFFER_DESC *d)
{
    if (!d)
        return D3DERR_INVALIDCALL;
    d->Format = D3DFMT_VERTEXDATA;
    d->Type = D3DRTYPE_VERTEXBUFFER;
    d->Usage = m_buf.usage;
    d->Pool = m_buf.pool;
    d->Size = m_buf.size;
    d->FVF = m_fvf;
    return D3D_OK;
}

IndexBuffer::IndexBuffer(Device *dev, UINT length, DWORD usage, D3DFORMAT fmt, D3DPOOL pool)
    : Resource(dev), m_format(fmt)
{
    m_buf.size = length;
    m_buf.usage = usage;
    m_buf.pool = pool;
    m_buf.target = GL_ELEMENT_ARRAY_BUFFER;
    m_buf.shadow.assign(length, 0);
}

IndexBuffer::~IndexBuffer()
{
    m_device->onIndexBufferDestroyed(this);
    m_buf.destroyGL(m_device);
}

void IndexBuffer::PreLoad()
{
    if (onDeviceThread())
        m_buf.flush(m_device);
}

HRESULT IndexBuffer::GetDesc(D3DINDEXBUFFER_DESC *d)
{
    if (!d)
        return D3DERR_INVALIDCALL;
    d->Format = m_format;
    d->Type = D3DRTYPE_INDEXBUFFER;
    d->Usage = m_buf.usage;
    d->Pool = m_buf.pool;
    d->Size = m_buf.size;
    return D3D_OK;
}

// ------------------------------------------------------------------------------------------------ VertexDecl

VertexDecl::VertexDecl(Device *dev, const D3DVERTEXELEMENT9 *elements, UINT count)
    : Resource(dev), m_elements(elements, elements + count)
{
}

VertexDecl::~VertexDecl() { m_device->onDeclDestroyed(this); }

HRESULT VertexDecl::GetDeclaration(D3DVERTEXELEMENT9 *pElement, UINT *pNumElements)
{
    if (!pNumElements)
        return D3DERR_INVALIDCALL;
    const UINT n = (UINT)m_elements.size() + 1;
    if (pElement) {
        memcpy(pElement, m_elements.data(), m_elements.size() * sizeof(D3DVERTEXELEMENT9));
        D3DVERTEXELEMENT9 end = D3DDECL_END();
        pElement[n - 1] = end;
    }
    *pNumElements = n;
    return D3D_OK;
}

} // namespace d3d9shim
