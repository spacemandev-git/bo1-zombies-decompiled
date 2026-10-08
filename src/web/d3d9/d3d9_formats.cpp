// d3d9_formats.cpp - D3DFORMAT -> WebGL2 format tables and pixel conversions (pure logic, no GL calls).
#include "d3d9_formats.h"

#include <string.h>
#include <math.h>

namespace d3d9shim {

namespace {

constexpr uint32_t kColorRT = FMT_SUPPORTED | FMT_RENDERABLE | FMT_FILTERABLE | FMT_BLENDABLE;

FormatInfo make(D3DFORMAT d3d, const char *name, uint8_t bpb, GLenum ifmt, GLenum fmt, GLenum type, uint8_t glBpp,
                Conv conv, uint32_t flags)
{
    FormatInfo fi;
    fi.d3d = d3d;
    fi.name = name;
    fi.bytesPerBlock = bpb;
    fi.internalFormat = ifmt;
    fi.format = fmt;
    fi.type = type;
    fi.glBytesPerPixel = glBpp;
    fi.conv = conv;
    fi.flags = flags;
    return fi;
}

FormatInfo makeDXT(D3DFORMAT d3d, const char *name, uint8_t bpb, GLenum compressed, Conv decode, const GLCaps &caps,
                   bool alpha)
{
    FormatInfo fi;
    fi.d3d = d3d;
    fi.name = name;
    fi.blockW = fi.blockH = 4;
    fi.bytesPerBlock = bpb;
    fi.flags = FMT_SUPPORTED | FMT_COMPRESSED | FMT_FILTERABLE | (alpha ? FMT_HAS_ALPHA : 0);
    if (caps.s3tc) {
        fi.internalFormat = compressed;
        fi.format = 0;
        fi.type = 0;
        fi.glBytesPerPixel = 0;
        fi.conv = Conv::None;
    } else {
        fi.internalFormat = GL_RGBA8;
        fi.format = GL_RGBA;
        fi.type = GL_UNSIGNED_BYTE;
        fi.glBytesPerPixel = 4;
        fi.conv = decode;
        fi.flags |= FMT_EMULATED;
    }
    return fi;
}

FormatInfo makeDepth(D3DFORMAT d3d, const char *name, uint8_t bpb, GLenum ifmt, GLenum fmt, GLenum type, uint8_t bits,
                     bool stencil)
{
    FormatInfo fi = make(d3d, name, bpb, ifmt, fmt, type, bpb, Conv::None,
                         FMT_SUPPORTED | FMT_DEPTH | (stencil ? FMT_STENCIL : 0));
    fi.depthBits = bits;
    return fi;
}

} // namespace

FormatInfo resolveFormat(D3DFORMAT fmt, const GLCaps &caps)
{
    const uint32_t floatRT = caps.colorBufferFloat ? FMT_RENDERABLE : 0;
    switch ((DWORD)fmt) {
    case D3DFMT_A8R8G8B8:
        return make(fmt, "A8R8G8B8", 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, Conv::BGRA8_RGBA8,
                    kColorRT | FMT_HAS_ALPHA);
    case D3DFMT_X8R8G8B8:
        return make(fmt, "X8R8G8B8", 4, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3, Conv::BGRX8_RGB8, kColorRT);
    case D3DFMT_A8B8G8R8:
        return make(fmt, "A8B8G8R8", 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, Conv::None, kColorRT | FMT_HAS_ALPHA);
    case D3DFMT_X8B8G8R8:
        return make(fmt, "X8B8G8R8", 4, GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, 4, Conv::None, kColorRT);
    case D3DFMT_R8G8B8:
        return make(fmt, "R8G8B8", 3, GL_RGB8, GL_RGB, GL_UNSIGNED_BYTE, 3, Conv::BGR8_RGB8, kColorRT);
    case D3DFMT_R5G6B5:
        return make(fmt, "R5G6B5", 2, GL_RGB565, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 2, Conv::None, kColorRT);
    case D3DFMT_X1R5G5B5:
        return make(fmt, "X1R5G5B5", 2, GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2, Conv::XRGB1555_RGBA5551,
                    kColorRT | FMT_EMULATED);
    case D3DFMT_A1R5G5B5:
        return make(fmt, "A1R5G5B5", 2, GL_RGB5_A1, GL_RGBA, GL_UNSIGNED_SHORT_5_5_5_1, 2, Conv::ARGB1555_RGBA5551,
                    kColorRT | FMT_HAS_ALPHA);
    case D3DFMT_A4R4G4B4:
        return make(fmt, "A4R4G4B4", 2, GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2, Conv::ARGB4444_RGBA4444,
                    kColorRT | FMT_HAS_ALPHA);
    case D3DFMT_X4R4G4B4:
        return make(fmt, "X4R4G4B4", 2, GL_RGBA4, GL_RGBA, GL_UNSIGNED_SHORT_4_4_4_4, 2, Conv::XRGB4444_RGBA4444,
                    kColorRT | FMT_EMULATED);
    case D3DFMT_A2B10G10R10:
        return make(fmt, "A2B10G10R10", 4, GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4, Conv::None,
                    kColorRT | FMT_HAS_ALPHA);
    case D3DFMT_A2R10G10B10:
        return make(fmt, "A2R10G10B10", 4, GL_RGB10_A2, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, 4,
                    Conv::A2R10G10B10_A2B10G10R10, kColorRT | FMT_HAS_ALPHA);
    case D3DFMT_A8:
        return make(fmt, "A8", 1, GL_ALPHA, GL_ALPHA, GL_UNSIGNED_BYTE, 1, Conv::None,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_UNSIZED | FMT_HAS_ALPHA);
    case D3DFMT_L8:
        return make(fmt, "L8", 1, GL_LUMINANCE, GL_LUMINANCE, GL_UNSIGNED_BYTE, 1, Conv::None,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_UNSIZED);
    case D3DFMT_A8L8:
        return make(fmt, "A8L8", 2, GL_LUMINANCE_ALPHA, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, 2, Conv::None,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_UNSIZED | FMT_HAS_ALPHA);
    case D3DFMT_V8U8:
        return make(fmt, "V8U8", 2, GL_RG8_SNORM, GL_RG, GL_BYTE, 2, Conv::None, FMT_SUPPORTED | FMT_FILTERABLE);
    case D3DFMT_Q8W8V8U8:
        return make(fmt, "Q8W8V8U8", 4, GL_RGBA8_SNORM, GL_RGBA, GL_BYTE, 4, Conv::None,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_HAS_ALPHA);
    case D3DFMT_G16R16:
        if (caps.norm16)
            return make(fmt, "G16R16", 4, GL_RG16_EXT, GL_RG, GL_UNSIGNED_SHORT, 4, Conv::None, kColorRT);
        return make(fmt, "G16R16", 4, GL_RG16F, GL_RG, GL_HALF_FLOAT, 4, Conv::U16_F16,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_BLENDABLE | FMT_FLOAT | FMT_EMULATED | floatRT);
    case D3DFMT_A16B16G16R16:
        if (caps.norm16)
            return make(fmt, "A16B16G16R16", 8, GL_RGBA16_EXT, GL_RGBA, GL_UNSIGNED_SHORT, 8, Conv::None,
                        kColorRT | FMT_HAS_ALPHA);
        return make(fmt, "A16B16G16R16", 8, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, Conv::U16_F16,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_BLENDABLE | FMT_FLOAT | FMT_EMULATED | FMT_HAS_ALPHA |
                        ((caps.colorBufferFloat || caps.colorBufferHalfFloat) ? FMT_RENDERABLE : 0));
    case D3DFMT_R16F:
        return make(fmt, "R16F", 2, GL_R16F, GL_RED, GL_HALF_FLOAT, 2, Conv::None,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_BLENDABLE | FMT_FLOAT | floatRT);
    case D3DFMT_G16R16F:
        return make(fmt, "G16R16F", 4, GL_RG16F, GL_RG, GL_HALF_FLOAT, 4, Conv::None,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_BLENDABLE | FMT_FLOAT | floatRT);
    case D3DFMT_A16B16G16R16F:
        return make(fmt, "A16B16G16R16F", 8, GL_RGBA16F, GL_RGBA, GL_HALF_FLOAT, 8, Conv::None,
                    FMT_SUPPORTED | FMT_FILTERABLE | FMT_BLENDABLE | FMT_FLOAT | FMT_HAS_ALPHA |
                        ((caps.colorBufferFloat || caps.colorBufferHalfFloat) ? FMT_RENDERABLE : 0));
    case D3DFMT_R32F:
        return make(fmt, "R32F", 4, GL_R32F, GL_RED, GL_FLOAT, 4, Conv::None,
                    FMT_SUPPORTED | FMT_FLOAT | floatRT | (caps.textureFloatLinear ? FMT_FILTERABLE : 0) |
                        (caps.floatBlend ? FMT_BLENDABLE : 0));
    case D3DFMT_G32R32F:
        return make(fmt, "G32R32F", 8, GL_RG32F, GL_RG, GL_FLOAT, 8, Conv::None,
                    FMT_SUPPORTED | FMT_FLOAT | floatRT | (caps.textureFloatLinear ? FMT_FILTERABLE : 0) |
                        (caps.floatBlend ? FMT_BLENDABLE : 0));
    case D3DFMT_A32B32G32R32F:
        return make(fmt, "A32B32G32R32F", 16, GL_RGBA32F, GL_RGBA, GL_FLOAT, 16, Conv::None,
                    FMT_SUPPORTED | FMT_FLOAT | FMT_HAS_ALPHA | floatRT |
                        (caps.textureFloatLinear ? FMT_FILTERABLE : 0) | (caps.floatBlend ? FMT_BLENDABLE : 0));
    case D3DFMT_DXT1:
        return makeDXT(fmt, "DXT1", 8, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT, Conv::DXT1_RGBA8, caps, true);
    case D3DFMT_DXT2:
        return makeDXT(fmt, "DXT2", 16, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, Conv::DXT3_RGBA8, caps, true);
    case D3DFMT_DXT3:
        return makeDXT(fmt, "DXT3", 16, GL_COMPRESSED_RGBA_S3TC_DXT3_EXT, Conv::DXT3_RGBA8, caps, true);
    case D3DFMT_DXT4:
        return makeDXT(fmt, "DXT4", 16, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, Conv::DXT5_RGBA8, caps, true);
    case D3DFMT_DXT5:
        return makeDXT(fmt, "DXT5", 16, GL_COMPRESSED_RGBA_S3TC_DXT5_EXT, Conv::DXT5_RGBA8, caps, true);
    case D3DFMT_D16:
        return makeDepth(fmt, "D16", 2, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 16, false);
    case D3DFMT_D16_LOCKABLE:
        return makeDepth(fmt, "D16_LOCKABLE", 2, GL_DEPTH_COMPONENT16, GL_DEPTH_COMPONENT, GL_UNSIGNED_SHORT, 16,
                         false);
    case D3DFMT_D24X8:
        return makeDepth(fmt, "D24X8", 4, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 24, false);
    case D3DFMT_D24S8:
        return makeDepth(fmt, "D24S8", 4, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 24, true);
    case D3DFMT_D24X4S4:
        return makeDepth(fmt, "D24X4S4", 4, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 24, true);
    case D3DFMT_D15S1:
        return makeDepth(fmt, "D15S1", 2, GL_DEPTH24_STENCIL8, GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, 24, true);
    case D3DFMT_D32:
    case D3DFMT_D32_LOCKABLE:
        return makeDepth(fmt, "D32", 4, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 24, false);
    case D3DFMT_D32F_LOCKABLE:
        return makeDepth(fmt, "D32F_LOCKABLE", 4, GL_DEPTH_COMPONENT32F, GL_DEPTH_COMPONENT, GL_FLOAT, 24, false);
    case D3DFMT_D24FS8:
        return makeDepth(fmt, "D24FS8", 4, GL_DEPTH32F_STENCIL8, GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV,
                         24, true);
    default:
        break;
    }
    FormatInfo fi;
    fi.d3d = fmt;
    fi.name = formatName(fmt);
    return fi;
}

const char *formatName(D3DFORMAT fmt)
{
    switch ((DWORD)fmt) {
#define N(x) \
    case D3DFMT_##x: return #x;
        N(UNKNOWN) N(R8G8B8) N(A8R8G8B8) N(X8R8G8B8) N(R5G6B5) N(X1R5G5B5) N(A1R5G5B5) N(A4R4G4B4) N(R3G3B2) N(A8)
        N(A8R3G3B2) N(X4R4G4B4) N(A2B10G10R10) N(A8B8G8R8) N(X8B8G8R8) N(G16R16) N(A2R10G10B10) N(A16B16G16R16)
        N(A8P8) N(P8) N(L8) N(A8L8) N(A4L4) N(V8U8) N(L6V5U5) N(X8L8V8U8) N(Q8W8V8U8) N(V16U16) N(A2W10V10U10)
        N(UYVY) N(R8G8_B8G8) N(YUY2) N(G8R8_G8B8) N(DXT1) N(DXT2) N(DXT3) N(DXT4) N(DXT5) N(D16_LOCKABLE) N(D32)
        N(D15S1) N(D24S8) N(D24X8) N(D24X4S4) N(D16) N(D32F_LOCKABLE) N(D24FS8) N(D32_LOCKABLE) N(S8_LOCKABLE)
        N(L16) N(VERTEXDATA) N(INDEX16) N(INDEX32) N(Q16W16V16U16) N(MULTI2_ARGB8) N(R16F) N(G16R16F)
        N(A16B16G16R16F) N(R32F) N(G32R32F) N(A32B32G32R32F) N(CxV8U8) N(A1) N(A2B10G10R10_XR_BIAS)
        N(BINARYBUFFER)
#undef N
    case D3D9SHIM_FOURCC_INTZ: return "INTZ";
    case D3D9SHIM_FOURCC_RESZ: return "RESZ";
    case D3D9SHIM_FOURCC_NULL: return "NULL";
    case D3D9SHIM_FOURCC_ATOC: return "ATOC";
    case D3D9SHIM_FOURCC_SSAA: return "SSAA";
    case D3D9SHIM_FOURCC_DF24: return "DF24";
    case D3D9SHIM_FOURCC_DF16: return "DF16";
    case D3D9SHIM_FOURCC_RAWZ: return "RAWZ";
    default: return "?";
    }
}

UINT formatPitch(const FormatInfo &fi, UINT width)
{
    if (fi.flags & FMT_COMPRESSED)
        return ((width + 3) / 4 ? (width + 3) / 4 : 1) * fi.bytesPerBlock;
    return width * fi.bytesPerBlock;
}

UINT formatRows(const FormatInfo &fi, UINT height)
{
    if (fi.flags & FMT_COMPRESSED)
        return (height + 3) / 4 ? (height + 3) / 4 : 1;
    return height;
}

UINT formatLevelSize(const FormatInfo &fi, UINT width, UINT height, UINT depth)
{
    return formatPitch(fi, width) * formatRows(fi, height) * (depth ? depth : 1);
}

UINT maxMipLevels(UINT w, UINT h, UINT d)
{
    UINT m = w > h ? w : h;
    if (d > m)
        m = d;
    UINT levels = 1;
    while (m > 1) {
        m >>= 1;
        ++levels;
    }
    return levels;
}

size_t uploadPitch(const FormatInfo &fi, UINT w)
{
    if ((fi.flags & FMT_COMPRESSED) && fi.conv == Conv::None)
        return formatPitch(fi, w);
    return (size_t)w * fi.glBytesPerPixel;
}

// ------------------------------------------------------------------------------------------------ half floats

uint16_t floatToHalf(float f)
{
    uint32_t x;
    memcpy(&x, &f, 4);
    uint32_t sign = (x >> 16) & 0x8000;
    int32_t exp = (int32_t)((x >> 23) & 0xff) - 127 + 15;
    uint32_t mant = x & 0x7fffff;
    if (((x >> 23) & 0xff) == 0xff) // inf / nan
        return (uint16_t)(sign | 0x7c00 | (mant ? 0x200 : 0));
    if (exp >= 31)
        return (uint16_t)(sign | 0x7c00);
    if (exp <= 0) {
        if (exp < -10)
            return (uint16_t)sign;
        mant |= 0x800000;
        uint32_t shift = (uint32_t)(14 - exp);
        uint32_t half = mant >> shift;
        uint32_t rem = mant & ((1u << shift) - 1);
        uint32_t mid = 1u << (shift - 1);
        if (rem > mid || (rem == mid && (half & 1)))
            ++half;
        return (uint16_t)(sign | half);
    }
    uint32_t half = sign | ((uint32_t)exp << 10) | (mant >> 13);
    uint32_t rem = mant & 0x1fff;
    if (rem > 0x1000 || (rem == 0x1000 && (half & 1)))
        ++half; // may carry into the exponent, which is the correct rounding
    return (uint16_t)half;
}

float halfToFloat(uint16_t h)
{
    uint32_t sign = (uint32_t)(h & 0x8000) << 16;
    uint32_t exp = (h >> 10) & 0x1f;
    uint32_t mant = h & 0x3ff;
    uint32_t x;
    if (exp == 0) {
        if (mant == 0) {
            x = sign;
        } else {
            exp = 127 - 15 + 1;
            while (!(mant & 0x400)) {
                mant <<= 1;
                --exp;
            }
            mant &= 0x3ff;
            x = sign | (exp << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        x = sign | 0x7f800000 | (mant << 13);
    } else {
        x = sign | ((exp + 127 - 15) << 23) | (mant << 13);
    }
    float f;
    memcpy(&f, &x, 4);
    return f;
}

// ------------------------------------------------------------------------------------------------ S3TC

namespace {

inline void rgb565(uint16_t c, uint8_t out[3])
{
    uint32_t r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = (uint8_t)((r << 3) | (r >> 2));
    out[1] = (uint8_t)((g << 2) | (g >> 4));
    out[2] = (uint8_t)((b << 3) | (b >> 2));
}

void decodeColorBlock(const uint8_t *b, uint8_t out[64], bool allowPunchThrough)
{
    uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8)), c1 = (uint16_t)(b[2] | (b[3] << 8));
    uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || !allowPunchThrough) {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k] + 1) / 3);
            pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k] + 1) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int k = 0; k < 3; ++k) {
            pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
            pal[3][k] = 0;
        }
        pal[2][3] = 255;
        pal[3][3] = 0;
    }
    uint32_t idx = (uint32_t)b[4] | ((uint32_t)b[5] << 8) | ((uint32_t)b[6] << 16) | ((uint32_t)b[7] << 24);
    for (int i = 0; i < 16; ++i) {
        memcpy(out + i * 4, pal[(idx >> (2 * i)) & 3], 4);
    }
}

} // namespace

