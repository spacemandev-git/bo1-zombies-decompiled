// shader_corpus.h - hand-assembled vs_3_0 / ps_3_0 shaders covering the instruction classes fxc emits (flow
// control, predication, gradients, biased/projected/LOD fetches, texkill, oDepth, relative addressing through aL,
// high texcoord indices, the engine's DEPTH vertex usage). Used by unit_tests.cpp: every shader must translate, and
// glslangValidator must compile the GLSL and link each VS with its PS.
#pragma once

#include "shader_asm.h"

namespace shader_corpus {

using A = ShaderAsm;

inline DWORD lit(float f)
{
    DWORD d;
    __builtin_memcpy(&d, &f, 4);
    return d;
}

// defi iN, x, y, z, w
inline void defi(A &a, DWORD reg, int x, int y, int z, int w)
{
    a.t.push_back(D3DSIO_DEFI | (5u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(A::dst(D3DSPR_CONSTINT, reg));
    a.t.push_back((DWORD)x);
    a.t.push_back((DWORD)y);
    a.t.push_back((DWORD)z);
    a.t.push_back((DWORD)w);
}

inline void defb(A &a, DWORD reg, bool v)
{
    a.t.push_back(D3DSIO_DEFB | (2u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(A::dst(D3DSPR_CONSTBOOL, reg));
    a.t.push_back(v ? 1u : 0u);
}

inline DWORD cmpCtl(D3DSHADER_COMPARISON c) { return (DWORD)c << D3DSHADER_COMPARISON_SHIFT; }

// Vertex shader: skinning-style loop over c[aL + 16], rep/if/else with integer and bool constants, m4x4, sat
// modifiers, many outputs incl. TEXCOORD13 and the DEPTH input usage the engine's declarations use.
inline std::vector<DWORD> flowVS()
{
    A a(A::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, A::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_NORMAL, 0, A::dst(D3DSPR_INPUT, 1));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, A::dst(D3DSPR_INPUT, 2));
    a.dcl(D3DDECLUSAGE_DEPTH, 0, A::dst(D3DSPR_INPUT, 3));
    a.dcl(D3DDECLUSAGE_COLOR, 0, A::dst(D3DSPR_INPUT, 4));
    a.dcl(D3DDECLUSAGE_POSITION, 0, A::dst(D3DSPR_OUTPUT, 0));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, A::dst(D3DSPR_OUTPUT, 1));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 13, A::dst(D3DSPR_OUTPUT, 2));
    a.dcl(D3DDECLUSAGE_COLOR, 0, A::dst(D3DSPR_OUTPUT, 3));
    a.dcl(D3DDECLUSAGE_FOG, 0, A::dst(D3DSPR_OUTPUT, 4, 0x1));
    a.def(100, 0.f, 1.f, 0.5f, 2.f);
    defi(a, 0, 4, 0, 1, 0);
    defi(a, 1, 3, 0, 0, 0);
    defb(a, 0, true);
    // r0 = 0
    a.op(D3DSIO_MOV, A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_CONST, 100, D3DSP_REPLICATERED));
    // loop aL, i0 ; add r0, r0, c16[aL] ; endloop
    a.op(D3DSIO_LOOP, {A::src(D3DSPR_LOOP, 0), A::src(D3DSPR_CONSTINT, 0)});
    a.t.push_back(D3DSIO_ADD | (4u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(A::dst(D3DSPR_TEMP, 0));
    a.t.push_back(A::src(D3DSPR_TEMP, 0));
    a.t.push_back(A::src(D3DSPR_CONST, 16) | D3DSHADER_ADDRMODE_RELATIVE);
    a.t.push_back(A::src(D3DSPR_LOOP, 0, D3DSP_REPLICATERED));
    a.op(D3DSIO_ENDLOOP, {});
    // rep i1 ; mad r0, r0, c100.z, v1 ; endrep
    a.op(D3DSIO_REP, {A::src(D3DSPR_CONSTINT, 1)});
    a.op(D3DSIO_MAD, A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_TEMP, 0),
         A::src(D3DSPR_CONST, 100, A::swz(2, 2, 2, 2)), A::src(D3DSPR_INPUT, 1));
    a.op(D3DSIO_ENDREP, {});
    // if b0 ; add r0, r0, v3 ; else ; mov r0, -r0 ; endif
    a.op(D3DSIO_IF, {A::src(D3DSPR_CONSTBOOL, 0)});
    a.op(D3DSIO_ADD, A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_TEMP, 0), A::src(D3DSPR_INPUT, 3, D3DSP_REPLICATERED));
    a.op(D3DSIO_ELSE, {});
    a.op(D3DSIO_MOV, A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_TEMP, 0, D3DSP_NOSWIZZLE, D3DSPSM_NEG));
    a.op(D3DSIO_ENDIF, {});
    // ifc_gt r0.x, c100.x ; mul r0, r0, c100.w ; endif
    a.op(D3DSIO_IFC, {A::src(D3DSPR_TEMP, 0, D3DSP_REPLICATERED), A::src(D3DSPR_CONST, 100, D3DSP_REPLICATERED)},
         cmpCtl(D3DSPC_GT));
    a.op(D3DSIO_MUL, A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_TEMP, 0), A::src(D3DSPR_CONST, 100, D3DSP_REPLICATEALPHA));
    a.op(D3DSIO_ENDIF, {});
    // r1 = v0 + r0 * 0 ; o0 = m4x4(r1, c0..c3)
    a.op(D3DSIO_MAD, A::dst(D3DSPR_TEMP, 1), A::src(D3DSPR_TEMP, 0), A::src(D3DSPR_CONST, 100, D3DSP_REPLICATERED),
         A::src(D3DSPR_INPUT, 0));
    a.op(D3DSIO_M4x4, A::dst(D3DSPR_OUTPUT, 0), A::src(D3DSPR_TEMP, 1), A::src(D3DSPR_CONST, 0));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_OUTPUT, 1), A::src(D3DSPR_INPUT, 2));
    a.op(D3DSIO_DP3, A::dst(D3DSPR_OUTPUT, 2, 0xF, D3DSPDM_SATURATE), A::src(D3DSPR_INPUT, 1), A::src(D3DSPR_CONST, 4));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_OUTPUT, 3), A::src(D3DSPR_INPUT, 4));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_OUTPUT, 4, 0x1), A::src(D3DSPR_TEMP, 1, A::swz(2, 2, 2, 2)));
    // mova a0.x, r0.x ; mov r2, c20[a0.x] (a second relative form)
    a.op(D3DSIO_MOVA, A::dst(D3DSPR_ADDR, 0, 0x1), A::src(D3DSPR_TEMP, 0, D3DSP_REPLICATERED));
    a.t.push_back(D3DSIO_MOV | (3u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(A::dst(D3DSPR_TEMP, 2));
    a.t.push_back(A::src(D3DSPR_CONST, 20) | D3DSHADER_ADDRMODE_RELATIVE);
    a.t.push_back(A::src(D3DSPR_ADDR, 0, D3DSP_REPLICATERED));
    a.op(D3DSIO_ADD, A::dst(D3DSPR_OUTPUT, 1, 0x8), A::src(D3DSPR_INPUT, 2), A::src(D3DSPR_TEMP, 2));
    return a.finish();
}

// Pixel shader: gradients, texldd/texldb/texldp/texldl, texkill, predication, break in rep, math instructions,
// _sat/_pp modifiers, abs/neg source modifiers, oDepth, the TEXCOORD13 / FOG varyings written by flowVS.
inline std::vector<DWORD> mathPS()
{
    A a(A::PS30);
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, A::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 13, A::dst(D3DSPR_INPUT, 1));
    a.dcl(D3DDECLUSAGE_COLOR, 0, A::dst(D3DSPR_INPUT, 2));
    a.dcl(D3DDECLUSAGE_FOG, 0, A::dst(D3DSPR_INPUT, 3, 0x1));
    a.dclSampler(D3DSTT_2D, 0);
    a.dclSampler(D3DSTT_2D, 1);
    a.dclSampler(D3DSTT_CUBE, 4);
    a.def(10, 0.5f, 2.f, 0.f, 1.f);
    a.def(11, -0.5f, 0.25f, 4.f, 0.1f);
    defi(a, 0, 3, 0, 1, 0);
    // dsx r1, v0 ; dsy r2, v0 ; texldd r0, v0, s0, r1, r2
    a.op(D3DSIO_DSX, A::dst(D3DSPR_TEMP, 1), A::src(D3DSPR_INPUT, 0));
    a.op(D3DSIO_DSY, A::dst(D3DSPR_TEMP, 2), A::src(D3DSPR_INPUT, 0));
    a.op(D3DSIO_TEXLDD, {A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_INPUT, 0), A::src(D3DSPR_SAMPLER, 0),
                         A::src(D3DSPR_TEMP, 1), A::src(D3DSPR_TEMP, 2)});
    // texldb r3, v0, s1 ; texldp r4, v0, s1 ; texldl r5, v1, s4 (cube)
    a.op(D3DSIO_TEX, {A::dst(D3DSPR_TEMP, 3), A::src(D3DSPR_INPUT, 0), A::src(D3DSPR_SAMPLER, 1)}, D3DSI_TEXLD_BIAS);
    a.op(D3DSIO_TEX, {A::dst(D3DSPR_TEMP, 4), A::src(D3DSPR_INPUT, 0), A::src(D3DSPR_SAMPLER, 1)}, D3DSI_TEXLD_PROJECT);
    a.op(D3DSIO_TEXLDL, A::dst(D3DSPR_TEMP, 5), A::src(D3DSPR_INPUT, 1), A::src(D3DSPR_SAMPLER, 4));
    // texkill r0 (discard if any component < 0; ps_3_0 texkill takes a temp)
    a.op(D3DSIO_TEXKILL, {A::dst(D3DSPR_TEMP, 0)});
    // setp_gt p0, r0, c10.x ; (p0.x) mov r6, c11 ; (!p0.y) add r6, r6, r3
    a.op(D3DSIO_SETP, {A::dst(D3DSPR_PREDICATE, 0), A::src(D3DSPR_TEMP, 0), A::src(D3DSPR_CONST, 10, D3DSP_REPLICATERED)},
         cmpCtl(D3DSPC_GT));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_TEMP, 6), A::src(D3DSPR_CONST, 11, D3DSP_NOSWIZZLE));
    a.t.push_back(D3DSIO_MOV | D3DSHADER_INSTRUCTION_PREDICATED | (3u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(A::dst(D3DSPR_TEMP, 6));
    a.t.push_back(A::src(D3DSPR_PREDICATE, 0, D3DSP_REPLICATERED));
    a.t.push_back(A::src(D3DSPR_CONST, 11));
    a.t.push_back(D3DSIO_ADD | D3DSHADER_INSTRUCTION_PREDICATED | (4u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(A::dst(D3DSPR_TEMP, 6));
    a.t.push_back(A::src(D3DSPR_PREDICATE, 0, D3DSP_REPLICATEGREEN, D3DSPSM_NOT));
    a.t.push_back(A::src(D3DSPR_TEMP, 6));
    a.t.push_back(A::src(D3DSPR_TEMP, 3));
    // rep i0 ; breakc_lt r6.x, c11.x ; mad r6, r6, c10.x, r4 ; break_p? (skip) ; endrep
    a.op(D3DSIO_REP, {A::src(D3DSPR_CONSTINT, 0)});
    a.op(D3DSIO_BREAKC, {A::src(D3DSPR_TEMP, 6, D3DSP_REPLICATERED), A::src(D3DSPR_CONST, 11, D3DSP_REPLICATERED)},
         cmpCtl(D3DSPC_LT));
    a.op(D3DSIO_MAD, A::dst(D3DSPR_TEMP, 6), A::src(D3DSPR_TEMP, 6), A::src(D3DSPR_CONST, 10, D3DSP_REPLICATERED),
         A::src(D3DSPR_TEMP, 4));
    a.op(D3DSIO_ENDREP, {});
    // math: nrm, pow, sincos, cmp, lrp, dp2add, frc, exp, log, rcp, rsq, min, max, abs
    a.op(D3DSIO_NRM, A::dst(D3DSPR_TEMP, 7, 0x7), A::src(D3DSPR_INPUT, 1));
    a.op(D3DSIO_POW, A::dst(D3DSPR_TEMP, 7, 0x8), A::src(D3DSPR_TEMP, 0, D3DSP_REPLICATERED, D3DSPSM_ABS),
         A::src(D3DSPR_CONST, 10, D3DSP_REPLICATEGREEN));
    a.op(D3DSIO_SINCOS, A::dst(D3DSPR_TEMP, 8, 0x3), A::src(D3DSPR_TEMP, 0, D3DSP_REPLICATEBLUE));
    a.op(D3DSIO_CMP, A::dst(D3DSPR_TEMP, 9), A::src(D3DSPR_TEMP, 6), A::src(D3DSPR_TEMP, 3), A::src(D3DSPR_TEMP, 5));
    a.op(D3DSIO_LRP, A::dst(D3DSPR_TEMP, 9), A::src(D3DSPR_INPUT, 2, D3DSP_REPLICATEALPHA), A::src(D3DSPR_TEMP, 9),
         A::src(D3DSPR_TEMP, 7));
    a.op(D3DSIO_DP2ADD, A::dst(D3DSPR_TEMP, 10, 0x1), A::src(D3DSPR_TEMP, 8), A::src(D3DSPR_CONST, 10),
         A::src(D3DSPR_CONST, 11, D3DSP_REPLICATEALPHA));
    a.op(D3DSIO_FRC, A::dst(D3DSPR_TEMP, 10, 0x2), A::src(D3DSPR_TEMP, 9));
    a.op(D3DSIO_EXP, A::dst(D3DSPR_TEMP, 10, 0x4), A::src(D3DSPR_TEMP, 9, D3DSP_REPLICATEBLUE));
    a.op(D3DSIO_LOG, A::dst(D3DSPR_TEMP, 10, 0x8), A::src(D3DSPR_TEMP, 9, D3DSP_REPLICATEALPHA, D3DSPSM_ABS));
    a.op(D3DSIO_RCP, A::dst(D3DSPR_TEMP, 11, 0x1), A::src(D3DSPR_TEMP, 10, D3DSP_REPLICATERED));
    a.op(D3DSIO_RSQ, A::dst(D3DSPR_TEMP, 11, 0x2), A::src(D3DSPR_TEMP, 10, D3DSP_REPLICATEGREEN, D3DSPSM_ABS));
    a.op(D3DSIO_MIN, A::dst(D3DSPR_TEMP, 11, 0x4), A::src(D3DSPR_TEMP, 10), A::src(D3DSPR_CONST, 10));
    a.op(D3DSIO_MAX, A::dst(D3DSPR_TEMP, 11, 0x8), A::src(D3DSPR_TEMP, 10), A::src(D3DSPR_CONST, 11));
    a.op(D3DSIO_ABS, A::dst(D3DSPR_TEMP, 11, 0xF, D3DSPDM_PARTIALPRECISION), A::src(D3DSPR_TEMP, 11));
    // combine with the varyings and the app constant c0; write color and depth
    a.op(D3DSIO_MAD, A::dst(D3DSPR_TEMP, 0, 0xF, D3DSPDM_SATURATE), A::src(D3DSPR_TEMP, 9), A::src(D3DSPR_CONST, 0),
         A::src(D3DSPR_TEMP, 11));
    a.op(D3DSIO_MUL, A::dst(D3DSPR_TEMP, 0, 0x7), A::src(D3DSPR_TEMP, 0), A::src(D3DSPR_INPUT, 3, D3DSP_REPLICATERED));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_COLOROUT, 0), A::src(D3DSPR_TEMP, 0));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_DEPTHOUT, 0), A::src(D3DSPR_TEMP, 6, D3DSP_REPLICATERED));
    return a.finish();
}

