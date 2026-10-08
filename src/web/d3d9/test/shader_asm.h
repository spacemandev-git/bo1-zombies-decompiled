// shader_asm.h - a tiny D3D9 shader-model-3 token-stream builder for the shim's tests (header-only).
//
// Builds the same DWORD tokens fxc emits, e.g.
//   ShaderAsm a(ShaderAsm::VS30);
//   a.dcl(D3DDECLUSAGE_POSITION, 0, a.dst(D3DSPR_INPUT, 0));
//   a.op(D3DSIO_DP4, a.dst(D3DSPR_OUTPUT, 0, 0x1), a.src(D3DSPR_INPUT, 0), a.src(D3DSPR_CONST, 0));
//   std::vector<DWORD> tokens = a.finish();
#pragma once

#include <d3d9.h>
#include <initializer_list>
#include <vector>

struct ShaderAsm {
    static constexpr DWORD VS30 = 0xFFFE0300;
    static constexpr DWORD PS30 = 0xFFFF0300;
    static constexpr DWORD VS20 = 0xFFFE0200;
    static constexpr DWORD PS20 = 0xFFFF0200;

    std::vector<DWORD> t;
    explicit ShaderAsm(DWORD version) { t.push_back(version); }

    static DWORD regBits(DWORD type, DWORD num)
    {
        return 0x80000000u | (num & D3DSP_REGNUM_MASK) | ((type << D3DSP_REGTYPE_SHIFT) & D3DSP_REGTYPE_MASK) |
               ((type << D3DSP_REGTYPE_SHIFT2) & D3DSP_REGTYPE_MASK2);
    }
    // writeMask: bits x=1,y=2,z=4,w=8
    static DWORD dst(DWORD type, DWORD num, DWORD writeMask = 0xF, DWORD mod = 0)
    {
        return regBits(type, num) | ((writeMask & 0xF) << 16) | mod;
    }
    // swizzle given as 4 component indices (0=x..3=w)
    static DWORD swz(int x, int y, int z, int w) { return (DWORD)((x | (y << 2) | (z << 4) | (w << 6)) << 16); }
    static DWORD src(DWORD type, DWORD num, DWORD swizzle = D3DSP_NOSWIZZLE, DWORD mod = D3DSPSM_NONE)
    {
        return regBits(type, num) | swizzle | mod;
    }

    void op(DWORD opcode, std::initializer_list<DWORD> params, DWORD control = 0)
    {
        t.push_back(opcode | ((DWORD)params.size() << D3DSI_INSTLENGTH_SHIFT) | control);
        for (DWORD p : params)
            t.push_back(p);
    }
    void op(DWORD opcode, DWORD d, DWORD s0) { op(opcode, {d, s0}); }
    void op(DWORD opcode, DWORD d, DWORD s0, DWORD s1) { op(opcode, {d, s0, s1}); }
    void op(DWORD opcode, DWORD d, DWORD s0, DWORD s1, DWORD s2) { op(opcode, {d, s0, s1, s2}); }

    // dcl_<usage><index> reg
    void dcl(DWORD usage, DWORD index, DWORD dstReg)
    {
        t.push_back(D3DSIO_DCL | (2u << D3DSI_INSTLENGTH_SHIFT));
        t.push_back(0x80000000u | (usage & 0x1f) | ((index & 0xf) << 16));
        t.push_back(dstReg);
    }
    // dcl_2d / dcl_cube / dcl_volume sN
    void dclSampler(DWORD textureType, DWORD sampler)
    {
        t.push_back(D3DSIO_DCL | (2u << D3DSI_INSTLENGTH_SHIFT));
        t.push_back(0x80000000u | textureType);
        t.push_back(dst(D3DSPR_SAMPLER, sampler));
    }
    // def cN, x, y, z, w
    void def(DWORD reg, float x, float y, float z, float w)
    {
        t.push_back(D3DSIO_DEF | (5u << D3DSI_INSTLENGTH_SHIFT));
        t.push_back(dst(D3DSPR_CONST, reg));
        const float v[4] = {x, y, z, w};
        for (float f : v) {
            DWORD d;
            static_assert(sizeof(d) == sizeof(f), "");
            __builtin_memcpy(&d, &f, 4);
            t.push_back(d);
        }
    }
    void comment(std::initializer_list<DWORD> payload)
    {
        t.push_back(D3DSHADER_COMMENT((DWORD)payload.size()));
        for (DWORD p : payload)
            t.push_back(p);
    }
    std::vector<DWORD> finish()
    {
        t.push_back(D3DVS_END());
        return t;
    }
};

