// corpus_check.cpp - compile a directory of D3D9 shader bytecode (*.bin) through the shim's translator and the
// browser's real WebGL2 shader compiler (ANGLE etc.), which can be stricter than glslang.
//
//   CORPUS=build/web-d3d9/corpus_wine src/web/d3d9/test/build_corpus_check.sh   -> build/web-d3d9/corpus_check.html
//   (the corpus is preloaded at /corpus; run with run_smoke_cdp.mjs or open it in a browser)
//
// Every vertex shader is linked with a pixel shader that reads nothing; every pixel shader with a vertex shader
// that declares (zeroed) every varying it reads - the same dummy-output mechanism the device uses.
#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "../d3d9_shader_translate.h"

using namespace d3d9shim;

static GLuint compile(GLenum type, const std::string &src, std::string *log)
{
    GLuint s = glCreateShader(type);
    const char *p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048] = {0};
        glGetShaderInfoLog(s, sizeof(buf) - 1, nullptr, buf);
        *log = buf;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

static bool link(GLuint vs, GLuint fs, std::string *log)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char buf[2048] = {0};
        glGetProgramInfoLog(p, sizeof(buf) - 1, nullptr, buf);
        *log = buf;
    }
    glDeleteProgram(p);
    return ok != 0;
}

int main()
{
    EmscriptenWebGLContextAttributes attrs;
    emscripten_webgl_init_context_attributes(&attrs);
    attrs.majorVersion = 2;
    EMSCRIPTEN_WEBGL_CONTEXT_HANDLE ctx = emscripten_webgl_create_context("#canvas", &attrs);
    if (ctx <= 0 || emscripten_webgl_make_context_current(ctx) != EMSCRIPTEN_RESULT_SUCCESS) {
        printf("FAIL no WebGL2 context\n");
        printf("D3D9 SMOKE TEST FAILED: 0 passed, 1 failed\n");
        return 1;
    }
    printf("renderer: %s\n", (const char *)glGetString(GL_RENDERER));
    // trivial partners
    const std::string trivialFS = "#version 300 es\nprecision highp float;\nout vec4 o;\nvoid main() { o = vec4(1.0); }\n";
    std::string log;
    GLuint fsTrivial = compile(GL_FRAGMENT_SHADER, trivialFS, &log);

    std::vector<std::string> names;
    if (DIR *d = opendir("/corpus")) {
        while (dirent *e = readdir(d)) {
            std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".bin") == 0)
                names.push_back(n);
        }
        closedir(d);
    }
    int pass = 0, fail = 0, skipped = 0;
    for (const std::string &n : names) {
        FILE *f = fopen(("/corpus/" + n).c_str(), "rb");
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
        TranslatedShader t = translateShader(tokens.data(), tokens.size() * 4);
        if (!t.ok) {
            ++skipped; // translation failures are the node test's business
            printf("SKIP %s (translation: %s)\n", n.c_str(), t.errors.c_str());
            continue;
        }
        bool ok;
        if (t.stage == ShaderStage::Vertex) {
            GLuint vs = compile(GL_VERTEX_SHADER, t.glsl, &log);
            ok = vs && link(vs, fsTrivial, &log);
            if (vs)
                glDeleteShader(vs);
        } else {
            GLuint fs = compile(GL_FRAGMENT_SHADER, t.glsl, &log);
            std::string vsrc = "#version 300 es\nin vec4 pos;\n";
            for (const auto &in : t.inputs)
                if (in.usage != 255)
                    vsrc += "out highp vec4 io_" + std::to_string(in.usage) + "_" + std::to_string(in.usageIndex) + ";\n";
            vsrc += "void main() {\n";
            for (const auto &in : t.inputs)
                if (in.usage != 255)
                    vsrc += "  io_" + std::to_string(in.usage) + "_" + std::to_string(in.usageIndex) + " = pos;\n";
            vsrc += "  gl_Position = pos;\n}\n";
            GLuint vs = compile(GL_VERTEX_SHADER, vsrc, &log);
            ok = fs && vs && link(vs, fs, &log);
            if (fs)
                glDeleteShader(fs);
            if (vs)
                glDeleteShader(vs);
        }
        if (ok) {
            ++pass;
            printf("PASS %s\n", n.c_str());
        } else {
            ++fail;
            printf("FAIL %s: %s\n", n.c_str(), log.c_str());
        }
    }
    printf("corpus: %d compiled+linked, %d failed, %d skipped (untranslatable)\n", pass, fail, skipped);
    printf("D3D9 SMOKE TEST %s: %d passed, %d failed\n", fail ? "FAILED" : "PASSED", pass, fail);
    return 0;
}
