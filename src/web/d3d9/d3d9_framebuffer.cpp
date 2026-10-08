// d3d9_framebuffer.cpp - Clear, StretchRect, ColorFill, readbacks (GetRenderTargetData / GetFrontBufferData),
// UpdateSurface / UpdateTexture and the Present blit.
//
// Every render target is a GL texture or renderbuffer rendered top-down (row 0 = D3D top row), so D3D rectangles
// are GL window rectangles and blits/readbacks need no flip; only Present flips into the canvas.
#include "d3d9_internal.h"

#include <algorithm>
#include <stdlib.h>

namespace d3d9shim {

static RECT intersect(const RECT &a, const RECT &b)
{
    RECT r = {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
              std::min(a.bottom, b.bottom)};
    if (r.right < r.left)
        r.right = r.left;
    if (r.bottom < r.top)
        r.bottom = r.top;
    return r;
}

static bool emptyRect(const RECT &r) { return r.right <= r.left || r.bottom <= r.top; }

HRESULT Device::Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil)
{
    D3D9_TRACE("Clear(%lu rects, flags 0x%lx, color 0x%08lx, z %g, stencil %lu)", (unsigned long)Count,
               (unsigned long)Flags, (unsigned long)Color, Z, (unsigned long)Stencil);
    if (!onDeviceThread()) {
        D3D9_WARN_ONCE("Clear called off the device thread: ignored");
        return D3D_OK;
    }
    processDeferred();
    bindRenderTargets();
    GLbitfield bits = 0;
    if ((Flags & D3DCLEAR_TARGET) && m_rt[0])
        bits |= GL_COLOR_BUFFER_BIT;
    if ((Flags & D3DCLEAR_ZBUFFER) && m_ds)
        bits |= GL_DEPTH_BUFFER_BIT;
    if ((Flags & D3DCLEAR_STENCIL) && m_ds && m_ds->formatInfo().internalFormat != GL_DEPTH_COMPONENT16 &&
        m_ds->formatInfo().internalFormat != GL_DEPTH_COMPONENT32F)
        bits |= GL_STENCIL_BUFFER_BIT;
    if (!bits)
        return D3D_OK;
    setFullWriteMasks();
    if (bits & GL_COLOR_BUFFER_BIT)
        glClearColor(((Color >> 16) & 0xff) / 255.f, ((Color >> 8) & 0xff) / 255.f, (Color & 0xff) / 255.f,
                     ((Color >> 24) & 0xff) / 255.f);
    if (bits & GL_DEPTH_BUFFER_BIT)
        glClearDepthf(Z < 0.f ? 0.f : (Z > 1.f ? 1.f : Z));
    if (bits & GL_STENCIL_BUFFER_BIT)
        glClearStencil((GLint)(Stencil & 0xff));

    Surface *bounds = m_rt[0] ? m_rt[0] : m_ds;
    RECT base = {(LONG)m_viewport.X, (LONG)m_viewport.Y, (LONG)(m_viewport.X + m_viewport.Width),
                 (LONG)(m_viewport.Y + m_viewport.Height)};
    base = intersect(base, RECT{0, 0, (LONG)bounds->width(), (LONG)bounds->height()});
    if (m_rs[D3DRS_SCISSORTESTENABLE])
        base = intersect(base, m_scissor);
    setScissorTest(true);
    if (Count && pRects) {
        for (DWORD i = 0; i < Count; ++i) {
            RECT r = intersect(base, RECT{pRects[i].x1, pRects[i].y1, pRects[i].x2, pRects[i].y2});
            if (emptyRect(r))
                continue;
            setScissorBox(r.left, r.top, r.right - r.left, r.bottom - r.top);
            glClear(bits);
        }
    } else if (!emptyRect(base)) {
        setScissorBox(base.left, base.top, base.right - base.left, base.bottom - base.top);
        glClear(bits);
    }
    m_rasterDirty = true;
    D3D9_GLCHECK("Clear");
    return D3D_OK;
}

