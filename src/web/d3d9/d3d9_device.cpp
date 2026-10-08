// d3d9_device.cpp - IDirect3DDevice9: creation, WebGL2 context, state setters, resource creation, lifetime.
// Draw calls and the GL state machinery live in d3d9_draw.cpp; render targets/copies in d3d9_framebuffer.cpp.
#include "d3d9_internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

namespace d3d9shim {

void setDeviceThreadToCurrent(); // d3d9_log.cpp

#define DEVICE_THREAD_CHECK(name)                                                                     \
    do {                                                                                              \
        if (!onDeviceThread())                                                                        \
            D3D9_WARN_ONCE("%s called off the device thread (WebGL context is bound to the thread "   \
                           "that called CreateDevice); the call may misbehave",                       \
                           name);                                                                     \
    } while (0)

static inline int samplerSlot(DWORD sampler)
{
    if (sampler < (DWORD)kMaxSamplers)
        return (int)sampler;
    if (sampler == D3DDMAPSAMPLER)
        return kMaxSamplers;
    if (sampler >= D3DVERTEXTEXTURESAMPLER0 && sampler <= D3DVERTEXTEXTURESAMPLER3)
        return kMaxSamplers + 1 + (int)(sampler - D3DVERTEXTEXTURESAMPLER0);
    return -1;
}

Device::Device(Direct3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior) : m_d3d(d3d)
{
    m_d3d->AddRef();
    m_creation.AdapterOrdinal = adapter;
    m_creation.DeviceType = type;
    m_creation.hFocusWindow = focus;
    m_creation.BehaviorFlags = behavior;
    memset(m_rs, 0, sizeof(m_rs));
    memset(m_ss, 0, sizeof(m_ss));
    memset(m_tss, 0, sizeof(m_tss));
    for (int i = 0; i < 256; ++i)
        m_gamma.red[i] = m_gamma.green[i] = m_gamma.blue[i] = (WORD)(i * 257);
}

void Device::createContext()
{
#ifdef __EMSCRIPTEN__
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    attrs.minorVersion = 0;
    attrs.alpha = EM_FALSE;
    attrs.depth = EM_FALSE;
    attrs.stencil = EM_FALSE;
    attrs.antialias = EM_FALSE;
    attrs.premultipliedAlpha = EM_FALSE;
    attrs.preserveDrawingBuffer = EM_FALSE;
    attrs.powerPreference = EM_WEBGL_POWER_PREFERENCE_HIGH_PERFORMANCE;
    attrs.failIfMajorPerformanceCaveat = EM_FALSE;
    attrs.enableExtensionsByDefault = EM_FALSE;
    attrs.explicitSwapControl = EM_FALSE;
    attrs.proxyContextToMainThread = EMSCRIPTEN_WEBGL_CONTEXT_PROXY_FALLBACK;
    // One context per canvas for the whole run: a second getContext('webgl2') on the same canvas would return the
    // old context anyway, so a re-created device (vid_restart) reuses it if it is still alive on this thread.
    static EMSCRIPTEN_WEBGL_CONTEXT_HANDLE s_ctx = 0;
    static std::string s_ctxSelector;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = 0;
    if (s_ctx > 0 && s_ctxSelector == g_canvasSelector && !emscripten_is_webgl_context_lost(s_ctx) &&
        emscripten_webgl_make_context_current(s_ctx) == EMSCRIPTEN_RESULT_SUCCESS)
        ctx = s_ctx;
    else
        ctx = emscripten_webgl_create_context(g_canvasSelector.c_str(), &attrs);
    s_ctx = ctx;
    s_ctxSelector = g_canvasSelector;
    if (ctx <= 0) {
        D3D9_ERROR("emscripten_webgl_create_context(\"%s\") failed (%d): no WebGL2?", g_canvasSelector.c_str(),
                   (int)ctx);
        return;
    }
    if (emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
        D3D9_ERROR("emscripten_webgl_make_context_current failed");
        emscripten_webgl_destroy_context(ctx);
        return;
    }
    m_context = (intptr_t)ctx;
    auto ext = [&](const char *name) { return emscripten_webgl_enable_extension(ctx, name) != 0; };
    GLCaps c;
    c.valid = true;
    c.colorBufferFloat = ext("EXT_color_buffer_float");
    c.colorBufferHalfFloat = ext("EXT_color_buffer_half_float");
    c.textureFloatLinear = ext("OES_texture_float_linear");
    c.s3tc = ext("WEBGL_compressed_texture_s3tc");
    ext("WEBGL_compressed_texture_s3tc_srgb");
    c.anisotropic = ext("EXT_texture_filter_anisotropic");
    c.norm16 = ext("EXT_texture_norm16");
    c.floatBlend = ext("EXT_float_blend");
    GLint v = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &v);
    c.maxTextureSize = v;
    glGetIntegerv(GL_MAX_CUBE_MAP_TEXTURE_SIZE, &v);
    c.maxCubeMapSize = v;
    glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &v);
    c.max3DTextureSize = v;
    glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &v);
    c.maxRenderbufferSize = v;
    glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &v);
    c.maxVertexAttribs = v;
    glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &v);
    c.maxTextureUnits = v;
    glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &v);
    c.maxVertexUniformVectors = v;
    glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &v);
    c.maxFragmentUniformVectors = v;
    if (c.anisotropic) {
        GLfloat a = 1.f;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &a);
        c.maxAnisotropy = a;
    } else {
        c.maxAnisotropy = 1.f;
    }
    GLfloat ps[2] = {1, 1};
    glGetFloatv(GL_ALIASED_POINT_SIZE_RANGE, ps);
    c.maxPointSize = ps[1];
    const char *r = (const char *)glGetString(GL_RENDERER);
    strncpy(c.renderer, r ? r : "WebGL2", sizeof(c.renderer) - 1);
    const char *vd = (const char *)glGetString(GL_VENDOR);
    strncpy(c.vendor, vd ? vd : "?", sizeof(c.vendor) - 1);
    while (glGetError() != GL_NO_ERROR) {
    }
    m_glcaps = c;
    setLiveGLCaps(c);
    D3D9_INFO("WebGL2 context on %s: %s / %s; max texture %d; s3tc %d, color_buffer_float %d, float_linear %d, "
              "aniso %.0f, norm16 %d",
              g_canvasSelector.c_str(), c.vendor, c.renderer, c.maxTextureSize, c.s3tc, c.colorBufferFloat,
              c.textureFloatLinear, c.maxAnisotropy, c.norm16);
#endif
}

