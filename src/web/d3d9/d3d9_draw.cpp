// d3d9_draw.cpp - draw calls and the GL state machinery: state cache, program cache (VS x PS x vertex-color
// swizzle), constant upload, vertex attribute setup, samplers, raster state.
#include "d3d9_internal.h"

#include <algorithm>
#include <stdio.h>

namespace d3d9shim {

static constexpr int kScratchUnit = 31; // WebGL2 guarantees 32 combined texture units

// ------------------------------------------------------------------------------------------------ GL state cache

void GLStateCache::invalidate()
{
    program = arrayBuffer = elementBuffer = drawFbo = readFbo = renderbuffer = ~0u;
    activeTexture = 0;
    for (int i = 0; i < 32; ++i)
        tex2D[i] = texCube[i] = tex3D[i] = sampler[i] = ~0u;
    blend = depthTest = stencilTest = cull = scissor = polygonOffset = -1;
    blendSrcRGB = blendDstRGB = blendSrcA = blendDstA = blendEqRGB = blendEqA = 0;
    for (float &c : blendColor)
        c = -1.f;
    depthFunc = 0;
    depthMask = -1;
    for (auto &m : colorMask)
        m = -1;
    cullFace = 0;
    for (int i = 0; i < 2; ++i) {
        stencilFunc[i] = 0;
        stencilRef[i] = -1;
        stencilValueMask[i] = 0;
        stencilWriteMask[i] = 1;
        stencilFail[i] = stencilZFail[i] = stencilPass[i] = 0;
    }
    for (int i = 0; i < 4; ++i)
        viewport[i] = scissorBox[i] = -1;
    depthRange[0] = depthRange[1] = -1.f;
    polyFactor = polyUnits = -1e30f;
    for (auto &a : attribs)
        a.valid = false;
}

void Device::bindArrayBuffer(GLuint b)
{
    if (m_cache.arrayBuffer != b) {
        glBindBuffer(GL_ARRAY_BUFFER, b);
        m_cache.arrayBuffer = b;
    }
}

void Device::bindElementBuffer(GLuint b)
{
    if (m_cache.elementBuffer != b) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, b);
        m_cache.elementBuffer = b;
    }
}

void Device::bindDrawFbo(GLuint f)
{
    if (m_cache.drawFbo != f) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, f);
        m_cache.drawFbo = f;
    }
}

void Device::bindReadFbo(GLuint f)
{
    if (m_cache.readFbo != f) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, f);
        m_cache.readFbo = f;
    }
}

static GLuint *unitSlot(GLStateCache &c, int unit, GLenum target)
{
    switch (target) {
    case GL_TEXTURE_CUBE_MAP: return &c.texCube[unit];
    case GL_TEXTURE_3D: return &c.tex3D[unit];
    default: return &c.tex2D[unit];
    }
}

void Device::bindTextureUnit(int unit, GLenum target, GLuint tex)
{
    GLuint *slot = unitSlot(m_cache, unit, target);
    if (*slot == tex)
        return;
    if (m_cache.activeTexture != (GLenum)(GL_TEXTURE0 + unit)) {
        glActiveTexture(GL_TEXTURE0 + unit);
        m_cache.activeTexture = GL_TEXTURE0 + unit;
    }
    glBindTexture(target, tex);
    *slot = tex;
}

void Device::bindTextureForEdit(GLenum target, GLuint tex)
{
    if (m_cache.activeTexture != (GLenum)(GL_TEXTURE0 + kScratchUnit)) {
        glActiveTexture(GL_TEXTURE0 + kScratchUnit);
        m_cache.activeTexture = GL_TEXTURE0 + kScratchUnit;
    }
    GLuint *slot = unitSlot(m_cache, kScratchUnit, target);
    if (*slot != tex) {
        glBindTexture(target, tex);
        *slot = tex;
    }
}

void Device::bindSamplerUnit(int unit, GLuint s)
{
    if (m_cache.sampler[unit] != s) {
        glBindSampler((GLuint)unit, s);
        m_cache.sampler[unit] = s;
    }
}

void Device::useProgram(GLuint p)
{
    if (m_cache.program != p) {
        glUseProgram(p);
        m_cache.program = p;
    }
}

void setCap(GLenum cap, signed char &cached, bool on)
{
    if (cached != (signed char)on) {
        if (on)
            glEnable(cap);
        else
            glDisable(cap);
        cached = (signed char)on;
    }
}

void Device::setScissorTest(bool on) { setCap(GL_SCISSOR_TEST, m_cache.scissor, on); }

void Device::setScissorBox(GLint x, GLint y, GLint w, GLint h)
{
    GLint *b = m_cache.scissorBox;
    if (b[0] != x || b[1] != y || b[2] != w || b[3] != h) {
        glScissor(x, y, w, h);
        b[0] = x, b[1] = y, b[2] = w, b[3] = h;
    }
}