HRESULT Device::StretchRect(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect, IDirect3DSurface9 *pDestSurface,
                            const RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter)
{
    D3D9_TRACE("StretchRect(%p -> %p, filter %d)", (void *)pSourceSurface, (void *)pDestSurface, (int)Filter);
    if (!pSourceSurface || !pDestSurface)
        return D3DERR_INVALIDCALL;
    if (!onDeviceThread()) {
        D3D9_WARN_ONCE("StretchRect called off the device thread: ignored");
        return D3DERR_INVALIDCALL;
    }
    Surface *s = static_cast<Surface *>(pSourceSurface);
    Surface *d = static_cast<Surface *>(pDestSurface);
    if (s == d)
        return D3DERR_INVALIDCALL;
    if ((s->storage() && s->storage()->cpuOnly) || (d->storage() && d->storage()->cpuOnly))
        return D3DERR_INVALIDCALL;
    processDeferred();
    const RECT sr = pSourceRect ? *pSourceRect : RECT{0, 0, (LONG)s->width(), (LONG)s->height()};
    const RECT dr = pDestRect ? *pDestRect : RECT{0, 0, (LONG)d->width(), (LONG)d->height()};
    if (emptyRect(sr) || emptyRect(dr))
        return D3D_OK;
    const bool depth = s->isDepth();
    if (depth != d->isDepth())
        return D3DERR_INVALIDCALL;
    s->ensureGL();
    d->ensureGL();
    GLuint readFbo = depth ? fboFor(nullptr, s) : fboFor(s, nullptr);
    GLuint drawFbo = depth ? fboFor(nullptr, d) : fboFor(d, nullptr);
    bindReadFbo(readFbo);
    bindDrawFbo(drawFbo);
    setScissorTest(false);
    setFullWriteMasks();
    const bool scaled = (sr.right - sr.left) != (dr.right - dr.left) || (sr.bottom - sr.top) != (dr.bottom - dr.top);
    GLenum filter = GL_NEAREST;
    if (!depth && scaled && Filter != D3DTEXF_NONE && Filter != D3DTEXF_POINT &&
        (s->formatInfo().flags & FMT_FILTERABLE))
        filter = GL_LINEAR;
    GLbitfield mask = GL_COLOR_BUFFER_BIT;
    if (depth) {
        mask = GL_DEPTH_BUFFER_BIT;
        if (s->formatInfo().flags & FMT_STENCIL)
            mask |= GL_STENCIL_BUFFER_BIT;
        filter = GL_NEAREST;
    }
    glBlitFramebuffer(sr.left, sr.top, sr.right, sr.bottom, dr.left, dr.top, dr.right, dr.bottom, mask, filter);
    D3D9_GLCHECK("StretchRect");
    m_fboDirty = true;
    m_rasterDirty = true;
    return D3D_OK;
}

HRESULT Device::ColorFill(IDirect3DSurface9 *pSurface, const RECT *pRect, D3DCOLOR color)
{
    if (!pSurface || !onDeviceThread())
        return D3DERR_INVALIDCALL;
    Surface *s = static_cast<Surface *>(pSurface);
    if (s->isDepth() || (s->storage() && s->storage()->cpuOnly))
        return D3DERR_INVALIDCALL;
    s->ensureGL();
    bindDrawFbo(fboFor(s, nullptr));
    RECT r = pRect ? *pRect : RECT{0, 0, (LONG)s->width(), (LONG)s->height()};
    r = intersect(r, RECT{0, 0, (LONG)s->width(), (LONG)s->height()});
    setFullWriteMasks();
    setScissorTest(true);
    setScissorBox(r.left, r.top, r.right - r.left, r.bottom - r.top);
    glClearColor(((color >> 16) & 0xff) / 255.f, ((color >> 8) & 0xff) / 255.f, (color & 0xff) / 255.f,
                 ((color >> 24) & 0xff) / 255.f);
    glClear(GL_COLOR_BUFFER_BIT);
    m_fboDirty = true;
    m_rasterDirty = true;
    return D3D_OK;
}

bool Device::readSurfacePixels(Surface *src, const RECT &r, uint8_t *dst, size_t dstPitch, D3DFORMAT dstFmt)
{
    if (src->isDepth()) {
        D3D9_WARN_ONCE("reading back depth surfaces is not supported");
        return false;
    }
    src->ensureGL();
    bindReadFbo(fboFor(src, nullptr));
    const UINT w = (UINT)(r.right - r.left), h = (UINT)(r.bottom - r.top);
    // Synchronous glReadPixels: stalls until the GPU has finished (rare paths: screenshots, probes, mip gen).
    if (src->formatInfo().flags & FMT_FLOAT) {
        std::vector<float> tmp((size_t)w * h * 4);
        glReadPixels(r.left, r.top, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_FLOAT, tmp.data());
        if (!convertRGBAFloatToD3D(dstFmt, tmp.data(), (size_t)w * 4, dst, dstPitch, w, h)) {
            D3D9_WARN_ONCE("readback: no float conversion to %s", formatName(dstFmt));
            return false;
        }
    } else {
        std::vector<uint8_t> tmp((size_t)w * h * 4);
        glReadPixels(r.left, r.top, (GLsizei)w, (GLsizei)h, GL_RGBA, GL_UNSIGNED_BYTE, tmp.data());
        if (!convertRGBA8ToD3D(dstFmt, tmp.data(), (size_t)w * 4, dst, dstPitch, w, h)) {
            D3D9_WARN_ONCE("readback: no 8-bit conversion to %s", formatName(dstFmt));
            return false;
        }
    }
    return true;
}