static void fillPresentDefaults(D3DPRESENT_PARAMETERS *pp)
{
#ifdef __EMSCRIPTEN__
    if (!pp->BackBufferWidth || !pp->BackBufferHeight) {
        int w = 0, h = 0;
        emscripten_get_canvas_element_size(g_canvasSelector.c_str(), &w, &h);
        if (!pp->BackBufferWidth)
            pp->BackBufferWidth = w > 0 ? (UINT)w : 640;
        if (!pp->BackBufferHeight)
            pp->BackBufferHeight = h > 0 ? (UINT)h : 480;
    }
#else
    if (!pp->BackBufferWidth)
        pp->BackBufferWidth = 640;
    if (!pp->BackBufferHeight)
        pp->BackBufferHeight = 480;
#endif
    if (pp->BackBufferFormat == D3DFMT_UNKNOWN)
        pp->BackBufferFormat = D3DFMT_X8R8G8B8;
    if (!pp->BackBufferCount)
        pp->BackBufferCount = 1;
    if (pp->MultiSampleType != D3DMULTISAMPLE_NONE) {
        D3D9_WARN("MSAA back buffers are not supported; using D3DMULTISAMPLE_NONE");
        pp->MultiSampleType = D3DMULTISAMPLE_NONE;
        pp->MultiSampleQuality = 0;
    }
}

static void setCanvasSize(UINT w, UINT h)
{
#ifdef __EMSCRIPTEN__
    emscripten_set_canvas_element_size(g_canvasSelector.c_str(), (int)w, (int)h);
#else
    (void)w;
    (void)h;
#endif
}

HRESULT Device::init(D3DPRESENT_PARAMETERS *pp)
{
    initLogFromEnv();
    setDeviceThreadToCurrent();
    createContext();
    if (!m_context)
        return D3DERR_NOTAVAILABLE;
    fillPresentDefaults(pp);
    setCanvasSize(pp->BackBufferWidth, pp->BackBufferHeight);

    glGenVertexArrays(1, &m_vao);
    glBindVertexArray(m_vao);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glFrontFace(GL_CCW);
    glGenFramebuffers(2, m_scratchFbo);
    m_cache.invalidate();

    m_swapChain = new SwapChain(this, *pp);
    if (pp->EnableAutoDepthStencil)
        m_autoDepth = new Surface(this, Surface::DepthStencil, pp->BackBufferWidth, pp->BackBufferHeight,
                                  pp->AutoDepthStencilFormat, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, false, true);
    resetState();
    D3D9_INFO("device created: %ux%u back buffer, behavior 0x%lx", pp->BackBufferWidth, pp->BackBufferHeight,
              (unsigned long)m_creation.BehaviorFlags);
    return D3D_OK;
}