void Device::setFullWriteMasks()
{
    if (m_cache.colorMask[0] != 1 || m_cache.colorMask[1] != 1 || m_cache.colorMask[2] != 1 ||
        m_cache.colorMask[3] != 1) {
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        for (auto &m : m_cache.colorMask)
            m = 1;
    }
    if (m_cache.depthMask != 1) {
        glDepthMask(GL_TRUE);
        m_cache.depthMask = 1;
    }
    if (m_cache.stencilWriteMask[0] != 0xFFu || m_cache.stencilWriteMask[1] != 0xFFu) {
        glStencilMask(0xFF);
        m_cache.stencilWriteMask[0] = m_cache.stencilWriteMask[1] = 0xFF;
    }
    m_rasterDirty = true;
}

// ------------------------------------------------------------------------------------------------ framebuffers

GLuint Device::fboFor(Surface *color, Surface *depth)
{
    for (auto &f : m_fbos)
        if (f.color == color && f.depth == depth)
            return f.fbo;
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    bindDrawFbo(fbo);
    if (color) {
        color->attach(GL_COLOR_ATTACHMENT0);
    } else {
        const GLenum none = GL_NONE;
        glDrawBuffers(1, &none);
    }
    if (depth) {
        const FormatInfo &fi = depth->formatInfo();
        const bool glStencil = fi.internalFormat == GL_DEPTH24_STENCIL8 || fi.internalFormat == GL_DEPTH32F_STENCIL8;
        depth->attach(glStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT);
    }
    GLenum status = glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        D3D9_ERROR("framebuffer incomplete (0x%x): color %s %ux%u, depth %s %ux%u", status,
                   color ? color->formatInfo().name : "-", color ? color->width() : 0, color ? color->height() : 0,
                   depth ? depth->formatInfo().name : "-", depth ? depth->width() : 0, depth ? depth->height() : 0);
    m_fbos.push_back({color ? color->id() : 0, depth ? depth->id() : 0, color, depth, fbo});
    return fbo;
}

void Device::bindRenderTargets()
{
    if (!m_fboDirty && m_cache.drawFbo == m_curFbo)
        return;
    m_fboDirty = false;
    m_curFbo = fboFor(m_rt[0], m_ds);
    bindDrawFbo(m_curFbo);
    const int bits = m_ds ? m_ds->formatInfo().depthBits : 24;
    const bool floatNoBlend = m_rt[0] && (m_rt[0]->formatInfo().flags & FMT_FLOAT) &&
                              !(m_rt[0]->formatInfo().flags & FMT_BLENDABLE);
    if (bits != m_curDepthBits || floatNoBlend != m_floatTargetNoBlend) {
        m_curDepthBits = bits;
        m_floatTargetNoBlend = floatNoBlend;
        m_rasterDirty = true;
    }
}

// ------------------------------------------------------------------------------------------------ raster state