HRESULT Device::GetRenderTargetData(IDirect3DSurface9 *pRenderTarget, IDirect3DSurface9 *pDestSurface)
{
    D3D9_TRACE("GetRenderTargetData(%p -> %p)", (void *)pRenderTarget, (void *)pDestSurface);
    if (!pRenderTarget || !pDestSurface || !onDeviceThread())
        return D3DERR_INVALIDCALL;
    Surface *s = static_cast<Surface *>(pRenderTarget);
    Surface *d = static_cast<Surface *>(pDestSurface);
    if (!d->storage() || !d->storage()->cpuOnly)
        return D3DERR_INVALIDCALL;
    const UINT w = std::min(s->width(), d->width()), h = std::min(s->height(), d->height());
    D3DLOCKED_RECT lr;
    if (FAILED(d->LockRect(&lr, nullptr, 0)))
        return D3DERR_INVALIDCALL;
    bool ok = readSurfacePixels(s, RECT{0, 0, (LONG)w, (LONG)h}, (uint8_t *)lr.pBits, (size_t)lr.Pitch, d->format());
    d->UnlockRect();
    m_fboDirty = true;
    return ok ? D3D_OK : D3DERR_INVALIDCALL;
}

HRESULT Device::GetFrontBufferData(UINT, IDirect3DSurface9 *pDestSurface)
{
    if (!pDestSurface || !onDeviceThread())
        return D3DERR_INVALIDCALL;
    // There is no readable front buffer on the web; the back buffer still holds the last presented frame unless
    // drawing has started again (documented approximation).
    Surface *bb = m_swapChain->backBuffer();
    Surface *d = static_cast<Surface *>(pDestSurface);
    if (!d->storage() || !d->storage()->cpuOnly)
        return D3DERR_INVALIDCALL;
    const UINT w = std::min(bb->width(), d->width()), h = std::min(bb->height(), d->height());
    D3DLOCKED_RECT lr;
    if (FAILED(d->LockRect(&lr, nullptr, 0)))
        return D3DERR_INVALIDCALL;
    bool ok = readSurfacePixels(bb, RECT{0, 0, (LONG)w, (LONG)h}, (uint8_t *)lr.pBits, (size_t)lr.Pitch, d->format());
    d->UnlockRect();
    m_fboDirty = true;
    return ok ? D3D_OK : D3DERR_INVALIDCALL;
}

// Uploads a CPU image region straight into a GPU-only texture level (render targets have no CPU copy).
static void directUpload(Device *dev, TexStorage &src, UINT srcFace, UINT srcLevel, TexStorage &dst, UINT dstFace,
                         UINT dstLevel, const RECT &r, const POINT &p)
{
    dst.ensureGL(dev);
    std::lock_guard<std::mutex> g(src.mtx);
    TexStorage::Image &si = src.image(srcFace, srcLevel);
    if (si.shadow.empty())
        return;
    const UINT w = (UINT)(r.right - r.left), h = (UINT)(r.bottom - r.top);
    const FormatInfo &fi = dst.fi;
    if (fi.flags & FMT_COMPRESSED)
        return; // render targets are never compressed
    std::vector<uint8_t> tmp(uploadPitch(fi, w) * h);
    const UINT sp = src.pitch(srcLevel);
    convertForUpload(fi, si.shadow.data() + (size_t)r.top * sp + (size_t)r.left * src.fi.bytesPerBlock, sp, tmp.data(),
                     uploadPitch(fi, w), w, h);
    dev->bindTextureForEdit(dst.target, dst.tex);
    glTexSubImage2D(dst.faceTarget(dstFace), (GLint)dstLevel, p.x, p.y, (GLsizei)w, (GLsizei)h, fi.format, fi.type,
                    tmp.data());
    g_stats.textureUploadBytes += tmp.size();
}

