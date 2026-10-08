// d3d9_log.cpp - logging, error strings, PIX stubs, GUIDs and the d3d9shim_* control API.
#include "d3d9_internal.h"

#include <dxerr.h>
#include <ddraw.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace d3d9shim {

int g_logLevel = 1;
bool g_trace = false;
bool g_glCheck = false;
Stats g_stats;
unsigned g_occlusionVisibleCount = 0x10000;
int g_displayWidth = 1920, g_displayHeight = 1080;
std::string g_canvasSelector = "#canvas";

static std::string g_dumpDir;
static bool g_dumpAll = false;
static pthread_t g_deviceThread;
static std::atomic<bool> g_haveDeviceThread{false};
static std::atomic<uint64_t> g_nextId{1};

void setDeviceThreadToCurrent()
{
    g_deviceThread = pthread_self();
    g_haveDeviceThread = true;
}

bool onDeviceThread()
{
    if (!g_haveDeviceThread)
        return true;
    return pthread_equal(pthread_self(), g_deviceThread) != 0;
}

uint64_t nextObjectId() { return g_nextId++; }

void initLogFromEnv()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    if (const char *v = getenv("D3D9SHIM_LOG"))
        g_logLevel = atoi(v);
    if (const char *v = getenv("D3D9SHIM_TRACE"))
        g_trace = atoi(v) != 0;
    if (const char *v = getenv("D3D9SHIM_GLCHECK"))
        g_glCheck = atoi(v) != 0;
    if (const char *v = getenv("D3D9SHIM_SHADER_DUMP"))
        g_dumpDir = v;
    if (const char *v = getenv("D3D9SHIM_SHADER_DUMP_ALL"))
        g_dumpAll = atoi(v) != 0;
}

bool shaderDumpEnabled(bool failure) { return !g_dumpDir.empty() && (failure || g_dumpAll); }

uint64_t shaderHash(const DWORD *tokens, size_t bytes)
{
    uint64_t h = 0xcbf29ce484222325ull;
    const uint8_t *p = (const uint8_t *)tokens;
    for (size_t i = 0; i < bytes; ++i) {
        h ^= p[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

void shaderDump(const char *kind, uint64_t hash, const char *ext, const void *data, size_t size)
{
    if (g_dumpDir.empty())
        return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s_%016llx.%s", g_dumpDir.c_str(), kind, (unsigned long long)hash, ext);
    FILE *f = fopen(path, "wb");
    if (!f) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            logf(1, "shader dump: cannot write %s", path);
        }
        return;
    }
    fwrite(data, 1, size, f);
    fclose(f);
}

void checkGLError(const char *where)
{
    static unsigned reported = 0;
    for (GLenum e = glGetError(); e != GL_NO_ERROR; e = glGetError()) {
        if (reported < 100) {
            ++reported;
            logf(0, "GL error 0x%04x after %s (frame %u)%s", e, where, g_stats.frame,
                 reported == 100 ? " - further GL errors are not reported" : "");
        }
    }
}

void logf(int level, const char *fmt, ...)
{
    if (level > g_logLevel)
        return;
    static const char *tags[] = {"error", "warning", "info", "debug"};
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t n = strlen(buf);
    while (n && buf[n - 1] == '\n')
        buf[--n] = 0;
    fprintf(level == 0 ? stderr : stdout, "[d3d9shim %s] %s\n", tags[level < 0 ? 0 : (level > 3 ? 3 : level)], buf);
}

void tracef(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fprintf(stdout, "[d3d9 %u] %s\n", g_stats.frame, buf);
}

} // namespace d3d9shim

using namespace d3d9shim;