void decodeDXT1Block(const uint8_t *block, uint8_t out[64]) { decodeColorBlock(block, out, true); }

void decodeDXT3Block(const uint8_t *block, uint8_t out[64])
{
    decodeColorBlock(block + 8, out, false);
    for (int i = 0; i < 16; ++i) {
        uint8_t a = (uint8_t)((block[i / 2] >> ((i & 1) * 4)) & 15);
        out[i * 4 + 3] = (uint8_t)(a | (a << 4));
    }
}

void decodeDXT5Block(const uint8_t *block, uint8_t out[64])
{
    decodeColorBlock(block + 8, out, false);
    uint8_t a[8];
    a[0] = block[0];
    a[1] = block[1];
    if (a[0] > a[1]) {
        for (int i = 1; i < 7; ++i)
            a[i + 1] = (uint8_t)(((7 - i) * a[0] + i * a[1] + 3) / 7);
    } else {
        for (int i = 1; i < 5; ++i)
            a[i + 1] = (uint8_t)(((5 - i) * a[0] + i * a[1] + 2) / 5);
        a[6] = 0;
        a[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i)
        bits |= (uint64_t)block[2 + i] << (8 * i);
    for (int i = 0; i < 16; ++i)
        out[i * 4 + 3] = a[(bits >> (3 * i)) & 7];
}

// ------------------------------------------------------------------------------------------------ conversions

void convertForUpload(const FormatInfo &fi, const uint8_t *src, size_t srcPitch, uint8_t *dst, size_t dstPitch, UINT w,
                      UINT h)
{
    switch (fi.conv) {
    case Conv::None:
    case Conv::V8U8_RG8S: {
        size_t rowBytes = (fi.flags & FMT_COMPRESSED) ? formatPitch(fi, w) : (size_t)w * fi.bytesPerBlock;
        UINT rows = (fi.flags & FMT_COMPRESSED) ? formatRows(fi, h) : h;
        for (UINT y = 0; y < rows; ++y)
            memcpy(dst + y * dstPitch, src + y * srcPitch, rowBytes);
        return;
    }
    case Conv::BGRA8_RGBA8:
        for (UINT y = 0; y < h; ++y) {
            const uint8_t *s = src + y * srcPitch;
            uint8_t *d = dst + y * dstPitch;
            for (UINT x = 0; x < w; ++x, s += 4, d += 4) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[3];
            }
        }
        return;
    case Conv::BGRX8_RGB8:
        for (UINT y = 0; y < h; ++y) {
            const uint8_t *s = src + y * srcPitch;
            uint8_t *d = dst + y * dstPitch;
            for (UINT x = 0; x < w; ++x, s += 4, d += 3) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
            }
        }
        return;
    case Conv::BGR8_RGB8:
        for (UINT y = 0; y < h; ++y) {
            const uint8_t *s = src + y * srcPitch;
            uint8_t *d = dst + y * dstPitch;
            for (UINT x = 0; x < w; ++x, s += 3, d += 3) {
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
            }
        }
        return;
    case Conv::ARGB1555_RGBA5551:
    case Conv::XRGB1555_RGBA5551: {
        const bool forceAlpha = fi.conv == Conv::XRGB1555_RGBA5551;
        for (UINT y = 0; y < h; ++y) {
            const uint16_t *s = (const uint16_t *)(src + y * srcPitch);
            uint16_t *d = (uint16_t *)(dst + y * dstPitch);
            for (UINT x = 0; x < w; ++x) {
                uint16_t v = s[x];
                uint16_t a = forceAlpha ? 1 : (uint16_t)(v >> 15);
                d[x] = (uint16_t)(((v & 0x7fff) << 1) | a);
            }
        }
        return;
    }
    case Conv::ARGB4444_RGBA4444:
    case Conv::XRGB4444_RGBA4444: {
        const bool forceAlpha = fi.conv == Conv::XRGB4444_RGBA4444;
        for (UINT y = 0; y < h; ++y) {
            const uint16_t *s = (const uint16_t *)(src + y * srcPitch);
            uint16_t *d = (uint16_t *)(dst + y * dstPitch);
            for (UINT x = 0; x < w; ++x) {
                uint16_t v = s[x];
                uint16_t a = forceAlpha ? 15 : (uint16_t)(v >> 12);
                d[x] = (uint16_t)(((v & 0x0fff) << 4) | a);
            }
        }
        return;
    }
    case Conv::A2R10G10B10_A2B10G10R10:
        for (UINT y = 0; y < h; ++y) {
            const uint32_t *s = (const uint32_t *)(src + y * srcPitch);
            uint32_t *d = (uint32_t *)(dst + y * dstPitch);
            for (UINT x = 0; x < w; ++x) {
                uint32_t v = s[x];
                uint32_t b = v & 0x3ff, g = (v >> 10) & 0x3ff, r = (v >> 20) & 0x3ff, a = v >> 30;
                d[x] = r | (g << 10) | (b << 20) | (a << 30);
            }
        }
        return;
    case Conv::U16_F16: {
        const UINT channels = fi.bytesPerBlock / 2;
        for (UINT y = 0; y < h; ++y) {
            const uint16_t *s = (const uint16_t *)(src + y * srcPitch);
            uint16_t *d = (uint16_t *)(dst + y * dstPitch);
            for (UINT i = 0; i < w * channels; ++i)
                d[i] = floatToHalf(s[i] / 65535.0f);
        }
        return;
    }
    case Conv::DXT1_RGBA8:
    case Conv::DXT3_RGBA8:
    case Conv::DXT5_RGBA8: {
        uint8_t px[64];
        const UINT bw = (w + 3) / 4, bh = (h + 3) / 4;
        for (UINT by = 0; by < bh; ++by) {
            const uint8_t *row = src + by * srcPitch;
            for (UINT bx = 0; bx < bw; ++bx) {
                const uint8_t *blk = row + bx * fi.bytesPerBlock;
                if (fi.conv == Conv::DXT1_RGBA8)
                    decodeDXT1Block(blk, px);
                else if (fi.conv == Conv::DXT3_RGBA8)
                    decodeDXT3Block(blk, px);
                else
                    decodeDXT5Block(blk, px);
                for (UINT py = 0; py < 4; ++py) {
                    UINT yy = by * 4 + py;
                    if (yy >= h)
                        break;
                    for (UINT pxx = 0; pxx < 4; ++pxx) {
                        UINT xx = bx * 4 + pxx;
                        if (xx >= w)
                            break;
                        memcpy(dst + yy * dstPitch + xx * 4, px + (py * 4 + pxx) * 4, 4);
                    }
                }
            }
        }
        return;
    }
    }
}