HRESULT Device::UpdateSurface(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect,
                              IDirect3DSurface9 *pDestinationSurface, const POINT *pDestPoint)
{
    if (!pSourceSurface || !pDestinationSurface)
        return D3DERR_INVALIDCALL;
    Surface *s = static_cast<Surface *>(pSourceSurface);
    Surface *d = static_cast<Surface *>(pDestinationSurface);
    if (!s->storage() || !d->storage() || s->format() != d->format())
        return D3DERR_INVALIDCALL;
    TexStorage &ss = *s->storage(), &ds = *d->storage();
    const RECT r = pSourceRect ? *pSourceRect : RECT{0, 0, (LONG)s->width(), (LONG)s->height()};
    const POINT p = pDestPoint ? *pDestPoint : POINT{0, 0};
    if (ds.renderTarget) {
        if (!onDeviceThread())
            return D3DERR_INVALIDCALL;
        directUpload(this, ss, s->face(), s->level(), ds, d->face(), d->level(), r, p);
    } else {
        ds.copyImageFrom(ss, s->face(), s->level(), d->face(), d->level(), &r, &p);
    }
    return D3D_OK;
}

HRESULT Device::UpdateTexture(IDirect3DBaseTexture9 *pSourceTexture, IDirect3DBaseTexture9 *pDestinationTexture)
{
    TexStorage *s = textureStorage(pSourceTexture), *d = textureStorage(pDestinationTexture);
    if (!s || !d || s->type != d->type || s->format != d->format || !s->cpuOnly)
        return D3DERR_INVALIDCALL;
    // Match levels by size: the source may have more (larger) levels than the destination.
    UINT srcStart = 0;
    while (srcStart < s->levels && s->levelWidth(srcStart) > d->width)
        ++srcStart;
    for (UINT f = 0; f < d->faces; ++f) {
        for (UINT l = 0; l < d->levels && srcStart + l < s->levels; ++l) {
            if (d->type == D3DRTYPE_VOLUMETEXTURE) {
                std::lock_guard<std::mutex> g1(s->mtx);
                std::lock_guard<std::mutex> g2(d->mtx);
                TexStorage::Image &si = s->image(f, srcStart + l), &di = d->image(f, l);
                di.shadow = si.shadow;
                di.dx0 = di.dy0 = di.dz0 = 0;
                di.dx1 = d->levelWidth(l), di.dy1 = d->levelHeight(l), di.dz1 = d->levelDepth(l);
                di.dirty = true;
                d->anyDirty = true;
            } else if (d->renderTarget) {
                if (!onDeviceThread())
                    return D3DERR_INVALIDCALL;
                directUpload(this, *s, f, srcStart + l, *d, f, l,
                             RECT{0, 0, (LONG)d->levelWidth(l), (LONG)d->levelHeight(l)}, POINT{0, 0});
            } else {
                d->copyImageFrom(*s, f, srcStart + l, f, l, nullptr, nullptr);
            }
        }
    }
    return D3D_OK;
}

static bool rampIsIdentity(const D3DGAMMARAMP &g)
{
    for (int i = 0; i < 256; ++i) {
        const int want = i * 257;
        if (abs((int)g.red[i] - want) > 128 || abs((int)g.green[i] - want) > 128 || abs((int)g.blue[i] - want) > 128)
            return false;
    }
    return true;
}

