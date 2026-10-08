// unit_tests.cpp - node-run unit tests of the D3D9 shim's pure logic (no WebGL needed).
//
//   src/web/d3d9/test/run_unit_tests.sh      (builds with emcc, runs under node, validates emitted GLSL with glslang)
//
// Covers: header values/layouts vs the engine's caps checks, format tables + pixel conversions (BGRA, X8R8G8B8, 1555,
// 4444, unorm16->half, S3TC decode), readback conversions, render-state -> GL mapping, depth bias, sampler mapping,
// vertex declaration -> attribute linking (incl. D3DCOLOR swizzle masks), shader translation of hand-assembled SM3
// shaders (GLSL written to argv[1] for glslangValidator), CheckDeviceFormat policy, D3DX helpers, error strings.
#include "../d3d9_internal.h"
#include "shader_asm.h"
#include "shader_corpus.h"

#include <d3dx9.h>
#include <dxerr.h>
#include <math.h>
#include <stdio.h>
#include <string>

using namespace d3d9shim;

static int g_pass = 0, g_fail = 0;
static const char *g_section = "";

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        if (cond) {                                                                       \
            ++g_pass;                                                                     \
        } else {                                                                          \
            ++g_fail;                                                                     \
            printf("FAIL [%s] %s:%d: %s\n", g_section, __FILE__, __LINE__, #cond);        \
        }                                                                                 \
    } while (0)