// Ready-made shaders used by the unit tests and the browser smoke test.
namespace test_shaders {

// vs_3_0: o0 = mul(v0, c0..c3) (row vectors dotted: dp4 per row), o1 = v1 (texcoord0), o2 = v2 (color0)
inline std::vector<DWORD> passThroughVS()
{
    ShaderAsm a(ShaderAsm::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_INPUT, 1));
    a.dcl(D3DDECLUSAGE_COLOR, 0, ShaderAsm::dst(D3DSPR_INPUT, 2));
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 1));
    a.dcl(D3DDECLUSAGE_COLOR, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 2));
    for (DWORD i = 0; i < 4; ++i)
        a.op(D3DSIO_DP4, ShaderAsm::dst(D3DSPR_OUTPUT, 0, 1u << i), ShaderAsm::src(D3DSPR_INPUT, 0),
             ShaderAsm::src(D3DSPR_CONST, i));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_OUTPUT, 1), ShaderAsm::src(D3DSPR_INPUT, 1));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_OUTPUT, 2), ShaderAsm::src(D3DSPR_INPUT, 2));
    return a.finish();
}

// ps_3_0: oC0 = tex2D(s0, v0.xy) * v1 (color0) * c0
inline std::vector<DWORD> textureModulatePS()
{
    ShaderAsm a(ShaderAsm::PS30);
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_INPUT, 0, 0x3));
    a.dcl(D3DDECLUSAGE_COLOR, 0, ShaderAsm::dst(D3DSPR_INPUT, 1));
    a.dclSampler(D3DSTT_2D, 0);
    a.op(D3DSIO_TEX, ShaderAsm::dst(D3DSPR_TEMP, 0), ShaderAsm::src(D3DSPR_INPUT, 0), ShaderAsm::src(D3DSPR_SAMPLER, 0));
    a.op(D3DSIO_MUL, ShaderAsm::dst(D3DSPR_TEMP, 0), ShaderAsm::src(D3DSPR_TEMP, 0), ShaderAsm::src(D3DSPR_INPUT, 1));
    a.op(D3DSIO_MUL, ShaderAsm::dst(D3DSPR_COLOROUT, 0), ShaderAsm::src(D3DSPR_TEMP, 0),
         ShaderAsm::src(D3DSPR_CONST, 0));
    return a.finish();
}

// ps_3_0: oC0 = v1 (vertex color) - no textures
inline std::vector<DWORD> vertexColorPS()
{
    ShaderAsm a(ShaderAsm::PS30);
    a.dcl(D3DDECLUSAGE_COLOR, 0, ShaderAsm::dst(D3DSPR_INPUT, 1));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_COLOROUT, 0), ShaderAsm::src(D3DSPR_INPUT, 1));
    return a.finish();
}

// ps_3_0: oC0 = c1 constant color, reads vPos and VFACE into r1 (exercises misc registers), texldl from a cube.
inline std::vector<DWORD> miscPS()
{
    ShaderAsm a(ShaderAsm::PS30);
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_INPUT, 0));
    a.dcl(0, 0, ShaderAsm::dst(D3DSPR_MISCTYPE, D3DSMO_POSITION, 0x3));
    a.dcl(0, 0, ShaderAsm::dst(D3DSPR_MISCTYPE, D3DSMO_FACE, 0xF));
    a.dclSampler(D3DSTT_CUBE, 2);
    a.dclSampler(D3DSTT_VOLUME, 3);
    a.def(5, 0.5f, 0.25f, 0.f, 1.f);
    a.op(D3DSIO_TEXLDL, ShaderAsm::dst(D3DSPR_TEMP, 0), ShaderAsm::src(D3DSPR_INPUT, 0),
         ShaderAsm::src(D3DSPR_SAMPLER, 2));
    a.op(D3DSIO_TEX, ShaderAsm::dst(D3DSPR_TEMP, 2), ShaderAsm::src(D3DSPR_INPUT, 0), ShaderAsm::src(D3DSPR_SAMPLER, 3));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_TEMP, 1, 0x3), ShaderAsm::src(D3DSPR_MISCTYPE, D3DSMO_POSITION));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_TEMP, 1, 0x4),
         ShaderAsm::src(D3DSPR_MISCTYPE, D3DSMO_FACE, D3DSP_REPLICATERED));
    a.op(D3DSIO_MAD, ShaderAsm::dst(D3DSPR_TEMP, 0), ShaderAsm::src(D3DSPR_TEMP, 1), ShaderAsm::src(D3DSPR_CONST, 5),
         ShaderAsm::src(D3DSPR_TEMP, 0));
    a.op(D3DSIO_ADD, ShaderAsm::dst(D3DSPR_TEMP, 0), ShaderAsm::src(D3DSPR_TEMP, 0), ShaderAsm::src(D3DSPR_TEMP, 2));
    a.op(D3DSIO_ADD, ShaderAsm::dst(D3DSPR_COLOROUT, 0), ShaderAsm::src(D3DSPR_TEMP, 0),
         ShaderAsm::src(D3DSPR_CONST, 1));
    return a.finish();
}