void Device::applyRasterState()
{
    m_rasterDirty = false;
    RasterState r;
    computeRasterState(m_rs, m_curDepthBits, &r);
    GLStateCache &c = m_cache;

    setCap(GL_DEPTH_TEST, c.depthTest, r.depthTest);
    if (c.depthFunc != r.depthFunc) {
        glDepthFunc(r.depthFunc);
        c.depthFunc = r.depthFunc;
    }
    if (c.depthMask != (signed char)r.depthWrite) {
        glDepthMask(r.depthWrite ? GL_TRUE : GL_FALSE);
        c.depthMask = (signed char)r.depthWrite;
    }

    setCap(GL_STENCIL_TEST, c.stencilTest, r.stencilTest);
    if (r.stencilTest) {
        static const GLenum faces[2] = {GL_FRONT, GL_BACK};
        for (int f = 0; f < 2; ++f) {
            if (c.stencilFunc[f] != r.stencilFunc[f] || c.stencilRef[f] != r.stencilRef ||
                c.stencilValueMask[f] != (r.stencilMask & 0xFF)) {
                glStencilFuncSeparate(faces[f], r.stencilFunc[f], r.stencilRef, r.stencilMask & 0xFF);
                c.stencilFunc[f] = r.stencilFunc[f];
                c.stencilRef[f] = r.stencilRef;
                c.stencilValueMask[f] = r.stencilMask & 0xFF;
            }
            if (c.stencilFail[f] != r.stencilFail[f] || c.stencilZFail[f] != r.stencilZFail[f] ||
                c.stencilPass[f] != r.stencilPass[f]) {
                glStencilOpSeparate(faces[f], r.stencilFail[f], r.stencilZFail[f], r.stencilPass[f]);
                c.stencilFail[f] = r.stencilFail[f];
                c.stencilZFail[f] = r.stencilZFail[f];
                c.stencilPass[f] = r.stencilPass[f];
            }
        }
        if (c.stencilWriteMask[0] != (r.stencilWriteMask & 0xFF) || c.stencilWriteMask[1] != (r.stencilWriteMask & 0xFF)) {
            glStencilMask(r.stencilWriteMask & 0xFF);
            c.stencilWriteMask[0] = c.stencilWriteMask[1] = r.stencilWriteMask & 0xFF;
        }
    }

    bool blend = r.blend;
    if (blend && m_floatTargetNoBlend) {
        D3D9_WARN_ONCE("blending into a 32-bit float render target needs EXT_float_blend: blending disabled");
        blend = false;
    }
    setCap(GL_BLEND, c.blend, blend);
    if (blend) {
        if (c.blendSrcRGB != r.blendSrcRGB || c.blendDstRGB != r.blendDstRGB || c.blendSrcA != r.blendSrcA ||
            c.blendDstA != r.blendDstA) {
            glBlendFuncSeparate(r.blendSrcRGB, r.blendDstRGB, r.blendSrcA, r.blendDstA);
            c.blendSrcRGB = r.blendSrcRGB, c.blendDstRGB = r.blendDstRGB;
            c.blendSrcA = r.blendSrcA, c.blendDstA = r.blendDstA;
        }
        if (c.blendEqRGB != r.blendEqRGB || c.blendEqA != r.blendEqA) {
            glBlendEquationSeparate(r.blendEqRGB, r.blendEqA);
            c.blendEqRGB = r.blendEqRGB, c.blendEqA = r.blendEqA;
        }
        if (memcmp(c.blendColor, r.blendColor, sizeof(r.blendColor)) != 0) {
            glBlendColor(r.blendColor[0], r.blendColor[1], r.blendColor[2], r.blendColor[3]);
            memcpy(c.blendColor, r.blendColor, sizeof(r.blendColor));
        }
    }

    if (c.colorMask[0] != (signed char)r.colorMask[0] || c.colorMask[1] != (signed char)r.colorMask[1] ||
        c.colorMask[2] != (signed char)r.colorMask[2] || c.colorMask[3] != (signed char)r.colorMask[3]) {
        glColorMask(r.colorMask[0], r.colorMask[1], r.colorMask[2], r.colorMask[3]);
        for (int i = 0; i < 4; ++i)
            c.colorMask[i] = (signed char)r.colorMask[i];
    }

    setCap(GL_CULL_FACE, c.cull, r.cullFace != 0);
    if (r.cullFace && c.cullFace != r.cullFace) {
        glCullFace(r.cullFace);
        c.cullFace = r.cullFace;
    }

    setScissorTest(r.scissorTest);
    if (r.scissorTest) {
        // Top-down rendering: D3D scissor coordinates are GL window coordinates.
        const LONG x0 = std::max<LONG>(0, m_scissor.left), y0 = std::max<LONG>(0, m_scissor.top);
        const LONG x1 = std::max<LONG>(x0, m_scissor.right), y1 = std::max<LONG>(y0, m_scissor.bottom);
        setScissorBox((GLint)x0, (GLint)y0, (GLsizei)(x1 - x0), (GLsizei)(y1 - y0));
    }

    setCap(GL_POLYGON_OFFSET_FILL, c.polygonOffset, r.polygonOffset);
    if (r.polygonOffset && (c.polyFactor != r.polygonOffsetFactor || c.polyUnits != r.polygonOffsetUnits)) {
        glPolygonOffset(r.polygonOffsetFactor, r.polygonOffsetUnits);
        c.polyFactor = r.polygonOffsetFactor;
        c.polyUnits = r.polygonOffsetUnits;
    }

    m_alphaTest[0] = r.alphaFunc;
    m_alphaTest[1] = r.alphaFunc != 0.f ? (float)(m_rs[D3DRS_ALPHAREF] & 0xFF) : 0.f;
}

void Device::applyViewport()
{
    m_viewportDirty = false;
    const D3DVIEWPORT9 &v = m_viewport;
    GLint *c = m_cache.viewport;
    if (c[0] != (GLint)v.X || c[1] != (GLint)v.Y || c[2] != (GLint)v.Width || c[3] != (GLint)v.Height) {
        glViewport((GLint)v.X, (GLint)v.Y, (GLsizei)v.Width, (GLsizei)v.Height);
        c[0] = (GLint)v.X, c[1] = (GLint)v.Y, c[2] = (GLint)v.Width, c[3] = (GLint)v.Height;
    }
    if (m_cache.depthRange[0] != v.MinZ || m_cache.depthRange[1] != v.MaxZ) {
        glDepthRangef(v.MinZ, v.MaxZ);
        m_cache.depthRange[0] = v.MinZ;
        m_cache.depthRange[1] = v.MaxZ;
    }
    // Clip-space fixups applied by every translated vertex shader (see d3d9_shader_translate.cpp):
    // y negated (top-down rendering), + D3D9 half-pixel offset (Wine's 63/64 trick).
    m_posFixup[0] = 1.f;
    m_posFixup[1] = -1.f;
    m_posFixup[2] = v.Width ? (63.f / 64.f) / (float)v.Width : 0.f;
    m_posFixup[3] = v.Height ? (63.f / 64.f) / (float)v.Height : 0.f;
}

// ------------------------------------------------------------------------------------------------ programs

static uint64_t fnv1a(uint64_t h, uint64_t v)
{
    for (int i = 0; i < 8; ++i) {
        h ^= (v >> (i * 8)) & 0xff;
        h *= 0x100000001b3ull;
    }
    return h;
}