#define CHECK_EQ(a, b)                                                                                     \
    do {                                                                                                   \
        long long va_ = (long long)(a), vb_ = (long long)(b);                                              \
        if (va_ == vb_) {                                                                                  \
            ++g_pass;                                                                                      \
        } else {                                                                                           \
            ++g_fail;                                                                                      \
            printf("FAIL [%s] %s:%d: %s == %s (%lld vs %lld)\n", g_section, __FILE__, __LINE__, #a, #b, va_, vb_); \
        }                                                                                                  \
    } while (0)
#define CHECK_NEAR(a, b, eps) CHECK(fabs((double)(a) - (double)(b)) <= (eps))

static bool contains(const std::string &s, const char *needle) { return s.find(needle) != std::string::npos; }

// ------------------------------------------------------------------------------------------------ caps vs engine

// Copied from src/gfx_d3d/r_caps.cpp (s_capsCheckBits / s_capsCheckInt): offset, setBits, clearBits, response.
enum { QUIT = 0, WARN = 1, INFO = 2, FORBID_SM3 = 3 };
struct CapBits { int offset; unsigned setBits, clearBits; int response; const char *msg; };
struct CapInt { int offset; unsigned min, max; int response; const char *msg; };
static const CapBits kCapBits[] = {
    {12, 0u, 536870912u, QUIT, "dynamic textures"}, {12, 0u, 131072u, WARN, "fullscreen gamma"},
    {16, 0u, 32u, QUIT, "alpha blending"}, {16, 0u, 256u, WARN, "accelerate dynamic textures"},
    {20, 0u, 2147483648u, WARN, "immediate swap"}, {20, 0u, 1u, WARN, "vsync"},
    {28, 0u, 32768u, QUIT, "DX7"}, {28, 0u, 66560u, WARN, "T&L"}, {28, 0u, 524288u, WARN, "HW raster"},
    {32, 0u, 2u, QUIT, "mask z"}, {32, 0u, 128u, QUIT, "color write enable"}, {32, 0u, 2048u, QUIT, "blend op"},
    {32, 0u, 131072u, QUIT, "separate alpha"}, {32, 0u, 112u, QUIT, "cull modes"},
    {36, 0u, 33554432u, INFO, "slope scale"}, {40, 0u, 141u, QUIT, "z cmp"}, {44, 0u, 1023u, QUIT, "src blend"},
    {48, 0u, 1023u, QUIT, "dst blend"}, {52, 0u, 210u, QUIT, "alpha cmp"}, {60, 0u, 4u, QUIT, "alpha tex"},
    {60, 0u, 2048u, QUIT, "cubemap"}, {60, 0u, 16384u, QUIT, "mipmap"}, {60, 2u, 256u, QUIT, "npot cond"},
    {60, 0u, 1u, WARN, "perspective"}, {60, 32u, 0u, QUIT, "square only"}, {64, 0u, 50529024u, QUIT, "filter"},
    {68, 0u, 50332416u, QUIT, "cube filter"}, {76, 0u, 4u, QUIT, "clamp"}, {76, 0u, 1u, QUIT, "wrap"},
    {136, 0u, 511u, INFO, "stencil ops"}, {212, 0u, 1u, QUIT, "stream offset"}, {244, 0u, 512u, WARN, "stretch"},
    {236, 0u, 1u, QUIT, "ubyte4n"}};
static const CapInt kCapInts[] = {{88, 2048u, 4294967295u, QUIT, "2D size"},     {92, 2048u, 4294967295u, QUIT, "2D size"},
                                  {96, 256u, 4294967295u, QUIT, "3D size"},      {148, 8u, 4294967295u, QUIT, "texcoords"},
                                  {152, 8u, 4294967295u, QUIT, "textures"},      {188, 1u, 4294967295u, QUIT, "dx9"},
                                  {196, 4294836736u, 4294901759u, QUIT, "vs2"},  {204, 4294902272u, 4294967295u, QUIT, "ps2"},
                                  {196, 4294836992u, 4294901759u, FORBID_SM3, "vs3"},
                                  {204, 4294902528u, 4294967295u, FORBID_SM3, "ps3"}};

static void testCaps()
{
    g_section = "caps";
    CHECK_EQ(sizeof(D3DCAPS9), 304);
    CHECK_EQ(sizeof(D3DVERTEXELEMENT9), 8);
    CHECK_EQ(sizeof(D3DLOCKED_RECT), 8);
    CHECK_EQ(sizeof(D3DSURFACE_DESC), 32);
    CHECK_EQ(sizeof(D3DPRESENT_PARAMETERS), 56);
    CHECK_EQ(sizeof(D3DVIEWPORT9), 24);
    CHECK_EQ(sizeof(D3DADAPTER_IDENTIFIER9), 1100);
    GLCaps gl; // defaults
    D3DCAPS9 caps;
    fillD3DCaps(&caps, gl);
    const unsigned char *base = (const unsigned char *)&caps.DeviceType;
    for (const CapBits &c : kCapBits) {
        unsigned bits;
        memcpy(&bits, base + c.offset, 4);
        bool respond = (!c.clearBits || (c.clearBits & ~bits) != 0) && (!c.setBits || (c.setBits & bits) != 0);
        if (respond && (c.response == QUIT || c.response == FORBID_SM3)) {
            printf("  engine would quit: %s\n", c.msg);
            CHECK(!"caps bit check would quit");
        } else {
            CHECK(true);
        }
        if (respond && c.response == WARN)
            printf("  (engine warning expected: %s)\n", c.msg);
    }
    for (const CapInt &c : kCapInts) {
        unsigned v;
        memcpy(&v, base + c.offset, 4);
        bool respond = v < c.min || v > c.max;
        if (respond)
            printf("  engine would respond to: %s (value %u)\n", c.msg, v);
        CHECK(!respond);
    }
    CHECK_EQ(caps.VertexShaderVersion, 0xFFFE0300u);
    CHECK_EQ(caps.PixelShaderVersion, 0xFFFF0300u);
    CHECK_EQ(caps.NumSimultaneousRTs, 1);
    CHECK_EQ(caps.MaxStreamStride, 255);
    CHECK(caps.MaxAnisotropy >= 1);
}

// ------------------------------------------------------------------------------------------------ formats

static void testFormats()
{
    g_section = "formats";
    GLCaps gl;
    FormatInfo f = resolveFormat(D3DFMT_A8R8G8B8, gl);
    CHECK_EQ(f.internalFormat, GL_RGBA8);
    CHECK(f.conv == Conv::BGRA8_RGBA8);
    CHECK(f.flags & FMT_RENDERABLE);
    f = resolveFormat(D3DFMT_X8R8G8B8, gl);
    CHECK_EQ(f.internalFormat, GL_RGB8); // alpha samples as 1, like D3D
    CHECK(f.conv == Conv::BGRX8_RGB8);
    f = resolveFormat(D3DFMT_DXT1, gl);
    CHECK_EQ(f.internalFormat, GL_COMPRESSED_RGBA_S3TC_DXT1_EXT);
    CHECK_EQ(formatPitch(f, 8), 16);
    CHECK_EQ(formatPitch(f, 1), 8);
    CHECK_EQ(formatRows(f, 6), 2);
    CHECK_EQ(formatLevelSize(f, 256, 256), 32768);
    f = resolveFormat(D3DFMT_DXT5, gl);
    CHECK_EQ(formatLevelSize(f, 4, 4), 16);
    GLCaps noS3tc;
    noS3tc.s3tc = false;
    f = resolveFormat(D3DFMT_DXT1, noS3tc);
    CHECK(f.conv == Conv::DXT1_RGBA8 && f.internalFormat == GL_RGBA8);
    CHECK_EQ(uploadPitch(f, 8), 32);
    f = resolveFormat(D3DFMT_A16B16G16R16, gl); // no norm16 by default
    CHECK(f.internalFormat == GL_RGBA16F && f.conv == Conv::U16_F16 && (f.flags & FMT_RENDERABLE));
    GLCaps n16;
    n16.norm16 = true;
    CHECK_EQ(resolveFormat(D3DFMT_A16B16G16R16, n16).internalFormat, GL_RGBA16_EXT);
    f = resolveFormat(D3DFMT_R32F, gl);
    CHECK(f.internalFormat == GL_R32F && !(f.flags & FMT_FILTERABLE) && (f.flags & FMT_RENDERABLE));
    GLCaps lin;
    lin.textureFloatLinear = true;
    CHECK(resolveFormat(D3DFMT_R32F, lin).flags & FMT_FILTERABLE);
    GLCaps nocbf;
    nocbf.colorBufferFloat = false;
    nocbf.colorBufferHalfFloat = false;
    CHECK(!(resolveFormat(D3DFMT_R32F, nocbf).flags & FMT_RENDERABLE));
    CHECK(!(resolveFormat(D3DFMT_A16B16G16R16F, nocbf).flags & FMT_RENDERABLE));
    f = resolveFormat(D3DFMT_L8, gl);
    CHECK(f.internalFormat == GL_LUMINANCE && (f.flags & FMT_UNSIZED) && !(f.flags & FMT_RENDERABLE));
    CHECK_EQ(resolveFormat(D3DFMT_A8, gl).internalFormat, GL_ALPHA);
    CHECK_EQ(resolveFormat(D3DFMT_A8L8, gl).internalFormat, GL_LUMINANCE_ALPHA);
    f = resolveFormat(D3DFMT_D24S8, gl);
    CHECK(f.internalFormat == GL_DEPTH24_STENCIL8 && (f.flags & FMT_DEPTH) && (f.flags & FMT_STENCIL));
    CHECK_EQ(f.depthBits, 24);
    CHECK_EQ(resolveFormat(D3DFMT_D16, gl).depthBits, 16);
    CHECK(!(resolveFormat((D3DFORMAT)D3D9SHIM_FOURCC_INTZ, gl).flags & FMT_SUPPORTED));
    CHECK(!(resolveFormat(D3DFMT_UNKNOWN, gl).flags & FMT_SUPPORTED));
    CHECK(strcmp(formatName((D3DFORMAT)D3D9SHIM_FOURCC_NULL), "NULL") == 0);
    CHECK_EQ(maxMipLevels(256, 128), 9);
    CHECK_EQ(maxMipLevels(1, 1), 1);
    CHECK_EQ(maxMipLevels(300, 7), 9);
    CHECK_EQ(mipDim(5, 3), 1);

    // BGRA -> RGBA
    const uint8_t bgra[8] = {0x10, 0x20, 0x30, 0x40, 0xA0, 0xB0, 0xC0, 0xD0};
    uint8_t out[16] = {0};
    convertForUpload(resolveFormat(D3DFMT_A8R8G8B8, gl), bgra, 8, out, 8, 2, 1);
    CHECK(out[0] == 0x30 && out[1] == 0x20 && out[2] == 0x10 && out[3] == 0x40);
    CHECK(out[4] == 0xC0 && out[5] == 0xB0 && out[6] == 0xA0 && out[7] == 0xD0);
    // BGRX -> RGB (alpha dropped: RGB8 storage)
    memset(out, 0, sizeof(out));
    convertForUpload(resolveFormat(D3DFMT_X8R8G8B8, gl), bgra, 8, out, 6, 2, 1);
    CHECK(out[0] == 0x30 && out[1] == 0x20 && out[2] == 0x10 && out[3] == 0xC0 && out[5] == 0xA0);
    // ARGB1555 -> RGBA5551: A=1 R=31 G=0 B=1  ->  R=31 G=0 B=1 A=1
    uint16_t v1555 = (uint16_t)((1u << 15) | (31u << 10) | 1u), o16 = 0;
    convertForUpload(resolveFormat(D3DFMT_A1R5G5B5, gl), (uint8_t *)&v1555, 2, (uint8_t *)&o16, 2, 1, 1);
    CHECK_EQ(o16, (31u << 11) | (1u << 1) | 1u);
    uint16_t x1555 = (uint16_t)(31u << 10);
    convertForUpload(resolveFormat(D3DFMT_X1R5G5B5, gl), (uint8_t *)&x1555, 2, (uint8_t *)&o16, 2, 1, 1);
    CHECK_EQ(o16 & 1, 1); // forced opaque
    // ARGB4444 -> RGBA4444: A=0xF R=0x1 G=0x2 B=0x3 -> 0x123F
    uint16_t v4444 = 0xF123;
    convertForUpload(resolveFormat(D3DFMT_A4R4G4B4, gl), (uint8_t *)&v4444, 2, (uint8_t *)&o16, 2, 1, 1);
    CHECK_EQ(o16, 0x123F);
    // R5G6B5 passes through (same bit layout in GL_UNSIGNED_SHORT_5_6_5)
    uint16_t v565 = 0xF81F;
    convertForUpload(resolveFormat(D3DFMT_R5G6B5, gl), (uint8_t *)&v565, 2, (uint8_t *)&o16, 2, 1, 1);
    CHECK_EQ(o16, 0xF81F);
    // unorm16 -> half
    uint16_t u16[4] = {0, 65535, 32768, 65535}, h16[4] = {0};
    convertForUpload(resolveFormat(D3DFMT_A16B16G16R16, gl), (uint8_t *)u16, 8, (uint8_t *)h16, 8, 1, 1);
    CHECK_EQ(h16[0], 0);
    CHECK_EQ(h16[1], 0x3C00);
    CHECK_NEAR(halfToFloat(h16[2]), 0.5, 0.001);
    // half round trips
    const float hv[] = {1.f, 0.5f, -2.f, 65504.f, 0.000061035f, 0.f, 3.14159f};
    for (float x : hv)
        CHECK_NEAR(halfToFloat(floatToHalf(x)), x, fabs(x) * 0.001 + 1e-7);
    CHECK_EQ(floatToHalf(1e9f), 0x7C00);

    // S3TC: DXT1 opaque block: c0 = red (0xF800) > c1 = blue (0x001F); all indices 0 -> red
    uint8_t dxt1[8] = {0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0}, px[64];
    decodeDXT1Block(dxt1, px);
    CHECK(px[0] == 255 && px[1] == 0 && px[2] == 0 && px[3] == 255);
    // index 1 everywhere -> blue
    memset(dxt1 + 4, 0x55, 4);
    decodeDXT1Block(dxt1, px);
    CHECK(px[60] == 0 && px[62] == 255 && px[63] == 255);
    // punch-through: c0 <= c1, index 3 -> transparent black
    uint8_t dxt1a[8] = {0x1F, 0x00, 0x00, 0xF8, 0xFF, 0xFF, 0xFF, 0xFF};
    decodeDXT1Block(dxt1a, px);
    CHECK(px[3] == 0 && px[0] == 0 && px[1] == 0 && px[2] == 0);
    // DXT5 alpha: a0=255 a1=0, index 1 -> 0, index 0 -> 255; index 2 -> (6*255+0)/7
    uint8_t dxt5[16] = {255, 0, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    dxt5[2] = 0x01 | (0x02 << 3); // pixel0 idx1, pixel1 idx2
    decodeDXT5Block(dxt5, px);
    CHECK_EQ(px[3], 0);
    CHECK_EQ(px[7], (6 * 255 + 3) / 7);
    CHECK_EQ(px[11], 255);
    CHECK(px[0] == 255 && px[1] == 0); // color block always 4-color mode
    // DXT3 explicit alpha: pixel0 nibble 0x3 -> 0x33, pixel1 nibble 0xC -> 0xCC
    uint8_t dxt3[16] = {0xC3, 0, 0, 0, 0, 0, 0, 0, 0x00, 0xF8, 0x1F, 0x00, 0, 0, 0, 0};
    decodeDXT3Block(dxt3, px);
    CHECK(px[3] == 0x33 && px[7] == 0xCC);
    // whole-level software decode of a 2x2 DXT1 level (partial block)
    uint8_t rgba[16];
    memset(dxt1 + 4, 0, 4);
    convertForUpload(resolveFormat(D3DFMT_DXT1, noS3tc), dxt1, 8, rgba, 8, 2, 2);
    CHECK(rgba[0] == 255 && rgba[12] == 255 && rgba[15] == 255);

    // readback conversions
    const uint8_t rgbaPix[8] = {1, 2, 3, 4, 250, 251, 252, 253};
    uint8_t d3d[8];
    CHECK(convertRGBA8ToD3D(D3DFMT_A8R8G8B8, rgbaPix, 8, d3d, 8, 2, 1));
    CHECK(d3d[0] == 3 && d3d[1] == 2 && d3d[2] == 1 && d3d[3] == 4 && d3d[4] == 252 && d3d[7] == 253);
    CHECK(convertRGBA8ToD3D(D3DFMT_X8R8G8B8, rgbaPix, 8, d3d, 8, 2, 1));
    CHECK(d3d[3] == 255 && d3d[0] == 3);
    uint16_t r565;
    const uint8_t white[4] = {255, 255, 255, 255};
    CHECK(convertRGBA8ToD3D(D3DFMT_R5G6B5, white, 4, (uint8_t *)&r565, 2, 1, 1));
    CHECK_EQ(r565, 0xFFFF);
    CHECK(!convertRGBA8ToD3D(D3DFMT_DXT1, white, 4, d3d, 8, 1, 1));
    const float fl[4] = {0.25f, 0.5f, 0.75f, 1.f};
    float r32;
    CHECK(convertRGBAFloatToD3D(D3DFMT_R32F, fl, 4, (uint8_t *)&r32, 4, 1, 1));
    CHECK_EQ(r32 == 0.25f, 1);
    uint16_t h4[4];
    CHECK(convertRGBAFloatToD3D(D3DFMT_A16B16G16R16F, fl, 4, (uint8_t *)h4, 8, 1, 1));
    CHECK(h4[3] == 0x3C00 && h4[1] == 0x3800);
}

// ------------------------------------------------------------------------------------------------ states

static void testStates()
{
    g_section = "states";
    CHECK_EQ(glCompareFunc(D3DCMP_NEVER), GL_NEVER);
    CHECK_EQ(glCompareFunc(D3DCMP_LESS), GL_LESS);
    CHECK_EQ(glCompareFunc(D3DCMP_EQUAL), GL_EQUAL);
    CHECK_EQ(glCompareFunc(D3DCMP_LESSEQUAL), GL_LEQUAL);
    CHECK_EQ(glCompareFunc(D3DCMP_GREATER), GL_GREATER);
    CHECK_EQ(glCompareFunc(D3DCMP_NOTEQUAL), GL_NOTEQUAL);
    CHECK_EQ(glCompareFunc(D3DCMP_GREATEREQUAL), GL_GEQUAL);
    CHECK_EQ(glCompareFunc(D3DCMP_ALWAYS), GL_ALWAYS);
    CHECK_EQ(glStencilOp(D3DSTENCILOP_INCRSAT), GL_INCR);
    CHECK_EQ(glStencilOp(D3DSTENCILOP_INCR), GL_INCR_WRAP);
    CHECK_EQ(glStencilOp(D3DSTENCILOP_DECR), GL_DECR_WRAP);
    CHECK_EQ(glStencilOp(D3DSTENCILOP_INVERT), GL_INVERT);
    CHECK_EQ(glBlendOp(D3DBLENDOP_REVSUBTRACT), GL_FUNC_REVERSE_SUBTRACT);
    CHECK_EQ(glBlendOp(D3DBLENDOP_MAX), GL_MAX);
    BlendFactors bf = glBlendFactors(D3DBLEND_SRCALPHA, D3DBLEND_INVSRCALPHA);
    CHECK(bf.src == GL_SRC_ALPHA && bf.dst == GL_ONE_MINUS_SRC_ALPHA && !bf.approximated);
    bf = glBlendFactors(D3DBLEND_BOTHSRCALPHA, D3DBLEND_ZERO);
    CHECK(bf.src == GL_SRC_ALPHA && bf.dst == GL_ONE_MINUS_SRC_ALPHA);
    bf = glBlendFactors(D3DBLEND_BOTHINVSRCALPHA, D3DBLEND_ONE);
    CHECK(bf.src == GL_ONE_MINUS_SRC_ALPHA && bf.dst == GL_SRC_ALPHA);
    bf = glBlendFactors(D3DBLEND_DESTCOLOR, D3DBLEND_BLENDFACTOR);
    CHECK(bf.src == GL_DST_COLOR && bf.dst == GL_CONSTANT_COLOR);
    bool approx = false;
    CHECK_EQ(glAddressMode(D3DTADDRESS_BORDER, &approx), GL_CLAMP_TO_EDGE);
    CHECK(approx);
    CHECK_EQ(glAddressMode(D3DTADDRESS_MIRROR), GL_MIRRORED_REPEAT);

    DWORD rs[256];
    defaultRenderStates(rs);
    RasterState r;
    computeRasterState(rs, 24, &r);
    CHECK(!r.depthTest && r.depthWrite && r.depthFunc == GL_LEQUAL);
    CHECK(r.cullFace == GL_BACK); // D3DCULL_CCW default
    CHECK(!r.blend && !r.stencilTest && !r.scissorTest && !r.polygonOffset);
    CHECK(r.colorMask[0] && r.colorMask[3]);
    CHECK(r.alphaFunc == 0.f);
    rs[D3DRS_ZENABLE] = D3DZB_TRUE;
    rs[D3DRS_CULLMODE] = D3DCULL_CW;
    rs[D3DRS_ALPHABLENDENABLE] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_SRCALPHA;
    rs[D3DRS_DESTBLEND] = D3DBLEND_INVSRCALPHA;
    rs[D3DRS_SEPARATEALPHABLENDENABLE] = TRUE;
    rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_MAX;
    rs[D3DRS_COLORWRITEENABLE] = D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_ALPHA;
    rs[D3DRS_STENCILENABLE] = TRUE;
    rs[D3DRS_TWOSIDEDSTENCILMODE] = TRUE;
    rs[D3DRS_STENCILPASS] = D3DSTENCILOP_INCR;
    rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_DECR;
    rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_EQUAL;
    rs[D3DRS_DEPTHBIAS] = floatAsDword(1.0f / (1 << 20));
    rs[D3DRS_SLOPESCALEDEPTHBIAS] = floatAsDword(2.f);
    rs[D3DRS_ALPHATESTENABLE] = TRUE;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_GREATEREQUAL;
    rs[D3DRS_ALPHAREF] = 128;
    computeRasterState(rs, 24, &r);
    CHECK(r.depthTest && r.cullFace == GL_FRONT);
    CHECK(r.blend && r.blendSrcRGB == GL_SRC_ALPHA && r.blendDstRGB == GL_ONE_MINUS_SRC_ALPHA);
    CHECK(r.blendSrcA == GL_ONE && r.blendDstA == GL_ZERO && r.blendEqA == GL_MAX && r.blendEqRGB == GL_FUNC_ADD);
    CHECK(r.colorMask[0] && !r.colorMask[1] && !r.colorMask[2] && r.colorMask[3]);
    CHECK(r.stencilTest && r.stencilPass[0] == GL_INCR_WRAP && r.stencilPass[1] == GL_DECR_WRAP);
    CHECK(r.stencilFunc[0] == GL_ALWAYS && r.stencilFunc[1] == GL_EQUAL);
    CHECK(r.polygonOffset && r.polygonOffsetFactor == 2.f);
    CHECK_NEAR(r.polygonOffsetUnits, 16.0, 1e-4); // 2^-20 normalized = 16 units of 2^-24
    CHECK_NEAR(depthBiasToUnits(1.0f / (1 << 16), 16), 1.0, 1e-6);
    CHECK_EQ((int)r.alphaFunc, D3DCMP_GREATEREQUAL);
    rs[D3DRS_CULLMODE] = D3DCULL_NONE;
    rs[D3DRS_TWOSIDEDSTENCILMODE] = FALSE;
    computeRasterState(rs, 24, &r);
    CHECK(r.cullFace == 0 && r.stencilPass[1] == GL_INCR_WRAP);
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    computeRasterState(rs, 24, &r);
    CHECK(r.alphaFunc == 0.f);
    rs[D3DRS_BLENDFACTOR] = 0x80FF0000;
    computeRasterState(rs, 24, &r);
    CHECK(r.blendColor[0] == 1.f && r.blendColor[1] == 0.f && fabs(r.blendColor[3] - 128 / 255.f) < 1e-6);

    DWORD ss[14];
    defaultSamplerStates(ss);
    SamplerParams sp = computeSamplerParams(ss, true, true, 16.f);
    CHECK(sp.minFilter == GL_NEAREST && sp.magFilter == GL_NEAREST && sp.wrapS == GL_REPEAT); // MIP NONE
    ss[D3DSAMP_MINFILTER] = D3DTEXF_LINEAR;
    ss[D3DSAMP_MAGFILTER] = D3DTEXF_LINEAR;
    ss[D3DSAMP_MIPFILTER] = D3DTEXF_LINEAR;
    ss[D3DSAMP_ADDRESSU] = D3DTADDRESS_CLAMP;
    sp = computeSamplerParams(ss, true, true, 16.f);
    CHECK(sp.minFilter == GL_LINEAR_MIPMAP_LINEAR && sp.magFilter == GL_LINEAR && sp.wrapS == GL_CLAMP_TO_EDGE);
    CHECK(sp.wrapT == GL_REPEAT && sp.maxAnisotropy == 1.f);
    sp = computeSamplerParams(ss, false, true, 16.f);
    CHECK(sp.minFilter == GL_LINEAR); // single level: no mip filter
    sp = computeSamplerParams(ss, true, false, 16.f);
    CHECK(sp.minFilter == GL_NEAREST_MIPMAP_NEAREST && sp.magFilter == GL_NEAREST); // R32F without float_linear
    ss[D3DSAMP_MINFILTER] = D3DTEXF_ANISOTROPIC;
    ss[D3DSAMP_MAXANISOTROPY] = 32;
    ss[D3DSAMP_MIPFILTER] = D3DTEXF_POINT;
    sp = computeSamplerParams(ss, true, true, 8.f);
    CHECK(sp.minFilter == GL_LINEAR_MIPMAP_NEAREST && sp.maxAnisotropy == 8.f);
    CHECK(strcmp(renderStateName(D3DRS_CCW_STENCILFUNC), "D3DRS_CCW_STENCILFUNC") == 0);
}

// ------------------------------------------------------------------------------------------------ vertex declarations

static void testVertexDecl()
{
    g_section = "vertexdecl";
    CHECK(declTypeInfo(D3DDECLTYPE_D3DCOLOR).bgra && declTypeInfo(D3DDECLTYPE_D3DCOLOR).normalized);
    CHECK_EQ(declTypeInfo(D3DDECLTYPE_UBYTE4).normalized, GL_FALSE);
    CHECK_EQ(declTypeInfo(D3DDECLTYPE_UBYTE4N).type, GL_UNSIGNED_BYTE);
    CHECK_EQ(declTypeInfo(D3DDECLTYPE_SHORT4N).type, GL_SHORT);
    CHECK_EQ(declTypeInfo(D3DDECLTYPE_FLOAT16_2).type, GL_HALF_FLOAT);
    CHECK_EQ(declTypeInfo(D3DDECLTYPE_FLOAT3).size, 3);
    CHECK_EQ(declTypeInfo(D3DDECLTYPE_UNUSED).type, 0);
    // Engine-like WORLD declaration (r_material.cpp s_streamSourceInfo): stride 44 stream 0 + stream 2 FLOAT16_2.
    const D3DVERTEXELEMENT9 decl[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0}, {0, 16, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},
        {0, 20, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_TEXCOORD, 0}, {0, 36, D3DDECLTYPE_UBYTE4N, 0, D3DDECLUSAGE_NORMAL, 0},
        {0, 40, D3DDECLTYPE_UBYTE4N, 0, D3DDECLUSAGE_TANGENT, 0}, {2, 0, D3DDECLTYPE_FLOAT16_2, 0, D3DDECLUSAGE_TEXCOORD, 5},
        D3DDECL_END()};
    CHECK_EQ(declElementCount(decl), 6);
    const ShaderInputSlot inputs[] = {{D3DDECLUSAGE_POSITION, 0, 0}, {D3DDECLUSAGE_NORMAL, 0, 1},
                                      {D3DDECLUSAGE_COLOR, 0, 2},    {D3DDECLUSAGE_TEXCOORD, 5, 3},
                                      {D3DDECLUSAGE_BINORMAL, 0, 4}};
    uint32_t bgra = 0, streams = 0;
    std::vector<AttribBinding> b = linkDeclaration(decl, 6, inputs, 5, &bgra, &streams);
    CHECK_EQ(b.size(), 4); // BINORMAL not provided -> generic attribute default
    CHECK_EQ(bgra, 1u << 2);
    CHECK_EQ(streams, (1u << 0) | (1u << 2));
    CHECK(b[0].location == 0 && b[0].offset == 0 && b[0].size == 4 && b[0].type == GL_FLOAT);
    CHECK(b[1].location == 1 && b[1].offset == 36 && b[1].normalized && b[1].type == GL_UNSIGNED_BYTE);
    CHECK(b[2].location == 2 && b[2].offset == 16 && b[2].normalized);
    CHECK(b[3].location == 3 && b[3].stream == 2 && b[3].type == GL_HALF_FLOAT && b[3].size == 2);
    // POSITIONT element feeds a POSITION input; overlapping elements are fine.
    const D3DVERTEXELEMENT9 decl2[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITIONT, 0},
                                       {0, 12, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0}, D3DDECL_END()};
    const ShaderInputSlot in2[] = {{D3DDECLUSAGE_POSITION, 0, 0}, {D3DDECLUSAGE_TEXCOORD, 0, 1}};
    b = linkDeclaration(decl2, declElementCount(decl2), in2, 2, &bgra, nullptr);
    CHECK(b.size() == 2 && bgra == 0 && b[1].offset == 12);
}

