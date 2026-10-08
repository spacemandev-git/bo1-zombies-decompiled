// smoke.cpp - browser smoke test of the D3D9 -> WebGL2 shim (single-threaded; any static file server works).
//
//   src/web/d3d9/test/build_smoke.sh            -> build/web-d3d9/smoke.html (+ .js/.wasm)
//   cd build/web-d3d9 && python3 -m http.server 8000   then open http://localhost:8000/smoke.html
//
// Creates a device on #canvas through Direct3DCreate9/CreateDevice and exercises vertex/index buffers, declarations
// (incl. D3DCOLOR), translated hand-assembled vs_3_0/ps_3_0 shaders, A8R8G8B8 / DXT1 / L8 / cube / volume /
// mipmapped textures, render-to-texture + StretchRect, depth, stencil, alpha test, blending, culling, scissor,
// DrawPrimitiveUP, relative constant addressing, queries, Reset and Present (with the canvas flip). Every check
// reads pixels back (GetRenderTargetData, or glReadPixels of the canvas) and prints PASS/FAIL lines to the page and
// the console. window.D3D9_SMOKE = {pass, fail, done} for automation.
#include <d3d9.h>
#include <d3d9shim.h>

#include <GLES3/gl3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>
#ifdef SMOKE_THREADED
#include <thread>
#endif

#include "shader_asm.h"

static const int W = 256, H = 256;
static IDirect3D9 *g_d3d;
static IDirect3DDevice9 *g_dev;
static IDirect3DSurface9 *g_bb;
static int g_pass, g_fail;

struct Vtx {
    float x, y, z, w;
    float u, v;
    D3DCOLOR color;
};
struct Vtx3 {
    float x, y, z, w;
    float s, t, r;
};
struct VtxIdx {
    float x, y, z, w;
    BYTE idx[4];
};

static IDirect3DVertexDeclaration9 *g_decl, *g_decl3, *g_declIdx;
static IDirect3DVertexShader9 *g_vs, *g_vs3, *g_vsRel;
static IDirect3DPixelShader9 *g_psTex, *g_psColor, *g_psConst, *g_psCube, *g_psVol;
static IDirect3DVertexBuffer9 *g_vb;
static IDirect3DIndexBuffer9 *g_ib;
static IDirect3DSurface9 *g_ds;
static IDirect3DQuery9 *g_occVisible, *g_occHidden;

static void result(bool ok, const char *name, const char *detail = "")
{
    if (ok)
        ++g_pass;
    else
        ++g_fail;
    printf("%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail[0] ? ": " : "", detail);
}

#define HRCHECK(expr)                                                         \
    do {                                                                      \
        HRESULT hr_ = (expr);                                                 \
        if (FAILED(hr_)) {                                                    \
            char b_[160];                                                     \
            snprintf(b_, sizeof(b_), "%s -> 0x%08lx", #expr, (unsigned long)hr_); \
            result(false, "call", b_);                                        \
        }                                                                     \
    } while (0)

// ------------------------------------------------------------------------------------------------ helpers

struct Pixels {
    std::vector<uint8_t> bgra;
    int w = 0, h = 0;
    bool ok = false;
    void at(int x, int y, int *r, int *g, int *b, int *a) const
    {
        const uint8_t *p = &bgra[((size_t)y * w + x) * 4];
        *b = p[0], *g = p[1], *r = p[2], *a = p[3];
    }
};

static Pixels readSurface(IDirect3DSurface9 *rt)
{
    Pixels px;
    D3DSURFACE_DESC d;
    rt->GetDesc(&d);
    IDirect3DSurface9 *sys = nullptr;
    if (FAILED(g_dev->CreateOffscreenPlainSurface(d.Width, d.Height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &sys, nullptr)))
        return px;
    if (SUCCEEDED(g_dev->GetRenderTargetData(rt, sys))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
            px.w = (int)d.Width;
            px.h = (int)d.Height;
            px.bgra.resize((size_t)px.w * px.h * 4);
            for (int y = 0; y < px.h; ++y)
                memcpy(&px.bgra[(size_t)y * px.w * 4], (uint8_t *)lr.pBits + (size_t)y * lr.Pitch, (size_t)px.w * 4);
            sys->UnlockRect();
            px.ok = true;
        }
    }
    sys->Release();
    return px;
}

static bool expectPx(const Pixels &px, const char *name, int x, int y, int er, int eg, int eb, int tol = 3)
{
    if (!px.ok) {
        result(false, name, "readback failed");
        return false;
    }
    int r, g, b, a;
    px.at(x, y, &r, &g, &b, &a);
    bool ok = abs(r - er) <= tol && abs(g - eg) <= tol && abs(b - eb) <= tol;
    char buf[160];
    snprintf(buf, sizeof(buf), "pixel (%d,%d) = (%d,%d,%d), expected (%d,%d,%d)", x, y, r, g, b, er, eg, eb);
    result(ok, name, ok ? "" : buf);
    return ok;
}

// Pixel rectangle (D3D screen coordinates, y down) -> clip space.
static void clipRect(int x0, int y0, int x1, int y1, float *cx0, float *cy0, float *cx1, float *cy1, int w = W,
                     int h = H)
{
    *cx0 = x0 / (float)w * 2.f - 1.f;
    *cx1 = x1 / (float)w * 2.f - 1.f;
    *cy0 = 1.f - y0 / (float)h * 2.f; // top
    *cy1 = 1.f - y1 / (float)h * 2.f; // bottom
}

// Quad over a pixel rectangle with uv (0,0) at its top-left; two D3D-clockwise triangles; DrawIndexedPrimitive.
static void drawQuad(int x0, int y0, int x1, int y1, float z, D3DCOLOR color, int w = W, int h = H)
{
    float l, t, r, b;
    clipRect(x0, y0, x1, y1, &l, &t, &r, &b, w, h);
    Vtx *v = nullptr;
    HRCHECK(g_vb->Lock(0, 4 * sizeof(Vtx), (void **)&v, D3DLOCK_DISCARD));
    v[0] = {l, t, z, 1.f, 0.f, 0.f, color};
    v[1] = {r, t, z, 1.f, 1.f, 0.f, color};
    v[2] = {l, b, z, 1.f, 0.f, 1.f, color};
    v[3] = {r, b, z, 1.f, 1.f, 1.f, color};
    HRCHECK(g_vb->Unlock());
    HRCHECK(g_dev->SetVertexDeclaration(g_decl));
    HRCHECK(g_dev->SetStreamSource(0, g_vb, 0, sizeof(Vtx)));
    HRCHECK(g_dev->SetIndices(g_ib));
    HRCHECK(g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2));
}

