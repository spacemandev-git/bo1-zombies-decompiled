// d3d9_internal.h - shared declarations of the D3D9 -> WebGL2 shim implementation.
//
// Object model: every D3D9 interface is implemented by exactly one class deriving (single inheritance) from
// Unknown<Interface>, so the interface's vtable is the object's primary vtable (IUnknown slots 0..2 as in COM).
// Downcasts from interface pointers use static_cast (no RTTI needed).
//
// Threading: GL is only touched on the device thread. Lock/Unlock record CPU-side changes under a per-resource
// mutex; uploads happen at the next use on the device thread. Final Release from another thread queues the
// destruction to the device thread.
#pragma once

#include <d3d9.h>
#include <d3d9shim.h>

#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>

#include <atomic>
#include <string.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "d3d9_formats.h"
#include "d3d9_shader_translate.h"
#include "d3d9_states.h"
#include "d3d9_vertexdecl.h"

// Resource<I> deliberately implements IDirect3DResource9 methods without `override` (it is also used for interfaces
// that do not have them); the concrete classes forward explicitly.
#pragma clang diagnostic ignored "-Winconsistent-missing-override"

namespace d3d9shim {

// ------------------------------------------------------------------------------------------------ logging
extern int g_logLevel;
extern bool g_trace;
extern bool g_glCheck; // D3D9SHIM_GLCHECK=1 / d3d9shim_set_gl_check: glGetError after every draw/clear/copy
void checkGLError(const char *where);
#define D3D9_GLCHECK(where)                \
    do {                                   \
        if (::d3d9shim::g_glCheck)         \
            ::d3d9shim::checkGLError(where); \
    } while (0)
void logf(int level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void tracef(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void initLogFromEnv();

#define D3D9_ERROR(...) ::d3d9shim::logf(0, __VA_ARGS__)
#define D3D9_WARN(...) ::d3d9shim::logf(1, __VA_ARGS__)
#define D3D9_INFO(...) ::d3d9shim::logf(2, __VA_ARGS__)
#define D3D9_DEBUG(...) ::d3d9shim::logf(3, __VA_ARGS__)
#define D3D9_WARN_ONCE(...)              \
    do {                                 \
        static bool d3d9_once_ = false;  \
        if (!d3d9_once_) {               \
            d3d9_once_ = true;           \
            ::d3d9shim::logf(1, __VA_ARGS__); \
        }                                \
    } while (0)
#define D3D9_TRACE(...)                \
    do {                               \
        if (::d3d9shim::g_trace)       \
            ::d3d9shim::tracef(__VA_ARGS__); \
    } while (0)

struct Stats {
    unsigned frame = 0, draws = 0, drawsThisFrame = 0, programsLinked = 0, programsFailed = 0, shadersFailed = 0;
    unsigned long long textureUploadBytes = 0, bufferUploadBytes = 0;
};
extern Stats g_stats;
extern unsigned g_occlusionVisibleCount;
extern int g_displayWidth, g_displayHeight;
extern std::string g_canvasSelector;

// Shader dump (d3d9_log.cpp). kind: "vs"/"ps"; what: "bin" bytecode, "glsl", "log".
bool shaderDumpEnabled(bool failure);
uint64_t shaderHash(const DWORD *tokens, size_t bytes);
void shaderDump(const char *kind, uint64_t hash, const char *ext, const void *data, size_t size);

// Capabilities of the WebGL2 implementation: from the live context once a device exists, else from a throwaway
// probe context (OffscreenCanvas) or defaults.
const GLCaps &currentGLCaps();
void setLiveGLCaps(const GLCaps &caps);
void fillD3DCaps(D3DCAPS9 *caps, const GLCaps &gl);
HRESULT checkDeviceFormat(const GLCaps &gl, DWORD usage, D3DRESOURCETYPE rtype, D3DFORMAT fmt);

bool onDeviceThread();
uint64_t nextObjectId();

class Device;
class Surface;

// ------------------------------------------------------------------------------------------------ COM base
template <class I> class Unknown : public I {
public:
    HRESULT QueryInterface(REFIID riid, void **ppv) override
    {
        if (!ppv)
            return E_POINTER;
        // Only IUnknown and the object's own interface (we never hand out another one).
        static const GUID kIUnknown = {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
        if (memcmp(&riid, &kIUnknown, sizeof(GUID)) == 0 || matchesIID(riid)) {
            *ppv = static_cast<I *>(this);
            this->AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG AddRef() override { return ++m_refs; }
    ULONG Release() override
    {
        ULONG r = --m_refs;
        if (r == 0)
            finalRelease();
        return r;
    }
    ULONG refs() const { return m_refs.load(); }

protected:
    virtual ~Unknown() {}
    virtual void finalRelease() { delete this; }
    virtual bool matchesIID(REFIID) { return false; }
    std::atomic<ULONG> m_refs{1};
};

// ------------------------------------------------------------------------------------------------ GL state cache
struct VertexAttribState {
    bool enabled = false;
    GLuint buffer = 0;
    GLint size = 0;
    GLenum type = 0;
    GLboolean normalized = 0;
    GLsizei stride = 0;
    uintptr_t offset = 0;
    bool valid = false;
};

void setCap(GLenum cap, signed char &cached, bool on); // glEnable/glDisable through the cache (d3d9_draw.cpp)

struct GLStateCache {
    GLuint program = ~0u, arrayBuffer = ~0u, elementBuffer = ~0u, drawFbo = ~0u, readFbo = ~0u, renderbuffer = ~0u;
    GLenum activeTexture = 0;
    GLuint tex2D[32], texCube[32], tex3D[32], sampler[32];
    signed char blend = -1, depthTest = -1, stencilTest = -1, cull = -1, scissor = -1, polygonOffset = -1;
    GLenum blendSrcRGB = 0, blendDstRGB = 0, blendSrcA = 0, blendDstA = 0, blendEqRGB = 0, blendEqA = 0;
    float blendColor[4] = {-1, -1, -1, -1};
    GLenum depthFunc = 0;
    signed char depthMask = -1;
    signed char colorMask[4] = {-1, -1, -1, -1};
    GLenum cullFace = 0;
    GLenum stencilFunc[2] = {0, 0};
    GLint stencilRef[2] = {-1, -1};
    GLuint stencilValueMask[2] = {0, 0}, stencilWriteMask[2] = {1, 1};
    GLenum stencilFail[2] = {0, 0}, stencilZFail[2] = {0, 0}, stencilPass[2] = {0, 0};
    GLint viewport[4] = {-1, -1, -1, -1};
    GLint scissorBox[4] = {-1, -1, -1, -1};
    float depthRange[2] = {-1, -1};
    float polyFactor = -1e30f, polyUnits = -1e30f;
    VertexAttribState attribs[16];
    GLStateCache() { invalidate(); }
    void invalidate();
};

// ------------------------------------------------------------------------------------------------ resources
template <class I> class Resource : public Unknown<I> {
public:
    // IDirect3DResource9-style helpers (IDirect3DVertexDeclaration9 etc. only use GetDevice)
    HRESULT GetDevice(IDirect3DDevice9 **pp);
    HRESULT SetPrivateData(REFGUID g, const void *data, DWORD size, DWORD flags);
    HRESULT GetPrivateData(REFGUID g, void *data, DWORD *size);
    HRESULT FreePrivateData(REFGUID g);
    DWORD SetPriority(DWORD p)
    {
        DWORD o = m_priority;
        m_priority = p;
        return o;
    }
    DWORD GetPriority() { return m_priority; }
    Device *device() const { return m_device; }
    uint64_t id() const { return m_id; }

protected:
    explicit Resource(Device *dev, bool holdDeviceRef = true);
    ~Resource() override;
    void finalRelease() override; // defers to the device thread if needed
    Device *m_device;
    bool m_holdsDeviceRef;
    uint64_t m_id;
    DWORD m_priority = 0;
    std::vector<std::pair<GUID, std::vector<uint8_t>>> m_private;
};

// Texture image storage shared by 2D/cube/volume textures and standalone color surfaces.
struct TexStorage {
    D3DRESOURCETYPE type = D3DRTYPE_TEXTURE; // TEXTURE, CUBETEXTURE, VOLUMETEXTURE (standalone surfaces: TEXTURE)
    GLenum target = GL_TEXTURE_2D;
    UINT width = 0, height = 0, depth = 1, levels = 1, faces = 1;
    D3DFORMAT format = D3DFMT_UNKNOWN;
    FormatInfo fi;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_MANAGED;
    bool cpuOnly = false;      // SYSTEMMEM / SCRATCH: never uploaded
    bool renderTarget = false; // RENDERTARGET/DEPTHSTENCIL usage: GPU-only, not lockable
    bool keepShadow = true;    // keep CPU copies after upload (dynamic, uncompressed, lockable data)
    GLuint tex = 0;
    bool glAllocated = false;
    DWORD lod = 0, lodApplied = 0;
    D3DTEXTUREFILTERTYPE autoGenFilter = D3DTEXF_LINEAR;

    struct Image {
        std::vector<uint8_t> shadow; // D3D layout, tight pitch
        bool dirty = false;
        UINT dx0 = 0, dy0 = 0, dz0 = 0, dx1 = 0, dy1 = 0, dz1 = 0; // dirty box (pixels), exclusive end
        UINT lx0 = 0, ly0 = 0, lz0 = 0, lx1 = 0, ly1 = 0, lz1 = 0; // pending lock box
        int lockCount = 0;
        bool lockWrites = false;
        bool contentsLost = false;
    };
    std::vector<Image> images; // faces * levels, index face*levels+level
    std::mutex mtx;
    std::atomic<bool> anyDirty{false};

    void init(D3DRESOURCETYPE type, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    Image &image(UINT face, UINT level) { return images[face * levels + level]; }
    UINT levelWidth(UINT l) const { return mipDim(width, l); }
    UINT levelHeight(UINT l) const { return mipDim(height, l); }
    UINT levelDepth(UINT l) const { return type == D3DRTYPE_VOLUMETEXTURE ? mipDim(depth, l) : 1; }
    UINT pitch(UINT l) const { return formatPitch(fi, levelWidth(l)); }
    UINT slicePitch(UINT l) const { return pitch(l) * formatRows(fi, levelHeight(l)); }
    size_t levelBytes(UINT l) const { return (size_t)slicePitch(l) * levelDepth(l); }

    HRESULT lock(UINT face, UINT level, const D3DBOX *box, DWORD flags, void **bits, INT *pitch, INT *slicePitch);
    HRESULT unlock(UINT face, UINT level);
    // Device thread only:
    void ensureGL(Device *dev);
    void flushUploads(Device *dev);
    GLenum faceTarget(UINT face) const
    {
        return type == D3DRTYPE_CUBETEXTURE ? (GLenum)(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face) : target;
    }
    void destroyGL(Device *dev);
    // Copies CPU data of another storage (UpdateTexture/UpdateSurface).
    void copyImageFrom(TexStorage &src, UINT srcFace, UINT srcLevel, UINT dstFace, UINT dstLevel, const RECT *srcRect,
                       const POINT *dstPoint);
};

template <class I> class TextureBase : public Resource<I> {
public:
    DWORD SetLOD(DWORD lod) override;
    DWORD GetLOD() override { return m_storage.lod; }
    DWORD GetLevelCount() override { return m_storage.levels; }
    HRESULT SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) override
    {
        m_storage.autoGenFilter = f;
        return D3D_OK;
    }
    D3DTEXTUREFILTERTYPE GetAutoGenFilterType() override { return m_storage.autoGenFilter; }
    void GenerateMipSubLevels() override;
    void PreLoad() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource<I>::GetDevice(pp); }
    HRESULT SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override
    {
        return Resource<I>::SetPrivateData(g, d, s, f);
    }
    HRESULT GetPrivateData(REFGUID g, void *d, DWORD *s) override { return Resource<I>::GetPrivateData(g, d, s); }
    HRESULT FreePrivateData(REFGUID g) override { return Resource<I>::FreePrivateData(g); }
    DWORD SetPriority(DWORD p) override { return Resource<I>::SetPriority(p); }
    DWORD GetPriority() override { return Resource<I>::GetPriority(); }
    TexStorage &storage() { return m_storage; }

protected:
    using Resource<I>::Resource;
    ~TextureBase() override;
    TexStorage m_storage;
};

class Texture final : public TextureBase<IDirect3DTexture9> {
public:
    Texture(Device *dev);
    ~Texture() override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_TEXTURE; }
    HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT GetSurfaceLevel(UINT Level, IDirect3DSurface9 **ppSurfaceLevel) override;
    HRESULT LockRect(UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT UnlockRect(UINT Level) override;
    HRESULT AddDirtyRect(const RECT *pDirtyRect) override;
    Surface *surface(UINT level);

private:
    std::vector<Surface *> m_surfaces;
};

class CubeTexture final : public TextureBase<IDirect3DCubeTexture9> {
public:
    CubeTexture(Device *dev);
    ~CubeTexture() override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }
    HRESULT GetLevelDesc(UINT Level, D3DSURFACE_DESC *pDesc) override;
    HRESULT GetCubeMapSurface(D3DCUBEMAP_FACES FaceType, UINT Level, IDirect3DSurface9 **pp) override;
    HRESULT LockRect(D3DCUBEMAP_FACES FaceType, UINT Level, D3DLOCKED_RECT *pLockedRect, const RECT *pRect,
                     DWORD Flags) override;
    HRESULT UnlockRect(D3DCUBEMAP_FACES FaceType, UINT Level) override;
    HRESULT AddDirtyRect(D3DCUBEMAP_FACES FaceType, const RECT *pDirtyRect) override;
    Surface *surface(UINT face, UINT level);

private:
    std::vector<Surface *> m_surfaces;
};

class Volume;
class VolumeTexture final : public TextureBase<IDirect3DVolumeTexture9> {
public:
    VolumeTexture(Device *dev);
    ~VolumeTexture() override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_VOLUMETEXTURE; }
    HRESULT GetLevelDesc(UINT Level, D3DVOLUME_DESC *pDesc) override;
    HRESULT GetVolumeLevel(UINT Level, IDirect3DVolume9 **ppVolumeLevel) override;
    HRESULT LockBox(UINT Level, D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) override;
    HRESULT UnlockBox(UINT Level) override;
    HRESULT AddDirtyBox(const D3DBOX *pDirtyBox) override;

private:
    std::vector<Volume *> m_volumes;
};

// Returns the storage behind any IDirect3DBaseTexture9 created by this shim.
TexStorage *textureStorage(IDirect3DBaseTexture9 *t);

class Surface final : public Resource<IDirect3DSurface9> {
public:
    enum Kind { TextureLevel, RenderTarget, DepthStencil, OffscreenPlain, BackBuffer };
    // Standalone surface (owns its storage / renderbuffer). deviceInternal: owned by the device (auto depth
    // stencil), holds no device reference and is destroyed by the device, not by Release.
    Surface(Device *dev, Kind kind, UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool, DWORD usage, bool lockable,
            bool deviceInternal = false);
    // Sub-surface of a texture or swap chain (refcount forwards to the container).
    Surface(Device *dev, IUnknown *container, TexStorage *storage, UINT face, UINT level, Kind kind = TextureLevel);
    ~Surface() override;

    ULONG AddRef() override;
    ULONG Release() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    HRESULT SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override
    {
        return Resource::SetPrivateData(g, d, s, f);
    }
    HRESULT GetPrivateData(REFGUID g, void *d, DWORD *s) override { return Resource::GetPrivateData(g, d, s); }
    HRESULT FreePrivateData(REFGUID g) override { return Resource::FreePrivateData(g); }
    DWORD SetPriority(DWORD p) override { return Resource::SetPriority(p); }
    DWORD GetPriority() override { return Resource::GetPriority(); }
    void PreLoad() override {}
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT GetContainer(REFIID riid, void **ppContainer) override;
    HRESULT GetDesc(D3DSURFACE_DESC *pDesc) override;
    HRESULT LockRect(D3DLOCKED_RECT *pLockedRect, const RECT *pRect, DWORD Flags) override;
    HRESULT UnlockRect() override;
    HRESULT GetDC(struct HDC__ **phdc) override;
    HRESULT ReleaseDC(struct HDC__ *hdc) override;

    Kind kind() const { return m_kind; }
    bool isDepth() const { return (m_storage ? m_storage->fi.flags : m_fi.flags) & FMT_DEPTH; }
    const FormatInfo &formatInfo() const { return m_storage ? m_storage->fi : m_fi; }
    D3DFORMAT format() const { return m_desc.Format; }
    UINT width() const { return m_desc.Width; }
    UINT height() const { return m_desc.Height; }
    TexStorage *storage() { return m_storage; }
    UINT face() const { return m_face; }
    UINT level() const { return m_level; }
    bool lockable() const { return m_lockable; }
    // Device thread: make sure GL storage exists (and uploads are flushed); attach to the bound FBO.
    void ensureGL();
    void attach(GLenum attachmentPoint);
    GLuint renderbuffer() const { return m_rb; }
    bool hasGLTexture() const { return m_storage != nullptr && !m_storage->cpuOnly; }
    // Back buffer / auto depth resize (Reset).
    void resize(UINT w, UINT h);
    void destroyInternal() { delete this; }

private:
    void finalRelease() override;
    Kind m_kind;
    IUnknown *m_container = nullptr;
    TexStorage *m_storage = nullptr; // owned when m_ownStorage
    bool m_ownStorage = false;
    UINT m_face = 0, m_level = 0;
    D3DSURFACE_DESC m_desc;
    FormatInfo m_fi; // depth surfaces without storage
    GLuint m_rb = 0;
    bool m_lockable = false;
    bool m_deviceInternal = false;
};

class Volume final : public Unknown<IDirect3DVolume9> {
public:
    Volume(Device *dev, VolumeTexture *container, TexStorage *storage, UINT level);
    ULONG AddRef() override;
    ULONG Release() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override;
    HRESULT SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3D_OK; }
    HRESULT GetPrivateData(REFGUID, void *, DWORD *) override { return D3DERR_NOTFOUND; }
    HRESULT FreePrivateData(REFGUID) override { return D3D_OK; }
    HRESULT GetContainer(REFIID riid, void **ppContainer) override;
    HRESULT GetDesc(D3DVOLUME_DESC *pDesc) override;
    HRESULT LockBox(D3DLOCKED_BOX *pLockedVolume, const D3DBOX *pBox, DWORD Flags) override;
    HRESULT UnlockBox() override;
    void destroy() { delete this; }

private:
    Device *m_device;
    VolumeTexture *m_container;
    [[maybe_unused]] TexStorage *m_storage;
    UINT m_level;
};

// Vertex / index buffers: CPU shadow + GL buffer, dirty range uploaded at use.
struct BufferStorage {
    std::vector<uint8_t> shadow;
    GLuint buf = 0;
    GLenum target = GL_ARRAY_BUFFER;
    UINT size = 0;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    bool glAllocated = false;
    std::mutex mtx;
    UINT dirtyBegin = 0, dirtyEnd = 0;     // pending upload
    UINT lockBegin = 0, lockEnd = 0;       // union of outstanding write locks
    int lockCount = 0;
    std::atomic<bool> dirty{false};
    HRESULT lock(UINT offset, UINT size, void **pp, DWORD flags);
    HRESULT unlock();
    void flush(Device *dev); // device thread
    void destroyGL(Device *dev);
};

class VertexBuffer final : public Resource<IDirect3DVertexBuffer9> {
public:
    VertexBuffer(Device *dev, UINT length, DWORD usage, DWORD fvf, D3DPOOL pool);
    ~VertexBuffer() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    HRESULT SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override
    {
        return Resource::SetPrivateData(g, d, s, f);
    }
    HRESULT GetPrivateData(REFGUID g, void *d, DWORD *s) override { return Resource::GetPrivateData(g, d, s); }
    HRESULT FreePrivateData(REFGUID g) override { return Resource::FreePrivateData(g); }
    DWORD SetPriority(DWORD p) override { return Resource::SetPriority(p); }
    DWORD GetPriority() override { return Resource::GetPriority(); }
    void PreLoad() override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) override
    {
        return m_buf.lock(OffsetToLock, SizeToLock, ppbData, Flags);
    }
    HRESULT Unlock() override { return m_buf.unlock(); }
    HRESULT GetDesc(D3DVERTEXBUFFER_DESC *pDesc) override;
    BufferStorage &buffer() { return m_buf; }

private:
    BufferStorage m_buf;
    DWORD m_fvf;
};

class IndexBuffer final : public Resource<IDirect3DIndexBuffer9> {
public:
    IndexBuffer(Device *dev, UINT length, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
    ~IndexBuffer() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    HRESULT SetPrivateData(REFGUID g, const void *d, DWORD s, DWORD f) override
    {
        return Resource::SetPrivateData(g, d, s, f);
    }
    HRESULT GetPrivateData(REFGUID g, void *d, DWORD *s) override { return Resource::GetPrivateData(g, d, s); }
    HRESULT FreePrivateData(REFGUID g) override { return Resource::FreePrivateData(g); }
    DWORD SetPriority(DWORD p) override { return Resource::SetPriority(p); }
    DWORD GetPriority() override { return Resource::GetPriority(); }
    void PreLoad() override;
    D3DRESOURCETYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT Lock(UINT OffsetToLock, UINT SizeToLock, void **ppbData, DWORD Flags) override
    {
        return m_buf.lock(OffsetToLock, SizeToLock, ppbData, Flags);
    }
    HRESULT Unlock() override { return m_buf.unlock(); }
    HRESULT GetDesc(D3DINDEXBUFFER_DESC *pDesc) override;
    BufferStorage &buffer() { return m_buf; }
    D3DFORMAT format() const { return m_format; }

private:
    BufferStorage m_buf;
    D3DFORMAT m_format;
};

class VertexDecl final : public Resource<IDirect3DVertexDeclaration9> {
public:
    VertexDecl(Device *dev, const D3DVERTEXELEMENT9 *elements, UINT count);
    ~VertexDecl() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    HRESULT GetDeclaration(D3DVERTEXELEMENT9 *pElement, UINT *pNumElements) override;
    const D3DVERTEXELEMENT9 *elements() const { return m_elements.data(); }
    UINT count() const { return (UINT)m_elements.size(); }

private:
    std::vector<D3DVERTEXELEMENT9> m_elements;
};

class VertexShader final : public Resource<IDirect3DVertexShader9> {
public:
    VertexShader(Device *dev, const DWORD *function, TranslatedShader &&base);
    ~VertexShader() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    HRESULT GetFunction(void *pData, UINT *pSizeOfData) override;
    const TranslatedShader &base() const { return m_base; }
    const std::vector<DWORD> &bytecode() const { return m_bytecode; }
    // Compiled GL shader for a variant (device thread). Returns 0 on failure.
    GLuint glShader(const TranslateOptions &opt, uint64_t variantKey, const TranslatedShader **variantInfo);

private:
    std::vector<DWORD> m_bytecode;
    TranslatedShader m_base;
    struct Variant {
        uint64_t key;
        GLuint shader;
        TranslatedShader tr;
    };
    std::vector<Variant> m_variants;
};

class PixelShader final : public Resource<IDirect3DPixelShader9> {
public:
    PixelShader(Device *dev, const DWORD *function, TranslatedShader &&tr);
    ~PixelShader() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    HRESULT GetFunction(void *pData, UINT *pSizeOfData) override;
    const TranslatedShader &tr() const { return m_tr; }
    const std::vector<DWORD> &bytecode() const { return m_bytecode; }
    GLuint glShader(); // device thread, lazy compile, 0 on failure

private:
    std::vector<DWORD> m_bytecode;
    TranslatedShader m_tr;
    GLuint m_shader = 0;
    bool m_compileFailed = false;
};

class Query final : public Resource<IDirect3DQuery9> {
public:
    Query(Device *dev, D3DQUERYTYPE type);
    ~Query() override;
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    D3DQUERYTYPE GetType() override { return m_type; }
    DWORD GetDataSize() override;
    HRESULT Issue(DWORD dwIssueFlags) override;
    HRESULT GetData(void *pData, DWORD dwSize, DWORD dwGetDataFlags) override;
    void deviceLost();

private:
    D3DQUERYTYPE m_type;
    GLuint m_query = 0;
    bool m_active = false;  // between BEGIN and END
    bool m_pending = false; // ended, result not read yet
    DWORD m_lastResult;
    uint64_t m_timestamp = 0;
};

class StateBlock final : public Resource<IDirect3DStateBlock9> {
public:
    StateBlock(Device *dev) : Resource(dev) {}
    HRESULT GetDevice(IDirect3DDevice9 **pp) override { return Resource::GetDevice(pp); }
    HRESULT Capture() override;
    HRESULT Apply() override;
};

class SwapChain final : public Unknown<IDirect3DSwapChain9> {
public:
    SwapChain(Device *dev, const D3DPRESENT_PARAMETERS &pp);
    ~SwapChain() override;
    ULONG Release() override; // device-owned: never deleted by Release
    HRESULT Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride,
                    const struct _RGNDATA *pDirtyRegion, DWORD dwFlags) override;
    HRESULT GetFrontBufferData(IDirect3DSurface9 *pDestSurface) override;
    HRESULT GetBackBuffer(UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **ppBackBuffer) override;
    HRESULT GetRasterStatus(D3DRASTER_STATUS *pRasterStatus) override;
    HRESULT GetDisplayMode(D3DDISPLAYMODE *pMode) override;
    HRESULT GetDevice(IDirect3DDevice9 **ppDevice) override;
    HRESULT GetPresentParameters(D3DPRESENT_PARAMETERS *pPresentationParameters) override;
    Surface *backBuffer() { return m_backBuffer; }
    void reset(const D3DPRESENT_PARAMETERS &pp);
    void destroy();

private:
    Device *m_device;
    D3DPRESENT_PARAMETERS m_pp;
    TexStorage m_bbStorage;
    Surface *m_backBuffer = nullptr;
};

// ------------------------------------------------------------------------------------------------ program cache
struct UniformRange {
    int reg;    // first D3D register
    int count;
    int packed; // first element of the packed GLSL array
};

struct Program {
    GLuint prog = 0;
    bool ok = false;
    uint64_t vsId = 0, psId = 0, variant = 0;
    VertexShader *vs = nullptr;
    PixelShader *ps = nullptr;
    GLint locVsF = -1, locVsI = -1, locVsB = -1, locPsF = -1, locPsI = -1, locPsB = -1;
    GLint locPosFixup = -1, locAlphaTest = -1, locVPosFlip = -1;
    std::vector<UniformRange> vsF, vsI, vsB, psF, psI, psB;
    // def'd float constants that lie inside an app-settable array (relative addressing): D3D gives the def value
    // precedence, so it is written into the packed array on every upload.
    struct DefOverride {
        int slot;
        float v[4];
    };
    std::vector<DefOverride> vsFDefs, psFDefs;
    int vsFCount = 0, vsICount = 0, vsBCount = 0, psFCount = 0, psICount = 0, psBCount = 0;
    std::vector<ShaderInputSlot> inputs; // VS inputs with bound locations
    std::vector<ShaderSamplerInfo> samplers; // PS samplers (unit == stage)
    uint32_t uploadedVsF = ~0u, uploadedPsF = ~0u, uploadedVsI = ~0u, uploadedPsI = ~0u, uploadedVsB = ~0u,
             uploadedPsB = ~0u;
    float posFixup[4] = {-9, -9, -9, -9};
    float alphaTest[2] = {-9, -9};
    struct DeclBinding {
        uint64_t declId;
        std::vector<AttribBinding> binds;
        uint32_t streamMask;
    };
    std::vector<DeclBinding> declCache;
};

// ------------------------------------------------------------------------------------------------ IDirect3D9
class Direct3D9 final : public Unknown<IDirect3D9Ex> {
public:
    explicit Direct3D9(bool ex) : m_ex(ex) {}
    HRESULT RegisterSoftwareDevice(void *) override { return D3DERR_NOTAVAILABLE; }
    UINT GetAdapterCount() override { return 1; }
    HRESULT GetAdapterIdentifier(UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier) override;
    UINT GetAdapterModeCount(UINT Adapter, D3DFORMAT Format) override;
    HRESULT EnumAdapterModes(UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode) override;
    HRESULT GetAdapterDisplayMode(UINT Adapter, D3DDISPLAYMODE *pMode) override;
    HRESULT CheckDeviceType(UINT Adapter, D3DDEVTYPE DevType, D3DFORMAT AdapterFormat, D3DFORMAT BackBufferFormat,
                            BOOL bWindowed) override;
    HRESULT CheckDeviceFormat(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage,
                              D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) override;
    HRESULT CheckDeviceMultiSampleType(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed,
                                       D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels) override;
    HRESULT CheckDepthStencilMatch(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat,
                                   D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) override;
    HRESULT CheckDeviceFormatConversion(UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat,
                                        D3DFORMAT TargetFormat) override;
    HRESULT GetDeviceCaps(UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps) override;
    HMONITOR GetAdapterMonitor(UINT Adapter) override;
    HRESULT CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                         D3DPRESENT_PARAMETERS *pPresentationParameters,
                         IDirect3DDevice9 **ppReturnedDeviceInterface) override;
    UINT GetAdapterModeCountEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter) override;
    HRESULT EnumAdapterModesEx(UINT Adapter, const D3DDISPLAYMODEFILTER *pFilter, UINT Mode,
                               D3DDISPLAYMODEEX *pMode) override;
    HRESULT GetAdapterDisplayModeEx(UINT Adapter, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) override;
    HRESULT CreateDeviceEx(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags,
                           D3DPRESENT_PARAMETERS *pPresentationParameters, D3DDISPLAYMODEEX *pFullscreenDisplayMode,
                           IDirect3DDevice9Ex **ppReturnedDeviceInterface) override;
    HRESULT GetAdapterLUID(UINT Adapter, struct _LUID *pLUID) override;

private:
    [[maybe_unused]] bool m_ex;
};

std::vector<D3DDISPLAYMODE> displayModeList();

} // namespace d3d9shim

#include "d3d9_device.h"
#include "d3d9_templates.h"
