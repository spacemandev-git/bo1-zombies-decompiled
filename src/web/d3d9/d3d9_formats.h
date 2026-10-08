// d3d9_formats.h - D3DFORMAT -> WebGL2 format tables and pixel conversions (pure logic, no GL calls).
#pragma once

#include <d3d9.h>
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <stddef.h>
#include <stdint.h>

#ifndef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT1_EXT 0x83F1
#define GL_COMPRESSED_RGBA_S3TC_DXT3_EXT 0x83F2
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif
#ifndef GL_R16_EXT
#define GL_R16_EXT 0x822A
#define GL_RG16_EXT 0x822C
#define GL_RGBA16_EXT 0x805B
#endif
#ifndef GL_TEXTURE_MAX_ANISOTROPY_EXT
#define GL_TEXTURE_MAX_ANISOTROPY_EXT 0x84FE
#define GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT 0x84FF
#endif

namespace d3d9shim {

// What the WebGL2 implementation offers (filled from the live context, or from a probe context before
// CreateDevice; see d3d9_caps.cpp). Defaults describe a typical desktop browser.
struct GLCaps {
    bool valid = false;           // filled from a real context
    int maxTextureSize = 8192;
    int maxCubeMapSize = 8192;
    int max3DTextureSize = 2048;
    int maxRenderbufferSize = 8192;
    int maxVertexAttribs = 16;
    int maxTextureUnits = 16;     // MAX_TEXTURE_IMAGE_UNITS (fragment)
    int maxVertexUniformVectors = 256;
    int maxFragmentUniformVectors = 224;
    float maxAnisotropy = 16.f;
    float maxPointSize = 64.f;
    bool s3tc = true;             // WEBGL_compressed_texture_s3tc
    bool colorBufferFloat = true; // EXT_color_buffer_float (R32F, RGBA16F, RG16F... renderable)
    bool colorBufferHalfFloat = true;
    bool textureFloatLinear = false; // OES_texture_float_linear (R32F filtering)
    bool anisotropic = true;      // EXT_texture_filter_anisotropic
    bool norm16 = false;          // EXT_texture_norm16 (R16/RG16/RGBA16 unorm, renderable)
    bool floatBlend = false;      // EXT_float_blend (blending into 32-bit float targets)
    char renderer[128] = "WebGL2";
    char vendor[64] = "Browser";
};

enum class Conv : uint8_t {
    None,
    BGRA8_RGBA8,    // A8R8G8B8: bytes B,G,R,A -> R,G,B,A
    BGRX8_RGB8,     // X8R8G8B8: bytes B,G,R,X -> R,G,B (RGB8 so alpha samples as 1, like D3D)
    BGR8_RGB8,      // R8G8B8 (24-bit): B,G,R -> R,G,B
    ARGB1555_RGBA5551,
    XRGB1555_RGBA5551, // X1R5G5B5: alpha forced to 1
    ARGB4444_RGBA4444,
    XRGB4444_RGBA4444,
    A2R10G10B10_A2B10G10R10,
    U16_F16,        // 16-bit unorm channels -> half floats (A16B16G16R16 / G16R16 / L16 without EXT_texture_norm16)
    DXT1_RGBA8,     // software S3TC decode (no WEBGL_compressed_texture_s3tc or non-multiple-of-4 level 0)
    DXT3_RGBA8,
    DXT5_RGBA8,
    V8U8_RG8S,      // signed formats: stored as-is (RG8_SNORM)
};

enum FormatFlags : uint32_t {
    FMT_COMPRESSED = 1u << 0,  // block compressed (4x4)
    FMT_DEPTH = 1u << 1,
    FMT_STENCIL = 1u << 2,
    FMT_RENDERABLE = 1u << 3,  // can be a color attachment
    FMT_FILTERABLE = 1u << 4,  // LINEAR filtering allowed
    FMT_UNSIZED = 1u << 5,     // legacy unsized format (LUMINANCE/ALPHA/LUMINANCE_ALPHA): glTexImage2D, no TexStorage
    FMT_FLOAT = 1u << 6,       // float/half color
    FMT_HAS_ALPHA = 1u << 7,
    FMT_SUPPORTED = 1u << 8,   // usable as a texture at all
    FMT_BLENDABLE = 1u << 9,
    FMT_EMULATED = 1u << 10,   // stored in a different GL format than D3D implies (conversion on upload/readback)
};

struct FormatInfo {
    D3DFORMAT d3d = D3DFMT_UNKNOWN;
    const char *name = "UNKNOWN";
    uint8_t blockW = 1, blockH = 1;
    uint8_t bytesPerBlock = 0; // D3D memory: bytes per pixel (uncompressed) or per 4x4 block
    GLenum internalFormat = 0; // GL storage
    GLenum format = 0;         // GL upload/read format
    GLenum type = 0;           // GL upload/read type
    uint8_t glBytesPerPixel = 0; // bytes per pixel of the converted upload data (0 for compressed uploads)
    Conv conv = Conv::None;
    uint32_t flags = 0;
    uint8_t depthBits = 0;     // depth formats: bits for polygon-offset scaling
};

// Resolves a D3D format to its GL representation given the context's capabilities. Unknown/unsupported formats
// return an info without FMT_SUPPORTED.
FormatInfo resolveFormat(D3DFORMAT fmt, const GLCaps &caps);
const char *formatName(D3DFORMAT fmt);

// D3D memory layout helpers (pitch = bytes per row of pixels, or per row of 4x4 blocks).
UINT formatPitch(const FormatInfo &fi, UINT width);
UINT formatRows(const FormatInfo &fi, UINT height); // rows of pixels or of blocks
UINT formatLevelSize(const FormatInfo &fi, UINT width, UINT height, UINT depth = 1);
inline UINT mipDim(UINT base, UINT level) { UINT v = base >> level; return v ? v : 1; }
UINT maxMipLevels(UINT w, UINT h, UINT d = 1);

// Converts a rectangle of D3D-layout pixels to the GL upload layout of `fi` (no-op copy for Conv::None).
// For the DXT decode conversions, w/h are in pixels and src points at blocks (srcPitch = bytes per block row).
void convertForUpload(const FormatInfo &fi, const uint8_t *src, size_t srcPitch, uint8_t *dst, size_t dstPitch,
                      UINT w, UINT h);
// Bytes per row of the converted upload data for `w` pixels (tight).
size_t uploadPitch(const FormatInfo &fi, UINT w);

// Readback: converts tightly packed GL RGBA8 rows (glReadPixels RGBA/UNSIGNED_BYTE) to D3D layout for 8-bit
// color formats (A8R8G8B8, X8R8G8B8, A8B8G8R8, R5G6B5, ...). Returns false if the format has no 8-bit path.
bool convertRGBA8ToD3D(D3DFORMAT dstFmt, const uint8_t *rgba, size_t rgbaPitch, uint8_t *dst, size_t dstPitch, UINT w,
                       UINT h);
// Readback for float targets: converts RGBA float rows (glReadPixels RGBA/FLOAT) to D3D layout (R32F, G32R32F,
// A32B32G32R32F, R16F, G16R16F, A16B16G16R16F, A16B16G16R16, G16R16).
bool convertRGBAFloatToD3D(D3DFORMAT dstFmt, const float *rgba, size_t rgbaPitchFloats, uint8_t *dst, size_t dstPitch,
                           UINT w, UINT h);

// S3TC block decoders (one 4x4 block -> 16 RGBA8 pixels, row-major).
void decodeDXT1Block(const uint8_t *block, uint8_t out[64]);
void decodeDXT3Block(const uint8_t *block, uint8_t out[64]);
void decodeDXT5Block(const uint8_t *block, uint8_t out[64]);

uint16_t floatToHalf(float f);
float halfToFloat(uint16_t h);

} // namespace d3d9shim