static void setConstColor(float r, float g, float b, float a)
{
    const float c[4] = {r, g, b, a};
    g_dev->SetPixelShaderConstantF(0, c, 1);
}

static void resetStates()
{
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_FALSE);
    g_dev->SetRenderState(D3DRS_ZWRITEENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW);
    g_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    for (DWORD s = 0; s < 2; ++s) {
        g_dev->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        g_dev->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        g_dev->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        g_dev->SetSamplerState(s, D3DSAMP_MAXMIPLEVEL, 0);
        g_dev->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        g_dev->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        g_dev->SetSamplerState(s, D3DSAMP_ADDRESSW, D3DTADDRESS_CLAMP);
    }
    g_dev->SetRenderTarget(0, g_bb);
    g_dev->SetDepthStencilSurface(g_ds);
    g_dev->SetVertexShader(g_vs);
    setConstColor(1, 1, 1, 1);
    const float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    g_dev->SetVertexShaderConstantF(0, identity, 4);
}

// ------------------------------------------------------------------------------------------------ tests

static void testClear()
{
    resetStates();
    HRCHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 32, 64, 128), 1.f, 0));
    Pixels px = readSurface(g_bb);
    expectPx(px, "clear back buffer (BGRA readback)", 10, 10, 32, 64, 128, 0);
    // Clear with a rectangle list
    D3DRECT r = {100, 20, 140, 60};
    HRCHECK(g_dev->Clear(1, &r, D3DCLEAR_TARGET, D3DCOLOR_XRGB(255, 255, 0), 1.f, 0));
    px = readSurface(g_bb);
    expectPx(px, "clear rect inside", 120, 40, 255, 255, 0, 0);
    expectPx(px, "clear rect outside", 90, 40, 32, 64, 128, 0);
}

static IDirect3DTexture9 *makeCheckerTexture()
{
    IDirect3DTexture9 *t = nullptr;
    HRCHECK(g_dev->CreateTexture(2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr));
    D3DLOCKED_RECT lr;
    HRCHECK(t->LockRect(0, &lr, nullptr, 0));
    DWORD *row0 = (DWORD *)lr.pBits, *row1 = (DWORD *)((uint8_t *)lr.pBits + lr.Pitch);
    row0[0] = 0xFFFF0000; // red    (memory bytes B,G,R,A)
    row0[1] = 0xFF00FF00; // green
    row1[0] = 0xFF0000FF; // blue
    row1[1] = 0xFFFFFFFF; // white
    HRCHECK(t->UnlockRect(0));
    return t;
}

static void testTexturedQuad()
{
    resetStates();
    IDirect3DTexture9 *t = makeCheckerTexture();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetPixelShader(g_psTex);
    g_dev->SetTexture(0, t);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "A8R8G8B8 texture: top-left texel (orientation)", 32, 32, 255, 0, 0);
    expectPx(px, "A8R8G8B8 texture: top-right texel", 224, 32, 0, 255, 0);
    expectPx(px, "A8R8G8B8 texture: bottom-left texel", 32, 224, 0, 0, 255);
    expectPx(px, "A8R8G8B8 texture: bottom-right texel", 224, 224, 255, 255, 255);
    // modulate by vertex color 0xFF102030 (D3DCOLOR is BGRA in memory: checks the attribute swizzle) and c0
    setConstColor(1, 1, 1, 1);
    drawQuad(0, 0, W, H, 0.5f, 0xFF808080);
    px = readSurface(g_bb);
    expectPx(px, "texture * vertex color", 224, 224, 128, 128, 128, 2);
    g_dev->SetTexture(0, nullptr);
    t->Release();
}

static void testVertexColorSwizzle()
{
    resetStates();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetPixelShader(g_psColor);
    drawQuad(0, 0, W, H, 0.5f, 0xFF102030);
    Pixels px = readSurface(g_bb);
    expectPx(px, "D3DCOLOR vertex attribute (BGRA swizzle)", 128, 128, 0x10, 0x20, 0x30, 1);
}

static void testDXT1()
{
    resetStates();
    IDirect3DTexture9 *t = nullptr;
    HRCHECK(g_dev->CreateTexture(8, 8, 1, 0, D3DFMT_DXT1, D3DPOOL_MANAGED, &t, nullptr));
    if (!t)
        return;
    D3DLOCKED_RECT lr;
    HRCHECK(t->LockRect(0, &lr, nullptr, 0));
    const uint16_t colors[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF}; // red, green, blue, white
    for (int by = 0; by < 2; ++by)
        for (int bx = 0; bx < 2; ++bx) {
            uint8_t *blk = (uint8_t *)lr.pBits + by * lr.Pitch + bx * 8;
            uint16_t c0 = colors[by * 2 + bx], c1 = 0;
            memcpy(blk, &c0, 2);
            memcpy(blk + 2, &c1, 2);
            memset(blk + 4, 0, 4); // all texels use color0
        }
    HRCHECK(t->UnlockRect(0));
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetPixelShader(g_psTex);
    g_dev->SetTexture(0, t);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "DXT1 texture: block (0,0) red", 32, 32, 255, 0, 0);
    expectPx(px, "DXT1 texture: block (1,0) green", 224, 32, 0, 255, 0);
    expectPx(px, "DXT1 texture: block (0,1) blue", 32, 224, 0, 0, 255);
    expectPx(px, "DXT1 texture: block (1,1) white", 224, 224, 255, 255, 255);
    g_dev->SetTexture(0, nullptr);
    t->Release();
}

static void testL8()
{
    resetStates();
    IDirect3DTexture9 *t = nullptr;
    HRCHECK(g_dev->CreateTexture(2, 1, 1, 0, D3DFMT_L8, D3DPOOL_MANAGED, &t, nullptr));
    if (!t)
        return;
    D3DLOCKED_RECT lr;
    HRCHECK(t->LockRect(0, &lr, nullptr, 0));
    ((uint8_t *)lr.pBits)[0] = 64;
    ((uint8_t *)lr.pBits)[1] = 200;
    HRCHECK(t->UnlockRect(0));
    g_dev->SetPixelShader(g_psTex);
    g_dev->SetTexture(0, t);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "L8 texture (LUMINANCE) left", 32, 128, 64, 64, 64);
    expectPx(px, "L8 texture (LUMINANCE) right", 224, 128, 200, 200, 200);
    g_dev->SetTexture(0, nullptr);
    t->Release();
}

