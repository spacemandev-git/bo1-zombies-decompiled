// d3d9_states.h - D3D9 render/sampler state -> GL mapping (pure logic, no GL calls).
#pragma once

#include <d3d9.h>
#include <GLES3/gl3.h>
#include <stdint.h>

namespace d3d9shim {

GLenum glCompareFunc(DWORD d3dcmp);       // D3DCMPFUNC -> GL_NEVER..GL_ALWAYS (invalid -> GL_ALWAYS)
GLenum glStencilOp(DWORD d3dop);          // D3DSTENCILOP -> GL_KEEP..
GLenum glBlendOp(DWORD d3dop);            // D3DBLENDOP -> GL_FUNC_ADD..
GLenum glAddressMode(DWORD d3daddr, bool *approximated = nullptr); // D3DTEXTUREADDRESS -> GL wrap mode
const char *renderStateName(DWORD rs);
const char *samplerStateName(DWORD ss);

// Blend factors. D3DBLEND_BOTHSRCALPHA / BOTHINVSRCALPHA override the destination factor, so translate pairs.
struct BlendFactors {
    GLenum src, dst;
    bool approximated; // a factor WebGL2 cannot express (SRCCOLOR2 dual-source) was replaced
};
BlendFactors glBlendFactors(DWORD d3dsrc, DWORD d3ddst);

// Complete fixed-function output state derived from the D3D render-state block (rs[] indexed by D3DRENDERSTATETYPE).
struct RasterState {
    // depth
    bool depthTest;
    bool depthWrite;
    GLenum depthFunc;
    // stencil (front = D3D clockwise = GL front with glFrontFace(GL_CCW) in our flipped rendering; back = CCW)
    bool stencilTest;
    GLenum stencilFunc[2]; // [0] front, [1] back
    GLint stencilRef;
    GLuint stencilMask, stencilWriteMask;
    GLenum stencilFail[2], stencilZFail[2], stencilPass[2];
    // blend
    bool blend;
    GLenum blendSrcRGB, blendDstRGB, blendSrcA, blendDstA, blendEqRGB, blendEqA;
    float blendColor[4];
    // color mask
    bool colorMask[4];
    // cull (GL face to cull, or 0 = culling disabled)
    GLenum cullFace;
    // scissor
    bool scissorTest;
    // polygon offset
    bool polygonOffset;
    float polygonOffsetFactor, polygonOffsetUnits;
    // alpha test (shader uniform): func 0 = disabled/always pass, else D3DCMPFUNC; ref in [0,1]
    float alphaFunc, alphaRef;
};

// depthBits: bits of the bound depth buffer (16/24), for converting D3DRS_DEPTHBIAS (normalized depth units) to
// glPolygonOffset units.
void computeRasterState(const DWORD *rs, int depthBits, RasterState *out);

// D3DRS_DEPTHBIAS (float bits, normalized depth) -> glPolygonOffset "units" for a depth buffer of `depthBits`.
float depthBiasToUnits(float d3dDepthBias, int depthBits);

// Sampler state -> GL sampler parameters.
struct SamplerParams {
    GLenum minFilter, magFilter;
    GLenum wrapS, wrapT, wrapR;
    float maxAnisotropy; // 1 = off
    float minLod;        // D3DSAMP_MAXMIPLEVEL
};
// ss is indexed by D3DSAMPLERSTATETYPE (14 entries). `hasMips`: texture has more than one level; `filterable`:
// format supports linear filtering; maxAnisoCap: implementation limit (1 if no extension).
SamplerParams computeSamplerParams(const DWORD *ss, bool hasMips, bool filterable, float maxAnisoCap);

// D3D9 default render state values (index = D3DRENDERSTATETYPE), as after CreateDevice/Reset.
void defaultRenderStates(DWORD rs[256]);
void defaultSamplerStates(DWORD ss[14]);

inline float dwordAsFloat(DWORD v)
{
    union {
        DWORD d;
        float f;
    } u;
    u.d = v;
    return u.f;
}
inline DWORD floatAsDword(float f)
{
    union {
        DWORD d;
        float f;
    } u;
    u.f = f;
    return u.d;
}

} // namespace d3d9shim
