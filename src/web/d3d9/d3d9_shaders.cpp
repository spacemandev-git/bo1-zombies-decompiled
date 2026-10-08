// d3d9_shaders.cpp - vertex/pixel shader objects: bytecode copies, MojoShader translations, GL shader compiles.
#include "d3d9_internal.h"

namespace d3d9shim {

static GLuint compileGL(GLenum type, const std::string &src, const char *what, const std::vector<DWORD> &bytecode)
{
    GLuint sh = glCreateShader(type);
    const char *p = src.c_str();
    glShaderSource(sh, 1, &p, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        GLint len = 0;
        glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
        std::string log((size_t)(len > 1 ? len : 1), '\0');
        glGetShaderInfoLog(sh, len, nullptr, &log[0]);
        D3D9_ERROR("%s compile failed:\n%s\n----- source -----\n%s", what, log.c_str(), src.c_str());
        if (shaderDumpEnabled(true)) {
            const char *kind = type == GL_VERTEX_SHADER ? "vs" : "ps";
            const uint64_t h = shaderHash(bytecode.data(), bytecode.size() * 4);
            shaderDump(kind, h, "bin", bytecode.data(), bytecode.size() * 4);
            shaderDump(kind, h, "glsl", src.data(), src.size());
            shaderDump(kind, h, "log", log.data(), strlen(log.c_str()));
        }
        glDeleteShader(sh);
        ++g_stats.shadersFailed;
        return 0;
    }
    return sh;
}

// ------------------------------------------------------------------------------------------------ VertexShader

VertexShader::VertexShader(Device *dev, const DWORD *function, TranslatedShader &&base)
    : Resource(dev), m_base(std::move(base))
{
    m_bytecode.assign(function, function + m_base.byteLength / 4);
}

VertexShader::~VertexShader()
{
    m_device->onVertexShaderDestroyed(this);
    for (auto &v : m_variants)
        if (v.shader) {
            if (onDeviceThread())
                glDeleteShader(v.shader);
            else
                m_device->queueGLDelete(GL_VERTEX_SHADER, v.shader);
        }
}

HRESULT VertexShader::GetFunction(void *pData, UINT *pSizeOfData)
{
    if (!pSizeOfData)
        return D3DERR_INVALIDCALL;
    const UINT bytes = (UINT)(m_bytecode.size() * 4);
    if (pData) {
        if (*pSizeOfData < bytes)
            return D3DERR_MOREDATA;
        memcpy(pData, m_bytecode.data(), bytes);
    }
    *pSizeOfData = bytes;
    return D3D_OK;
}

GLuint VertexShader::glShader(const TranslateOptions &opt, uint64_t key, const TranslatedShader **info)
{
    for (auto &v : m_variants)
        if (v.key == key) {
            if (info)
                *info = &v.tr;
            return v.shader;
        }
    Variant v;
    v.key = key;
    v.shader = 0;
    if (key == 0)
        v.tr = m_base;
    else
        v.tr = translateShader(m_bytecode.data(), m_bytecode.size() * 4, &opt);
    if (!v.tr.ok)
        D3D9_ERROR("vertex shader variant translation failed: %s", v.tr.errors.c_str());
    else
        v.shader = compileGL(GL_VERTEX_SHADER, v.tr.glsl, "vertex shader", m_bytecode);
    m_variants.push_back(std::move(v));
    if (info)
        *info = &m_variants.back().tr;
    return m_variants.back().shader;
}

// ------------------------------------------------------------------------------------------------ PixelShader

PixelShader::PixelShader(Device *dev, const DWORD *function, TranslatedShader &&tr) : Resource(dev), m_tr(std::move(tr))
{
    m_bytecode.assign(function, function + m_tr.byteLength / 4);
}

PixelShader::~PixelShader()
{
    m_device->onPixelShaderDestroyed(this);
    if (m_shader) {
        if (onDeviceThread())
            glDeleteShader(m_shader);
        else
            m_device->queueGLDelete(GL_FRAGMENT_SHADER, m_shader);
    }
}

HRESULT PixelShader::GetFunction(void *pData, UINT *pSizeOfData)
{
    if (!pSizeOfData)
        return D3DERR_INVALIDCALL;
    const UINT bytes = (UINT)(m_bytecode.size() * 4);
    if (pData) {
        if (*pSizeOfData < bytes)
            return D3DERR_MOREDATA;
        memcpy(pData, m_bytecode.data(), bytes);
    }
    *pSizeOfData = bytes;
    return D3D_OK;
}

GLuint PixelShader::glShader()
{
    if (m_shader || m_compileFailed)
        return m_shader;
    m_shader = compileGL(GL_FRAGMENT_SHADER, m_tr.glsl, "pixel shader", m_bytecode);
    m_compileFailed = m_shader == 0;
    return m_shader;
}

} // namespace d3d9shim