static std::vector<UniformRange> packRanges(const std::vector<ShaderUniformInfo> &us, uint8_t type, int *total)
{
    std::vector<UniformRange> out;
    int packed = 0;
    for (const auto &u : us) {
        if (u.type != type || u.constant)
            continue;
        out.push_back({u.index, u.count, packed});
        packed += u.count;
    }
    *total = packed;
    return out;
}

Program *Device::linkProgram(VertexShader *vs, PixelShader *ps, uint32_t bgraMask)
{
    Program *p = new Program();
    p->vs = vs;
    p->ps = ps;
    p->vsId = vs->id();
    p->psId = ps->id();
    p->variant = bgraMask;

    const TranslatedShader &vbase = vs->base();
    const TranslatedShader &pt = ps->tr();
    TranslateOptions opt;
    for (size_t i = 0; i < vbase.inputs.size() && i < 32; ++i)
        if (bgraMask & (1u << i))
            opt.bgraInputs.push_back({vbase.inputs[i].usage, vbase.inputs[i].usageIndex});
    for (const auto &in : pt.inputs) {
        if (in.usage == 255)
            continue; // vPos / vFace
        bool written = false;
        for (const auto &o : vbase.outputs)
            if (o.usage == in.usage && o.usageIndex == in.usageIndex)
                written = true;
        if (!written)
            opt.extraOutputs.push_back({in.usage, in.usageIndex});
    }
    uint64_t key = 0;
    if (!opt.bgraInputs.empty() || !opt.extraOutputs.empty()) {
        key = 0xcbf29ce484222325ull;
        key = fnv1a(key, bgraMask);
        for (auto &e : opt.extraOutputs)
            key = fnv1a(key, ((uint64_t)e.first << 8) | e.second);
        key |= 1; // never 0 (0 = base translation)
    }
    const TranslatedShader *vt = nullptr;
    GLuint vsh = vs->glShader(opt, key, &vt);
    GLuint fsh = ps->glShader();
    if (!vsh || !fsh || !vt) {
        ++g_stats.programsFailed;
        return p;
    }
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vsh);
    glAttachShader(prog, fsh);
    for (size_t i = 0; i < vt->inputs.size() && i < 16; ++i)
        glBindAttribLocation(prog, (GLuint)i, vt->inputs[i].name.c_str());
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len);
        std::string log((size_t)(len > 1 ? len : 1), '\0');
        glGetProgramInfoLog(prog, len, nullptr, &log[0]);
        D3D9_ERROR("program link failed: %s\n----- vertex -----\n%s\n----- pixel -----\n%s", log.c_str(),
                   vt->glsl.c_str(), pt.glsl.c_str());
        if (shaderDumpEnabled(true)) {
            const uint64_t hv = shaderHash(vs->bytecode().data(), vs->bytecode().size() * 4);
            const uint64_t hp = shaderHash(ps->bytecode().data(), ps->bytecode().size() * 4);
            shaderDump("vs", hv, "bin", vs->bytecode().data(), vs->bytecode().size() * 4);
            shaderDump("ps", hp, "bin", ps->bytecode().data(), ps->bytecode().size() * 4);
            char head[96];
            snprintf(head, sizeof(head), "link of vs_%016llx + ps_%016llx: ", (unsigned long long)hv,
                     (unsigned long long)hp);
            std::string msg = head + log;
            shaderDump("ps", hp, "linklog", msg.data(), strlen(msg.c_str()));
        }
        glDeleteProgram(prog);
        ++g_stats.programsFailed;
        return p;
    }
    p->prog = prog;
    p->ok = true;
    ++g_stats.programsLinked;
    useProgram(prog);
    p->locVsF = glGetUniformLocation(prog, "vs_uniforms_vec4");
    p->locVsI = glGetUniformLocation(prog, "vs_uniforms_ivec4");
    p->locVsB = glGetUniformLocation(prog, "vs_uniforms_bool");
    p->locPsF = glGetUniformLocation(prog, "ps_uniforms_vec4");
    p->locPsI = glGetUniformLocation(prog, "ps_uniforms_ivec4");
    p->locPsB = glGetUniformLocation(prog, "ps_uniforms_bool");
    p->locPosFixup = glGetUniformLocation(prog, kPosFixupUniform);
    p->locAlphaTest = glGetUniformLocation(prog, kAlphaTestUniform);
    p->locVPosFlip = glGetUniformLocation(prog, kVPosFlipUniform);
    p->vsF = packRanges(vt->uniforms, 0, &p->vsFCount);
    p->vsI = packRanges(vt->uniforms, 1, &p->vsICount);
    p->vsB = packRanges(vt->uniforms, 2, &p->vsBCount);
    p->psF = packRanges(pt.uniforms, 0, &p->psFCount);
    p->psI = packRanges(pt.uniforms, 1, &p->psICount);
    p->psB = packRanges(pt.uniforms, 2, &p->psBCount);
    auto collectDefs = [](const TranslatedShader &t, const std::vector<UniformRange> &ranges,
                          std::vector<Program::DefOverride> &out) {
        for (const auto &c : t.constants) {
            if (c.type != 0)
                continue;
            for (const auto &r : ranges)
                if (c.index >= r.reg && c.index < r.reg + r.count) {
                    Program::DefOverride d;
                    d.slot = r.packed + (c.index - r.reg);
                    memcpy(d.v, c.f, sizeof(d.v));
                    out.push_back(d);
                }
        }
    };
    collectDefs(*vt, p->vsF, p->vsFDefs);
    collectDefs(pt, p->psF, p->psFDefs);
    // def'd constant arrays (relative addressing into literals): set once.
    const TranslatedShader *stages[2] = {vt, &pt};
    for (const TranslatedShader *t : stages) {
        for (const auto &u : t->uniforms) {
            if (!u.constant || u.type != 0)
                continue;
            GLint loc = glGetUniformLocation(prog, u.name.c_str());
            if (loc < 0)
                continue;
            std::vector<float> vals((size_t)u.count * 4, 0.f);
            for (const auto &c : t->constants)
                if (c.type == 0 && c.index >= u.index && c.index < u.index + u.count)
                    memcpy(&vals[(size_t)(c.index - u.index) * 4], c.f, sizeof(c.f));
            glUniform4fv(loc, u.count, vals.data());
        }
    }
    for (const auto &s : pt.samplers) {
        GLint loc = glGetUniformLocation(prog, s.name.c_str());
        if (loc >= 0)
            glUniform1i(loc, s.stage);
    }
    for (const auto &s : vt->samplers) {
        GLint loc = glGetUniformLocation(prog, s.name.c_str());
        if (loc >= 0)
            glUniform1i(loc, kMaxSamplers + s.stage); // unsupported vertex textures: an empty unit
    }
    if (p->locVPosFlip >= 0)
        glUniform2f(p->locVPosFlip, 1.f, -0.5f); // top-down rendering; D3D9 VPOS has integer pixel centers
    p->samplers = pt.samplers;
    for (size_t i = 0; i < vt->inputs.size() && i < 16; ++i)
        p->inputs.push_back({vt->inputs[i].usage, vt->inputs[i].usageIndex, (int)i});
    return p;
}