// ------------------------------------------------------------------------------------------------ shaders

static std::string g_outDir;

static void writeFile(const std::string &name, const std::string &text)
{
    if (g_outDir.empty())
        return;
    std::string path = g_outDir + "/" + name;
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
        printf("note: cannot write %s\n", path.c_str());
        return;
    }
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
}

static void testShaders()
{
    g_section = "shaders";
    std::vector<DWORD> vs = test_shaders::passThroughVS();
    CHECK_EQ(shaderByteLength(vs.data(), 0), vs.size() * 4);
    TranslatedShader t = translateShader(vs.data(), vs.size() * 4);
    CHECK(t.ok);
    if (!t.ok)
        printf("  errors: %s\n", t.errors.c_str());
    CHECK(t.stage == ShaderStage::Vertex);
    CHECK(t.glsl.compare(0, 16, "#version 300 es\n") == 0);
    CHECK(contains(t.glsl, "precision highp float;"));
    CHECK(contains(t.glsl, "precision highp sampler3D;"));
    CHECK(!contains(t.glsl, "mediump"));
    CHECK(contains(t.glsl, "void d3d9_main()"));
    CHECK(contains(t.glsl, "uniform vec4 d3d9_PosFixup;"));
    CHECK(contains(t.glsl, "gl_Position.z = gl_Position.z * 2.0 - gl_Position.w;"));
    CHECK(contains(t.glsl, "gl_Position.xy += d3d9_PosFixup.zw * gl_Position.ww;"));
    CHECK(contains(t.glsl, "gl_PointSize = 1.0;"));
    CHECK_EQ(t.inputs.size(), 3);
    CHECK(t.inputs.size() == 3 && t.inputs[2].usage == D3DDECLUSAGE_COLOR && t.inputs[2].name == "vs_v2");
    CHECK_EQ(t.outputs.size(), 3);
    CHECK_EQ(t.uniforms.size(), 4);
    CHECK(!t.uniforms.empty() && t.uniforms[3].index == 3 && t.uniforms[3].type == 0);
    writeFile("pass.vert", t.glsl);

    TranslateOptions bgra;
    bgra.bgraInputs.push_back({D3DDECLUSAGE_COLOR, 0});
    TranslatedShader tb = translateShader(vs.data(), vs.size() * 4, &bgra);
    CHECK(tb.ok && contains(tb.glsl, "vs_v2.zyxw"));
    writeFile("pass_bgra.vert", tb.glsl);

    std::vector<DWORD> ps = test_shaders::textureModulatePS();
    TranslatedShader p = translateShader(ps.data(), 0);
    CHECK(p.ok);
    if (!p.ok)
        printf("  errors: %s\n", p.errors.c_str());
    CHECK(p.stage == ShaderStage::Pixel && p.writesColor0);
    CHECK(contains(p.glsl, "uniform sampler2D ps_s0;"));
    CHECK(contains(p.glsl, "layout(location = 0) out highp vec4 _gl_FragData_0;"));
    CHECK(contains(p.glsl, "uniform vec2 d3d9_AlphaTest;"));
    CHECK(contains(p.glsl, "if (!d3d9_pass) discard;"));
    CHECK(!contains(p.glsl, "mediump"));
    CHECK(p.samplers.size() == 1 && p.samplers[0].stage == 0 && p.samplers[0].type == 0);
    CHECK_EQ(p.inputs.size(), 2);
    writeFile("texmod.frag", p.glsl);

    std::vector<DWORD> ps2 = test_shaders::vertexColorPS();
    TranslatedShader p2 = translateShader(ps2.data(), 0);
    CHECK(p2.ok);
    writeFile("vcolor.frag", p2.glsl);

    std::vector<DWORD> misc = test_shaders::miscPS();
    TranslatedShader m = translateShader(misc.data(), 0);
    CHECK(m.ok);
    if (!m.ok)
        printf("  errors: %s\n", m.errors.c_str());
    CHECK(m.usesVPos && contains(m.glsl, "gl_FragCoord.x - 0.5"));
    CHECK(contains(m.glsl, "gl_FrontFacing ? 1.0 : -1.0"));
    CHECK(contains(m.glsl, "uniform samplerCube ps_s2;") && contains(m.glsl, "uniform sampler3D ps_s3;"));
    CHECK(contains(m.glsl, "textureCubeLod(ps_s2"));
    CHECK(contains(m.glsl, "const vec4 ps_c5 = vec4(0.5, 0.25, 0.0, 1.0);"));
    writeFile("misc.frag", m.glsl);

    // Relative addressing without a CTAB: synthetic CTAB fallback makes c0..c255 one array.
    std::vector<DWORD> rel = test_shaders::relativeAddressVS();
    TranslatedShader r = translateShader(rel.data(), 0);
    CHECK(r.ok && r.syntheticCTAB);
    if (!r.ok)
        printf("  errors: %s\n", r.errors.c_str());
    CHECK(contains(r.glsl, "uniform vec4 vs_uniforms_vec4[256];"));
    CHECK(r.uniforms.size() == 1 && r.uniforms[0].count == 256 && !r.uniforms[0].constant);
    writeFile("reladdr.vert", r.glsl);

    // The PS reads TEXCOORD0 + COLOR0; a VS writing only position needs those varyings added.
    ShaderAsm a(ShaderAsm::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_OUTPUT, 0), ShaderAsm::src(D3DSPR_INPUT, 0));
    std::vector<DWORD> posOnly = a.finish();
    TranslateOptions extra;
    extra.extraOutputs = {{D3DDECLUSAGE_TEXCOORD, 0}, {D3DDECLUSAGE_COLOR, 0}};
    TranslatedShader e = translateShader(posOnly.data(), 0, &extra);
    CHECK(e.ok && contains(e.glsl, "out highp vec4 io_5_0;") && contains(e.glsl, "io_10_0 = vec4(0.0);"));
    writeFile("posonly_extra.vert", e.glsl);

    // Packed uniform layout: registers c3 and c7 map to vs_uniforms_vec4[0], [1] in uniform order.
    ShaderAsm c(ShaderAsm::VS30);
    c.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_INPUT, 0));
    c.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    c.op(D3DSIO_MAD, ShaderAsm::dst(D3DSPR_OUTPUT, 0), ShaderAsm::src(D3DSPR_INPUT, 0), ShaderAsm::src(D3DSPR_CONST, 7),
         ShaderAsm::src(D3DSPR_CONST, 3));
    std::vector<DWORD> sparse = c.finish();
    TranslatedShader sp = translateShader(sparse.data(), 0);
    CHECK(sp.ok);
    int packed = 0;
    for (const auto &u : sp.uniforms) {
        char def[96];
        snprintf(def, sizeof(def), "#define vs_c%d vs_uniforms_vec4[%d]", u.index, packed);
        CHECK(contains(sp.glsl, def));
        packed += u.count;
    }
    CHECK_EQ(packed, 2);

    // Garbage is rejected, not crashed on.
    const DWORD junk[] = {0x12345678, 0xFFFF};
    CHECK(!translateShader(junk, sizeof(junk)).ok);
    const DWORD noEnd[] = {0xFFFE0300, 0x01000001, 0x800F0000, 0x90E40000};
    CHECK(!translateShader(noEnd, sizeof(noEnd)).ok);

    // CTAB plumbing: synthetic table round-trips through D3DXGetShaderConstantTable.
    std::vector<DWORD> withCtab = withSyntheticCTAB(vs.data(), vs.size(), 8);
    LPD3DXCONSTANTTABLE ct = nullptr;
    CHECK(SUCCEEDED(D3DXGetShaderConstantTable(withCtab.data(), &ct)) && ct);
    if (ct) {
        const D3DXSHADER_CONSTANTTABLE *tab = (const D3DXSHADER_CONSTANTTABLE *)ct->GetBufferPointer();
        CHECK(tab->Size == 28 && tab->Constants == 1 && tab->Version == 0xFFFE0300);
        const D3DXSHADER_CONSTANTINFO *ci =
            (const D3DXSHADER_CONSTANTINFO *)((const char *)tab + tab->ConstantInfo);
        CHECK(ci->RegisterSet == 2 && ci->RegisterCount == 8);
        CHECK(strcmp((const char *)tab + ci->Name, "c") == 0);
        CHECK_EQ(ct->Release(), 0);
    }
    CHECK(D3DXGetShaderConstantTable(vs.data(), &ct) == D3DXERR_INVALIDDATA);
    TranslatedShader wc = translateShader(withCtab.data(), 0);
    CHECK(wc.ok);
    D3DXSEMANTIC sem[16];
    UINT n = 0;
    CHECK(SUCCEEDED(D3DXGetShaderInputSemantics(vs.data(), sem, &n)) && n == 3);
    CHECK(n == 3 && sem[1].Usage == D3DDECLUSAGE_TEXCOORD && sem[2].Usage == D3DDECLUSAGE_COLOR);
    CHECK_EQ(D3DXGetShaderSize(vs.data()), vs.size() * 4);
    LPD3DXBUFFER buf = nullptr;
    CHECK(SUCCEEDED(D3DXCreateBuffer(64, &buf)) && buf && buf->GetBufferSize() == 64);
    if (buf)
        CHECK_EQ(buf->Release(), 0);
}