static void testMips()
{
    resetStates();
    IDirect3DTexture9 *t = nullptr;
    HRCHECK(g_dev->CreateTexture(4, 4, 0, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &t, nullptr));
    if (!t)
        return;
    const DWORD levelColor[3] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF};
    bool okLevels = t->GetLevelCount() == 3;
    result(okLevels, "full mip chain level count (4x4 -> 3)");
    for (UINT l = 0; l < t->GetLevelCount() && l < 3; ++l) {
        D3DSURFACE_DESC d;
        t->GetLevelDesc(l, &d);
        D3DLOCKED_RECT lr;
        HRCHECK(t->LockRect(l, &lr, nullptr, 0));
        for (UINT y = 0; y < d.Height; ++y)
            for (UINT x = 0; x < d.Width; ++x)
                ((DWORD *)((uint8_t *)lr.pBits + y * lr.Pitch))[x] = levelColor[l];
        HRCHECK(t->UnlockRect(l));
    }
    g_dev->SetPixelShader(g_psTex);
    g_dev->SetTexture(0, t);
    g_dev->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_POINT);
    g_dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 1);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "mip level 1 via D3DSAMP_MAXMIPLEVEL", 128, 128, 0, 255, 0);
    g_dev->SetSamplerState(0, D3DSAMP_MAXMIPLEVEL, 0);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    px = readSurface(g_bb);
    expectPx(px, "mip level 0 when magnified", 128, 128, 255, 0, 0);
    g_dev->SetTexture(0, nullptr);
    t->Release();
}

static void testRenderToTexture()
{
    resetStates();
    IDirect3DTexture9 *rt = nullptr;
    HRCHECK(g_dev->CreateTexture(64, 64, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &rt, nullptr));
    if (!rt)
        return;
    IDirect3DSurface9 *rts = nullptr;
    HRCHECK(rt->GetSurfaceLevel(0, &rts));
    IDirect3DSurface9 *rts2 = nullptr;
    rt->GetSurfaceLevel(0, &rts2);
    result(rts == rts2, "GetSurfaceLevel returns the same surface object");
    if (rts2)
        rts2->Release();
    HRCHECK(g_dev->SetRenderTarget(0, rts));
    HRCHECK(g_dev->SetDepthStencilSurface(nullptr));
    D3DVIEWPORT9 vp;
    g_dev->GetViewport(&vp);
    result(vp.Width == 64 && vp.Height == 64, "SetRenderTarget resets the viewport to the target size");
    HRCHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 255, 0), 1.f, 0));
    g_dev->SetPixelShader(g_psConst);
    setConstColor(1, 0, 0, 1);
    drawQuad(0, 0, 32, 32, 0.5f, 0xFFFFFFFF, 64, 64); // top-left quarter of the RT
    Pixels px = readSurface(rts);
    expectPx(px, "render target: drawn top-left quarter", 8, 8, 255, 0, 0);
    expectPx(px, "render target: cleared elsewhere", 48, 48, 0, 255, 0);

    // StretchRect RT -> back buffer (0,0)-(128,128) with scaling
    HRCHECK(g_dev->SetRenderTarget(0, g_bb));
    HRCHECK(g_dev->SetDepthStencilSurface(g_ds));
    HRCHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0));
    RECT dst = {0, 0, 128, 128};
    HRCHECK(g_dev->StretchRect(rts, nullptr, g_bb, &dst, D3DTEXF_LINEAR));
    px = readSurface(g_bb);
    expectPx(px, "StretchRect: scaled top-left quarter red", 20, 20, 255, 0, 0);
    expectPx(px, "StretchRect: green elsewhere in dest", 110, 110, 0, 255, 0);
    expectPx(px, "StretchRect: outside dest untouched", 200, 200, 0, 0, 0);

    // sample the RT as a texture: same orientation as an uploaded texture
    g_dev->SetPixelShader(g_psTex);
    setConstColor(1, 1, 1, 1);
    g_dev->SetTexture(0, rt);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    px = readSurface(g_bb);
    expectPx(px, "render target sampled as texture: top-left red", 30, 30, 255, 0, 0);
    expectPx(px, "render target sampled as texture: bottom-right green", 220, 220, 0, 255, 0);
    g_dev->SetTexture(0, nullptr);
    rts->Release();
    ULONG refs = rt->Release();
    result(refs == 0, "texture refcount reaches 0 after releasing surface + texture");
}

static void testDepth()
{
    resetStates();
    HRCHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.f, 0));
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    g_dev->SetRenderState(D3DRS_ZFUNC, D3DCMP_LESS);
    g_dev->SetPixelShader(g_psConst);
    setConstColor(0, 0, 1, 1);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    setConstColor(0, 1, 0, 1);
    drawQuad(64, 64, 192, 192, 0.25f, 0xFFFFFFFF); // nearer: wins
    setConstColor(1, 0, 0, 1);
    drawQuad(0, 0, W, H, 0.75f, 0xFFFFFFFF); // farther: rejected everywhere
    Pixels px = readSurface(g_bb);
    expectPx(px, "depth test: near quad visible", 128, 128, 0, 255, 0);
    expectPx(px, "depth test: far quad rejected", 20, 20, 0, 0, 255);
    // D3D z in [0,1] maps to the full depth range: z=0.99 passes against cleared 1.0, z=1.01 is clipped.
    setConstColor(1, 1, 0, 1);
    g_dev->Clear(0, nullptr, D3DCLEAR_ZBUFFER, 0, 1.f, 0);
    drawQuad(0, 0, 32, 32, 0.99f, 0xFFFFFFFF);
    drawQuad(224, 0, 256, 32, 1.01f, 0xFFFFFFFF);
    px = readSurface(g_bb);
    expectPx(px, "depth: z=0.99 inside [0,1]", 16, 16, 255, 255, 0);
    expectPx(px, "depth: z=1.01 clipped (D3D clip space)", 240, 16, 0, 0, 255);
    // z = -0.01 must be clipped too (GL would keep it without the 2z-w fixup)
    drawQuad(224, 224, 256, 256, -0.01f, 0xFFFFFFFF);
    px = readSurface(g_bb);
    expectPx(px, "depth: z=-0.01 clipped (D3D near plane at z=0)", 240, 240, 0, 0, 255);
}

