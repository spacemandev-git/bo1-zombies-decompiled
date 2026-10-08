// d3d9_queries.cpp - IDirect3DQuery9: EVENT (immediately signaled), OCCLUSION (ANY_SAMPLES_PASSED, never blocks),
// TIMESTAMP* (CPU clock, profiling only).
#include "d3d9_internal.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <time.h>

namespace d3d9shim {

static uint64_t nowNanoseconds()
{
#ifdef __EMSCRIPTEN__
    return (uint64_t)(emscripten_get_now() * 1e6);
#else
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

Query::Query(Device *dev, D3DQUERYTYPE type) : Resource(dev), m_type(type), m_lastResult(g_occlusionVisibleCount) {}

Query::~Query()
{
    m_device->onQueryDestroyed(this);
    if (m_query) {
        if (onDeviceThread())
            glDeleteQueries(1, &m_query);
        else
            m_device->queueGLDelete(GL_ANY_SAMPLES_PASSED, m_query);
    }
}

DWORD Query::GetDataSize()
{
    switch (m_type) {
    case D3DQUERYTYPE_EVENT: return sizeof(BOOL);
    case D3DQUERYTYPE_OCCLUSION: return sizeof(DWORD);
    case D3DQUERYTYPE_TIMESTAMP:
    case D3DQUERYTYPE_TIMESTAMPFREQ: return sizeof(uint64_t);
    case D3DQUERYTYPE_TIMESTAMPDISJOINT: return sizeof(BOOL);
    default: return 0;
    }
}

HRESULT Query::Issue(DWORD flags)
{
    switch (m_type) {
    case D3DQUERYTYPE_EVENT:
        return D3D_OK;
    case D3DQUERYTYPE_OCCLUSION:
        if (!onDeviceThread()) {
            D3D9_WARN_ONCE("occlusion query issued off the device thread: ignored");
            return D3D_OK;
        }
        if (flags & D3DISSUE_BEGIN) {
            if (!m_query)
                glGenQueries(1, &m_query);
            m_device->beginOcclusion(this);
            glBeginQuery(GL_ANY_SAMPLES_PASSED, m_query);
            m_active = true;
            m_pending = false;
        }
        if (flags & D3DISSUE_END) {
            if (m_active) {
                glEndQuery(GL_ANY_SAMPLES_PASSED);
                m_device->endOcclusion(this);
                m_active = false;
                m_pending = true;
            }
        }
        return D3D_OK;
    case D3DQUERYTYPE_TIMESTAMP:
        if (flags & D3DISSUE_END)
            m_timestamp = nowNanoseconds();
        return D3D_OK;
    default:
        return D3D_OK;
    }
}

void Query::deviceLost()
{
    m_active = false;
    m_pending = false;
}

HRESULT Query::GetData(void *pData, DWORD dwSize, DWORD)
{
    switch (m_type) {
    case D3DQUERYTYPE_EVENT:
        // WebGL commands complete in order and nothing in the engine needs real GPU fences: always signaled.
        if (pData && dwSize >= sizeof(BOOL))
            *(BOOL *)pData = TRUE;
        return D3D_OK;
    case D3DQUERYTYPE_OCCLUSION: {
        // WebGL2 results become available only after the thread yields to the browser, and only as any/none.
        // Never return S_FALSE (the engine spin-waits on it): report the latest available result, or the previous
        // one (initially "visible").
        if (m_pending && m_query && onDeviceThread()) {
            GLuint avail = 0;
            glGetQueryObjectuiv(m_query, GL_QUERY_RESULT_AVAILABLE, &avail);
            if (avail) {
                GLuint any = 0;
                glGetQueryObjectuiv(m_query, GL_QUERY_RESULT, &any);
                m_lastResult = any ? g_occlusionVisibleCount : 0;
                m_pending = false;
            }
        }
        if (pData && dwSize >= sizeof(DWORD))
            *(DWORD *)pData = m_lastResult;
        return D3D_OK;
    }
    case D3DQUERYTYPE_TIMESTAMP:
        if (pData && dwSize >= sizeof(uint64_t))
            *(uint64_t *)pData = m_timestamp;
        return D3D_OK;
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        if (pData && dwSize >= sizeof(uint64_t))
            *(uint64_t *)pData = 1000000000ull;
        return D3D_OK;
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        if (pData && dwSize >= sizeof(BOOL))
            *(BOOL *)pData = FALSE;
        return D3D_OK;
    default:
        return D3DERR_NOTAVAILABLE;
    }
}

} // namespace d3d9shim