static void testCorpus()
{
    g_section = "shader corpus";
    struct Item {
        const char *file;
        std::vector<DWORD> code;
    } items[] = {{"flow.vert", shader_corpus::flowVS()}, {"math.frag", shader_corpus::mathPS()},
                 {"math.vert", shader_corpus::mathVS()}, {"uloop.vert", shader_corpus::uniformLoopVS()}};
    for (auto &it : items) {
        TranslatedShader t = translateShader(it.code.data(), it.code.size() * 4);
        CHECK(t.ok);
        if (!t.ok) {
            printf("  %s: %s\n", it.file, t.errors.c_str());
            continue;
        }
        writeFile(it.file, t.glsl);
    }
    // Predicate token order: D3D stores (dst, pred, src...); MojoShader wants (dst, src..., pred).
    ShaderAsm pa(ShaderAsm::PS30);
    pa.t.push_back(D3DSIO_MOV | D3DSHADER_INSTRUCTION_PREDICATED | (3u << D3DSI_INSTLENGTH_SHIFT));
    pa.t.push_back(0xA1);
    pa.t.push_back(0xB2); // predicate
    pa.t.push_back(0xC3);
    std::vector<DWORD> pt = pa.finish();
    std::vector<DWORD> ro = reorderPredicateTokens(pt.data(), pt.size());
    CHECK(ro.size() == pt.size() && ro[2] == 0xA1 && ro[3] == 0xC3 && ro[4] == 0xB2);
    CHECK(reorderPredicateTokens(items[2].code.data(), items[2].code.size()).empty()); // nothing predicated
    TranslatedShader f = translateShader(items[0].code.data(), 0);
    CHECK(f.ok && f.syntheticCTAB); // relative addressing through aL / a0 without a CTAB
    CHECK(contains(f.glsl, "for ("));  // loop / rep
    TranslatedShader m = translateShader(items[1].code.data(), 0);
    CHECK(m.ok && contains(m.glsl, "discard") && contains(m.glsl, "gl_FragDepth"));
    CHECK(contains(m.glsl, "textureGrad") || contains(m.glsl, "texture2DGrad"));
    TranslatedShader ul = translateShader(items[3].code.data(), 0);
    CHECK(ul.ok && contains(ul.glsl, "int aLend = ") && !contains(ul.glsl, "const int aLend"));
    TranslatedShader v = translateShader(items[2].code.data(), 0);
    CHECK(v.ok && v.writesPointSize);
    CHECK(!contains(v.glsl, "\tgl_PointSize = 1.0;")); // the shader writes PSIZE itself
}