static void testStencil()
{
    resetStates();
    HRCHECK(g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_STENCIL | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 255),
                         1.f, 0));
    g_dev->SetPixelShader(g_psConst);
    g_dev->SetRenderState(D3DRS_STENCILENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
    g_dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
    g_dev->SetRenderState(D3DRS_STENCILREF, 1);
    g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0);
    drawQuad(0, 0, 128, 128, 0.5f, 0xFFFFFFFF);
    g_dev->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    g_dev->SetRenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
    g_dev->SetRenderState(D3DRS_STENCILPASS, D3DSTENCILOP_KEEP);
    setConstColor(1, 0, 0, 1);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "stencil: masked region drawn", 64, 64, 255, 0, 0);
    expectPx(px, "stencil: outside mask untouched", 200, 200, 0, 0, 255);
    expectPx(px, "color write disabled pass left no color", 64, 200, 0, 0, 255);
}

static void testAlphaTestAndBlend()
{
    resetStates();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.f, 0);
    g_dev->SetPixelShader(g_psColor);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_GREATER);
    g_dev->SetRenderState(D3DRS_ALPHAREF, 128);
    drawQuad(0, 0, 128, H, 0.5f, 0x40FF0000); // alpha 64: rejected
    g_dev->SetRenderState(D3DRS_ALPHAREF, 32);
    drawQuad(128, 0, W, H, 0.5f, 0x40FF0000); // alpha 64 > 32: drawn
    Pixels px = readSurface(g_bb);
    expectPx(px, "alpha test GREATER 128 rejects alpha 64", 64, 128, 0, 0, 255);
    expectPx(px, "alpha test GREATER 32 keeps alpha 64", 192, 128, 255, 0, 0);
    g_dev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);

    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.f, 0);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    drawQuad(0, 0, W, H, 0.5f, 0x80FF0000);
    px = readSurface(g_bb);
    expectPx(px, "alpha blend SRCALPHA/INVSRCALPHA", 128, 128, 128, 0, 127, 3);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
}

static void testCullAndScissor()
{
    resetStates();
    g_dev->SetPixelShader(g_psConst);
    setConstColor(1, 0, 0, 1);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.f, 0);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CW); // our quads are D3D clockwise: culled
    drawQuad(0, 0, 128, H, 0.5f, 0xFFFFFFFF);
    g_dev->SetRenderState(D3DRS_CULLMODE, D3DCULL_CCW); // default: clockwise is front, drawn
    drawQuad(128, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "D3DCULL_CW culls clockwise triangles", 64, 128, 0, 0, 255);
    expectPx(px, "D3DCULL_CCW keeps clockwise triangles", 192, 128, 255, 0, 0);

    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.f, 0);
    RECT sc = {0, 0, 128, 64};
    g_dev->SetScissorRect(&sc);
    g_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 255, 0), 1.f, 0); // Clear honors the scissor too
    g_dev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    px = readSurface(g_bb);
    expectPx(px, "scissor: inside (top-left, y down)", 64, 32, 0, 255, 0);
    expectPx(px, "scissor: outside right", 192, 32, 0, 0, 255);
    expectPx(px, "scissor: outside below", 64, 192, 0, 0, 255);
}

static void testDrawPrimitiveUP()
{
    resetStates();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetPixelShader(g_psColor);
    g_dev->SetVertexDeclaration(g_decl);
    Vtx v[4] = {{-1, 1, 0.5f, 1, 0, 0, 0xFF00FFFF},
                {0, 1, 0.5f, 1, 1, 0, 0xFF00FFFF},
                {-1, 0, 0.5f, 1, 0, 1, 0xFF00FFFF},
                {0, 0, 0.5f, 1, 1, 1, 0xFF00FFFF}};
    HRCHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx)));
    Pixels px = readSurface(g_bb);
    expectPx(px, "DrawPrimitiveUP triangle strip", 64, 64, 0, 255, 255);
    expectPx(px, "DrawPrimitiveUP outside", 192, 192, 0, 0, 0);
    IDirect3DVertexBuffer9 *sb = (IDirect3DVertexBuffer9 *)1;
    UINT off = 1, stride = 1;
    g_dev->GetStreamSource(0, &sb, &off, &stride);
    result(sb == nullptr, "stream 0 is unset after DrawPrimitiveUP");
    // indexed UP: a triangle fan
    const uint16_t idx[6] = {0, 1, 2, 2, 1, 3};
    for (auto &x : v)
        x.x += 1.f, x.color = 0xFFFF00FF;
    HRCHECK(g_dev->DrawIndexedPrimitiveUP(D3DPT_TRIANGLELIST, 0, 4, 2, idx, D3DFMT_INDEX16, v, sizeof(Vtx)));
    px = readSurface(g_bb);
    expectPx(px, "DrawIndexedPrimitiveUP", 192, 64, 255, 0, 255);
}

static void testCubeAndVolume()
{
    resetStates();
    IDirect3DCubeTexture9 *cube = nullptr;
    HRCHECK(g_dev->CreateCubeTexture(1, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &cube, nullptr));
    const DWORD faceColor[6] = {0xFFFF0000, 0xFF00FF00, 0xFF0000FF, 0xFFFFFF00, 0xFFFF00FF, 0xFF00FFFF};
    if (cube) {
        for (int f = 0; f < 6; ++f) {
            D3DLOCKED_RECT lr;
            HRCHECK(cube->LockRect((D3DCUBEMAP_FACES)f, 0, &lr, nullptr, 0));
            *(DWORD *)lr.pBits = faceColor[f];
            HRCHECK(cube->UnlockRect((D3DCUBEMAP_FACES)f, 0));
        }
    }
    IDirect3DVolumeTexture9 *vol = nullptr;
    HRCHECK(g_dev->CreateVolumeTexture(1, 1, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &vol, nullptr));
    if (vol) {
        D3DLOCKED_BOX lb;
        HRCHECK(vol->LockBox(0, &lb, nullptr, 0));
        *(DWORD *)lb.pBits = 0xFFFF0000;
        *(DWORD *)((uint8_t *)lb.pBits + lb.SlicePitch) = 0xFF00FF00;
        HRCHECK(vol->UnlockBox(0));
    }
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetVertexShader(g_vs3);
    g_dev->SetVertexDeclaration(g_decl3);
    auto quad3 = [&](float x0, float x1, float s, float t, float r) {
        Vtx3 v[4] = {{x0, 1, 0.5f, 1, s, t, r}, {x1, 1, 0.5f, 1, s, t, r}, {x0, -1, 0.5f, 1, s, t, r},
                     {x1, -1, 0.5f, 1, s, t, r}};
        g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx3));
    };
    g_dev->SetPixelShader(g_psCube);
    g_dev->SetTexture(0, cube);
    quad3(-1.f, -0.5f, 1.f, 0.f, 0.f);  // +X
    quad3(-0.5f, 0.f, 0.f, 0.f, -1.f);  // -Z
    g_dev->SetPixelShader(g_psVol);
    g_dev->SetTexture(0, vol);
    quad3(0.f, 0.5f, 0.5f, 0.5f, 0.25f); // slice 0
    quad3(0.5f, 1.f, 0.5f, 0.5f, 0.75f); // slice 1
    Pixels px = readSurface(g_bb);
    expectPx(px, "cube texture +X face", 32, 128, 255, 0, 0);
    expectPx(px, "cube texture -Z face", 96, 128, 0, 255, 255);
    expectPx(px, "volume texture slice 0", 160, 128, 255, 0, 0);
    expectPx(px, "volume texture slice 1", 224, 128, 0, 255, 0);
    g_dev->SetTexture(0, nullptr);
    if (cube)
        cube->Release();
    if (vol)
        vol->Release();
}