void Device::resetState()
{
    defaultRenderStates(m_rs);
    m_rs[D3DRS_ZENABLE] = m_autoDepth ? D3DZB_TRUE : D3DZB_FALSE;
    for (int i = 0; i < kSamplerSlots; ++i)
        defaultSamplerStates(m_ss[i]);
    memset(m_tss, 0, sizeof(m_tss));
    for (int i = 0; i < 8; ++i) {
        m_tss[i][D3DTSS_COLOROP] = i == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        m_tss[i][D3DTSS_ALPHAOP] = i == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        m_tss[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        m_tss[i][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        m_tss[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        m_tss[i][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        m_tss[i][D3DTSS_TEXCOORDINDEX] = (DWORD)i;
    }
    for (auto &t : m_transforms) {
        memset(&t, 0, sizeof(t));
        t._11 = t._22 = t._33 = t._44 = 1.f;
    }
    for (int i = 0; i < kSamplerSlots; ++i) {
        m_textures[i] = nullptr;
        m_textureStorage[i] = nullptr;
    }
    for (auto &s : m_streams)
        s = Stream();
    m_ib = nullptr;
    m_decl = nullptr;
    m_vs = nullptr;
    m_ps = nullptr;
    memset(m_vsF, 0, sizeof(m_vsF));
    memset(m_psF, 0, sizeof(m_psF));
    memset(m_vsI, 0, sizeof(m_vsI));
    memset(m_psI, 0, sizeof(m_psI));
    memset(m_vsB, 0, sizeof(m_vsB));
    memset(m_psB, 0, sizeof(m_psB));
    ++m_vsFVersion, ++m_psFVersion, ++m_vsIVersion, ++m_psIVersion, ++m_vsBVersion, ++m_psBVersion;
    Surface *bb = m_swapChain->backBuffer();
    m_rt[0] = bb;
    m_rt[1] = m_rt[2] = m_rt[3] = nullptr;
    m_ds = m_autoDepth;
    m_viewport = {0, 0, bb->width(), bb->height(), 0.f, 1.f};
    m_scissor = {0, 0, (LONG)bb->width(), (LONG)bb->height()};
    m_rasterDirty = m_viewportDirty = m_fboDirty = m_attribsDirty = m_samplersDirty = m_bgraDirty = true;
    m_samplerDirtyMask = 0xffffffffu;
    m_curProgram = nullptr;
}

Device::~Device()
{
    processDeferred();
    destroyAllGL();
    if (m_swapChain)
        m_swapChain->destroy();
    if (m_autoDepth)
        m_autoDepth->destroyInternal();
    m_swapChain = nullptr;
    m_autoDepth = nullptr;
    processDeferred();
    // The WebGL context is kept for the next device on this canvas (see createContext).
    m_d3d->Release();
}

void Device::destroyAllGL()
{
    for (auto &kv : m_programs) {
        if (kv.second->prog)
            glDeleteProgram(kv.second->prog);
        delete kv.second;
    }
    m_programs.clear();
    for (auto &f : m_fbos)
        glDeleteFramebuffers(1, &f.fbo);
    m_fbos.clear();
    for (auto &kv : m_samplerObjects)
        glDeleteSamplers(1, &kv.second);
    m_samplerObjects.clear();
    if (m_presentProg)
        glDeleteProgram(m_presentProg);
    if (m_gammaLut)
        glDeleteTextures(1, &m_gammaLut);
    m_presentProg = m_gammaLut = 0;
    if (m_upVB)
        glDeleteBuffers(1, &m_upVB);
    if (m_upIB)
        glDeleteBuffers(1, &m_upIB);
    m_upVB = m_upIB = 0;
    if (m_scratchFbo[0])
        glDeleteFramebuffers(2, m_scratchFbo);
    m_scratchFbo[0] = m_scratchFbo[1] = 0;
    if (m_vao)
        glDeleteVertexArrays(1, &m_vao);
    m_vao = 0;
    m_cache.invalidate();
}

// ------------------------------------------------------------------------------------------------ lifetime plumbing

bool Device::checkThread(const char *method)
{
    if (onDeviceThread())
        return true;
    D3D9_WARN("%s called off the device thread", method);
    return false;
}

void Device::deferDestroy(std::function<void()> fn)
{
    std::lock_guard<std::mutex> g(m_deferMtx);
    m_deferred.push_back(std::move(fn));
    m_hasDeferred = true;
}

void Device::queueGLDelete(GLenum kind, GLuint name)
{
    std::lock_guard<std::mutex> g(m_deferMtx);
    m_glDeletes.emplace_back(kind, name);
    m_hasDeferred = true;
}

void Device::processDeferred()
{
    if (!m_hasDeferred.load() || !onDeviceThread())
        return;
    std::vector<std::function<void()>> fns;
    std::vector<std::pair<GLenum, GLuint>> dels;
    {
        std::lock_guard<std::mutex> g(m_deferMtx);
        fns.swap(m_deferred);
        dels.swap(m_glDeletes);
        m_hasDeferred = false;
    }
    for (auto &f : fns)
        f();
    for (auto &d : dels) {
        switch (d.first) {
        case GL_TEXTURE: glDeleteTextures(1, &d.second); break;
        case GL_ARRAY_BUFFER: glDeleteBuffers(1, &d.second); break;
        case GL_RENDERBUFFER: glDeleteRenderbuffers(1, &d.second); break;
        case GL_FRAMEBUFFER: glDeleteFramebuffers(1, &d.second); break;
        case GL_ANY_SAMPLES_PASSED: glDeleteQueries(1, &d.second); break;
        case GL_VERTEX_SHADER:
        case GL_FRAGMENT_SHADER: glDeleteShader(d.second); break;
        default: break;
        }
    }
    if (!dels.empty())
        m_cache.invalidate();
}

void Device::purgeFbos(Surface *s)
{
    for (size_t i = 0; i < m_fbos.size();) {
        if (m_fbos[i].color == s || m_fbos[i].depth == s) {
            if (onDeviceThread())
                glDeleteFramebuffers(1, &m_fbos[i].fbo);
            else
                queueGLDelete(GL_FRAMEBUFFER, m_fbos[i].fbo);
            if (m_curFbo == m_fbos[i].fbo)
                m_curFbo = 0;
            if (m_cache.drawFbo == m_fbos[i].fbo)
                m_cache.drawFbo = ~0u;
            if (m_cache.readFbo == m_fbos[i].fbo)
                m_cache.readFbo = ~0u;
            m_fbos[i] = m_fbos.back();
            m_fbos.pop_back();
        } else {
            ++i;
        }
    }
    m_fboDirty = true;
}

void Device::onSurfaceDestroyed(Surface *s)
{
    purgeFbos(s);
    for (auto &rt : m_rt)
        if (rt == s)
            rt = nullptr;
    if (m_ds == s)
        m_ds = nullptr;
}

void Device::onTextureStorageDestroyed(TexStorage *t)
{
    for (auto &u : m_unitSamplerTex)
        if (u == t)
            u = nullptr;
    for (int i = 0; i < kSamplerSlots; ++i)
        if (m_textureStorage[i] == t) {
            m_textures[i] = nullptr;
            m_textureStorage[i] = nullptr;
            m_samplerDirtyMask |= 1u << i;
        }
}

void Device::onVertexBufferDestroyed(VertexBuffer *vb)
{
    for (auto &s : m_streams)
        if (s.vb == vb) {
            s.vb = nullptr;
            m_attribsDirty = true;
        }
}

void Device::onIndexBufferDestroyed(IndexBuffer *ib)
{
    if (m_ib == ib)
        m_ib = nullptr;
}

void Device::onDeclDestroyed(VertexDecl *d)
{
    if (m_decl == d) {
        m_decl = nullptr;
        m_attribsDirty = m_bgraDirty = true;
    }
    for (auto &kv : m_programs) {
        auto &dc = kv.second->declCache;
        for (size_t i = 0; i < dc.size();)
            if (dc[i].declId == d->id()) {
                dc[i] = dc.back();
                dc.pop_back();
            } else {
                ++i;
            }
    }
}

void Device::onVertexShaderDestroyed(VertexShader *s)
{
    if (m_vs == s) {
        m_vs = nullptr;
        m_bgraDirty = true;
    }
    for (auto it = m_programs.begin(); it != m_programs.end();) {
        if (it->second->vs == s) {
            if (m_curProgram == it->second)
                m_curProgram = nullptr;
            if (it->second->prog) {
                glDeleteProgram(it->second->prog);
                if (m_cache.program == it->second->prog)
                    m_cache.program = ~0u;
            }
            delete it->second;
            it = m_programs.erase(it);
        } else {
            ++it;
        }
    }
}

void Device::onPixelShaderDestroyed(PixelShader *s)
{
    if (m_ps == s)
        m_ps = nullptr;
    for (auto it = m_programs.begin(); it != m_programs.end();) {
        if (it->second->ps == s) {
            if (m_curProgram == it->second)
                m_curProgram = nullptr;
            if (it->second->prog) {
                glDeleteProgram(it->second->prog);
                if (m_cache.program == it->second->prog)
                    m_cache.program = ~0u;
            }
            delete it->second;
            it = m_programs.erase(it);
        } else {
            ++it;
        }
    }
}

void Device::onQueryDestroyed(Query *q)
{
    if (m_activeOcclusion == q) {
        glEndQuery(GL_ANY_SAMPLES_PASSED);
        m_activeOcclusion = nullptr;
    }
}

void Device::beginOcclusion(Query *q)
{
    if (m_activeOcclusion && m_activeOcclusion != q)
        m_activeOcclusion->Issue(D3DISSUE_END); // WebGL allows one active query per target
    m_activeOcclusion = q;
}

void Device::endOcclusion(Query *q)
{
    if (m_activeOcclusion == q)
        m_activeOcclusion = nullptr;
}

// ------------------------------------------------------------------------------------------------ misc device

HRESULT Device::TestCooperativeLevel()
{
#ifdef __EMSCRIPTEN__
    if (m_context && onDeviceThread() && emscripten_is_webgl_context_lost((EMSCRIPTEN_WEBGL_CONTEXT_HANDLE)m_context)) {
        D3D9_WARN_ONCE("WebGL context lost: reporting D3DERR_DEVICELOST (context restore is not implemented)");
        return D3DERR_DEVICELOST;
    }
#endif
    return D3D_OK;
}

UINT Device::GetAvailableTextureMem() { return 1536u * 1024u * 1024u; }
HRESULT Device::EvictManagedResources() { return D3D_OK; }

HRESULT Device::GetDirect3D(IDirect3D9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    m_d3d->AddRef();
    *pp = m_d3d;
    return D3D_OK;
}

HRESULT Device::GetDeviceCaps(D3DCAPS9 *pCaps)
{
    if (!pCaps)
        return D3DERR_INVALIDCALL;
    fillD3DCaps(pCaps, m_glcaps);
    return D3D_OK;
}

HRESULT Device::GetDisplayMode(UINT, D3DDISPLAYMODE *pMode)
{
    if (!pMode)
        return D3DERR_INVALIDCALL;
    pMode->Width = (UINT)g_displayWidth;
    pMode->Height = (UINT)g_displayHeight;
    pMode->RefreshRate = 60;
    pMode->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

HRESULT Device::GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation)
{
    if (pRotation)
        *pRotation = D3DDISPLAYROTATION_IDENTITY;
    if (!pMode)
        return D3D_OK;
    D3DDISPLAYMODE m;
    GetDisplayMode(iSwapChain, &m);
    pMode->Size = sizeof(*pMode);
    pMode->Width = m.Width;
    pMode->Height = m.Height;
    pMode->RefreshRate = m.RefreshRate;
    pMode->Format = m.Format;
    pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    return D3D_OK;
}

HRESULT Device::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *p)
{
    if (!p)
        return D3DERR_INVALIDCALL;
    *p = m_creation;
    return D3D_OK;
}

HRESULT Device::SetCursorProperties(UINT, UINT, IDirect3DSurface9 *) { return D3D_OK; }
void Device::SetCursorPosition(int, int, DWORD) {}
BOOL Device::ShowCursor(BOOL) { return FALSE; }

HRESULT Device::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **pp)
{
    if (pp)
        *pp = nullptr;
    D3D9_WARN_ONCE("CreateAdditionalSwapChain is not supported");
    return D3DERR_NOTAVAILABLE;
}

HRESULT Device::GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9 **pp)
{
    if (!pp || iSwapChain != 0)
        return D3DERR_INVALIDCALL;
    m_swapChain->AddRef();
    *pp = m_swapChain;
    return D3D_OK;
}

HRESULT Device::Reset(D3DPRESENT_PARAMETERS *pp)
{
    DEVICE_THREAD_CHECK("Reset");
    if (!pp)
        return D3DERR_INVALIDCALL;
    processDeferred();
    if (m_activeOcclusion)
        m_activeOcclusion->Issue(D3DISSUE_END);
    fillPresentDefaults(pp);
    setCanvasSize(pp->BackBufferWidth, pp->BackBufferHeight);
    m_swapChain->reset(*pp);
    if (m_autoDepth) {
        m_autoDepth->destroyInternal();
        m_autoDepth = nullptr;
    }
    if (pp->EnableAutoDepthStencil)
        m_autoDepth = new Surface(this, Surface::DepthStencil, pp->BackBufferWidth, pp->BackBufferHeight,
                                  pp->AutoDepthStencilFormat, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL, false, true);
    m_inScene = false;
    resetState();
    D3D9_INFO("device reset: %ux%u", pp->BackBufferWidth, pp->BackBufferHeight);
    return D3D_OK;
}

HRESULT Device::Present(const RECT *, const RECT *, HWND, const struct _RGNDATA *)
{
    DEVICE_THREAD_CHECK("Present");
    D3D9_TRACE("Present (frame %u, %u draws)", m_frame, g_stats.drawsThisFrame);
    presentBlit();
    ++m_frame;
    g_stats.frame = m_frame;
    g_stats.draws = g_stats.drawsThisFrame;
    g_stats.drawsThisFrame = 0;
    processDeferred();
    return D3D_OK;
}

HRESULT Device::GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type, IDirect3DSurface9 **pp)
{
    if (iSwapChain != 0)
        return D3DERR_INVALIDCALL;
    return m_swapChain->GetBackBuffer(iBackBuffer, Type, pp);
}

