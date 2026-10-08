/*
 * d3d9shim.h - web-only controls of the D3D9 -> WebGL2 shim (not part of Direct3D). Safe to include from engine
 * platform code (src/web/...). All functions are C ABI and may be called before Direct3DCreate9.
 */
#ifndef D3D9SHIM_CONTROL_H
#define D3D9SHIM_CONTROL_H

#ifdef __cplusplus
extern "C" {
#endif

/* CSS selector of the canvas CreateDevice renders to (default "#canvas"). Copied. */
void d3d9shim_set_canvas_selector(const char *selector);

/* Log verbosity: 0 errors, 1 warnings (default), 2 info, 3 debug. Also read once from the environment variable
 * D3D9SHIM_LOG (Module.ENV / -sENVIRONMENT). */
void d3d9shim_set_log_level(int level);

/* Mirror of the engine's r_logFile: when on, every IDirect3DDevice9 call is printed (very verbose). Also enabled by
 * the environment variable D3D9SHIM_TRACE=1. */
void d3d9shim_set_trace(int enabled);

/* Debugging: call glGetError after every draw, clear, copy and present and log GL errors with the D3D call that
 * caused them (slow: forces synchronization). Also enabled by D3D9SHIM_GLCHECK=1. */
void d3d9shim_set_gl_check(int enabled);

/* Shader dump for bring-up: writes <dir>/<vs|ps>_<hash>.bin (D3D bytecode), .glsl (translation) and .log (errors)
 * for every shader (all != 0) or only for shaders that fail to translate, compile or link. The directory must exist
 * (e.g. /opfs/bo1/home/shaders). Re-check a dump offline with
 *   node build/web-d3d9/unit/unit_tests.js --corpus <dump-dir> <out-dir>   (see src/web/d3d9/README.md).
 * Also enabled by D3D9SHIM_SHADER_DUMP=<dir> (+ D3D9SHIM_SHADER_DUMP_ALL=1). NULL/"" disables. */
void d3d9shim_set_shader_dump(const char *dir, int all);

/* Size reported by IDirect3D9::GetAdapterDisplayMode / EnumAdapterModes as the desktop mode (default 1920x1080).
 * The platform layer should pass the screen size (CSS px * devicePixelRatio) before the renderer initializes. */
void d3d9shim_set_display_size(int width, int height);

/* Value GetData returns for an occlusion query whose draws produced any sample (WebGL2 only reports any/none).
 * Default 0x10000. Ratios between two queries (coronas, sun) then come out as 0 or 1. */
void d3d9shim_set_occlusion_visible_count(unsigned count);

/* Counters for the last presented frame (draw calls, shader programs linked so far, texture uploads in bytes). */
typedef struct d3d9shim_stats {
    unsigned frame;
    unsigned draws;
    unsigned programsLinked;
    unsigned programsFailed;
    unsigned shadersFailed;
    unsigned long long textureUploadBytes;
    unsigned long long bufferUploadBytes;
} d3d9shim_stats;
void d3d9shim_get_stats(d3d9shim_stats *out);

#ifdef __cplusplus
}
#endif

#endif /* D3D9SHIM_CONTROL_H */
