// d3d9_shader_translate.cpp - D3D9 SM2/SM3 bytecode -> GLSL ES 3.00 (MojoShader glsles3 + shim fixups).
//
// MojoShader is used unmodified; its output is adapted here (see README "Shader translation"):
//   * precision: highp everywhere, plus explicit sampler precisions (GLSL ES 3.00 has no default for sampler3D)
//   * main() -> d3d9_main() + wrapper main(): vertex = D3D->GL clip fixups; pixel = alpha test
//   * vPos: D3D9 VPOS has integer pixel centers (gl_FragCoord.x - 0.5)
#include "d3d9_shader_translate.h"

#include <stdio.h>
#include <string.h>

#include "mojoshader.h"

namespace d3d9shim {

const char *const kPosFixupUniform = "d3d9_PosFixup";
const char *const kAlphaTestUniform = "d3d9_AlphaTest";
const char *const kVPosFlipUniform = "vposFlip";

size_t shaderByteLength(const DWORD *tokens, size_t maxBytes)
{
    if (!tokens)
        return 0;
    const size_t maxTokens = maxBytes ? maxBytes / 4 : (size_t)1 << 20;
    size_t i = 1; // skip version
    while (i < maxTokens) {
        const DWORD t = tokens[i];
        if (t == D3DVS_END())
            return (i + 1) * 4;
        if ((t & 0xFFFF) == D3DSIO_COMMENT) {
            i += 1 + ((t & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT);
            continue;
        }
        const DWORD opcode = t & D3DSI_OPCODE_MASK;
        DWORD len = (t & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT;
        const DWORD major = (tokens[0] >> 8) & 0xFF;
        if (major < 2) {
            // SM1 has no length field: count parameter tokens (high bit set) instead.
            len = 0;
            while (i + 1 + len < maxTokens && (tokens[i + 1 + len] & 0x80000000u))
                ++len;
            if (opcode == D3DSIO_DEF)
                len = 5;
        }
        i += 1 + len;
    }
    return 0;
}

std::vector<DWORD> withSyntheticCTAB(const DWORD *tokens, size_t tokenCount, unsigned registerCount)
{
    // CTAB payload (offsets relative to the byte after the 'CTAB' fourcc):
    //   0: header (7 dwords)  28: one D3DXSHADER_CONSTANTINFO (5 dwords)  48: D3DXSHADER_TYPEINFO (4 dwords)
    //   64: "c\0" name, then the target string, then the creator string.
    const DWORD version = tokens[0];
    const bool vs = (version & 0xFFFF0000) == 0xFFFE0000;
    char target[8] = {vs ? 'v' : 'p', 's', '_', (char)('0' + ((version >> 8) & 0xFF)), '_',
                      (char)('0' + (version & 0xFF)), 0, 0};
    const char creator[] = "bo1-web-d3d9shim";
    std::vector<uint8_t> payload(64, 0);
    auto put32 = [&](size_t off, DWORD v) { memcpy(&payload[off], &v, 4); };
    auto put16 = [&](size_t off, uint16_t v) { memcpy(&payload[off], &v, 2); };
    const DWORD nameOff = 64;
    payload.push_back('c');
    payload.push_back(0);
    const DWORD targetOff = (DWORD)payload.size();
    payload.insert(payload.end(), target, target + 7);
    const DWORD creatorOff = (DWORD)payload.size();
    payload.insert(payload.end(), creator, creator + sizeof(creator));
    while (payload.size() % 4)
        payload.push_back(0);
    put32(0, 28);          // Size
    put32(4, creatorOff);  // Creator
    put32(8, version);     // Version
    put32(12, 1);          // Constants
    put32(16, 28);         // ConstantInfo
    put32(20, 0);          // Flags
    put32(24, targetOff);  // Target
    put32(28, nameOff);    // Name
    put16(32, 2);          // RegisterSet = FLOAT4
    put16(34, 0);          // RegisterIndex
    put16(36, (uint16_t)registerCount);
    put16(38, 0);
    put32(40, 48);         // TypeInfo
    put32(44, 0);          // DefaultValue
    put16(48, 1);          // Class = VECTOR
    put16(50, 3);          // Type = FLOAT
    put16(52, 1);          // Rows
    put16(54, 4);          // Columns
    put16(56, (uint16_t)registerCount); // Elements
    put16(58, 0);          // StructMembers
    put32(60, 0);

    std::vector<DWORD> out;
    out.reserve(tokenCount + payload.size() / 4 + 2);
    out.push_back(version);
    out.push_back(D3DSHADER_COMMENT((DWORD)(payload.size() / 4 + 1)));
    out.push_back(MAKEFOURCC('C', 'T', 'A', 'B'));
    for (size_t i = 0; i < payload.size(); i += 4) {
        DWORD d;
        memcpy(&d, &payload[i], 4);
        out.push_back(d);
    }
    out.insert(out.end(), tokens + 1, tokens + tokenCount);
    return out;
}

std::vector<DWORD> reorderPredicateTokens(const DWORD *tokens, size_t count)
{
    std::vector<DWORD> out;
    const DWORD major = (tokens[0] >> 8) & 0xFF;
    if (major < 2)
        return out; // no instruction lengths before SM2 (and no predication)
    bool any = false;
    for (size_t pass = 0; pass < 2; ++pass) {
        size_t i = 1;
        while (i < count) {
            const DWORD t = tokens[i];
            if (t == D3DVS_END())
                break;
            if ((t & 0xFFFF) == D3DSIO_COMMENT) {
                i += 1 + ((t & D3DSI_COMMENTSIZE_MASK) >> D3DSI_COMMENTSIZE_SHIFT);
                continue;
            }
            const DWORD len = (t & D3DSI_INSTLENGTH_MASK) >> D3DSI_INSTLENGTH_SHIFT;
            if ((t & D3DSHADER_INSTRUCTION_PREDICATED) && len >= 2 && i + len < count) {
                if (pass == 0) {
                    any = true;
                } else {
                    // params: dst [dst-relative] pred src... -> dst [dst-relative] src... pred
                    size_t dstLen = 1;
                    if ((tokens[i + 1] & D3DSHADER_ADDRMODE_RELATIVE) && len >= 3)
                        dstLen = 2;
                    const size_t predAt = i + 1 + dstLen;
                    const DWORD pred = out[predAt];
                    for (size_t k = predAt; k < i + len; ++k)
                        out[k] = out[k + 1];
                    out[i + len] = pred;
                }
            }
            i += 1 + len;
        }
        if (pass == 0) {
            if (!any)
                return out;
            out.assign(tokens, tokens + count);
        }
    }
    return out;
}

namespace {

bool replaceOnce(std::string &s, const std::string &from, const std::string &to)
{
    size_t p = s.find(from);
    if (p == std::string::npos)
        return false;
    if (s.find(from, p + from.size()) != std::string::npos)
        return false; // ambiguous
    s.replace(p, from.size(), to);
    return true;
}

} // namespace

bool postProcessGLSL(const std::string &mojo, ShaderStage stage, const TranslateOptions *opt, bool writesColor0,
                     bool writesPointSize, std::string *out, std::string *error,
                     const std::vector<ShaderSemantic> *scalarOutputs)
{
    std::string s;
    s.reserve(mojo.size() + 1024);
    // 1) precision: drop MojoShader's precision statements, emit ours right after #version.
    {
        size_t pos = 0;
        bool sawVersion = false;
        while (pos < mojo.size()) {
            size_t eol = mojo.find('\n', pos);
            if (eol == std::string::npos)
                eol = mojo.size();
            std::string line = mojo.substr(pos, eol - pos);
            pos = eol + 1;
            if (line.compare(0, 10, "precision ") == 0)
                continue;
            s += line;
            s += '\n';
            if (!sawVersion && line.compare(0, 8, "#version") == 0) {
                sawVersion = true;
                s += "precision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                     "precision highp samplerCube;\nprecision highp sampler3D;\n";
            }
        }
        if (!sawVersion) {
            *error = "no #version line in MojoShader output";
            return false;
        }
    }
    // 1b) MojoShader declares the loop end of "loop aL, iN" as "const int aLend = iN.x + iN.y;", which GLSL ES only
    //     accepts for constant expressions (defi), not for app-set integer constants (uniforms).
    {
        size_t p = 0;
        while ((p = s.find("const int aLend = ", p)) != std::string::npos) {
            s.erase(p, 6);
            p += 4;
        }
    }
    // 2) main -> d3d9_main
    if (!replaceOnce(s, "\nvoid main()\n", "\nvoid d3d9_main()\n")) {
        *error = "could not find a unique 'void main()' in MojoShader output";
        return false;
    }
    // 3) stage-specific fixups + wrapper main
    std::string scalarInit, scalarCopy;
    if (stage == ShaderStage::Vertex && scalarOutputs) {
        // MojoShader writes FOG / PSIZE(n>0) outputs as scalars ("vs_o4 = r1.z;") but declares FOG0 as a vec4
        // varying in the ES profiles; pixel shaders always read vec4. Route them through a float local.
        for (const ShaderSemantic &o : *scalarOutputs) {
            const std::string io = "io_" + std::to_string(o.usage) + "_" + std::to_string(o.usageIndex);
            const std::string tmp = "d3d9_" + io;
            const std::string def = "#define " + o.name + " " + io + "\n";
            bool declared = replaceOnce(s, "out highp vec4 " + io + ";\n", "out highp vec4 " + io + ";\nhighp float " + tmp + ";\n") ||
                            replaceOnce(s, "out highp float " + io + ";\n", "out highp vec4 " + io + ";\nhighp float " + tmp + ";\n");
            if (!declared || !replaceOnce(s, def, "#define " + o.name + " " + tmp + "\n"))
                continue;
            scalarInit += "\t" + tmp + " = 0.0;\n";
            scalarCopy += "\t" + io + " = vec4(" + tmp + ", 0.0, 0.0, 1.0);\n";
        }
    }
    if (stage == ShaderStage::Vertex) {
        s += "uniform vec4 ";
        s += kPosFixupUniform;
        s += ";\n";
        if (opt) {
            for (auto &v : opt->extraOutputs)
                s += "out highp vec4 io_" + std::to_string(v.first) + "_" + std::to_string(v.second) + ";\n";
        }
        s += "void main()\n{\n";
        if (!writesPointSize)
            s += "\tgl_PointSize = 1.0;\n";
        if (opt) {
            for (auto &v : opt->extraOutputs)
                s += "\tio_" + std::to_string(v.first) + "_" + std::to_string(v.second) + " = vec4(0.0);\n";
        }
        s += scalarInit;
        s += "\td3d9_main();\n";
        s += scalarCopy;
        s += "\tgl_Position.y = gl_Position.y * ";
        s += kPosFixupUniform;
        s += ".y;\n\tgl_Position.xy += ";
        s += kPosFixupUniform;
        s += ".zw * gl_Position.ww;\n\tgl_Position.z = gl_Position.z * 2.0 - gl_Position.w;\n}\n";
    } else {
        // D3D9 VPOS: integer pixel centers. MojoShader: vec4(gl_FragCoord.x, (gl_FragCoord.y * vposFlip.x) + ...
        const std::string vposFrom = "vec4(gl_FragCoord.x, (gl_FragCoord.y * vposFlip.x) + vposFlip.y,";
        if (s.find(vposFrom) != std::string::npos) {
            size_t p;
            while ((p = s.find(vposFrom)) != std::string::npos)
                s.replace(p, vposFrom.size(),
                          "vec4(gl_FragCoord.x - 0.5, (gl_FragCoord.y * vposFlip.x) + vposFlip.y,");
        }
        s += "uniform vec2 ";
        s += kAlphaTestUniform;
        s += ";\nvoid main()\n{\n\td3d9_main();\n";
        if (writesColor0) {
            // Alpha test as D3D9 does it: 8-bit alpha vs ALPHAREF, D3DCMPFUNC 1..8 (0 = disabled).
            s += "\tint d3d9_f = int(";
            s += kAlphaTestUniform;
            s += ".x);\n\tif (d3d9_f != 0) {\n"
                 "\t\tfloat d3d9_a = floor(clamp(_gl_FragData_0.a, 0.0, 1.0) * 255.0 + 0.5);\n"
                 "\t\tfloat d3d9_r = ";
            s += kAlphaTestUniform;
            s += ".y;\n"
                 "\t\tbool d3d9_pass = (d3d9_f == 2) ? (d3d9_a < d3d9_r) : (d3d9_f == 3) ? (d3d9_a == d3d9_r) :\n"
                 "\t\t\t(d3d9_f == 4) ? (d3d9_a <= d3d9_r) : (d3d9_f == 5) ? (d3d9_a > d3d9_r) :\n"
                 "\t\t\t(d3d9_f == 6) ? (d3d9_a != d3d9_r) : (d3d9_f == 7) ? (d3d9_a >= d3d9_r) : (d3d9_f == 8);\n"
                 "\t\tif (!d3d9_pass) discard;\n\t}\n";
        }
        s += "}\n";
    }
    *out = std::move(s);
    return true;
}

TranslatedShader translateShader(const DWORD *tokens, size_t maxBytes, const TranslateOptions *opt)
{
    TranslatedShader r;
    if (!tokens) {
        r.errors = "null bytecode";
        return r;
    }
    r.version = tokens[0];
    const DWORD kind = r.version & 0xFFFF0000;
    if (kind != 0xFFFE0000 && kind != 0xFFFF0000) {
        r.errors = "not a D3D9 shader (bad version token)";
        return r;
    }
    r.stage = kind == 0xFFFE0000 ? ShaderStage::Vertex : ShaderStage::Pixel;
    r.byteLength = shaderByteLength(tokens, maxBytes);
    if (!r.byteLength) {
        r.errors = "no END token";
        return r;
    }

    std::vector<MOJOSHADER_swizzle> swz;
    if (opt) {
        for (auto &b : opt->bgraInputs) {
            MOJOSHADER_swizzle sw;
            sw.usage = (MOJOSHADER_usage)b.first;
            sw.index = b.second;
            sw.swizzles[0] = 2;
            sw.swizzles[1] = 1;
            sw.swizzles[2] = 0;
            sw.swizzles[3] = 3;
            swz.push_back(sw);
        }
    }

    std::vector<DWORD> reordered = reorderPredicateTokens(tokens, r.byteLength / 4);
    const DWORD *src = reordered.empty() ? tokens : reordered.data();
    std::vector<DWORD> patched;
    const unsigned char *buf = (const unsigned char *)src;
    unsigned int bufLen = (unsigned int)r.byteLength;
    const MOJOSHADER_parseData *pd = nullptr;
    for (int attempt = 0; attempt < 2; ++attempt) {
        pd = MOJOSHADER_parse(MOJOSHADER_PROFILE_GLSLES3, "main", buf, bufLen, swz.empty() ? nullptr : swz.data(),
                              (unsigned)swz.size(), nullptr, 0, nullptr, nullptr, nullptr);
        if (pd->error_count == 0 || attempt == 1)
            break;
        bool needCtab = false;
        for (int i = 0; i < pd->error_count; ++i)
            if (pd->errors[i].error && strstr(pd->errors[i].error, "without a CTAB"))
                needCtab = true;
        if (!needCtab)
            break;
        MOJOSHADER_freeParseData(pd);
        pd = nullptr;
        patched = withSyntheticCTAB(src, r.byteLength / 4, r.stage == ShaderStage::Vertex ? 256 : 224);
        buf = (const unsigned char *)patched.data();
        bufLen = (unsigned int)(patched.size() * 4);
        r.syntheticCTAB = true;
    }

    if (pd->error_count > 0) {
        for (int i = 0; i < pd->error_count; ++i) {
            char line[512];
            snprintf(line, sizeof(line), "%s(pos %d) ", pd->errors[i].error ? pd->errors[i].error : "?",
                     pd->errors[i].error_position);
            r.errors += line;
        }
        MOJOSHADER_freeParseData(pd);
        return r;
    }

    for (int i = 0; i < pd->attribute_count; ++i)
        r.inputs.push_back({(uint8_t)pd->attributes[i].usage, (uint8_t)pd->attributes[i].index,
                            pd->attributes[i].name ? pd->attributes[i].name : ""});
    for (int i = 0; i < pd->output_count; ++i) {
        const MOJOSHADER_attribute &o = pd->outputs[i];
        r.outputs.push_back({(uint8_t)o.usage, (uint8_t)o.index, o.name ? o.name : ""});
        if (r.stage == ShaderStage::Vertex && o.usage == MOJOSHADER_USAGE_POINTSIZE && o.index == 0)
            r.writesPointSize = true;
    }
    for (int i = 0; i < pd->uniform_count; ++i) {
        const MOJOSHADER_uniform &u = pd->uniforms[i];
        r.uniforms.push_back({(uint8_t)u.type, u.index, u.array_count ? u.array_count : 1, u.constant != 0,
                              u.name ? u.name : ""});
    }
    for (int i = 0; i < pd->sampler_count; ++i)
        r.samplers.push_back({pd->samplers[i].index, (uint8_t)pd->samplers[i].type,
                              pd->samplers[i].name ? pd->samplers[i].name : ""});
    for (int i = 0; i < pd->constant_count; ++i) {
        ShaderConstantInfo c;
        memset(&c, 0, sizeof(c));
        c.type = (uint8_t)pd->constants[i].type;
        c.index = pd->constants[i].index;
        memcpy(c.f, pd->constants[i].value.f, sizeof(c.f));
        r.constants.push_back(c);
    }

    const std::string mojo = pd->output ? std::string(pd->output, pd->output_len) : std::string();
    MOJOSHADER_freeParseData(pd);

    r.writesColor0 = r.stage == ShaderStage::Pixel && mojo.find("_gl_FragData_0") != std::string::npos;
    r.usesVPos = mojo.find("uniform vec2 vposFlip;") != std::string::npos;
    std::string err;
    std::vector<ShaderSemantic> scalarOuts;
    if (r.stage == ShaderStage::Vertex)
        for (const ShaderSemantic &o : r.outputs)
            if (o.usage == MOJOSHADER_USAGE_FOG || (o.usage == MOJOSHADER_USAGE_POINTSIZE && o.usageIndex > 0))
                scalarOuts.push_back(o);
    if (!postProcessGLSL(mojo, r.stage, opt, r.writesColor0, r.writesPointSize, &r.glsl, &err, &scalarOuts)) {
        r.errors = err;
        return r;
    }
    r.ok = true;
    return r;
}

} // namespace d3d9shim