HRESULT Device::GetRasterStatus(UINT, D3DRASTER_STATUS *p) { return m_swapChain->GetRasterStatus(p); }

void Device::SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *pRamp)
{
    // No hardware gamma on the web: Present applies the ramp with a LUT pass (d3d9_framebuffer.cpp) when it is not
    // the identity. Stores only, so any thread may call it.
    if (pRamp && memcmp(&m_gamma, pRamp, sizeof(m_gamma)) != 0) {
        m_gamma = *pRamp;
        m_gammaDirty = true;
    }
}

void Device::GetGammaRamp(UINT, D3DGAMMARAMP *pRamp)
{
    if (pRamp)
        *pRamp = m_gamma;
}

// ------------------------------------------------------------------------------------------------ resource creation

static bool isFourccHack(D3DFORMAT f)
{
    switch ((DWORD)f) {
    case D3D9SHIM_FOURCC_INTZ:
    case D3D9SHIM_FOURCC_RESZ:
    case D3D9SHIM_FOURCC_NULL:
    case D3D9SHIM_FOURCC_ATOC:
    case D3D9SHIM_FOURCC_SSAA:
    case D3D9SHIM_FOURCC_DF24:
    case D3D9SHIM_FOURCC_DF16:
    case D3D9SHIM_FOURCC_RAWZ: return true;
    default: return false;
    }
}

static HRESULT validateTextureFormat(const GLCaps &gl, D3DFORMAT fmt, DWORD usage, const char *what)
{
    if (isFourccHack(fmt)) {
        D3D9_WARN("%s: vendor FOURCC format %s is not supported", what, formatName(fmt));
        return D3DERR_NOTAVAILABLE;
    }
    const FormatInfo fi = resolveFormat(fmt, gl);
    if (!(fi.flags & FMT_SUPPORTED)) {
        D3D9_WARN("%s: format %s (0x%lx) is not supported", what, fi.name, (unsigned long)fmt);
        return D3DERR_INVALIDCALL;
    }
    if ((usage & D3DUSAGE_RENDERTARGET) && !(fi.flags & FMT_RENDERABLE)) {
        D3D9_WARN("%s: format %s is not renderable here", what, fi.name);
        return D3DERR_NOTAVAILABLE;
    }
    if ((usage & D3DUSAGE_DEPTHSTENCIL) && !(fi.flags & FMT_DEPTH))
        return D3DERR_INVALIDCALL;
    return D3D_OK;
}

HRESULT Device::CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                              IDirect3DTexture9 **pp, HANDLE *)
{
    if (!pp || !Width || !Height)
        return D3DERR_INVALIDCALL;
    *pp = nullptr;
    HRESULT hr = validateTextureFormat(m_glcaps, Format, Usage, "CreateTexture");
    if (FAILED(hr))
        return hr;
    Texture *t = new Texture(this);
    t->storage().init(D3DRTYPE_TEXTURE, Width, Height, 1, Levels, Usage, Format, Pool);
    D3D9_TRACE("CreateTexture(%ux%u, %u levels, usage 0x%lx, %s, pool %d) -> %p", Width, Height, Levels,
               (unsigned long)Usage, formatName(Format), (int)Pool, (void *)t);
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format,
                                    D3DPOOL Pool, IDirect3DVolumeTexture9 **pp, HANDLE *)
{
    if (!pp || !Width || !Height || !Depth)
        return D3DERR_INVALIDCALL;
    *pp = nullptr;
    if (Usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
        return D3DERR_INVALIDCALL;
    HRESULT hr = validateTextureFormat(m_glcaps, Format, Usage, "CreateVolumeTexture");
    if (FAILED(hr))
        return hr;
    VolumeTexture *t = new VolumeTexture(this);
    t->storage().init(D3DRTYPE_VOLUMETEXTURE, Width, Height, Depth, Levels, Usage, Format, Pool);
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DCubeTexture9 **pp, HANDLE *)
{
    if (!pp || !EdgeLength)
        return D3DERR_INVALIDCALL;
    *pp = nullptr;
    HRESULT hr = validateTextureFormat(m_glcaps, Format, Usage, "CreateCubeTexture");
    if (FAILED(hr))
        return hr;
    CubeTexture *t = new CubeTexture(this);
    t->storage().init(D3DRTYPE_CUBETEXTURE, EdgeLength, EdgeLength, 1, Levels, Usage, Format, Pool);
    *pp = t;
    return D3D_OK;
}

HRESULT Device::CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool, IDirect3DVertexBuffer9 **pp,
                                   HANDLE *)
{
    if (!pp || !Length)
        return D3DERR_INVALIDCALL;
    *pp = new VertexBuffer(this, Length, Usage, FVF, Pool);
    return D3D_OK;
}