Program *Device::programFor(VertexShader *vs, PixelShader *ps, uint32_t bgraMask)
{
    ProgKey k{vs->id(), ps->id(), bgraMask};
    auto it = m_programs.find(k);
    if (it != m_programs.end())
        return it->second;
    Program *p = linkProgram(vs, ps, bgraMask);
    m_programs.emplace(k, p);
    return p;
}

void Device::uploadUniforms(Program *p)
{
    static thread_local std::vector<float> fbuf;
    static thread_local std::vector<int> ibuf;
    auto uploadF = [&](GLint loc, const std::vector<UniformRange> &ranges, int total, const float (*src)[4], int cap,
                       uint32_t version, uint32_t &uploaded, const std::vector<Program::DefOverride> &defs) {
        if (loc < 0 || uploaded == version || !total)
            return;
        fbuf.assign((size_t)total * 4, 0.f);
        for (const auto &r : ranges) {
            int n = std::min(r.count, cap - r.reg);
            if (n > 0)
                memcpy(&fbuf[(size_t)r.packed * 4], src[r.reg], sizeof(float) * 4 * (size_t)n);
        }
        for (const auto &d : defs)
            if (d.slot >= 0 && d.slot < total)
                memcpy(&fbuf[(size_t)d.slot * 4], d.v, sizeof(d.v));
        glUniform4fv(loc, total, fbuf.data());
        uploaded = version;
    };
    auto uploadI = [&](GLint loc, const std::vector<UniformRange> &ranges, int total, const int (*src)[4],
                       uint32_t version, uint32_t &uploaded) {
        if (loc < 0 || uploaded == version || !total)
            return;
        ibuf.assign((size_t)total * 4, 0);
        for (const auto &r : ranges) {
            int n = std::min(r.count, kConstI - r.reg);
            if (n > 0)
                memcpy(&ibuf[(size_t)r.packed * 4], src[r.reg], sizeof(int) * 4 * (size_t)n);
        }
        glUniform4iv(loc, total, ibuf.data());
        uploaded = version;
    };
    auto uploadB = [&](GLint loc, const std::vector<UniformRange> &ranges, int total, const BOOL *src,
                       uint32_t version, uint32_t &uploaded) {
        if (loc < 0 || uploaded == version || !total)
            return;
        ibuf.assign((size_t)total, 0);
        for (const auto &r : ranges)
            for (int i = 0; i < r.count && r.reg + i < kConstB; ++i)
                ibuf[(size_t)(r.packed + i)] = src[r.reg + i] ? 1 : 0;
        glUniform1iv(loc, total, ibuf.data());
        uploaded = version;
    };
    uploadF(p->locVsF, p->vsF, p->vsFCount, m_vsF, kVSConstF, m_vsFVersion, p->uploadedVsF, p->vsFDefs);
    uploadF(p->locPsF, p->psF, p->psFCount, m_psF, kPSConstF, m_psFVersion, p->uploadedPsF, p->psFDefs);
    uploadI(p->locVsI, p->vsI, p->vsICount, m_vsI, m_vsIVersion, p->uploadedVsI);
    uploadI(p->locPsI, p->psI, p->psICount, m_psI, m_psIVersion, p->uploadedPsI);
    uploadB(p->locVsB, p->vsB, p->vsBCount, m_vsB, m_vsBVersion, p->uploadedVsB);
    uploadB(p->locPsB, p->psB, p->psBCount, m_psB, m_psBVersion, p->uploadedPsB);
    if (p->locPosFixup >= 0 && memcmp(p->posFixup, m_posFixup, sizeof(m_posFixup)) != 0) {
        glUniform4fv(p->locPosFixup, 1, m_posFixup);
        memcpy(p->posFixup, m_posFixup, sizeof(m_posFixup));
    }
    if (p->locAlphaTest >= 0 && memcmp(p->alphaTest, m_alphaTest, sizeof(m_alphaTest)) != 0) {
        glUniform2fv(p->locAlphaTest, 1, m_alphaTest);
        memcpy(p->alphaTest, m_alphaTest, sizeof(m_alphaTest));
    }
}