// vs_3_0 with relative constant addressing (skinning-style): o0 = c[a0.x + 10] using v1.x as the index.
inline std::vector<DWORD> relativeAddressVS()
{
    ShaderAsm a(ShaderAsm::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_BLENDINDICES, 0, ShaderAsm::dst(D3DSPR_INPUT, 1));
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    a.op(D3DSIO_MOVA, ShaderAsm::dst(D3DSPR_ADDR, 0, 0x1), ShaderAsm::src(D3DSPR_INPUT, 1, D3DSP_REPLICATERED));
    // add o0, v0, c10[a0.x]
    a.t.push_back(D3DSIO_ADD | (4u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    a.t.push_back(ShaderAsm::src(D3DSPR_INPUT, 0));
    a.t.push_back(ShaderAsm::src(D3DSPR_CONST, 10) | D3DSHADER_ADDRMODE_RELATIVE);
    a.t.push_back(ShaderAsm::src(D3DSPR_ADDR, 0, D3DSP_REPLICATERED));
    return a.finish();
}

// vs_3_0: like relativeAddressVS, but c12 is def'd: c[10 + a0.x] with a0.x = 2 must read the def value
inline std::vector<DWORD> relativeDefVS()
{
    ShaderAsm a(ShaderAsm::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_BLENDINDICES, 0, ShaderAsm::dst(D3DSPR_INPUT, 1));
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    a.def(12, 1.f, 0.f, 0.f, 0.f);
    a.op(D3DSIO_MOVA, ShaderAsm::dst(D3DSPR_ADDR, 0, 0x1), ShaderAsm::src(D3DSPR_INPUT, 1, D3DSP_REPLICATERED));
    a.t.push_back(D3DSIO_ADD | (4u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    a.t.push_back(ShaderAsm::src(D3DSPR_INPUT, 0));
    a.t.push_back(ShaderAsm::src(D3DSPR_CONST, 10) | D3DSHADER_ADDRMODE_RELATIVE);
    a.t.push_back(ShaderAsm::src(D3DSPR_ADDR, 0, D3DSP_REPLICATERED));
    return a.finish();
}

// ps_3_0: oC0 = c0
inline std::vector<DWORD> constColorPS()
{
    ShaderAsm a(ShaderAsm::PS30);
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_COLOROUT, 0), ShaderAsm::src(D3DSPR_CONST, 0));
    return a.finish();
}

// ps_3_0: oC0 = texCUBE(s0, v0.xyz)
inline std::vector<DWORD> cubePS()
{
    ShaderAsm a(ShaderAsm::PS30);
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_INPUT, 0, 0x7));
    a.dclSampler(D3DSTT_CUBE, 0);
    a.op(D3DSIO_TEX, ShaderAsm::dst(D3DSPR_COLOROUT, 0), ShaderAsm::src(D3DSPR_INPUT, 0),
         ShaderAsm::src(D3DSPR_SAMPLER, 0));
    return a.finish();
}

// ps_3_0: oC0 = tex3D(s0, v0.xyz)
inline std::vector<DWORD> volumePS()
{
    ShaderAsm a(ShaderAsm::PS30);
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_INPUT, 0, 0x7));
    a.dclSampler(D3DSTT_VOLUME, 0);
    a.op(D3DSIO_TEX, ShaderAsm::dst(D3DSPR_COLOROUT, 0), ShaderAsm::src(D3DSPR_INPUT, 0),
         ShaderAsm::src(D3DSPR_SAMPLER, 0));
    return a.finish();
}

// vs_3_0 for 3-component texcoords: o0 = v0 (clip space), o1 = v1 (texcoord0 xyz)
inline std::vector<DWORD> passThroughVS3()
{
    ShaderAsm a(ShaderAsm::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_INPUT, 1));
    a.dcl(D3DDECLUSAGE_POSITION, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 0));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, ShaderAsm::dst(D3DSPR_OUTPUT, 1));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_OUTPUT, 0), ShaderAsm::src(D3DSPR_INPUT, 0));
    a.op(D3DSIO_MOV, ShaderAsm::dst(D3DSPR_OUTPUT, 1), ShaderAsm::src(D3DSPR_INPUT, 1));
    return a.finish();
}

} // namespace test_shaders