HRESULT Device::CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                                  IDirect3DIndexBuffer9 **pp, HANDLE *)
{
    if (!pp || !Length || (Format != D3DFMT_INDEX16 && Format != D3DFMT_INDEX32))
        return D3DERR_INVALIDCALL;
    *pp = new IndexBuffer(this, Length, Usage, Format, Pool);
    return D3D_OK;
}

HRESULT Device::CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample, DWORD,
                                   BOOL Lockable, IDirect3DSurface9 **pp, HANDLE *)
{
    if (!pp || !Width || !Height)
        return D3DERR_INVALIDCALL;
    *pp = nullptr;
    HRESULT hr = validateTextureFormat(m_glcaps, Format, D3DUSAGE_RENDERTARGET, "CreateRenderTarget");
    if (FAILED(hr))
        return hr;
    if (MultiSample != D3DMULTISAMPLE_NONE)
        D3D9_WARN_ONCE("CreateRenderTarget: multisampling ignored");
    *pp = new Surface(this, Surface::RenderTarget, Width, Height, Format, D3DPOOL_DEFAULT, D3DUSAGE_RENDERTARGET,
                      Lockable != 0);
    return D3D_OK;
}

HRESULT Device::CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                          DWORD, BOOL, IDirect3DSurface9 **pp, HANDLE *)
{
    if (!pp || !Width || !Height)
        return D3DERR_INVALIDCALL;
    *pp = nullptr;
    const FormatInfo fi = resolveFormat(Format, m_glcaps);
    if (!(fi.flags & FMT_DEPTH)) {
        D3D9_WARN("CreateDepthStencilSurface: %s is not a depth format", fi.name);
        return D3DERR_INVALIDCALL;
    }
    if (MultiSample != D3DMULTISAMPLE_NONE)
        D3D9_WARN_ONCE("CreateDepthStencilSurface: multisampling ignored");
    *pp = new Surface(this, Surface::DepthStencil, Width, Height, Format, D3DPOOL_DEFAULT, D3DUSAGE_DEPTHSTENCIL,
                      false);
    return D3D_OK;
}

HRESULT Device::CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
                                            IDirect3DSurface9 **pp, HANDLE *)
{
    if (!pp || !Width || !Height)
        return D3DERR_INVALIDCALL;
    *pp = nullptr;
    HRESULT hr = validateTextureFormat(m_glcaps, Format, 0, "CreateOffscreenPlainSurface");
    if (FAILED(hr))
        return hr;
    *pp = new Surface(this, Surface::OffscreenPlain, Width, Height, Format, Pool, 0, true);
    return D3D_OK;
}

// ------------------------------------------------------------------------------------------------ render targets

HRESULT Device::SetRenderTarget(DWORD idx, IDirect3DSurface9 *pRenderTarget)
{
    D3D9_TRACE("SetRenderTarget(%lu, %p)", (unsigned long)idx, (void *)pRenderTarget);
    if (idx >= 4)
        return D3DERR_INVALIDCALL;
    if (idx == 0 && !pRenderTarget)
        return D3DERR_INVALIDCALL;
    if (idx > 0) {
        if (pRenderTarget) {
            D3D9_WARN_ONCE("multiple render targets are not supported (NumSimultaneousRTs = 1)");
            return D3DERR_INVALIDCALL;
        }
        m_rt[idx] = nullptr;
        return D3D_OK;
    }
    Surface *s = static_cast<Surface *>(pRenderTarget);
    m_rt[0] = s;
    m_fboDirty = true;
    // D3D9 resets the viewport and scissor rectangle to the full new render target.
    m_viewport = {0, 0, s->width(), s->height(), 0.f, 1.f};
    m_scissor = {0, 0, (LONG)s->width(), (LONG)s->height()};
    m_viewportDirty = true;
    m_rasterDirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderTarget(DWORD idx, IDirect3DSurface9 **pp)
{
    if (!pp || idx >= 4)
        return D3DERR_INVALIDCALL;
    *pp = m_rt[idx];
    if (!m_rt[idx])
        return D3DERR_NOTFOUND;
    m_rt[idx]->AddRef();
    return D3D_OK;
}

HRESULT Device::SetDepthStencilSurface(IDirect3DSurface9 *pNewZStencil)
{
    D3D9_TRACE("SetDepthStencilSurface(%p)", (void *)pNewZStencil);
    m_ds = static_cast<Surface *>(pNewZStencil);
    m_fboDirty = true;
    return D3D_OK;
}

HRESULT Device::GetDepthStencilSurface(IDirect3DSurface9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_ds;
    if (!m_ds)
        return D3DERR_NOTFOUND;
    m_ds->AddRef();
    return D3D_OK;
}

HRESULT Device::BeginScene()
{
    D3D9_TRACE("BeginScene");
    if (m_inScene)
        return D3DERR_INVALIDCALL;
    m_inScene = true;
    processDeferred();
    return D3D_OK;
}

HRESULT Device::EndScene()
{
    D3D9_TRACE("EndScene");
    if (!m_inScene)
        return D3DERR_INVALIDCALL;
    m_inScene = false;
    return D3D_OK;
}

// ------------------------------------------------------------------------------------------------ fixed function (stored only)

static int transformIndex(D3DTRANSFORMSTATETYPE s)
{
    DWORD v = (DWORD)s;
    return v < 512 ? (int)v : -1;
}

HRESULT Device::SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix)
{
    int i = transformIndex(State);
    if (i < 0 || !pMatrix)
        return D3DERR_INVALIDCALL;
    m_transforms[i] = *pMatrix;
    return D3D_OK;
}

HRESULT Device::GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix)
{
    int i = transformIndex(State);
    if (i < 0 || !pMatrix)
        return D3DERR_INVALIDCALL;
    *pMatrix = m_transforms[i];
    return D3D_OK;
}

HRESULT Device::MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *m)
{
    int i = transformIndex(State);
    if (i < 0 || !m)
        return D3DERR_INVALIDCALL;
    D3DMATRIX r, &a = m_transforms[i];
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            r.m[y][x] = m->m[y][0] * a.m[0][x] + m->m[y][1] * a.m[1][x] + m->m[y][2] * a.m[2][x] + m->m[y][3] * a.m[3][x];
    a = r;
    return D3D_OK;
}

HRESULT Device::SetViewport(const D3DVIEWPORT9 *vp)
{
    if (!vp)
        return D3DERR_INVALIDCALL;
    D3D9_TRACE("SetViewport(%lu,%lu %lux%lu z %g..%g)", (unsigned long)vp->X, (unsigned long)vp->Y,
               (unsigned long)vp->Width, (unsigned long)vp->Height, vp->MinZ, vp->MaxZ);
    m_viewport = *vp;
    m_viewportDirty = true;
    return D3D_OK;
}