bool convertRGBA8ToD3D(D3DFORMAT dstFmt, const uint8_t *rgba, size_t rgbaPitch, uint8_t *dst, size_t dstPitch, UINT w,
                       UINT h)
{
    for (UINT y = 0; y < h; ++y) {
        const uint8_t *s = rgba + y * rgbaPitch;
        uint8_t *d = dst + y * dstPitch;
        for (UINT x = 0; x < w; ++x, s += 4) {
            switch ((DWORD)dstFmt) {
            case D3DFMT_A8R8G8B8:
            case D3DFMT_X8R8G8B8:
                d[x * 4 + 0] = s[2];
                d[x * 4 + 1] = s[1];
                d[x * 4 + 2] = s[0];
                d[x * 4 + 3] = dstFmt == D3DFMT_X8R8G8B8 ? 255 : s[3];
                break;
            case D3DFMT_A8B8G8R8:
            case D3DFMT_X8B8G8R8:
                memcpy(d + x * 4, s, 4);
                if (dstFmt == D3DFMT_X8B8G8R8)
                    d[x * 4 + 3] = 255;
                break;
            case D3DFMT_R8G8B8:
                d[x * 3 + 0] = s[2];
                d[x * 3 + 1] = s[1];
                d[x * 3 + 2] = s[0];
                break;
            case D3DFMT_R5G6B5: {
                uint16_t v = (uint16_t)(((s[0] >> 3) << 11) | ((s[1] >> 2) << 5) | (s[2] >> 3));
                memcpy(d + x * 2, &v, 2);
                break;
            }
            case D3DFMT_X1R5G5B5:
            case D3DFMT_A1R5G5B5: {
                uint16_t a = dstFmt == D3DFMT_X1R5G5B5 ? 1 : (s[3] >= 128);
                uint16_t v = (uint16_t)((a << 15) | ((s[0] >> 3) << 10) | ((s[1] >> 3) << 5) | (s[2] >> 3));
                memcpy(d + x * 2, &v, 2);
                break;
            }
            case D3DFMT_A4R4G4B4:
            case D3DFMT_X4R4G4B4: {
                uint16_t a = dstFmt == D3DFMT_X4R4G4B4 ? 15 : (s[3] >> 4);
                uint16_t v = (uint16_t)((a << 12) | ((s[0] >> 4) << 8) | ((s[1] >> 4) << 4) | (s[2] >> 4));
                memcpy(d + x * 2, &v, 2);
                break;
            }
            case D3DFMT_L8:
                d[x] = s[0];
                break;
            case D3DFMT_A8:
                d[x] = s[3];
                break;
            case D3DFMT_A8L8:
                d[x * 2 + 0] = s[0];
                d[x * 2 + 1] = s[3];
                break;
            default:
                return false;
            }
        }
    }
    return true;
}