static GLuint compilePresentShader(GLenum type, const char *src)
{
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0};
        glGetShaderInfoLog(sh, sizeof(log) - 1, nullptr, log);
        D3D9_ERROR("present shader: %s", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

// Back buffer -> canvas through the gamma ramp (a 256-entry RGB LUT), flipping vertically.
bool Device::presentWithGamma(Surface *bb)
{
    if (m_presentProgFailed)
        return false;
    if (!m_presentProg) {
        static const char *vs = "#version 300 es\n"
                                "out vec2 uv;\n"
                                "void main() {\n"
                                "  vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));\n"
                                "  uv = vec2(p.x, 1.0 - p.y);\n"
                                "  gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);\n"
                                "}\n";
        static const char *fs = "#version 300 es\n"
                                "precision highp float;\n"
                                "uniform sampler2D src;\n"
                                "uniform sampler2D lut;\n"
                                "in vec2 uv;\n"
                                "out vec4 o;\n"
                                "void main() {\n"
                                "  vec3 c = clamp(texture(src, uv).rgb, 0.0, 1.0) * (255.0 / 256.0) + (0.5 / 256.0);\n"
                                "  o = vec4(texture(lut, vec2(c.r, 0.5)).r, texture(lut, vec2(c.g, 0.5)).g,\n"
                                "           texture(lut, vec2(c.b, 0.5)).b, 1.0);\n"
                                "}\n";
        GLuint v = compilePresentShader(GL_VERTEX_SHADER, vs), f = compilePresentShader(GL_FRAGMENT_SHADER, fs);
        GLuint p = (v && f) ? glCreateProgram() : 0;
        if (p) {
            glAttachShader(p, v);
            glAttachShader(p, f);
            glLinkProgram(p);
            GLint ok = 0;
            glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) {
                glDeleteProgram(p);
                p = 0;
            }
        }
        if (v)
            glDeleteShader(v);
        if (f)
            glDeleteShader(f);
        if (!p) {
            m_presentProgFailed = true;
            D3D9_WARN("gamma present program failed: gamma ramp ignored");
            return false;
        }
        m_presentProg = p;
        m_presentLocSrc = glGetUniformLocation(p, "src");
        m_presentLocLut = glGetUniformLocation(p, "lut");
    }
    if (!m_gammaLut) {
        glGenTextures(1, &m_gammaLut);
        bindTextureForEdit(GL_TEXTURE_2D, m_gammaLut);
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 256, 1);
        m_gammaLutValid = false;
    }
    if (!m_gammaLutValid || memcmp(&m_gammaUploaded, &m_gamma, sizeof(m_gamma)) != 0) {
        uint8_t lut[256 * 4];
        for (int i = 0; i < 256; ++i) {
            lut[i * 4 + 0] = (uint8_t)(m_gamma.red[i] >> 8);
            lut[i * 4 + 1] = (uint8_t)(m_gamma.green[i] >> 8);
            lut[i * 4 + 2] = (uint8_t)(m_gamma.blue[i] >> 8);
            lut[i * 4 + 3] = 255;
        }
        bindTextureForEdit(GL_TEXTURE_2D, m_gammaLut);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, lut);
        m_gammaUploaded = m_gamma;
        m_gammaLutValid = true;
    }
    const GLint w = (GLint)bb->width(), h = (GLint)bb->height();
    bindDrawFbo(0);
    setFullWriteMasks();
    setScissorTest(false);
    setCap(GL_BLEND, m_cache.blend, false);
    setCap(GL_DEPTH_TEST, m_cache.depthTest, false);
    setCap(GL_STENCIL_TEST, m_cache.stencilTest, false);
    setCap(GL_CULL_FACE, m_cache.cull, false);
    setCap(GL_POLYGON_OFFSET_FILL, m_cache.polygonOffset, false);
    if (m_cache.viewport[0] != 0 || m_cache.viewport[1] != 0 || m_cache.viewport[2] != w || m_cache.viewport[3] != h) {
        glViewport(0, 0, w, h);
        m_cache.viewport[0] = 0, m_cache.viewport[1] = 0, m_cache.viewport[2] = w, m_cache.viewport[3] = h;
    }
    for (int i = 0; i < 16; ++i)
        if (m_cache.attribs[i].enabled) {
            glDisableVertexAttribArray((GLuint)i);
            m_cache.attribs[i].enabled = false;
        }
    useProgram(m_presentProg);
    glUniform1i(m_presentLocSrc, 0);
    glUniform1i(m_presentLocLut, 1);
    SamplerParams sp = {GL_NEAREST, GL_NEAREST, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE, 1.f, 0.f};
    const GLuint s = samplerObjectFor(sp);
    bindTextureUnit(0, GL_TEXTURE_2D, bb->storage()->tex);
    bindTextureUnit(1, GL_TEXTURE_2D, m_gammaLut);
    bindSamplerUnit(0, s);
    bindSamplerUnit(1, s);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    D3D9_GLCHECK("Present (gamma)");
    m_samplerDirtyMask |= 3u;
    m_unitSamplerTex[0] = m_unitSamplerTex[1] = nullptr;
    m_viewportDirty = true;
    m_rasterDirty = true;
    m_fboDirty = true;
    return true;
}

void Device::presentBlit()
{
    Surface *bb = m_swapChain->backBuffer();
    if (!bb)
        return;
    bb->ensureGL();
    if (m_gammaDirty || m_gammaLutValid) {
        m_gammaDirty = false;
        if (rampIsIdentity(m_gamma))
            m_gammaLutValid = false; // back to the plain blit
        else if (presentWithGamma(bb))
            return;
    }
    bindReadFbo(fboFor(bb, nullptr));
    bindDrawFbo(0);
    setScissorTest(false);
    setFullWriteMasks();
    const GLint w = (GLint)bb->width(), h = (GLint)bb->height();
    // Flip: our row 0 is the top of the image, the canvas' row 0 is the bottom.
    glBlitFramebuffer(0, 0, w, h, 0, h, w, 0, GL_COLOR_BUFFER_BIT, GL_NEAREST);
    D3D9_GLCHECK("Present");
    m_fboDirty = true;
    m_rasterDirty = true;
}

} // namespace d3d9shim