// Real fxc output: the mjpeg YUV->RGB shaders embedded as byte arrays in src/mjpeg/yuv.cpp.
static std::vector<DWORD> parseByteArray(const std::string &src, const char *name)
{
    std::vector<DWORD> out;
    size_t p = src.find(name);
    if (p == std::string::npos)
        return out;
    p = src.find('{', p);
    size_t e = src.find("};", p);
    if (p == std::string::npos || e == std::string::npos)
        return out;
    std::vector<uint8_t> bytes;
    for (size_t i = p + 1; i < e;) {
        if (src[i] >= '0' && src[i] <= '9') {
            unsigned v = (unsigned)strtoul(src.c_str() + i, nullptr, 0);
            bytes.push_back((uint8_t)v);
            while (i < e && src[i] != ',')
                ++i;
        }
        ++i;
    }
    out.resize(bytes.size() / 4);
    memcpy(out.data(), bytes.data(), out.size() * 4);
    return out;
}

static void testRealShaders(const char *yuvPath)
{
    g_section = "fxc shaders";
    FILE *f = fopen(yuvPath, "rb");
    if (!f) {
        printf("note: %s not readable, skipping the mjpeg shader test\n", yuvPath);
        return;
    }
    std::string src;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        src.append(buf, n);
    fclose(f);
    std::vector<DWORD> vs = parseByteArray(src, "g_vs30_mjpeg_shader_vtx[");
    std::vector<DWORD> ps = parseByteArray(src, "g_ps30_mjpeg_shader_yuv[");
    CHECK(vs.size() == 43 && ps.size() == 124);
    if (vs.empty() || ps.empty())
        return;
    CHECK_EQ(vs[0], 0xFFFE0300u);
    CHECK_EQ(ps[0], 0xFFFF0300u);
    TranslatedShader tv = translateShader(vs.data(), vs.size() * 4);
    TranslatedShader tp = translateShader(ps.data(), ps.size() * 4);
    CHECK(tv.ok && tp.ok);
    if (!tv.ok)
        printf("  vs errors: %s\n", tv.errors.c_str());
    if (!tp.ok)
        printf("  ps errors: %s\n", tp.errors.c_str());
    CHECK_EQ(tv.byteLength, vs.size() * 4);
    CHECK_EQ(tp.byteLength, ps.size() * 4);
    writeFile("mjpeg.vert", tv.glsl);
    writeFile("mjpeg.frag", tp.glsl);
    // the engine binds Y/Cr/Cb (+A) planes on samplers 0..
    CHECK(!tp.samplers.empty());
    LPD3DXCONSTANTTABLE ct = nullptr;
    if (SUCCEEDED(D3DXGetShaderConstantTable(ps.data(), &ct)) && ct) {
        CHECK(((const D3DXSHADER_CONSTANTTABLE *)ct->GetBufferPointer())->Size == 28);
        ct->Release();
    }
}