HRESULT Device::GetViewport(D3DVIEWPORT9 *vp)
{
    if (!vp)
        return D3DERR_INVALIDCALL;
    *vp = m_viewport;
    return D3D_OK;
}

HRESULT Device::SetMaterial(const D3DMATERIAL9 *m)
{
    if (m)
        m_material = *m;
    return D3D_OK;
}

HRESULT Device::GetMaterial(D3DMATERIAL9 *m)
{
    if (m)
        *m = m_material;
    return D3D_OK;
}

HRESULT Device::SetLight(DWORD, const D3DLIGHT9 *) { return D3D_OK; }
HRESULT Device::GetLight(DWORD, D3DLIGHT9 *) { return D3DERR_INVALIDCALL; }
HRESULT Device::LightEnable(DWORD, BOOL) { return D3D_OK; }

HRESULT Device::GetLightEnable(DWORD, BOOL *p)
{
    if (p)
        *p = FALSE;
    return D3D_OK;
}

HRESULT Device::SetClipPlane(DWORD Index, const float *pPlane)
{
    if (Index >= 6 || !pPlane)
        return D3DERR_INVALIDCALL;
    memcpy(m_clipPlanes[Index], pPlane, sizeof(float) * 4);
    D3D9_WARN_ONCE("user clip planes are not supported (MaxUserClipPlanes = 0)");
    return D3D_OK;
}

HRESULT Device::GetClipPlane(DWORD Index, float *pPlane)
{
    if (Index >= 6 || !pPlane)
        return D3DERR_INVALIDCALL;
    memcpy(pPlane, m_clipPlanes[Index], sizeof(float) * 4);
    return D3D_OK;
}

// ------------------------------------------------------------------------------------------------ render / sampler states

HRESULT Device::SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
{
    if ((DWORD)State >= 256)
        return D3DERR_INVALIDCALL;
    D3D9_TRACE("SetRenderState(%s, 0x%lx)", renderStateName(State), (unsigned long)Value);
    if (m_rs[State] == Value)
        return D3D_OK;
    m_rs[State] = Value;
    switch (State) {
    case D3DRS_FILLMODE:
        if (Value != D3DFILL_SOLID)
            D3D9_WARN_ONCE("D3DRS_FILLMODE wireframe/point is not supported by WebGL2: drawing solid");
        break;
    case D3DRS_ADAPTIVETESS_Y:
        if (Value == D3D9SHIM_FOURCC_ATOC || Value == D3D9SHIM_FOURCC_SSAA)
            D3D9_WARN_ONCE("ATOC/SSAA driver hack via D3DRS_ADAPTIVETESS_Y ignored");
        break;
    case D3DRS_SRGBWRITEENABLE:
        if (Value)
            D3D9_WARN_ONCE("D3DRS_SRGBWRITEENABLE is not supported");
        break;
    case D3DRS_CLIPPLANEENABLE:
        if (Value)
            D3D9_WARN_ONCE("D3DRS_CLIPPLANEENABLE is not supported");
        break;
    default:
        break;
    }
    m_rasterDirty = true;
    return D3D_OK;
}

HRESULT Device::GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue)
{
    if ((DWORD)State >= 256 || !pValue)
        return D3DERR_INVALIDCALL;
    *pValue = m_rs[State];
    return D3D_OK;
}

HRESULT StateBlock::Capture()
{
    D3D9_WARN_ONCE("state blocks are not implemented (Capture/Apply are no-ops)");
    return D3D_OK;
}

HRESULT StateBlock::Apply() { return D3D_OK; }

HRESULT Device::CreateStateBlock(D3DSTATEBLOCKTYPE, IDirect3DStateBlock9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    D3D9_WARN_ONCE("state blocks are not implemented (Capture/Apply are no-ops)");
    *pp = new StateBlock(this);
    return D3D_OK;
}

HRESULT Device::BeginStateBlock() { return D3D_OK; }

HRESULT Device::EndStateBlock(IDirect3DStateBlock9 **pp) { return CreateStateBlock(D3DSBT_ALL, pp); }

HRESULT Device::SetClipStatus(const D3DCLIPSTATUS9 *p)
{
    if (p)
        m_clipStatus = *p;
    return D3D_OK;
}

HRESULT Device::GetClipStatus(D3DCLIPSTATUS9 *p)
{
    if (p)
        *p = m_clipStatus;
    return D3D_OK;
}

HRESULT Device::GetTexture(DWORD Stage, IDirect3DBaseTexture9 **pp)
{
    int slot = samplerSlot(Stage);
    if (slot < 0 || !pp)
        return D3DERR_INVALIDCALL;
    *pp = m_textures[slot];
    if (*pp)
        (*pp)->AddRef();
    return D3D_OK;
}

HRESULT Device::SetTexture(DWORD Stage, IDirect3DBaseTexture9 *pTexture)
{
    int slot = samplerSlot(Stage);
    if (slot < 0)
        return D3DERR_INVALIDCALL;
    D3D9_TRACE("SetTexture(%lu, %p)", (unsigned long)Stage, (void *)pTexture);
    if (m_textures[slot] == pTexture)
        return D3D_OK;
    m_textures[slot] = pTexture;
    m_textureStorage[slot] = textureStorage(pTexture);
    m_samplerDirtyMask |= 1u << slot;
    if (slot >= kMaxSamplers && pTexture)
        D3D9_WARN_ONCE("vertex texture fetch / displacement samplers are not supported");
    return D3D_OK;
}

HRESULT Device::GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue)
{
    if (Stage >= 8 || (DWORD)Type > 32 || !pValue)
        return D3DERR_INVALIDCALL;
    *pValue = m_tss[Stage][Type];
    return D3D_OK;
}

HRESULT Device::SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
{
    if (Stage >= 8 || (DWORD)Type > 32)
        return D3DERR_INVALIDCALL;
    m_tss[Stage][Type] = Value;
    return D3D_OK;
}

HRESULT Device::GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD *pValue)
{
    int slot = samplerSlot(Sampler);
    if (slot < 0 || (DWORD)Type >= 14 || !pValue)
        return D3DERR_INVALIDCALL;
    *pValue = m_ss[slot][Type];
    return D3D_OK;
}

