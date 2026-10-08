// d3d9_vertexdecl.h - D3DVERTEXELEMENT9 -> glVertexAttribPointer mapping (pure logic, no GL calls).
#pragma once

#include <d3d9.h>
#include <GLES3/gl3.h>
#include <stdint.h>
#include <vector>

namespace d3d9shim {

struct DeclTypeInfo {
    GLenum type;       // 0 = unsupported/unused
    GLint size;        // components
    GLboolean normalized;
    bool bgra;         // D3DCOLOR: bytes are B,G,R,A -> the shader must read .zyxw
    uint8_t bytes;     // element size in the vertex
};
DeclTypeInfo declTypeInfo(BYTE d3dDeclType);
const char *declUsageName(BYTE usage);

// Parses a D3DDECL_END-terminated element list (max MAXD3DDECLLENGTH). Returns the element count (without END).
UINT declElementCount(const D3DVERTEXELEMENT9 *elements);

// One vertex-shader input (from the translated shader): semantic + the GL attribute location bound at link time.
struct ShaderInputSlot {
    uint8_t usage, usageIndex;
    int location;
};

// One glVertexAttribPointer call. offset is relative to the start of the stream (add the stream offset at draw).
struct AttribBinding {
    int location;
    uint8_t stream;
    uint16_t offset;
    GLenum type;
    GLint size;
    GLboolean normalized;
};

// Matches shader inputs to declaration elements by (usage, usageIndex), as D3D9 does. Inputs without a matching
// element are left unbound (GL then supplies the current generic value (0,0,0,1), which is what D3D reads for
// missing elements). POSITIONT elements also satisfy POSITION inputs. Returns the bindings; `bgraMask` gets bit i
// set when shader input i is fed by a D3DCOLOR element (the VS variant must swizzle it).
std::vector<AttribBinding> linkDeclaration(const D3DVERTEXELEMENT9 *elements, UINT count,
                                           const ShaderInputSlot *inputs, UINT inputCount, uint32_t *bgraMask,
                                           uint32_t *streamMask);

} // namespace d3d9shim