// ------------------------------------------------------------------------------------------------ control API
extern "C" {

void d3d9shim_set_canvas_selector(const char *selector) { g_canvasSelector = selector ? selector : "#canvas"; }
void d3d9shim_set_log_level(int level) { g_logLevel = level; }
void d3d9shim_set_trace(int enabled) { g_trace = enabled != 0; }
void d3d9shim_set_gl_check(int enabled) { g_glCheck = enabled != 0; }
void d3d9shim_set_shader_dump(const char *dir, int all)
{
    g_dumpDir = dir ? dir : "";
    g_dumpAll = all != 0;
}
void d3d9shim_set_display_size(int width, int height)
{
    if (width > 0 && height > 0) {
        g_displayWidth = width;
        g_displayHeight = height;
    }
}
void d3d9shim_set_occlusion_visible_count(unsigned count) { g_occlusionVisibleCount = count ? count : 1; }
void d3d9shim_get_stats(d3d9shim_stats *out)
{
    if (!out)
        return;
    out->frame = g_stats.frame;
    out->draws = g_stats.draws;
    out->programsLinked = g_stats.programsLinked;
    out->programsFailed = g_stats.programsFailed;
    out->shadersFailed = g_stats.shadersFailed;
    out->textureUploadBytes = g_stats.textureUploadBytes;
    out->bufferUploadBytes = g_stats.bufferUploadBytes;
}

// ------------------------------------------------------------------------------------------------ PIX
int WINAPI D3DPERF_BeginEvent(D3DCOLOR, const wchar_t *) { return 0; }
int WINAPI D3DPERF_EndEvent(void) { return 0; }
void WINAPI D3DPERF_SetMarker(D3DCOLOR, const wchar_t *) {}
void WINAPI D3DPERF_SetRegion(D3DCOLOR, const wchar_t *) {}
BOOL WINAPI D3DPERF_QueryRepeatFrame(void) { return FALSE; }
void WINAPI D3DPERF_SetOptions(DWORD) {}
DWORD WINAPI D3DPERF_GetStatus(void) { return 0; }

// ------------------------------------------------------------------------------------------------ GUIDs (SDK values)
const GUID IID_IDirect3D9 = {0x81bdcbca, 0x64d4, 0x426d, {0xae, 0x8d, 0xad, 0x01, 0x47, 0xf4, 0x27, 0x5c}};
const GUID IID_IDirect3DDevice9 = {0xd0223b96, 0xbf7a, 0x43fd, {0x92, 0xbd, 0xa4, 0x3b, 0x0d, 0x82, 0xb9, 0xeb}};
const GUID IID_IDirect3DResource9 = {0x05eec05d, 0x8f7d, 0x4362, {0xb9, 0x99, 0xd1, 0xba, 0xf3, 0x57, 0xc7, 0x04}};
const GUID IID_IDirect3DBaseTexture9 = {0x580ca87e, 0x1d3c, 0x4d54, {0x99, 0x1d, 0xb7, 0xd3, 0xe3, 0xc2, 0x98, 0xce}};
const GUID IID_IDirect3DTexture9 = {0x85c31227, 0x3de5, 0x4f00, {0x9b, 0x3a, 0xf1, 0x1a, 0xc3, 0x8c, 0x18, 0xb5}};
const GUID IID_IDirect3DCubeTexture9 = {0xfff32f81, 0xd953, 0x473a, {0x92, 0x23, 0x93, 0xd6, 0x52, 0xab, 0xa9, 0x3f}};
const GUID IID_IDirect3DVolumeTexture9 = {0x2518526c, 0xe789, 0x4111, {0xa7, 0xb9, 0x47, 0xef, 0x32, 0x8d, 0x13, 0xe6}};
const GUID IID_IDirect3DSurface9 = {0x0cfbaf3a, 0x9ff6, 0x429a, {0x99, 0xb3, 0xa2, 0x79, 0x6a, 0xf8, 0xb8, 0x9b}};
const GUID IID_IDirect3DSwapChain9 = {0x794950f2, 0xadfc, 0x458a, {0x90, 0x5e, 0x10, 0xa1, 0x0b, 0x0b, 0x50, 0x3b}};
const GUID IID_IDirectDraw7 = {0x15e65ec0, 0x3b9c, 0x11d2, {0xb9, 0x2f, 0x00, 0x60, 0x97, 0x97, 0xea, 0x5b}};

// ------------------------------------------------------------------------------------------------ dxerr
struct ErrInfo {
    HRESULT hr;
    const char *name;
    const char *desc;
};
static const ErrInfo kErrors[] = {
    {D3D_OK, "D3D_OK", "No error occurred."},
    {S_FALSE, "S_FALSE", "The call succeeded but the result is not yet available."},
    {D3DOK_NOAUTOGEN, "D3DOK_NOAUTOGEN", "Mipmaps cannot be generated automatically for this format."},
    {D3DERR_WRONGTEXTUREFORMAT, "D3DERR_WRONGTEXTUREFORMAT", "The pixel format of the texture is not supported."},
    {D3DERR_UNSUPPORTEDCOLOROPERATION, "D3DERR_UNSUPPORTEDCOLOROPERATION", "Unsupported color operation."},
    {D3DERR_UNSUPPORTEDCOLORARG, "D3DERR_UNSUPPORTEDCOLORARG", "Unsupported color argument."},
    {D3DERR_UNSUPPORTEDALPHAOPERATION, "D3DERR_UNSUPPORTEDALPHAOPERATION", "Unsupported alpha operation."},
    {D3DERR_UNSUPPORTEDALPHAARG, "D3DERR_UNSUPPORTEDALPHAARG", "Unsupported alpha argument."},
    {D3DERR_TOOMANYOPERATIONS, "D3DERR_TOOMANYOPERATIONS", "Too many texture-filtering operations."},
    {D3DERR_CONFLICTINGTEXTUREFILTER, "D3DERR_CONFLICTINGTEXTUREFILTER", "Conflicting texture filters."},
    {D3DERR_UNSUPPORTEDFACTORVALUE, "D3DERR_UNSUPPORTEDFACTORVALUE", "Unsupported texture factor value."},
    {D3DERR_CONFLICTINGRENDERSTATE, "D3DERR_CONFLICTINGRENDERSTATE", "Conflicting render states."},
    {D3DERR_UNSUPPORTEDTEXTUREFILTER, "D3DERR_UNSUPPORTEDTEXTUREFILTER", "Unsupported texture filter."},
    {D3DERR_CONFLICTINGTEXTUREPALETTE, "D3DERR_CONFLICTINGTEXTUREPALETTE", "Conflicting texture palettes."},
    {D3DERR_DRIVERINTERNALERROR, "D3DERR_DRIVERINTERNALERROR", "Internal driver error."},
    {D3DERR_NOTFOUND, "D3DERR_NOTFOUND", "The requested item was not found."},
    {D3DERR_MOREDATA, "D3DERR_MOREDATA", "There is more data available than the specified buffer can hold."},
    {D3DERR_DEVICELOST, "D3DERR_DEVICELOST", "The device has been lost but cannot be reset at this time."},
    {D3DERR_DEVICENOTRESET, "D3DERR_DEVICENOTRESET", "The device has been lost but can be reset at this time."},
    {D3DERR_NOTAVAILABLE, "D3DERR_NOTAVAILABLE", "This device does not support the queried technique."},
    {D3DERR_OUTOFVIDEOMEMORY, "D3DERR_OUTOFVIDEOMEMORY", "Out of video memory."},
    {D3DERR_INVALIDDEVICE, "D3DERR_INVALIDDEVICE", "The requested device type is not valid."},
    {D3DERR_INVALIDCALL, "D3DERR_INVALIDCALL", "Invalid call."},
    {D3DERR_DRIVERINVALIDCALL, "D3DERR_DRIVERINVALIDCALL", "Driver invalid call."},
    {D3DERR_WASSTILLDRAWING, "D3DERR_WASSTILLDRAWING", "The device was still drawing."},
    {D3DERR_DEVICEREMOVED, "D3DERR_DEVICEREMOVED", "The hardware adapter has been removed."},
    {D3DERR_DEVICEHUNG, "D3DERR_DEVICEHUNG", "The device stopped responding."},
    {S_PRESENT_OCCLUDED, "S_PRESENT_OCCLUDED", "The presentation area is occluded."},
    {S_PRESENT_MODE_CHANGED, "S_PRESENT_MODE_CHANGED", "The display mode has changed."},
    {E_FAIL, "E_FAIL", "Unspecified error."},
    {E_INVALIDARG, "E_INVALIDARG", "An invalid parameter was passed."},
    {E_OUTOFMEMORY, "E_OUTOFMEMORY", "Out of memory."},
    {E_NOTIMPL, "E_NOTIMPL", "Not implemented."},
    {E_NOINTERFACE, "E_NOINTERFACE", "No such interface supported."},
    {E_POINTER, "E_POINTER", "Invalid pointer."},
};

const char *WINAPI DXGetErrorStringA(HRESULT hr)
{
    for (const ErrInfo &e : kErrors)
        if (e.hr == hr)
            return e.name;
    static thread_local char buf[32];
    snprintf(buf, sizeof(buf), "0x%08lx", (unsigned long)hr);
    return buf;
}

const char *WINAPI DXGetErrorDescriptionA(HRESULT hr)
{
    for (const ErrInfo &e : kErrors)
        if (e.hr == hr)
            return e.desc;
    return "Unknown error.";
}

} // extern "C"