// Vertex shader with the vs_3_0 math set (sge/slt/lit/dst/expp/logp/crs/sgn/frc) and a texture-less output set.
inline std::vector<DWORD> mathVS()
{
    A a(A::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, A::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_TANGENT, 0, A::dst(D3DSPR_INPUT, 1));
    a.dcl(D3DDECLUSAGE_BLENDWEIGHT, 0, A::dst(D3DSPR_INPUT, 2));
    a.dcl(D3DDECLUSAGE_POSITION, 0, A::dst(D3DSPR_OUTPUT, 0));
    a.dcl(D3DDECLUSAGE_TEXCOORD, 0, A::dst(D3DSPR_OUTPUT, 1));
    a.dcl(D3DDECLUSAGE_COLOR, 0, A::dst(D3DSPR_OUTPUT, 2));
    a.dcl(D3DDECLUSAGE_PSIZE, 0, A::dst(D3DSPR_OUTPUT, 3, 0x1));
    a.op(D3DSIO_SGE, A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_INPUT, 1), A::src(D3DSPR_CONST, 5));
    a.op(D3DSIO_SLT, A::dst(D3DSPR_TEMP, 1), A::src(D3DSPR_INPUT, 1), A::src(D3DSPR_CONST, 5));
    a.op(D3DSIO_LIT, A::dst(D3DSPR_TEMP, 2), A::src(D3DSPR_INPUT, 2));
    a.op(D3DSIO_DST, A::dst(D3DSPR_TEMP, 3), A::src(D3DSPR_TEMP, 2), A::src(D3DSPR_TEMP, 0));
    a.op(D3DSIO_EXPP, A::dst(D3DSPR_TEMP, 4, 0x1), A::src(D3DSPR_INPUT, 2, D3DSP_REPLICATERED));
    a.op(D3DSIO_LOGP, A::dst(D3DSPR_TEMP, 4, 0x2), A::src(D3DSPR_INPUT, 2, D3DSP_REPLICATEGREEN, D3DSPSM_ABS));
    a.op(D3DSIO_CRS, A::dst(D3DSPR_TEMP, 5, 0x7), A::src(D3DSPR_INPUT, 1), A::src(D3DSPR_TEMP, 0));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_TEMP, 7), A::src(D3DSPR_CONST, 5));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_TEMP, 8), A::src(D3DSPR_CONST, 5));
    a.op(D3DSIO_SGN, {A::dst(D3DSPR_TEMP, 6), A::src(D3DSPR_INPUT, 1), A::src(D3DSPR_TEMP, 7), A::src(D3DSPR_TEMP, 8)});
    a.op(D3DSIO_FRC, A::dst(D3DSPR_TEMP, 7), A::src(D3DSPR_INPUT, 2));
    a.op(D3DSIO_M4x3, A::dst(D3DSPR_TEMP, 8, 0x7), A::src(D3DSPR_INPUT, 0), A::src(D3DSPR_CONST, 0));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_TEMP, 8, 0x8), A::src(D3DSPR_INPUT, 0, D3DSP_REPLICATEALPHA));
    a.op(D3DSIO_M4x4, A::dst(D3DSPR_OUTPUT, 0), A::src(D3DSPR_TEMP, 8), A::src(D3DSPR_CONST, 4));
    a.op(D3DSIO_ADD, A::dst(D3DSPR_OUTPUT, 1), A::src(D3DSPR_TEMP, 3), A::src(D3DSPR_TEMP, 4));
    a.op(D3DSIO_MAD, A::dst(D3DSPR_OUTPUT, 2, 0xF, D3DSPDM_SATURATE), A::src(D3DSPR_TEMP, 5), A::src(D3DSPR_TEMP, 6),
         A::src(D3DSPR_TEMP, 7));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_OUTPUT, 3, 0x1), A::src(D3DSPR_TEMP, 1, D3DSP_REPLICATERED));
    return a.finish();
}