static void testRelativeAddressing()
{
    resetStates();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetVertexShader(g_vsRel);
    g_dev->SetPixelShader(g_psConst);
    setConstColor(1, 1, 1, 1);
    const float c10[8] = {0, 0, 0, 0, 1.f, 0, 0, 0}; // c10 = 0, c11 = +1 clip unit in x
    g_dev->SetVertexShaderConstantF(10, c10, 2);
    g_dev->SetVertexDeclaration(g_declIdx);
    VtxIdx v[4] = {{-1, 1, 0.5f, 1, {1, 0, 0, 0}}, {0, 1, 0.5f, 1, {1, 0, 0, 0}}, {-1, 0, 0.5f, 1, {1, 0, 0, 0}},
                   {0, 0, 0.5f, 1, {1, 0, 0, 0}}};
    HRCHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxIdx)));
    Pixels px = readSurface(g_bb);
    expectPx(px, "relative constant addressing c[10 + a0.x] (synthetic CTAB)", 192, 64, 255, 255, 255);
    expectPx(px, "relative addressing: original position empty", 64, 64, 0, 0, 0);

    // A relative read that lands on a def'd register sees the def value (c12 = +1), not the app's c12 = 0.
    auto relDef = test_shaders::relativeDefVS();
    IDirect3DVertexShader9 *vsDef = nullptr;
    HRCHECK(g_dev->CreateVertexShader(relDef.data(), &vsDef));
    const float zero12[4] = {0, 0, 0, 0};
    g_dev->SetVertexShaderConstantF(12, zero12, 1);
    g_dev->SetVertexShader(vsDef);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    for (auto &x : v)
        x.idx[0] = 2;
    HRCHECK(g_dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(VtxIdx)));
    px = readSurface(g_bb);
    expectPx(px, "relative read of a def'd register uses the def value", 192, 64, 255, 255, 255);
    g_dev->SetVertexShader(g_vs);
    if (vsDef)
        vsDef->Release();
}

static void testQueries()
{
    resetStates();
    IDirect3DQuery9 *ev = nullptr;
    HRCHECK(g_dev->CreateQuery(D3DQUERYTYPE_EVENT, &ev));
    if (ev) {
        ev->Issue(D3DISSUE_END);
        BOOL done = FALSE;
        HRESULT hr = ev->GetData(&done, sizeof(done), D3DGETDATA_FLUSH);
        result(hr == S_OK && done, "event query signaled immediately");
        ev->Release();
    }
    result(g_dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, nullptr) == D3D_OK, "occlusion queries supported");
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1.f, 0);
    g_dev->SetPixelShader(g_psConst);
    HRCHECK(g_dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &g_occVisible));
    HRCHECK(g_dev->CreateQuery(D3DQUERYTYPE_OCCLUSION, &g_occHidden));
    g_dev->SetRenderState(D3DRS_ZENABLE, D3DZB_TRUE);
    drawQuad(0, 0, W, H, 0.1f, 0xFFFFFFFF); // occluder
    g_occVisible->Issue(D3DISSUE_BEGIN);
    drawQuad(0, 0, 64, 64, 0.05f, 0xFFFFFFFF);
    g_occVisible->Issue(D3DISSUE_END);
    g_occHidden->Issue(D3DISSUE_BEGIN);
    drawQuad(0, 0, 64, 64, 0.5f, 0xFFFFFFFF); // behind the occluder
    g_occHidden->Issue(D3DISSUE_END);
    DWORD n = 12345;
    HRESULT hr = g_occVisible->GetData(&n, sizeof(n), D3DGETDATA_FLUSH);
    result(hr == S_OK, "occlusion GetData never returns S_FALSE (no spin)");
}

static void testPresentFlip()
{
    resetStates();
    g_dev->SetPixelShader(g_psConst);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.f, 0);
    setConstColor(1, 0, 0, 1);
    drawQuad(0, 0, W, 64, 0.5f, 0xFFFFFFFF); // red band at the top
    IDirect3DSwapChain9 *sc = nullptr;
    HRCHECK(g_dev->GetSwapChain(0, &sc));
    if (sc) {
        HRCHECK(sc->Present(nullptr, nullptr, nullptr, nullptr, 0));
        sc->Release();
    }
    // Read the canvas (default framebuffer, bottom-left origin) before yielding; restore the shim's binding after.
    GLint prevRead = 0;
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    uint8_t top[4] = {0}, bottom[4] = {0};
    glReadPixels(10, H - 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top);
    glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bottom);
    char buf[128];
    snprintf(buf, sizeof(buf), "canvas top (%d,%d,%d) bottom (%d,%d,%d)", top[0], top[1], top[2], bottom[0], bottom[1],
             bottom[2]);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    result(top[0] > 250 && top[2] < 5 && bottom[2] > 250 && bottom[0] < 5, "Present flips into the canvas", buf);

    // Gamma ramp (engine brightness): applied by Present. Inverted ramp: red top band -> cyan.
    D3DGAMMARAMP ramp;
    for (int i = 0; i < 256; ++i)
        ramp.red[i] = ramp.green[i] = ramp.blue[i] = (WORD)((255 - i) * 257);
    g_dev->SetGammaRamp(0, 0, &ramp);
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    glReadPixels(10, H - 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, top);
    glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, bottom);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
    snprintf(buf, sizeof(buf), "canvas top (%d,%d,%d) bottom (%d,%d,%d)", top[0], top[1], top[2], bottom[0], bottom[1],
             bottom[2]);
    result(top[0] < 5 && top[1] > 250 && top[2] > 250 && bottom[0] > 250 && bottom[2] < 5,
           "SetGammaRamp applied at Present (inverted ramp, still flipped)", buf);
    for (int i = 0; i < 256; ++i)
        ramp.red[i] = ramp.green[i] = ramp.blue[i] = (WORD)(i * 257);
    g_dev->SetGammaRamp(0, 0, &ramp);
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
    // the device must still draw correctly after the gamma pass touched its GL state
    g_dev->SetRenderTarget(0, g_bb);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetVertexShader(g_vs);
    g_dev->SetPixelShader(g_psTex);
    setConstColor(1, 1, 1, 1);
    IDirect3DTexture9 *t = makeCheckerTexture();
    g_dev->SetTexture(0, t);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "draw after gamma present (state restored)", 224, 32, 0, 255, 0);
    g_dev->SetTexture(0, nullptr);
    t->Release();
}