// ------------------------------------------------------------------------------------------------ IDirect3D9 (no device)

static void testDirect3D()
{
    g_section = "direct3d";
    GLCaps gl;
    CHECK(checkDeviceFormat(gl, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, (D3DFORMAT)D3D9SHIM_FOURCC_INTZ) ==
          D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, (D3DFORMAT)D3D9SHIM_FOURCC_RESZ) ==
          D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_RENDERTARGET, D3DRTYPE_SURFACE, (D3DFORMAT)D3D9SHIM_FOURCC_NULL) ==
          D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, 0, D3DRTYPE_SURFACE, (D3DFORMAT)D3D9SHIM_FOURCC_SSAA) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24S8) == D3D_OK);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE, D3DFMT_D24S8) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_SURFACE, D3DFMT_D24FS8) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F) == D3D_OK);
    GLCaps nocbf;
    nocbf.colorBufferFloat = nocbf.colorBufferHalfFloat = false;
    CHECK(checkDeviceFormat(nocbf, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_R32F) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8) == D3D_OK);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_A16B16G16R16F) == D3D_OK);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_DXT1) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE, D3DFMT_L8) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, 0, D3DRTYPE_TEXTURE, D3DFMT_DXT5) == D3D_OK);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_QUERY_FILTER, D3DRTYPE_TEXTURE, D3DFMT_R32F) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_QUERY_SRGBREAD, D3DRTYPE_TEXTURE, D3DFMT_A8R8G8B8) == D3DERR_NOTAVAILABLE);
    CHECK(checkDeviceFormat(gl, D3DUSAGE_AUTOGENMIPMAP, D3DRTYPE_TEXTURE, D3DFMT_DXT1) == D3DOK_NOAUTOGEN);
    CHECK(checkDeviceFormat(gl, 0, D3DRTYPE_VERTEXBUFFER, D3DFMT_VERTEXDATA) == D3D_OK);

    IDirect3D9 *d3d = Direct3DCreate9(D3D_SDK_VERSION);
    CHECK(d3d != nullptr);
    CHECK_EQ(d3d->GetAdapterCount(), 1);
    D3DADAPTER_IDENTIFIER9 id;
    CHECK(SUCCEEDED(d3d->GetAdapterIdentifier(0, 0, &id)));
    CHECK(id.VendorId == 0 && strncmp(id.Description, "WebGL2", 6) == 0);
    d3d9shim_set_display_size(2560, 1440);
    D3DDISPLAYMODE mode;
    CHECK(SUCCEEDED(d3d->GetAdapterDisplayMode(0, &mode)) && mode.Width == 2560 && mode.Height == 1440);
    UINT modes = d3d->GetAdapterModeCount(0, D3DFMT_X8R8G8B8);
    CHECK(modes > 5);
    bool sawDesktop = false;
    for (UINT i = 0; i < modes; ++i) {
        CHECK(SUCCEEDED(d3d->EnumAdapterModes(0, D3DFMT_X8R8G8B8, i, &mode)));
        sawDesktop |= mode.Width == 2560 && mode.Height == 1440;
        CHECK(mode.Width <= 2560 && mode.Height <= 1440);
    }
    CHECK(sawDesktop);
    DWORD q = 7;
    CHECK(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES, &q) ==
          D3DERR_NOTAVAILABLE);
    CHECK(SUCCEEDED(d3d->CheckDeviceMultiSampleType(0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, TRUE, D3DMULTISAMPLE_NONE, &q)));
    CHECK(SUCCEEDED(d3d->CheckDepthStencilMatch(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DFMT_A8R8G8B8, D3DFMT_D24S8)));
    D3DCAPS9 caps;
    CHECK(SUCCEEDED(d3d->GetDeviceCaps(0, D3DDEVTYPE_HAL, &caps)) && caps.PixelShaderVersion == D3DPS_VERSION(3, 0));
    CHECK(d3d->GetAdapterMonitor(0) != nullptr);
    CHECK_EQ(d3d->Release(), 0);
    IDirect3D9Ex *ex = nullptr;
    CHECK(SUCCEEDED(Direct3DCreate9Ex(D3D_SDK_VERSION, &ex)) && ex);
    if (ex)
        CHECK_EQ(ex->Release(), 0);

    CHECK(strcmp(DXGetErrorStringA(D3DERR_DEVICELOST), "D3DERR_DEVICELOST") == 0);
    CHECK(strcmp(DXGetErrorStringA(D3DERR_NOTAVAILABLE), "D3DERR_NOTAVAILABLE") == 0);
    CHECK(strcmp(DXGetErrorDescriptionA(D3D_OK), "No error occurred.") == 0);
    CHECK_EQ(D3DERR_DEVICELOST, (HRESULT)0x88760868);
    CHECK_EQ(D3DPERF_BeginEvent(0xffffffff, L"Float Z"), 0);
    CHECK_EQ(D3DPERF_EndEvent(), 0);
}