// vs_3_0 loop whose count comes from an app-set integer constant (i1, no defi): the loop end is not a GLSL constant.
inline std::vector<DWORD> uniformLoopVS()
{
    A a(A::VS30);
    a.dcl(D3DDECLUSAGE_POSITION, 0, A::dst(D3DSPR_INPUT, 0));
    a.dcl(D3DDECLUSAGE_POSITION, 0, A::dst(D3DSPR_OUTPUT, 0));
    a.dcl(D3DDECLUSAGE_COLOR, 0, A::dst(D3DSPR_OUTPUT, 1));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_TEMP, 0), A::src(D3DSPR_CONST, 0));
    a.op(D3DSIO_LOOP, {A::src(D3DSPR_LOOP, 0), A::src(D3DSPR_CONSTINT, 1)});
    a.t.push_back(D3DSIO_ADD | (4u << D3DSI_INSTLENGTH_SHIFT));
    a.t.push_back(A::dst(D3DSPR_TEMP, 0));
    a.t.push_back(A::src(D3DSPR_TEMP, 0));
    a.t.push_back(A::src(D3DSPR_CONST, 1) | D3DSHADER_ADDRMODE_RELATIVE);
    a.t.push_back(A::src(D3DSPR_LOOP, 0, D3DSP_REPLICATERED));
    a.op(D3DSIO_ENDLOOP, {});
    a.op(D3DSIO_MOV, A::dst(D3DSPR_OUTPUT, 0), A::src(D3DSPR_INPUT, 0));
    a.op(D3DSIO_MOV, A::dst(D3DSPR_OUTPUT, 1), A::src(D3DSPR_TEMP, 0));
    return a.finish();
}

} // namespace shader_corpus
