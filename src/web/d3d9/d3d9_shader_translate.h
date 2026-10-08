// d3d9_shader_translate.h - D3D9 SM2/SM3 bytecode -> GLSL ES 3.00 (MojoShader glsles3 + shim fixups).
// Pure logic: no GL calls, unit-tested under node (test/unit_tests.cpp).
#pragma once

#include <d3d9.h>
#include <stdint.h>
#include <string>
#include <utility>
#include <vector>

namespace d3d9shim {

enum class ShaderStage : uint8_t { Vertex, Pixel };

// Names the shim adds to the GLSL (set by the device at draw time).
extern const char *const kPosFixupUniform;  // vec4 (unused, flipY, 1/vpW*63/64, 1/vpH*63/64) - vertex
extern const char *const kAlphaTestUniform; // vec2 (D3DCMPFUNC or 0 = off, ALPHAREF 0..255) - pixel
extern const char *const kVPosFlipUniform;  // vec2 (scale, offset) for vPos.y - pixel (MojoShader's name)

struct ShaderSemantic {
    uint8_t usage;      // MOJOSHADER_usage == D3DDECLUSAGE
    uint8_t usageIndex;
    std::string name;   // GLSL variable (e.g. vs_v0, or io_5_0 for varyings)
};

struct ShaderUniformInfo {
    uint8_t type;       // 0 float4, 1 int4, 2 bool (MOJOSHADER_uniformType)
    int index;          // first D3D register
    int count;          // registers (1 for non-arrays)
    bool constant;      // a def'd constant array baked at link time (not app-settable)
    std::string name;
};

struct ShaderSamplerInfo {
    int stage;          // sN
    uint8_t type;       // 0 2D, 1 cube, 2 volume (MOJOSHADER_samplerType)
    std::string name;   // GLSL uniform (ps_s0)
};

struct ShaderConstantInfo {
    uint8_t type;
    int index;
    float f[4];
    int i[4];
    int b;
};

struct TranslateOptions {
    // Vertex inputs fed by D3DCOLOR elements: read as .zyxw (BGRA bytes).
    std::vector<std::pair<uint8_t, uint8_t>> bgraInputs;
    // Varyings (usage, index) the pixel shader reads but this vertex shader does not write: declared and zeroed.
    std::vector<std::pair<uint8_t, uint8_t>> extraOutputs;
};

struct TranslatedShader {
    bool ok = false;
    std::string errors;
    ShaderStage stage = ShaderStage::Vertex;
    DWORD version = 0;
    size_t byteLength = 0;  // bytecode length including the END token
    std::string glsl;       // final GLSL ES 3.00 source
    std::vector<ShaderSemantic> inputs;   // VS: vertex attributes; PS: varyings read
    std::vector<ShaderSemantic> outputs;  // VS: varyings written (incl. position)
    std::vector<ShaderUniformInfo> uniforms;
    std::vector<ShaderSamplerInfo> samplers;
    std::vector<ShaderConstantInfo> constants;
    bool writesColor0 = false;
    bool writesPointSize = false;
    bool usesVPos = false;
    bool syntheticCTAB = false; // relative addressing without CTAB: c0..cN treated as one array
};

// Length in bytes of a token stream up to and including the END token (0 if no END within maxBytes; maxBytes 0 =
// unbounded, like D3D9's CreateVertexShader which receives no size).
size_t shaderByteLength(const DWORD *tokens, size_t maxBytes);

TranslatedShader translateShader(const DWORD *tokens, size_t maxBytes, const TranslateOptions *opt = nullptr);

// Exposed for tests: the text fixups applied to MojoShader's glsles3 output. scalarOutputs: VS outputs MojoShader
// writes as scalars (FOG, PSIZE index > 0); they are routed through a float local into a vec4 varying.
bool postProcessGLSL(const std::string &mojo, ShaderStage stage, const TranslateOptions *opt, bool writesColor0,
                     bool writesPointSize, std::string *out, std::string *error,
                     const std::vector<ShaderSemantic> *scalarOutputs = nullptr);
// Exposed for tests: D3D9 bytecode stores a predicated instruction's predicate token right after the destination
// (as Wine/vkd3d parse it); MojoShader reads it after the sources. Returns the stream with predicate tokens moved
// last (empty if the shader has no predicated instruction).
std::vector<DWORD> reorderPredicateTokens(const DWORD *tokens, size_t tokenCount);
// Exposed for tests: inserts a CTAB declaring one float4 array "c" over registers [0, count).
std::vector<DWORD> withSyntheticCTAB(const DWORD *tokens, size_t tokenCount, unsigned registerCount);

} // namespace d3d9shim