// ------------------------------------------------------------------------------------------------ vertex input

void Device::setupVertexAttribs(Program *p, GLuint upBuffer, UINT upStride, INT baseVertex)
{
    const Program::DeclBinding *db = nullptr;
    if (m_decl) {
        for (auto &d : p->declCache)
            if (d.declId == m_decl->id()) {
                db = &d;
                break;
            }
        if (!db) {
            Program::DeclBinding nd;
            nd.declId = m_decl->id();
            uint32_t bgra = 0;
            nd.binds = linkDeclaration(m_decl->elements(), m_decl->count(), p->inputs.data(), (UINT)p->inputs.size(),
                                       &bgra, &nd.streamMask);
            p->declCache.push_back(std::move(nd));
            db = &p->declCache.back();
        }
    }
    uint32_t used = 0;
    if (db) {
        for (const AttribBinding &b : db->binds) {
            GLuint buffer;
            UINT stride, streamOffset;
            if (upBuffer && b.stream == 0) {
                buffer = upBuffer;
                stride = upStride;
                streamOffset = 0;
            } else {
                const Stream &s = m_streams[b.stream];
                if (!s.vb) {
                    D3D9_WARN_ONCE("draw with a vertex declaration element on stream %u but no stream source", b.stream);
                    continue;
                }
                s.vb->buffer().flush(this);
                buffer = s.vb->buffer().buf;
                stride = s.stride;
                streamOffset = s.offset;
            }
            const intptr_t off = (intptr_t)streamOffset + b.offset + (intptr_t)baseVertex * (intptr_t)stride;
            if (off < 0)
                continue;
            VertexAttribState &a = m_cache.attribs[b.location];
            if (!a.valid || a.buffer != buffer || a.size != b.size || a.type != b.type || a.normalized != b.normalized ||
                a.stride != (GLsizei)stride || a.offset != (uintptr_t)off) {
                bindArrayBuffer(buffer);
                glVertexAttribPointer((GLuint)b.location, b.size, b.type, b.normalized, (GLsizei)stride,
                                      (const void *)off);
                a.buffer = buffer;
                a.size = b.size;
                a.type = b.type;
                a.normalized = b.normalized;
                a.stride = (GLsizei)stride;
                a.offset = (uintptr_t)off;
                a.valid = true;
            }
            if (!a.enabled) {
                glEnableVertexAttribArray((GLuint)b.location);
                a.enabled = true;
            }
            used |= 1u << b.location;
        }
    }
    for (int i = 0; i < 16; ++i) {
        VertexAttribState &a = m_cache.attribs[i];
        if (a.enabled && !(used & (1u << i))) {
            glDisableVertexAttribArray((GLuint)i);
            a.enabled = false;
        }
    }
}

// ------------------------------------------------------------------------------------------------ samplers

GLuint Device::samplerObjectFor(const SamplerParams &sp)
{
    auto idx = [](GLenum e) -> uint64_t {
        switch (e) {
        case GL_NEAREST: return 0;
        case GL_LINEAR: return 1;
        case GL_NEAREST_MIPMAP_NEAREST: return 2;
        case GL_LINEAR_MIPMAP_NEAREST: return 3;
        case GL_NEAREST_MIPMAP_LINEAR: return 4;
        case GL_LINEAR_MIPMAP_LINEAR: return 5;
        case GL_REPEAT: return 0;
        case GL_MIRRORED_REPEAT: return 1;
        case GL_CLAMP_TO_EDGE: return 2;
        default: return 7;
        }
    };
    const uint64_t key = idx(sp.minFilter) | idx(sp.magFilter) << 3 | idx(sp.wrapS) << 6 | idx(sp.wrapT) << 8 |
                         idx(sp.wrapR) << 10 | (uint64_t)(sp.maxAnisotropy * 4.f) << 12 |
                         (uint64_t)(sp.minLod * 16.f) << 24;
    auto it = m_samplerObjects.find(key);
    if (it != m_samplerObjects.end())
        return it->second;
    GLuint s = 0;
    glGenSamplers(1, &s);
    glSamplerParameteri(s, GL_TEXTURE_MIN_FILTER, (GLint)sp.minFilter);
    glSamplerParameteri(s, GL_TEXTURE_MAG_FILTER, (GLint)sp.magFilter);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_S, (GLint)sp.wrapS);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_T, (GLint)sp.wrapT);
    glSamplerParameteri(s, GL_TEXTURE_WRAP_R, (GLint)sp.wrapR);
    if (sp.minLod > 0.f)
        glSamplerParameterf(s, GL_TEXTURE_MIN_LOD, sp.minLod);
    if (sp.maxAnisotropy > 1.f && m_glcaps.anisotropic)
        glSamplerParameterf(s, GL_TEXTURE_MAX_ANISOTROPY_EXT, sp.maxAnisotropy);
    m_samplerObjects.emplace(key, s);
    return s;
}