static void testReset()
{
    resetStates();
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = 128;
    pp.BackBufferHeight = 96;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = (HWND)1;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    g_bb->Release();
    g_bb = nullptr;
    HRESULT hr = g_dev->Reset(&pp);
    result(SUCCEEDED(hr), "Reset to 128x96");
    g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &g_bb);
    D3DSURFACE_DESC d;
    g_bb->GetDesc(&d);
    result(d.Width == 128 && d.Height == 96, "back buffer resized by Reset");
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(9, 99, 199), 1.f, 0);
    Pixels px = readSurface(g_bb);
    expectPx(px, "clear after Reset", 100, 80, 9, 99, 199, 0);
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    g_bb->Release();
    g_dev->Reset(&pp);
    g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &g_bb);
}

static void testCapsAndFormats()
{
    D3DCAPS9 caps;
    HRCHECK(g_dev->GetDeviceCaps(&caps));
    result(caps.VertexShaderVersion == D3DVS_VERSION(3, 0) && caps.PixelShaderVersion == D3DPS_VERSION(3, 0),
           "device caps report shader model 3.0");
    char buf[96];
    snprintf(buf, sizeof(buf), "MaxTextureWidth %lu, MaxAnisotropy %lu", (unsigned long)caps.MaxTextureWidth,
             (unsigned long)caps.MaxAnisotropy);
    result(caps.MaxTextureWidth >= 2048, "device caps texture size from the live context", buf);
    result(g_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_RENDERTARGET, D3DRTYPE_TEXTURE,
                                    D3DFMT_R32F) == D3D_OK,
           "R32F render targets (EXT_color_buffer_float)");
    result(g_d3d->CheckDeviceFormat(0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_DEPTHSTENCIL, D3DRTYPE_TEXTURE,
                                    (D3DFORMAT)D3D9SHIM_FOURCC_INTZ) == D3DERR_NOTAVAILABLE,
           "INTZ reported unsupported");
    // float render target clear + readback through the float path
    IDirect3DTexture9 *ft = nullptr;
    if (SUCCEEDED(g_dev->CreateTexture(16, 16, 1, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT, &ft, nullptr))) {
        IDirect3DSurface9 *fs = nullptr, *sys = nullptr;
        ft->GetSurfaceLevel(0, &fs);
        g_dev->SetRenderTarget(0, fs);
        g_dev->SetDepthStencilSurface(nullptr);
        g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_ARGB(255, 128, 0, 0), 1.f, 0);
        g_dev->CreateOffscreenPlainSurface(16, 16, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &sys, nullptr);
        bool ok = false;
        float v = -1.f;
        if (sys && SUCCEEDED(g_dev->GetRenderTargetData(fs, sys))) {
            D3DLOCKED_RECT lr;
            if (SUCCEEDED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) {
                v = *(float *)lr.pBits;
                ok = fabsf(v - 128.f / 255.f) < 0.01f;
                sys->UnlockRect();
            }
        }
        snprintf(buf, sizeof(buf), "read %f", v);
        result(ok, "R32F render target clear + float readback", buf);
        if (sys)
            sys->Release();
        fs->Release();
        ft->Release();
        g_dev->SetRenderTarget(0, g_bb);
        g_dev->SetDepthStencilSurface(g_ds);
    } else {
        result(false, "R32F render target creation");
    }
}

#ifdef SMOKE_THREADED
// The engine locks the water VB / water texture and polls fences from worker threads: the shim must accept that
// (CPU-side only) and upload on the device thread at the next draw.
static void testWorkerThreadLocks()
{
    resetStates();
    IDirect3DTexture9 *t = nullptr;
    HRCHECK(g_dev->CreateTexture(2, 2, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &t, nullptr));
    IDirect3DQuery9 *fence = nullptr;
    g_dev->CreateQuery(D3DQUERYTYPE_EVENT, &fence);
    if (fence)
        fence->Issue(D3DISSUE_END);
    bool workerOk = true;
    std::thread worker([&] {
        float l, t0, r, b;
        clipRect(0, 0, W, H, &l, &t0, &r, &b);
        Vtx *v = nullptr;
        if (FAILED(g_vb->Lock(0, 4 * sizeof(Vtx), (void **)&v, D3DLOCK_DISCARD))) {
            workerOk = false;
            return;
        }
        v[0] = {l, t0, 0.5f, 1.f, 0.f, 0.f, 0xFFFFFFFF};
        v[1] = {r, t0, 0.5f, 1.f, 1.f, 0.f, 0xFFFFFFFF};
        v[2] = {l, b, 0.5f, 1.f, 0.f, 1.f, 0xFFFFFFFF};
        v[3] = {r, b, 0.5f, 1.f, 1.f, 1.f, 0xFFFFFFFF};
        g_vb->Unlock();
        D3DLOCKED_RECT lr;
        if (FAILED(t->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) {
            workerOk = false;
            return;
        }
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 2; ++x)
                ((DWORD *)((uint8_t *)lr.pBits + y * lr.Pitch))[x] = 0xFFFF8000; // orange
        t->UnlockRect(0);
        BOOL done = FALSE;
        if (!fence || fence->GetData(&done, sizeof(done), D3DGETDATA_FLUSH) != S_OK || !done)
            workerOk = false;
    });
    worker.join();
    result(workerOk, "Lock/LockRect/GetData(EVENT) from a worker thread");
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, 0, 1.f, 0);
    g_dev->SetPixelShader(g_psTex);
    g_dev->SetTexture(0, t);
    g_dev->SetVertexDeclaration(g_decl);
    g_dev->SetStreamSource(0, g_vb, 0, sizeof(Vtx));
    g_dev->SetIndices(g_ib);
    g_dev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, 4, 0, 2);
    Pixels px = readSurface(g_bb);
    expectPx(px, "worker-thread writes uploaded on the device thread", 128, 128, 255, 128, 0, 1);
    g_dev->SetTexture(0, nullptr);
    t->Release();
    if (fence)
        fence->Release();
}
#endif

