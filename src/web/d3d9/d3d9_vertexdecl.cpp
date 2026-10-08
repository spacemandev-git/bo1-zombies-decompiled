// d3d9_vertexdecl.cpp - D3DVERTEXELEMENT9 -> glVertexAttribPointer mapping (pure logic, no GL calls).
#include "d3d9_vertexdecl.h"

namespace d3d9shim {

DeclTypeInfo declTypeInfo(BYTE t)
{
    switch (t) {
    case D3DDECLTYPE_FLOAT1: return {GL_FLOAT, 1, GL_FALSE, false, 4};
    case D3DDECLTYPE_FLOAT2: return {GL_FLOAT, 2, GL_FALSE, false, 8};
    case D3DDECLTYPE_FLOAT3: return {GL_FLOAT, 3, GL_FALSE, false, 12};
    case D3DDECLTYPE_FLOAT4: return {GL_FLOAT, 4, GL_FALSE, false, 16};
    case D3DDECLTYPE_D3DCOLOR: return {GL_UNSIGNED_BYTE, 4, GL_TRUE, true, 4};
    case D3DDECLTYPE_UBYTE4: return {GL_UNSIGNED_BYTE, 4, GL_FALSE, false, 4};
    case D3DDECLTYPE_SHORT2: return {GL_SHORT, 2, GL_FALSE, false, 4};
    case D3DDECLTYPE_SHORT4: return {GL_SHORT, 4, GL_FALSE, false, 8};
    case D3DDECLTYPE_UBYTE4N: return {GL_UNSIGNED_BYTE, 4, GL_TRUE, false, 4};
    case D3DDECLTYPE_SHORT2N: return {GL_SHORT, 2, GL_TRUE, false, 4};
    case D3DDECLTYPE_SHORT4N: return {GL_SHORT, 4, GL_TRUE, false, 8};
    case D3DDECLTYPE_USHORT2N: return {GL_UNSIGNED_SHORT, 2, GL_TRUE, false, 4};
    case D3DDECLTYPE_USHORT4N: return {GL_UNSIGNED_SHORT, 4, GL_TRUE, false, 8};
    // UDEC3/DEC3N: D3D expands to (x, y, z, 1); GL's packed type also delivers the 2-bit w (approximation).
    case D3DDECLTYPE_UDEC3: return {GL_UNSIGNED_INT_2_10_10_10_REV, 4, GL_FALSE, false, 4};
    case D3DDECLTYPE_DEC3N: return {GL_INT_2_10_10_10_REV, 4, GL_TRUE, false, 4};
    case D3DDECLTYPE_FLOAT16_2: return {GL_HALF_FLOAT, 2, GL_FALSE, false, 4};
    case D3DDECLTYPE_FLOAT16_4: return {GL_HALF_FLOAT, 4, GL_FALSE, false, 8};
    default: return {0, 0, GL_FALSE, false, 0};
    }
}

const char *declUsageName(BYTE u)
{
    static const char *names[] = {"POSITION", "BLENDWEIGHT", "BLENDINDICES", "NORMAL", "PSIZE",
                                  "TEXCOORD", "TANGENT",     "BINORMAL",     "TESSFACTOR", "POSITIONT",
                                  "COLOR",    "FOG",         "DEPTH",        "SAMPLE"};
    return u < 14 ? names[u] : "?";
}

UINT declElementCount(const D3DVERTEXELEMENT9 *e)
{
    UINT n = 0;
    while (n < MAXD3DDECLLENGTH && e[n].Stream != 0xFF && e[n].Type != D3DDECLTYPE_UNUSED)
        ++n;
    return n;
}

std::vector<AttribBinding> linkDeclaration(const D3DVERTEXELEMENT9 *elements, UINT count,
                                           const ShaderInputSlot *inputs, UINT inputCount, uint32_t *bgraMask,
                                           uint32_t *streamMask)
{
    std::vector<AttribBinding> out;
    uint32_t bgra = 0, streams = 0;
    for (UINT i = 0; i < inputCount; ++i) {
        const ShaderInputSlot &in = inputs[i];
        const D3DVERTEXELEMENT9 *match = nullptr;
        for (UINT e = 0; e < count; ++e) {
            const D3DVERTEXELEMENT9 &el = elements[e];
            BYTE usage = el.Usage;
            if (usage == D3DDECLUSAGE_POSITIONT)
                usage = D3DDECLUSAGE_POSITION;
            const BYTE want = in.usage == D3DDECLUSAGE_POSITIONT ? (BYTE)D3DDECLUSAGE_POSITION : in.usage;
            if (usage == want && el.UsageIndex == in.usageIndex) {
                match = &el;
                break;
            }
        }
        if (!match)
            continue;
        DeclTypeInfo ti = declTypeInfo(match->Type);
        if (!ti.type)
            continue;
        AttribBinding b;
        b.location = in.location;
        b.stream = (uint8_t)match->Stream;
        b.offset = match->Offset;
        b.type = ti.type;
        b.size = ti.size;
        b.normalized = ti.normalized;
        out.push_back(b);
        if (ti.bgra && i < 32)
            bgra |= 1u << i;
        if (match->Stream < 32)
            streams |= 1u << match->Stream;
    }
    if (bgraMask)
        *bgraMask = bgra;
    if (streamMask)
        *streamMask = streams;
    return out;
}

} // namespace d3d9shim