void Device::bindSamplers(Program *p)
{
    TexStorage *rtStorage = m_rt[0] ? m_rt[0]->storage() : nullptr;
    for (const ShaderSamplerInfo &s : p->samplers) {
        const int unit = s.stage;
        if (unit < 0 || unit >= kMaxSamplers)
            continue;
        TexStorage *t = m_textureStorage[unit];
        static const GLenum kTargets[3] = {GL_TEXTURE_2D, GL_TEXTURE_CUBE_MAP, GL_TEXTURE_3D};
        const GLenum want = s.type < 3 ? kTargets[s.type] : GL_TEXTURE_2D;
        if (t && t->cpuOnly) {
            D3D9_WARN_ONCE("a SYSTEMMEM/SCRATCH texture was bound for sampling: ignored");
            t = nullptr;
        }
        if (t && t == rtStorage) {
            D3D9_WARN_ONCE("texture bound on sampler %d is also the current render target (feedback loop): unbound",
                           unit);
            t = nullptr;
        }
        if (t && t->target != want) {
            D3D9_WARN_ONCE("sampler %d: shader expects %s, bound texture is %s", unit,
                           want == GL_TEXTURE_2D ? "2D" : (want == GL_TEXTURE_CUBE_MAP ? "cube" : "volume"),
                           t->target == GL_TEXTURE_2D ? "2D" : (t->target == GL_TEXTURE_CUBE_MAP ? "cube" : "volume"));
            t = nullptr;
        }
        if (t) {
            t->ensureGL(this);
            t->flushUploads(this);
            bindTextureUnit(unit, want, t->tex);
        } else {
            bindTextureUnit(unit, want, 0);
        }
        if ((m_samplerDirtyMask & (1u << unit)) || m_cache.sampler[unit] == ~0u || m_unitSamplerTex[unit] != t) {
            m_unitSamplerTex[unit] = t;
            const bool hasMips = t && t->levels > 1;
            const bool filterable = !t || (t->fi.flags & FMT_FILTERABLE);
            SamplerParams sp = computeSamplerParams(m_ss[unit], hasMips, filterable,
                                                    m_glcaps.anisotropic ? m_glcaps.maxAnisotropy : 1.f);
            bindSamplerUnit(unit, samplerObjectFor(sp));
        }
        m_samplerDirtyMask &= ~(1u << unit);
    }
}

// ------------------------------------------------------------------------------------------------ draw

GLenum Device::primitiveMode(D3DPRIMITIVETYPE t, UINT n, GLsizei *count)
{
    switch (t) {
    case D3DPT_POINTLIST: *count = (GLsizei)n; return GL_POINTS;
    case D3DPT_LINELIST: *count = (GLsizei)(n * 2); return GL_LINES;
    case D3DPT_LINESTRIP: *count = (GLsizei)(n + 1); return GL_LINE_STRIP;
    case D3DPT_TRIANGLELIST: *count = (GLsizei)(n * 3); return GL_TRIANGLES;
    case D3DPT_TRIANGLESTRIP: *count = (GLsizei)(n + 2); return GL_TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN: *count = (GLsizei)(n + 2); return GL_TRIANGLE_FAN;
    default: *count = 0; return GL_TRIANGLES;
    }
}