HRESULT Device::SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value)
{
    int slot = samplerSlot(Sampler);
    if (slot < 0 || (DWORD)Type >= 14)
        return D3DERR_INVALIDCALL;
    D3D9_TRACE("SetSamplerState(%lu, %s, %lu)", (unsigned long)Sampler, samplerStateName(Type), (unsigned long)Value);
    if (m_ss[slot][Type] == Value)
        return D3D_OK;
    m_ss[slot][Type] = Value;
    m_samplerDirtyMask |= 1u << slot;
    switch (Type) {
    case D3DSAMP_MIPMAPLODBIAS:
        if (Value)
            D3D9_WARN_ONCE("D3DSAMP_MIPMAPLODBIAS is not supported by WebGL2 samplers: ignored");
        break;
    case D3DSAMP_SRGBTEXTURE:
        if (Value)
            D3D9_WARN_ONCE("D3DSAMP_SRGBTEXTURE is not supported: sampling without sRGB decode");
        break;
    case D3DSAMP_ADDRESSU:
    case D3DSAMP_ADDRESSV:
    case D3DSAMP_ADDRESSW:
        if (Value == D3DTADDRESS_BORDER)
            D3D9_WARN_ONCE("D3DTADDRESS_BORDER is not supported by WebGL2: using clamp");
        break;
    default:
        break;
    }
    return D3D_OK;
}

HRESULT Device::ValidateDevice(DWORD *pNumPasses)
{
    if (pNumPasses)
        *pNumPasses = 1;
    return D3D_OK;
}

HRESULT Device::SetPaletteEntries(UINT, const struct tagPALETTEENTRY *) { return D3DERR_NOTAVAILABLE; }
HRESULT Device::GetPaletteEntries(UINT, struct tagPALETTEENTRY *) { return D3DERR_NOTAVAILABLE; }
HRESULT Device::SetCurrentTexturePalette(UINT) { return D3DERR_NOTAVAILABLE; }
HRESULT Device::GetCurrentTexturePalette(UINT *) { return D3DERR_NOTAVAILABLE; }

HRESULT Device::SetScissorRect(const RECT *pRect)
{
    if (!pRect)
        return D3DERR_INVALIDCALL;
    D3D9_TRACE("SetScissorRect(%ld,%ld,%ld,%ld)", (long)pRect->left, (long)pRect->top, (long)pRect->right,
               (long)pRect->bottom);
    m_scissor = *pRect;
    m_rasterDirty = true;
    return D3D_OK;
}

HRESULT Device::GetScissorRect(RECT *pRect)
{
    if (!pRect)
        return D3DERR_INVALIDCALL;
    *pRect = m_scissor;
    return D3D_OK;
}

HRESULT Device::SetSoftwareVertexProcessing(BOOL) { return D3D_OK; }
HRESULT Device::SetNPatchMode(float) { return D3D_OK; }

HRESULT Device::ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9 *, IDirect3DVertexDeclaration9 *, DWORD)
{
    D3D9_WARN_ONCE("ProcessVertices is not supported");
    return D3DERR_NOTAVAILABLE;
}

// ------------------------------------------------------------------------------------------------ declarations, shaders, constants

HRESULT Device::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *pVertexElements, IDirect3DVertexDeclaration9 **pp)
{
    if (!pVertexElements || !pp)
        return D3DERR_INVALIDCALL;
    const UINT n = declElementCount(pVertexElements);
    for (UINT i = 0; i < n; ++i) {
        if (!declTypeInfo(pVertexElements[i].Type).type) {
            D3D9_WARN("CreateVertexDeclaration: unsupported element type %u", pVertexElements[i].Type);
            return D3DERR_INVALIDCALL;
        }
    }
    *pp = new VertexDecl(this, pVertexElements, n);
    return D3D_OK;
}

HRESULT Device::SetVertexDeclaration(IDirect3DVertexDeclaration9 *pDecl)
{
    D3D9_TRACE("SetVertexDeclaration(%p)", (void *)pDecl);
    VertexDecl *d = static_cast<VertexDecl *>(pDecl);
    if (d == m_decl)
        return D3D_OK;
    m_decl = d;
    m_attribsDirty = m_bgraDirty = true;
    return D3D_OK;
}

HRESULT Device::GetVertexDeclaration(IDirect3DVertexDeclaration9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_decl;
    if (m_decl)
        m_decl->AddRef();
    return D3D_OK;
}

HRESULT Device::SetFVF(DWORD FVF)
{
    m_fvf = FVF;
    if (FVF)
        D3D9_WARN_ONCE("SetFVF / fixed-function vertex processing is not supported (use declarations + shaders)");
    return D3D_OK;
}

HRESULT Device::GetFVF(DWORD *pFVF)
{
    if (!pFVF)
        return D3DERR_INVALIDCALL;
    *pFVF = m_fvf;
    return D3D_OK;
}

HRESULT Device::CreateVertexShader(const DWORD *pFunction, IDirect3DVertexShader9 **pp)
{
    if (!pFunction || !pp)
        return D3DERR_INVALIDCALL;
    TranslatedShader tr = translateShader(pFunction, 0, nullptr);
    if (tr.byteLength == 0 || tr.stage != ShaderStage::Vertex) {
        D3D9_ERROR("CreateVertexShader: not vertex shader bytecode (%s)", tr.errors.c_str());
        return D3DERR_INVALIDCALL;
    }
    if (!tr.ok) {
        // Keep going: the object exists, draws that use it are skipped.
        D3D9_ERROR("CreateVertexShader: translation failed (draws using it will be skipped): %s", tr.errors.c_str());
        ++g_stats.shadersFailed;
    }
    if (shaderDumpEnabled(!tr.ok)) {
        const uint64_t h = shaderHash(pFunction, tr.byteLength);
        shaderDump("vs", h, "bin", pFunction, tr.byteLength);
        shaderDump("vs", h, tr.ok ? "glsl" : "log", tr.ok ? tr.glsl.data() : tr.errors.data(),
                   tr.ok ? tr.glsl.size() : tr.errors.size());
    }
    *pp = new VertexShader(this, pFunction, std::move(tr));
    return D3D_OK;
}

HRESULT Device::SetVertexShader(IDirect3DVertexShader9 *pShader)
{
    D3D9_TRACE("SetVertexShader(%p)", (void *)pShader);
    VertexShader *s = static_cast<VertexShader *>(pShader);
    if (s == m_vs)
        return D3D_OK;
    m_vs = s;
    m_bgraDirty = true;
    return D3D_OK;
}

HRESULT Device::GetVertexShader(IDirect3DVertexShader9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_vs;
    if (m_vs)
        m_vs->AddRef();
    return D3D_OK;
}

template <class T>
static HRESULT setConsts(T *dst, UINT capacity, UINT start, const T *src, UINT count, UINT width, uint32_t &version)
{
    if (!src || start + count > capacity)
        return D3DERR_INVALIDCALL;
    if (memcmp(dst + start * width, src, sizeof(T) * width * count) != 0) {
        memcpy(dst + start * width, src, sizeof(T) * width * count);
        ++version;
    }
    return D3D_OK;
}

template <class T>
static HRESULT getConsts(const T *src, UINT capacity, UINT start, T *dst, UINT count, UINT width)
{
    if (!dst || start + count > capacity)
        return D3DERR_INVALIDCALL;
    memcpy(dst, src + start * width, sizeof(T) * width * count);
    return D3D_OK;
}

