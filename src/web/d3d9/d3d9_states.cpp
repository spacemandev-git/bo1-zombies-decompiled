// d3d9_states.cpp - D3D9 render/sampler state -> GL mapping (pure logic, no GL calls).
#include "d3d9_states.h"

#include <math.h>
#include <string.h>

namespace d3d9shim {

GLenum glCompareFunc(DWORD c)
{
    switch (c) {
    case D3DCMP_NEVER: return GL_NEVER;
    case D3DCMP_LESS: return GL_LESS;
    case D3DCMP_EQUAL: return GL_EQUAL;
    case D3DCMP_LESSEQUAL: return GL_LEQUAL;
    case D3DCMP_GREATER: return GL_GREATER;
    case D3DCMP_NOTEQUAL: return GL_NOTEQUAL;
    case D3DCMP_GREATEREQUAL: return GL_GEQUAL;
    default: return GL_ALWAYS;
    }
}

GLenum glStencilOp(DWORD op)
{
    switch (op) {
    case D3DSTENCILOP_ZERO: return GL_ZERO;
    case D3DSTENCILOP_REPLACE: return GL_REPLACE;
    case D3DSTENCILOP_INCRSAT: return GL_INCR;
    case D3DSTENCILOP_DECRSAT: return GL_DECR;
    case D3DSTENCILOP_INVERT: return GL_INVERT;
    case D3DSTENCILOP_INCR: return GL_INCR_WRAP;
    case D3DSTENCILOP_DECR: return GL_DECR_WRAP;
    default: return GL_KEEP;
    }
}

GLenum glBlendOp(DWORD op)
{
    switch (op) {
    case D3DBLENDOP_SUBTRACT: return GL_FUNC_SUBTRACT;
    case D3DBLENDOP_REVSUBTRACT: return GL_FUNC_REVERSE_SUBTRACT;
    case D3DBLENDOP_MIN: return GL_MIN;
    case D3DBLENDOP_MAX: return GL_MAX;
    default: return GL_FUNC_ADD;
    }
}

GLenum glAddressMode(DWORD a, bool *approximated)
{
    if (approximated)
        *approximated = false;
    switch (a) {
    case D3DTADDRESS_WRAP: return GL_REPEAT;
    case D3DTADDRESS_MIRROR: return GL_MIRRORED_REPEAT;
    case D3DTADDRESS_CLAMP: return GL_CLAMP_TO_EDGE;
    case D3DTADDRESS_MIRRORONCE: // no MIRROR_CLAMP in WebGL2
        if (approximated)
            *approximated = true;
        return GL_MIRRORED_REPEAT;
    case D3DTADDRESS_BORDER: // no border color in WebGL2
        if (approximated)
            *approximated = true;
        return GL_CLAMP_TO_EDGE;
    default: return GL_REPEAT;
    }
}

static GLenum blendFactor(DWORD b, bool *approx)
{
    switch (b) {
    case D3DBLEND_ZERO: return GL_ZERO;
    case D3DBLEND_ONE: return GL_ONE;
    case D3DBLEND_SRCCOLOR: return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR: return GL_ONE_MINUS_SRC_COLOR;
    case D3DBLEND_SRCALPHA: return GL_SRC_ALPHA;
    case D3DBLEND_INVSRCALPHA: return GL_ONE_MINUS_SRC_ALPHA;
    case D3DBLEND_DESTALPHA: return GL_DST_ALPHA;
    case D3DBLEND_INVDESTALPHA: return GL_ONE_MINUS_DST_ALPHA;
    case D3DBLEND_DESTCOLOR: return GL_DST_COLOR;
    case D3DBLEND_INVDESTCOLOR: return GL_ONE_MINUS_DST_COLOR;
    case D3DBLEND_SRCALPHASAT: return GL_SRC_ALPHA_SATURATE;
    case D3DBLEND_BLENDFACTOR: return GL_CONSTANT_COLOR;
    case D3DBLEND_INVBLENDFACTOR: return GL_ONE_MINUS_CONSTANT_COLOR;
    case D3DBLEND_SRCCOLOR2: *approx = true; return GL_SRC_COLOR;
    case D3DBLEND_INVSRCCOLOR2: *approx = true; return GL_ONE_MINUS_SRC_COLOR;
    default: *approx = true; return GL_ONE;
    }
}

BlendFactors glBlendFactors(DWORD src, DWORD dst)
{
    BlendFactors f;
    f.approximated = false;
    if (src == D3DBLEND_BOTHSRCALPHA) {
        f.src = GL_SRC_ALPHA;
        f.dst = GL_ONE_MINUS_SRC_ALPHA;
        return f;
    }
    if (src == D3DBLEND_BOTHINVSRCALPHA) {
        f.src = GL_ONE_MINUS_SRC_ALPHA;
        f.dst = GL_SRC_ALPHA;
        return f;
    }
    f.src = blendFactor(src, &f.approximated);
    f.dst = blendFactor(dst, &f.approximated);
    return f;
}

float depthBiasToUnits(float bias, int depthBits)
{
    if (bias == 0.f)
        return 0.f;
    if (depthBits <= 0)
        depthBits = 24;
    return bias * (float)(1u << (depthBits > 24 ? 24 : depthBits));
}

void computeRasterState(const DWORD *rs, int depthBits, RasterState *o)
{
    memset(o, 0, sizeof(*o));
    o->depthTest = rs[D3DRS_ZENABLE] != D3DZB_FALSE;
    o->depthWrite = rs[D3DRS_ZWRITEENABLE] != 0;
    o->depthFunc = glCompareFunc(rs[D3DRS_ZFUNC]);

    o->stencilTest = rs[D3DRS_STENCILENABLE] != 0;
    o->stencilRef = (GLint)rs[D3DRS_STENCILREF];
    o->stencilMask = (GLuint)rs[D3DRS_STENCILMASK];
    o->stencilWriteMask = (GLuint)rs[D3DRS_STENCILWRITEMASK];
    o->stencilFunc[0] = glCompareFunc(rs[D3DRS_STENCILFUNC]);
    o->stencilFail[0] = glStencilOp(rs[D3DRS_STENCILFAIL]);
    o->stencilZFail[0] = glStencilOp(rs[D3DRS_STENCILZFAIL]);
    o->stencilPass[0] = glStencilOp(rs[D3DRS_STENCILPASS]);
    if (rs[D3DRS_TWOSIDEDSTENCILMODE]) {
        o->stencilFunc[1] = glCompareFunc(rs[D3DRS_CCW_STENCILFUNC]);
        o->stencilFail[1] = glStencilOp(rs[D3DRS_CCW_STENCILFAIL]);
        o->stencilZFail[1] = glStencilOp(rs[D3DRS_CCW_STENCILZFAIL]);
        o->stencilPass[1] = glStencilOp(rs[D3DRS_CCW_STENCILPASS]);
    } else {
        o->stencilFunc[1] = o->stencilFunc[0];
        o->stencilFail[1] = o->stencilFail[0];
        o->stencilZFail[1] = o->stencilZFail[0];
        o->stencilPass[1] = o->stencilPass[0];
    }

    o->blend = rs[D3DRS_ALPHABLENDENABLE] != 0;
    BlendFactors rgb = glBlendFactors(rs[D3DRS_SRCBLEND], rs[D3DRS_DESTBLEND]);
    o->blendSrcRGB = rgb.src;
    o->blendDstRGB = rgb.dst;
    o->blendEqRGB = glBlendOp(rs[D3DRS_BLENDOP]);
    if (rs[D3DRS_SEPARATEALPHABLENDENABLE]) {
        BlendFactors a = glBlendFactors(rs[D3DRS_SRCBLENDALPHA], rs[D3DRS_DESTBLENDALPHA]);
        o->blendSrcA = a.src;
        o->blendDstA = a.dst;
        o->blendEqA = glBlendOp(rs[D3DRS_BLENDOPALPHA]);
    } else {
        o->blendSrcA = rgb.src;
        o->blendDstA = rgb.dst;
        o->blendEqA = o->blendEqRGB;
    }
    // GL_SRC_ALPHA_SATURATE on the alpha channel is ONE in both APIs.
    D3DCOLOR bf = rs[D3DRS_BLENDFACTOR];
    o->blendColor[0] = ((bf >> 16) & 0xff) / 255.f;
    o->blendColor[1] = ((bf >> 8) & 0xff) / 255.f;
    o->blendColor[2] = (bf & 0xff) / 255.f;
    o->blendColor[3] = ((bf >> 24) & 0xff) / 255.f;

    DWORD cw = rs[D3DRS_COLORWRITEENABLE];
    o->colorMask[0] = (cw & D3DCOLORWRITEENABLE_RED) != 0;
    o->colorMask[1] = (cw & D3DCOLORWRITEENABLE_GREEN) != 0;
    o->colorMask[2] = (cw & D3DCOLORWRITEENABLE_BLUE) != 0;
    o->colorMask[3] = (cw & D3DCOLORWRITEENABLE_ALPHA) != 0;

    // We render every target top-down (clip-space Y negated, see README "Y flip"); window coordinates then equal
    // D3D screen coordinates, so a D3D clockwise triangle is GL counter-clockwise. With glFrontFace(GL_CCW):
    // D3DCULL_CCW (cull D3D-CCW) culls GL back faces, D3DCULL_CW culls GL front faces.
    switch (rs[D3DRS_CULLMODE]) {
    case D3DCULL_CW: o->cullFace = GL_FRONT; break;
    case D3DCULL_CCW: o->cullFace = GL_BACK; break;
    default: o->cullFace = 0; break;
    }

    o->scissorTest = rs[D3DRS_SCISSORTESTENABLE] != 0;

    float slope = dwordAsFloat(rs[D3DRS_SLOPESCALEDEPTHBIAS]);
    float bias = dwordAsFloat(rs[D3DRS_DEPTHBIAS]);
    o->polygonOffset = slope != 0.f || bias != 0.f;
    o->polygonOffsetFactor = slope;
    o->polygonOffsetUnits = depthBiasToUnits(bias, depthBits);

    if (rs[D3DRS_ALPHATESTENABLE] && rs[D3DRS_ALPHAFUNC] != D3DCMP_ALWAYS) {
        o->alphaFunc = (float)rs[D3DRS_ALPHAFUNC];
        o->alphaRef = (rs[D3DRS_ALPHAREF] & 0xff) / 255.f;
    } else {
        o->alphaFunc = 0.f;
        o->alphaRef = 0.f;
    }
}

SamplerParams computeSamplerParams(const DWORD *ss, bool hasMips, bool filterable, float maxAnisoCap)
{
    SamplerParams p;
    DWORD minF = ss[D3DSAMP_MINFILTER], magF = ss[D3DSAMP_MAGFILTER], mipF = ss[D3DSAMP_MIPFILTER];
    const bool minLinear = filterable && (minF == D3DTEXF_LINEAR || minF == D3DTEXF_ANISOTROPIC ||
                                          minF == D3DTEXF_PYRAMIDALQUAD || minF == D3DTEXF_GAUSSIANQUAD);
    const bool magLinear = filterable && (magF == D3DTEXF_LINEAR || magF == D3DTEXF_ANISOTROPIC ||
                                          magF == D3DTEXF_PYRAMIDALQUAD || magF == D3DTEXF_GAUSSIANQUAD);
    p.magFilter = magLinear ? GL_LINEAR : GL_NEAREST;
    if (!hasMips || mipF == D3DTEXF_NONE) {
        p.minFilter = minLinear ? GL_LINEAR : GL_NEAREST;
    } else {
        const bool mipLinear = filterable && mipF == D3DTEXF_LINEAR;
        if (minLinear)
            p.minFilter = mipLinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
        else
            p.minFilter = mipLinear ? GL_NEAREST_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_NEAREST;
    }
    p.wrapS = glAddressMode(ss[D3DSAMP_ADDRESSU]);
    p.wrapT = glAddressMode(ss[D3DSAMP_ADDRESSV]);
    p.wrapR = glAddressMode(ss[D3DSAMP_ADDRESSW]);
    p.maxAnisotropy = 1.f;
    if (filterable && (minF == D3DTEXF_ANISOTROPIC || magF == D3DTEXF_ANISOTROPIC)) {
        float a = (float)ss[D3DSAMP_MAXANISOTROPY];
        if (a < 1.f)
            a = 1.f;
        if (a > maxAnisoCap)
            a = maxAnisoCap;
        p.maxAnisotropy = a;
    }
    p.minLod = (float)ss[D3DSAMP_MAXMIPLEVEL];
    return p;
}

void defaultRenderStates(DWORD rs[256])
{
    memset(rs, 0, 256 * sizeof(DWORD));
    rs[D3DRS_ZENABLE] = D3DZB_FALSE; // TRUE only with an auto depth stencil (set by the device)
    rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    rs[D3DRS_ZWRITEENABLE] = TRUE;
    rs[D3DRS_ALPHATESTENABLE] = FALSE;
    rs[D3DRS_LASTPIXEL] = TRUE;
    rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    rs[D3DRS_ALPHAREF] = 0;
    rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_DITHERENABLE] = FALSE;
    rs[D3DRS_ALPHABLENDENABLE] = FALSE;
    rs[D3DRS_FOGENABLE] = FALSE;
    rs[D3DRS_SPECULARENABLE] = FALSE;
    rs[D3DRS_FOGCOLOR] = 0;
    rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
    rs[D3DRS_FOGSTART] = floatAsDword(0.f);
    rs[D3DRS_FOGEND] = floatAsDword(1.f);
    rs[D3DRS_FOGDENSITY] = floatAsDword(1.f);
    rs[D3DRS_RANGEFOGENABLE] = FALSE;
    rs[D3DRS_STENCILENABLE] = FALSE;
    rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_STENCILREF] = 0;
    rs[D3DRS_STENCILMASK] = 0xFFFFFFFF;
    rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFF;
    rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_CLIPPING] = TRUE;
    rs[D3DRS_LIGHTING] = TRUE;
    rs[D3DRS_AMBIENT] = 0;
    rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
    rs[D3DRS_COLORVERTEX] = TRUE;
    rs[D3DRS_LOCALVIEWER] = TRUE;
    rs[D3DRS_NORMALIZENORMALS] = FALSE;
    rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    rs[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
    rs[D3DRS_CLIPPLANEENABLE] = 0;
    rs[D3DRS_POINTSIZE] = floatAsDword(1.f);
    rs[D3DRS_POINTSIZE_MIN] = floatAsDword(1.f);
    rs[D3DRS_POINTSPRITEENABLE] = FALSE;
    rs[D3DRS_POINTSCALEENABLE] = FALSE;
    rs[D3DRS_POINTSCALE_A] = floatAsDword(1.f);
    rs[D3DRS_POINTSCALE_B] = floatAsDword(0.f);
    rs[D3DRS_POINTSCALE_C] = floatAsDword(0.f);
    rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFF;
    rs[D3DRS_PATCHEDGESTYLE] = D3DPATCHEDGE_DISCRETE;
    rs[D3DRS_DEBUGMONITORTOKEN] = D3DDMT_ENABLE;
    rs[D3DRS_POINTSIZE_MAX] = floatAsDword(64.f);
    rs[D3DRS_INDEXEDVERTEXBLENDENABLE] = FALSE;
    rs[D3DRS_COLORWRITEENABLE] = 0x0000000F;
    rs[D3DRS_TWEENFACTOR] = floatAsDword(0.f);
    rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    rs[D3DRS_POSITIONDEGREE] = D3DDEGREE_CUBIC;
    rs[D3DRS_NORMALDEGREE] = D3DDEGREE_LINEAR;
    rs[D3DRS_SCISSORTESTENABLE] = FALSE;
    rs[D3DRS_SLOPESCALEDEPTHBIAS] = 0;
    rs[D3DRS_ANTIALIASEDLINEENABLE] = FALSE;
    rs[D3DRS_MINTESSELLATIONLEVEL] = floatAsDword(1.f);
    rs[D3DRS_MAXTESSELLATIONLEVEL] = floatAsDword(1.f);
    rs[D3DRS_ADAPTIVETESS_X] = floatAsDword(0.f);
    rs[D3DRS_ADAPTIVETESS_Y] = floatAsDword(0.f);
    rs[D3DRS_ADAPTIVETESS_Z] = floatAsDword(1.f);
    rs[D3DRS_ADAPTIVETESS_W] = floatAsDword(0.f);
    rs[D3DRS_ENABLEADAPTIVETESSELLATION] = FALSE;
    rs[D3DRS_TWOSIDEDSTENCILMODE] = FALSE;
    rs[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    rs[D3DRS_COLORWRITEENABLE1] = 0x0000000F;
    rs[D3DRS_COLORWRITEENABLE2] = 0x0000000F;
    rs[D3DRS_COLORWRITEENABLE3] = 0x0000000F;
    rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFF;
    rs[D3DRS_SRGBWRITEENABLE] = 0;
    rs[D3DRS_DEPTHBIAS] = 0;
    rs[D3DRS_SEPARATEALPHABLENDENABLE] = FALSE;
    rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
}

void defaultSamplerStates(DWORD ss[14])
{
    memset(ss, 0, 14 * sizeof(DWORD));
    ss[D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
    ss[D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
    ss[D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
    ss[D3DSAMP_BORDERCOLOR] = 0;
    ss[D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
    ss[D3DSAMP_MINFILTER] = D3DTEXF_POINT;
    ss[D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
    ss[D3DSAMP_MIPMAPLODBIAS] = 0;
    ss[D3DSAMP_MAXMIPLEVEL] = 0;
    ss[D3DSAMP_MAXANISOTROPY] = 1;
    ss[D3DSAMP_SRGBTEXTURE] = 0;
    ss[D3DSAMP_ELEMENTINDEX] = 0;
    ss[D3DSAMP_DMAPOFFSET] = 0;
}

const char *renderStateName(DWORD rs)
{
    switch (rs) {
#define N(x) \
    case D3DRS_##x: return "D3DRS_" #x;
        N(ZENABLE) N(FILLMODE) N(SHADEMODE) N(ZWRITEENABLE) N(ALPHATESTENABLE) N(LASTPIXEL) N(SRCBLEND) N(DESTBLEND)
        N(CULLMODE) N(ZFUNC) N(ALPHAREF) N(ALPHAFUNC) N(DITHERENABLE) N(ALPHABLENDENABLE) N(FOGENABLE)
        N(SPECULARENABLE) N(FOGCOLOR) N(FOGTABLEMODE) N(FOGSTART) N(FOGEND) N(FOGDENSITY) N(RANGEFOGENABLE)
        N(STENCILENABLE) N(STENCILFAIL) N(STENCILZFAIL) N(STENCILPASS) N(STENCILFUNC) N(STENCILREF) N(STENCILMASK)
        N(STENCILWRITEMASK) N(TEXTUREFACTOR) N(WRAP0) N(WRAP1) N(WRAP2) N(WRAP3) N(WRAP4) N(WRAP5) N(WRAP6) N(WRAP7)
        N(CLIPPING) N(LIGHTING) N(AMBIENT) N(FOGVERTEXMODE) N(COLORVERTEX) N(LOCALVIEWER) N(NORMALIZENORMALS)
        N(DIFFUSEMATERIALSOURCE) N(SPECULARMATERIALSOURCE) N(AMBIENTMATERIALSOURCE) N(EMISSIVEMATERIALSOURCE)
        N(VERTEXBLEND) N(CLIPPLANEENABLE) N(POINTSIZE) N(POINTSIZE_MIN) N(POINTSPRITEENABLE) N(POINTSCALEENABLE)
        N(POINTSCALE_A) N(POINTSCALE_B) N(POINTSCALE_C) N(MULTISAMPLEANTIALIAS) N(MULTISAMPLEMASK) N(PATCHEDGESTYLE)
        N(DEBUGMONITORTOKEN) N(POINTSIZE_MAX) N(INDEXEDVERTEXBLENDENABLE) N(COLORWRITEENABLE) N(TWEENFACTOR)
        N(BLENDOP) N(POSITIONDEGREE) N(NORMALDEGREE) N(SCISSORTESTENABLE) N(SLOPESCALEDEPTHBIAS)
        N(ANTIALIASEDLINEENABLE) N(MINTESSELLATIONLEVEL) N(MAXTESSELLATIONLEVEL) N(ADAPTIVETESS_X) N(ADAPTIVETESS_Y)
        N(ADAPTIVETESS_Z) N(ADAPTIVETESS_W) N(ENABLEADAPTIVETESSELLATION) N(TWOSIDEDSTENCILMODE) N(CCW_STENCILFAIL)
        N(CCW_STENCILZFAIL) N(CCW_STENCILPASS) N(CCW_STENCILFUNC) N(COLORWRITEENABLE1) N(COLORWRITEENABLE2)
        N(COLORWRITEENABLE3) N(BLENDFACTOR) N(SRGBWRITEENABLE) N(DEPTHBIAS) N(WRAP8) N(WRAP9) N(WRAP10) N(WRAP11)
        N(WRAP12) N(WRAP13) N(WRAP14) N(WRAP15) N(SEPARATEALPHABLENDENABLE) N(SRCBLENDALPHA) N(DESTBLENDALPHA)
        N(BLENDOPALPHA)
#undef N
    default: return "D3DRS_?";
    }
}

const char *samplerStateName(DWORD ss)
{
    static const char *names[] = {"?", "ADDRESSU", "ADDRESSV", "ADDRESSW", "BORDERCOLOR", "MAGFILTER", "MINFILTER",
                                  "MIPFILTER", "MIPMAPLODBIAS", "MAXMIPLEVEL", "MAXANISOTROPY", "SRGBTEXTURE",
                                  "ELEMENTINDEX", "DMAPOFFSET"};
    return ss < 14 ? names[ss] : "?";
}

} // namespace d3d9shim