bool convertRGBAFloatToD3D(D3DFORMAT dstFmt, const float *rgba, size_t rgbaPitchFloats, uint8_t *dst, size_t dstPitch,
                           UINT w, UINT h)
{
    for (UINT y = 0; y < h; ++y) {
        const float *s = rgba + y * rgbaPitchFloats;
        uint8_t *d = dst + y * dstPitch;
        for (UINT x = 0; x < w; ++x, s += 4) {
            switch ((DWORD)dstFmt) {
            case D3DFMT_R32F:
                memcpy(d + x * 4, s, 4);
                break;
            case D3DFMT_G32R32F:
                memcpy(d + x * 8, s, 8);
                break;
            case D3DFMT_A32B32G32R32F:
                memcpy(d + x * 16, s, 16);
                break;
            case D3DFMT_R16F: {
                uint16_t v = floatToHalf(s[0]);
                memcpy(d + x * 2, &v, 2);
                break;
            }
            case D3DFMT_G16R16F:
                for (int c = 0; c < 2; ++c) {
                    uint16_t v = floatToHalf(s[c]);
                    memcpy(d + x * 4 + c * 2, &v, 2);
                }
                break;
            case D3DFMT_A16B16G16R16F:
                for (int c = 0; c < 4; ++c) {
                    uint16_t v = floatToHalf(s[c]);
                    memcpy(d + x * 8 + c * 2, &v, 2);
                }
                break;
            case D3DFMT_G16R16:
            case D3DFMT_A16B16G16R16: {
                const int n = dstFmt == D3DFMT_G16R16 ? 2 : 4;
                for (int c = 0; c < n; ++c) {
                    float f = s[c] < 0.f ? 0.f : (s[c] > 1.f ? 1.f : s[c]);
                    uint16_t v = (uint16_t)lrintf(f * 65535.f);
                    memcpy(d + x * n * 2 + c * 2, &v, 2);
                }
                break;
            }
            default:
                return false;
            }
        }
    }
    return true;
}

} // namespace d3d9shim