bool Device::prepareDraw(GLuint upBuffer, UINT upStride, INT baseVertex)
{
    processDeferred();
    if (!m_vs || !m_ps) {
        D3D9_WARN_ONCE("draw without a vertex or pixel shader (fixed-function pipeline is not supported): skipped");
        return false;
    }
    if (!m_vs->base().ok || !m_ps->tr().ok)
        return false;
    bindRenderTargets();
    if (m_bgraDirty) {
        uint32_t mask = 0;
        if (m_decl) {
            std::vector<ShaderInputSlot> slots;
            const auto &ins = m_vs->base().inputs;
            for (size_t i = 0; i < ins.size() && i < 16; ++i)
                slots.push_back({ins[i].usage, ins[i].usageIndex, (int)i});
            linkDeclaration(m_decl->elements(), m_decl->count(), slots.data(), (UINT)slots.size(), &mask, nullptr);
        }
        m_curBgraMask = mask;
        m_bgraDirty = false;
        m_curProgram = nullptr;
    }
    if (!m_curProgram || m_curProgram->vs != m_vs || m_curProgram->ps != m_ps ||
        m_curProgram->variant != m_curBgraMask) {
        m_curProgram = programFor(m_vs, m_ps, m_curBgraMask);
        m_samplerDirtyMask = 0xffffffffu;
    }
    Program *p = m_curProgram;
    if (!p->ok)
        return false;
    useProgram(p->prog);
    if (m_viewportDirty)
        applyViewport();
    if (m_rasterDirty)
        applyRasterState();
    uploadUniforms(p);
    setupVertexAttribs(p, upBuffer, upStride, baseVertex);
    bindSamplers(p);
    return true;
}

HRESULT Device::DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount)
{
    D3D9_TRACE("DrawPrimitive(%d, %u, %u)", (int)PrimitiveType, StartVertex, PrimitiveCount);
    GLsizei count;
    GLenum mode = primitiveMode(PrimitiveType, PrimitiveCount, &count);
    if (!count)
        return D3D_OK;
    if (!prepareDraw(0, 0, 0))
        return D3D_OK;
    glDrawArrays(mode, (GLint)StartVertex, count);
    D3D9_GLCHECK("DrawPrimitive");
    ++g_stats.drawsThisFrame;
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT, UINT,
                                     UINT startIndex, UINT primCount)
{
    D3D9_TRACE("DrawIndexedPrimitive(%d, base %d, start %u, prims %u)", (int)PrimitiveType, BaseVertexIndex,
               startIndex, primCount);
    if (!m_ib)
        return D3DERR_INVALIDCALL;
    GLsizei count;
    GLenum mode = primitiveMode(PrimitiveType, primCount, &count);
    if (!count)
        return D3D_OK;
    if (!prepareDraw(0, 0, BaseVertexIndex))
        return D3D_OK;
    m_ib->buffer().flush(this);
    bindElementBuffer(m_ib->buffer().buf);
    const bool i32 = m_ib->format() == D3DFMT_INDEX32;
    glDrawElements(mode, count, i32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT,
                   (const void *)(uintptr_t)(startIndex * (i32 ? 4u : 2u)));
    D3D9_GLCHECK("DrawIndexedPrimitive");
    ++g_stats.drawsThisFrame;
    return D3D_OK;
}

HRESULT Device::DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void *pData, UINT Stride)
{
    D3D9_TRACE("DrawPrimitiveUP(%d, %u, stride %u)", (int)PrimitiveType, PrimitiveCount, Stride);
    if (!pData || !Stride)
        return D3DERR_INVALIDCALL;
    GLsizei count;
    GLenum mode = primitiveMode(PrimitiveType, PrimitiveCount, &count);
    if (count) {
        if (!m_upVB)
            glGenBuffers(1, &m_upVB);
        bindArrayBuffer(m_upVB);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)count * Stride, pData, GL_STREAM_DRAW);
        for (auto &a : m_cache.attribs)
            if (a.buffer == m_upVB)
                a.valid = false;
        if (prepareDraw(m_upVB, Stride, 0)) {
            glDrawArrays(mode, 0, count);
            D3D9_GLCHECK("DrawPrimitiveUP");
            ++g_stats.drawsThisFrame;
        }
    }
    // D3D9: the stream 0 source is unset after a *UP draw.
    m_streams[0] = Stream();
    m_attribsDirty = true;
    return D3D_OK;
}

HRESULT Device::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices,
                                       UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                       const void *pVertexData, UINT Stride)
{
    if (!pIndexData || !pVertexData || !Stride)
        return D3DERR_INVALIDCALL;
    GLsizei count;
    GLenum mode = primitiveMode(PrimitiveType, PrimitiveCount, &count);
    if (count) {
        const bool i32 = IndexDataFormat == D3DFMT_INDEX32;
        if (!m_upVB)
            glGenBuffers(1, &m_upVB);
        if (!m_upIB)
            glGenBuffers(1, &m_upIB);
        bindArrayBuffer(m_upVB);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(MinVertexIndex + NumVertices) * Stride, pVertexData,
                     GL_STREAM_DRAW);
        for (auto &a : m_cache.attribs)
            if (a.buffer == m_upVB)
                a.valid = false;
        if (prepareDraw(m_upVB, Stride, 0)) {
            bindElementBuffer(m_upIB);
            glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)count * (i32 ? 4 : 2), pIndexData, GL_STREAM_DRAW);
            glDrawElements(mode, count, i32 ? GL_UNSIGNED_INT : GL_UNSIGNED_SHORT, nullptr);
            D3D9_GLCHECK("DrawIndexedPrimitiveUP");
            ++g_stats.drawsThisFrame;
        }
    }
    m_streams[0] = Stream();
    m_ib = nullptr;
    m_attribsDirty = true;
    return D3D_OK;
}

} // namespace d3d9shim