// One-texel textures of various formats drawn full screen over a blue background with SRCALPHA/INVSRCALPHA
// blending (so the sampled alpha matters too).
static void drawTexel(D3DFORMAT fmt, UINT w, UINT h, const void *data, size_t rowBytes, UINT rows, const char *name,
                      int er, int eg, int eb, int tol = 3)
{
    resetStates();
    IDirect3DTexture9 *t = nullptr;
    HRESULT hr = g_dev->CreateTexture(w, h, 1, 0, fmt, D3DPOOL_MANAGED, &t, nullptr);
    if (FAILED(hr) || !t) {
        result(false, name, "CreateTexture failed");
        return;
    }
    D3DLOCKED_RECT lr;
    HRCHECK(t->LockRect(0, &lr, nullptr, 0));
    for (UINT y = 0; y < rows; ++y)
        memcpy((uint8_t *)lr.pBits + y * lr.Pitch, (const uint8_t *)data + y * rowBytes, rowBytes);
    HRCHECK(t->UnlockRect(0));
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 255), 1.f, 0);
    g_dev->SetPixelShader(g_psTex);
    g_dev->SetTexture(0, t);
    g_dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    g_dev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    g_dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    drawQuad(0, 0, W, H, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, name, W / 4, H / 4, er, eg, eb, tol);
    g_dev->SetTexture(0, nullptr);
    t->Release();
}

static void testMoreFormats()
{
    const DWORD x8 = 0x00FF0000; // X8R8G8B8 red with X = 0: must sample alpha 1
    drawTexel(D3DFMT_X8R8G8B8, 1, 1, &x8, 4, 1, "X8R8G8B8 samples alpha = 1", 255, 0, 0);
    const uint16_t a1555[2] = {(uint16_t)(0x8000 | (31 << 10)), (uint16_t)(31 << 5)}; // opaque red, clear green
    drawTexel(D3DFMT_A1R5G5B5, 2, 1, a1555, 4, 1, "A1R5G5B5 opaque texel", 255, 0, 0);
    const uint16_t x1555 = (uint16_t)(31 << 5); // X bit 0 but must be opaque
    drawTexel(D3DFMT_X1R5G5B5, 1, 1, &x1555, 2, 1, "X1R5G5B5 samples alpha = 1", 0, 255, 0);
    const uint16_t a4444 = 0xF0F0; // A=F R=0 G=F B=0
    drawTexel(D3DFMT_A4R4G4B4, 1, 1, &a4444, 2, 1, "A4R4G4B4", 0, 255, 0);
    const uint16_t r565 = 0xF800;
    drawTexel(D3DFMT_R5G6B5, 1, 1, &r565, 2, 1, "R5G6B5", 255, 0, 0);
    const uint16_t a16[4] = {65535, 32768, 0, 65535}; // R, G, B, A (D3D A16B16G16R16: R in the low word)
    drawTexel(D3DFMT_A16B16G16R16, 1, 1, a16, 8, 1, "A16B16G16R16 (unorm16 via RGBA16F or norm16)", 255, 128, 0, 2);
    const uint8_t a8 = 128; // (0,0,0,0.5) over blue
    drawTexel(D3DFMT_A8, 1, 1, &a8, 1, 1, "A8 texture (ALPHA)", 0, 0, 127);
    const uint8_t a8l8[2] = {200, 255}; // L, A
    drawTexel(D3DFMT_A8L8, 1, 1, a8l8, 2, 1, "A8L8 texture (LUMINANCE_ALPHA)", 200, 200, 200);
    uint8_t dxt1[8] = {0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0}; // 2x2 level 0: not 4-aligned -> software decode
    drawTexel(D3DFMT_DXT1, 2, 2, dxt1, 8, 1, "DXT1 2x2 (software S3TC decode path)", 255, 0, 0);
    uint8_t dxt5[16] = {255, 255, 0, 0, 0, 0, 0, 0, 0x1F, 0x00, 0x00, 0x00, 0, 0, 0, 0}; // opaque blue
    drawTexel(D3DFMT_DXT5, 4, 4, dxt5, 16, 1, "DXT5 texture", 0, 0, 255);
}

// Releases every object, checks the device refcount reaches 0, then creates a new device (reusing the canvas'
// WebGL context) and renders with it.
static bool createDevice();
static void releaseAll();
static void testRecreate()
{
    releaseAll();
    ULONG refs = g_dev->Release();
    char buf[64];
    snprintf(buf, sizeof(buf), "device refcount after release: %lu", (unsigned long)refs);
    result(refs == 0, "all objects released -> device refcount 0", buf);
    g_dev = nullptr;
    bool ok = createDevice();
    result(ok, "re-create device on the same canvas");
    if (!ok)
        return;
    resetStates();
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(1, 2, 3), 1.f, 0);
    g_dev->SetPixelShader(g_psConst);
    setConstColor(1, 0.5f, 0, 1);
    drawQuad(0, 0, W, H / 2, 0.5f, 0xFFFFFFFF);
    Pixels px = readSurface(g_bb);
    expectPx(px, "draw on the re-created device", 128, 32, 255, 128, 0, 1);
    expectPx(px, "clear on the re-created device", 128, 200, 1, 2, 3, 0);
}

// ------------------------------------------------------------------------------------------------ setup / frames

static void releaseAll()
{
    IUnknown *objs[] = {g_decl, g_decl3, g_declIdx, g_vs, g_vs3, g_vsRel, g_psTex, g_psColor, g_psConst,
                        g_psCube, g_psVol, g_vb, g_ib, g_ds, g_bb};
    g_dev->SetRenderTarget(0, g_bb);
    for (IUnknown *o : objs)
        if (o)
            o->Release();
    g_decl = g_decl3 = g_declIdx = nullptr;
    g_vs = g_vs3 = g_vsRel = nullptr;
    g_psTex = g_psColor = g_psConst = g_psCube = g_psVol = nullptr;
    g_vb = nullptr;
    g_ib = nullptr;
    g_ds = g_bb = nullptr;
}