// --corpus <dump-dir> <out-dir>: re-translate every *.bin shader of a d3d9shim shader dump (or any directory of raw
// D3D9 shader bytecode files) and write <name>.vert/.frag for glslangValidator. Exit 1 if any translation failed.
#include <dirent.h>
static int runCorpus(const char *dir, const char *outDir)
{
    DIR *d = opendir(dir);
    if (!d) {
        printf("cannot open %s\n", dir);
        return 2;
    }
    int ok = 0, bad = 0;
    std::vector<std::string> names;
    while (dirent *e = readdir(d)) {
        std::string n = e->d_name;
        if (n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0)
            names.push_back(n);
    }
    closedir(d);
    g_outDir = outDir ? outDir : "";
    for (const std::string &n : names) {
        std::string path = std::string(dir) + "/" + n;
        FILE *f = fopen(path.c_str(), "rb");
        if (!f)
            continue;
        std::vector<uint8_t> bytes;
        uint8_t buf[4096];
        size_t k;
        while ((k = fread(buf, 1, sizeof(buf), f)) > 0)
            bytes.insert(bytes.end(), buf, buf + k);
        fclose(f);
        std::vector<DWORD> tokens(bytes.size() / 4);
        memcpy(tokens.data(), bytes.data(), tokens.size() * 4);
        TranslatedShader t = tokens.empty() ? TranslatedShader() : translateShader(tokens.data(), tokens.size() * 4);
        if (!t.ok) {
            ++bad;
            printf("FAIL %s: %s\n", n.c_str(), t.errors.c_str());
            continue;
        }
        ++ok;
        writeFile(n.substr(0, n.size() - 4) + (t.stage == ShaderStage::Vertex ? ".vert" : ".frag"), t.glsl);
    }
    printf("corpus %s: %d translated, %d failed\n", dir, ok, bad);
    return bad ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 2 && strcmp(argv[1], "--corpus") == 0)
        return runCorpus(argv[2], argc > 3 ? argv[3] : nullptr);
    if (argc > 1)
        g_outDir = argv[1];
    const char *yuv = argc > 2 ? argv[2] : "src/mjpeg/yuv.cpp";
    d3d9shim_set_log_level(0);
    testCaps();
    testFormats();
    testStates();
    testVertexDecl();
    testShaders();
    testCorpus();
    testRealShaders(yuv);
    testDirect3D();
    printf("d3d9shim unit tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