HRESULT Device::SetVertexShaderConstantF(UINT s, const float *d, UINT n)
{
    return setConsts(&m_vsF[0][0], kVSConstF, s, d, n, 4, m_vsFVersion);
}
HRESULT Device::GetVertexShaderConstantF(UINT s, float *d, UINT n) { return getConsts(&m_vsF[0][0], kVSConstF, s, d, n, 4); }
HRESULT Device::SetVertexShaderConstantI(UINT s, const int *d, UINT n)
{
    return setConsts(&m_vsI[0][0], kConstI, s, d, n, 4, m_vsIVersion);
}
HRESULT Device::GetVertexShaderConstantI(UINT s, int *d, UINT n) { return getConsts(&m_vsI[0][0], kConstI, s, d, n, 4); }
HRESULT Device::SetVertexShaderConstantB(UINT s, const BOOL *d, UINT n)
{
    return setConsts(m_vsB, kConstB, s, d, n, 1, m_vsBVersion);
}
HRESULT Device::GetVertexShaderConstantB(UINT s, BOOL *d, UINT n) { return getConsts(m_vsB, kConstB, s, d, n, 1); }
HRESULT Device::SetPixelShaderConstantF(UINT s, const float *d, UINT n)
{
    return setConsts(&m_psF[0][0], kPSConstF, s, d, n, 4, m_psFVersion);
}
HRESULT Device::GetPixelShaderConstantF(UINT s, float *d, UINT n) { return getConsts(&m_psF[0][0], kPSConstF, s, d, n, 4); }
HRESULT Device::SetPixelShaderConstantI(UINT s, const int *d, UINT n)
{
    return setConsts(&m_psI[0][0], kConstI, s, d, n, 4, m_psIVersion);
}
HRESULT Device::GetPixelShaderConstantI(UINT s, int *d, UINT n) { return getConsts(&m_psI[0][0], kConstI, s, d, n, 4); }
HRESULT Device::SetPixelShaderConstantB(UINT s, const BOOL *d, UINT n)
{
    return setConsts(m_psB, kConstB, s, d, n, 1, m_psBVersion);
}
HRESULT Device::GetPixelShaderConstantB(UINT s, BOOL *d, UINT n) { return getConsts(m_psB, kConstB, s, d, n, 1); }

HRESULT Device::SetStreamSource(UINT n, IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes, UINT Stride)
{
    if (n >= (UINT)kMaxStreams)
        return D3DERR_INVALIDCALL;
    D3D9_TRACE("SetStreamSource(%u, %p, %u, %u)", n, (void *)pStreamData, OffsetInBytes, Stride);
    Stream &s = m_streams[n];
    VertexBuffer *vb = static_cast<VertexBuffer *>(pStreamData);
    if (s.vb == vb && s.offset == OffsetInBytes && s.stride == Stride)
        return D3D_OK;
    s.vb = vb;
    s.offset = OffsetInBytes;
    s.stride = Stride;
    if (Stride > 255)
        D3D9_WARN_ONCE("vertex stride %u exceeds WebGL's limit of 255 bytes", Stride);
    m_attribsDirty = true;
    return D3D_OK;
}

HRESULT Device::GetStreamSource(UINT n, IDirect3DVertexBuffer9 **pp, UINT *pOffset, UINT *pStride)
{
    if (n >= (UINT)kMaxStreams || !pp)
        return D3DERR_INVALIDCALL;
    *pp = m_streams[n].vb;
    if (*pp)
        (*pp)->AddRef();
    if (pOffset)
        *pOffset = m_streams[n].offset;
    if (pStride)
        *pStride = m_streams[n].stride;
    return D3D_OK;
}

HRESULT Device::SetStreamSourceFreq(UINT n, UINT Setting)
{
    if (n >= (UINT)kMaxStreams)
        return D3DERR_INVALIDCALL;
    m_streams[n].freq = Setting;
    if (Setting != 1)
        D3D9_WARN_ONCE("SetStreamSourceFreq (geometry instancing) is not supported");
    return D3D_OK;
}

HRESULT Device::GetStreamSourceFreq(UINT n, UINT *pSetting)
{
    if (n >= (UINT)kMaxStreams || !pSetting)
        return D3DERR_INVALIDCALL;
    *pSetting = m_streams[n].freq;
    return D3D_OK;
}

HRESULT Device::SetIndices(IDirect3DIndexBuffer9 *pIndexData)
{
    D3D9_TRACE("SetIndices(%p)", (void *)pIndexData);
    m_ib = static_cast<IndexBuffer *>(pIndexData);
    return D3D_OK;
}

HRESULT Device::GetIndices(IDirect3DIndexBuffer9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_ib;
    if (m_ib)
        m_ib->AddRef();
    return D3D_OK;
}

HRESULT Device::CreatePixelShader(const DWORD *pFunction, IDirect3DPixelShader9 **pp)
{
    if (!pFunction || !pp)
        return D3DERR_INVALIDCALL;
    TranslatedShader tr = translateShader(pFunction, 0, nullptr);
    if (tr.byteLength == 0 || tr.stage != ShaderStage::Pixel) {
        D3D9_ERROR("CreatePixelShader: not pixel shader bytecode (%s)", tr.errors.c_str());
        return D3DERR_INVALIDCALL;
    }
    if (!tr.ok) {
        D3D9_ERROR("CreatePixelShader: translation failed (draws using it will be skipped): %s", tr.errors.c_str());
        ++g_stats.shadersFailed;
    }
    if (shaderDumpEnabled(!tr.ok)) {
        const uint64_t h = shaderHash(pFunction, tr.byteLength);
        shaderDump("ps", h, "bin", pFunction, tr.byteLength);
        shaderDump("ps", h, tr.ok ? "glsl" : "log", tr.ok ? tr.glsl.data() : tr.errors.data(),
                   tr.ok ? tr.glsl.size() : tr.errors.size());
    }
    *pp = new PixelShader(this, pFunction, std::move(tr));
    return D3D_OK;
}

HRESULT Device::SetPixelShader(IDirect3DPixelShader9 *pShader)
{
    D3D9_TRACE("SetPixelShader(%p)", (void *)pShader);
    m_ps = static_cast<PixelShader *>(pShader);
    return D3D_OK;
}

HRESULT Device::GetPixelShader(IDirect3DPixelShader9 **pp)
{
    if (!pp)
        return D3DERR_INVALIDCALL;
    *pp = m_ps;
    if (m_ps)
        m_ps->AddRef();
    return D3D_OK;
}

HRESULT Device::DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) { return D3DERR_NOTAVAILABLE; }
HRESULT Device::DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) { return D3DERR_NOTAVAILABLE; }
HRESULT Device::DeletePatch(UINT) { return D3DERR_NOTAVAILABLE; }

HRESULT Device::CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9 **pp)
{
    switch (Type) {
    case D3DQUERYTYPE_EVENT:
    case D3DQUERYTYPE_OCCLUSION:
    case D3DQUERYTYPE_TIMESTAMP:
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        break;
    default:
        return D3DERR_NOTAVAILABLE;
    }
    if (!pp)
        return D3D_OK; // support check
    *pp = new Query(this, Type);
    return D3D_OK;
}

} // namespace d3d9shim