static bool createDevice()
{
    D3DPRESENT_PARAMETERS pp = {};
    pp.BackBufferWidth = W;
    pp.BackBufferHeight = H;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = (HWND)1;
    pp.EnableAutoDepthStencil = FALSE;
    pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    HRESULT hr = g_d3d->CreateDevice(0, D3DDEVTYPE_HAL, (HWND)1, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &g_dev);
    if (FAILED(hr) || !g_dev)
        return false;
    g_dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &g_bb);
    HRCHECK(g_dev->CreateDepthStencilSurface(W, H, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_ds, nullptr));

    const D3DVERTEXELEMENT9 decl[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0},
                                      {0, 16, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0},
                                      {0, 24, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0},
                                      D3DDECL_END()};
    HRCHECK(g_dev->CreateVertexDeclaration(decl, &g_decl));
    const D3DVERTEXELEMENT9 decl3[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0},
                                       {0, 16, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_TEXCOORD, 0},
                                       D3DDECL_END()};
    HRCHECK(g_dev->CreateVertexDeclaration(decl3, &g_decl3));
    const D3DVERTEXELEMENT9 declIdx[] = {{0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITION, 0},
                                         {0, 16, D3DDECLTYPE_UBYTE4, 0, D3DDECLUSAGE_BLENDINDICES, 0},
                                         D3DDECL_END()};
    HRCHECK(g_dev->CreateVertexDeclaration(declIdx, &g_declIdx));

    auto vs = test_shaders::passThroughVS(), vs3 = test_shaders::passThroughVS3(),
         vsRel = test_shaders::relativeAddressVS();
    auto psTex = test_shaders::textureModulatePS(), psColor = test_shaders::vertexColorPS(),
         psConst = test_shaders::constColorPS(), psCube = test_shaders::cubePS(), psVol = test_shaders::volumePS();
    HRCHECK(g_dev->CreateVertexShader(vs.data(), &g_vs));
    HRCHECK(g_dev->CreateVertexShader(vs3.data(), &g_vs3));
    HRCHECK(g_dev->CreateVertexShader(vsRel.data(), &g_vsRel));
    HRCHECK(g_dev->CreatePixelShader(psTex.data(), &g_psTex));
    HRCHECK(g_dev->CreatePixelShader(psColor.data(), &g_psColor));
    HRCHECK(g_dev->CreatePixelShader(psConst.data(), &g_psConst));
    HRCHECK(g_dev->CreatePixelShader(psCube.data(), &g_psCube));
    HRCHECK(g_dev->CreatePixelShader(psVol.data(), &g_psVol));

    HRCHECK(g_dev->CreateVertexBuffer(64 * sizeof(Vtx), D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, 0, D3DPOOL_DEFAULT,
                                      &g_vb, nullptr));
    HRCHECK(g_dev->CreateIndexBuffer(6 * sizeof(uint16_t), D3DUSAGE_WRITEONLY, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_ib,
                                     nullptr));
    uint16_t *idx = nullptr;
    HRCHECK(g_ib->Lock(0, 0, (void **)&idx, 0));
    const uint16_t quad[6] = {0, 1, 2, 2, 1, 3};
    memcpy(idx, quad, sizeof(quad));
    HRCHECK(g_ib->Unlock());
    return g_vs && g_psTex && g_vb && g_ib && g_decl;
}

static bool setup()
{
    d3d9shim_set_log_level(1);
    d3d9shim_set_gl_check(1);
    g_d3d = Direct3DCreate9(D3D_SDK_VERSION);
    result(g_d3d != nullptr, "Direct3DCreate9");
    if (!g_d3d)
        return false;
    bool ok = createDevice();
    result(ok, "CreateDevice (WebGL2 context on #canvas) + resources");
    return ok;
}

static void finish()
{
    d3d9shim_stats st;
    d3d9shim_get_stats(&st);
    printf("stats: %u programs linked, %u failed, %u shader failures, %llu texture bytes uploaded\n",
           st.programsLinked, st.programsFailed, st.shadersFailed, st.textureUploadBytes);
    printf("D3D9 SMOKE TEST %s: %d passed, %d failed\n", g_fail ? "FAILED" : "PASSED", g_pass, g_fail);
    // Leave a visible verdict on the canvas: green = pass, red = fail.
    g_dev->SetRenderTarget(0, g_bb);
    g_dev->Clear(0, nullptr, D3DCLEAR_TARGET, g_fail ? D3DCOLOR_XRGB(200, 0, 0) : D3DCOLOR_XRGB(0, 160, 0), 1.f, 0);
    g_dev->Present(nullptr, nullptr, nullptr, nullptr);
}

static int g_frame = 0;

static void frame()
{
    ++g_frame;
    if (g_frame == 1) {
        testCapsAndFormats();
        testClear();
        testTexturedQuad();
        testVertexColorSwizzle();
        testDXT1();
        testL8();
        testMips();
        testRenderToTexture();
        testDepth();
        testStencil();
        testAlphaTestAndBlend();
        testCullAndScissor();
        testDrawPrimitiveUP();
        testCubeAndVolume();
        testRelativeAddressing();
        testMoreFormats();
#ifdef SMOKE_THREADED
        testWorkerThreadLocks();
#endif
        testQueries();
        testPresentFlip();
        return; // yield so the occlusion results become available
    }
    // Query results become available only after the page's event loop ran; poll for up to ~2 s.
    DWORD vis = 0, hid = 777;
    HRESULT h1 = g_occVisible->GetData(&vis, sizeof(vis), D3DGETDATA_FLUSH);
    HRESULT h2 = g_occHidden->GetData(&hid, sizeof(hid), D3DGETDATA_FLUSH);
    if (hid != 0 && g_frame < 60)
        return;
    char buf[96];
    snprintf(buf, sizeof(buf), "visible=%lu hidden=%lu", (unsigned long)vis, (unsigned long)hid);
    result(h1 == S_OK && h2 == S_OK && vis > 0 && hid == 0, "occlusion query results after yielding", buf);
    g_occVisible->Release();
    g_occHidden->Release();
    testReset();
    testRecreate();
    finish();
    emscripten_cancel_main_loop();
}

int main()
{
    if (!setup()) {
        printf("D3D9 SMOKE TEST FAILED: setup\n");
        return 1;
    }
    // Timer-driven (not requestAnimationFrame) so headless browsers run it too.
    emscripten_set_main_loop(frame, 30, 0);
    return 0;
}
