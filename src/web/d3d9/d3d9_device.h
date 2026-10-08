// d3d9_device.h - IDirect3DDevice9(Ex) implementation (included by d3d9_internal.h).
#pragma once

namespace d3d9shim {

constexpr int kMaxSamplers = 16;          // ps_3_0 s0..s15
constexpr int kMaxVertexSamplers = 4;     // D3DVERTEXTEXTURESAMPLER0..3 (state kept, not sampled)
constexpr int kSamplerSlots = kMaxSamplers + 1 + kMaxVertexSamplers; // + D3DDMAPSAMPLER
constexpr int kMaxStreams = 16;
constexpr int kVSConstF = 256, kPSConstF = 224, kConstI = 16, kConstB = 16;

class Direct3D9;

class Device final : public Unknown<IDirect3DDevice9Ex> {
public:
    Device(Direct3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND focus, DWORD behavior);
    HRESULT init(D3DPRESENT_PARAMETERS *pp);

    // IDirect3DDevice9
    HRESULT TestCooperativeLevel() override;
    UINT GetAvailableTextureMem() override;
    HRESULT EvictManagedResources() override;
    HRESULT GetDirect3D(IDirect3D9 **ppD3D9) override;
    HRESULT GetDeviceCaps(D3DCAPS9 *pCaps) override;
    HRESULT GetDisplayMode(UINT iSwapChain, D3DDISPLAYMODE *pMode) override;
    HRESULT GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *pParameters) override;
    HRESULT SetCursorProperties(UINT XHotSpot, UINT YHotSpot, IDirect3DSurface9 *pCursorBitmap) override;
    void SetCursorPosition(int X, int Y, DWORD Flags) override;
    BOOL ShowCursor(BOOL bShow) override;
    HRESULT CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pp, IDirect3DSwapChain9 **pSwapChain) override;
    HRESULT GetSwapChain(UINT iSwapChain, IDirect3DSwapChain9 **pSwapChain) override;
    UINT GetNumberOfSwapChains() override { return 1; }
    HRESULT Reset(D3DPRESENT_PARAMETERS *pPresentationParameters) override;
    HRESULT Present(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride,
                    const struct _RGNDATA *pDirtyRegion) override;
    HRESULT GetBackBuffer(UINT iSwapChain, UINT iBackBuffer, D3DBACKBUFFER_TYPE Type,
                          IDirect3DSurface9 **ppBackBuffer) override;
    HRESULT GetRasterStatus(UINT iSwapChain, D3DRASTER_STATUS *pRasterStatus) override;
    HRESULT SetDialogBoxMode(BOOL) override { return D3D_OK; }
    void SetGammaRamp(UINT iSwapChain, DWORD Flags, const D3DGAMMARAMP *pRamp) override;
    void GetGammaRamp(UINT iSwapChain, D3DGAMMARAMP *pRamp) override;
    HRESULT CreateTexture(UINT Width, UINT Height, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                          IDirect3DTexture9 **ppTexture, HANDLE *pSharedHandle) override;
    HRESULT CreateVolumeTexture(UINT Width, UINT Height, UINT Depth, UINT Levels, DWORD Usage, D3DFORMAT Format,
                                D3DPOOL Pool, IDirect3DVolumeTexture9 **ppVolumeTexture, HANDLE *pSharedHandle) override;
    HRESULT CreateCubeTexture(UINT EdgeLength, UINT Levels, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                              IDirect3DCubeTexture9 **ppCubeTexture, HANDLE *pSharedHandle) override;
    HRESULT CreateVertexBuffer(UINT Length, DWORD Usage, DWORD FVF, D3DPOOL Pool,
                               IDirect3DVertexBuffer9 **ppVertexBuffer, HANDLE *pSharedHandle) override;
    HRESULT CreateIndexBuffer(UINT Length, DWORD Usage, D3DFORMAT Format, D3DPOOL Pool,
                              IDirect3DIndexBuffer9 **ppIndexBuffer, HANDLE *pSharedHandle) override;
    HRESULT CreateRenderTarget(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                               DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9 **ppSurface,
                               HANDLE *pSharedHandle) override;
    HRESULT CreateDepthStencilSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                      DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9 **ppSurface,
                                      HANDLE *pSharedHandle) override;
    HRESULT UpdateSurface(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect,
                          IDirect3DSurface9 *pDestinationSurface, const POINT *pDestPoint) override;
    HRESULT UpdateTexture(IDirect3DBaseTexture9 *pSourceTexture, IDirect3DBaseTexture9 *pDestinationTexture) override;
    HRESULT GetRenderTargetData(IDirect3DSurface9 *pRenderTarget, IDirect3DSurface9 *pDestSurface) override;
    HRESULT GetFrontBufferData(UINT iSwapChain, IDirect3DSurface9 *pDestSurface) override;
    HRESULT StretchRect(IDirect3DSurface9 *pSourceSurface, const RECT *pSourceRect, IDirect3DSurface9 *pDestSurface,
                        const RECT *pDestRect, D3DTEXTUREFILTERTYPE Filter) override;
    HRESULT ColorFill(IDirect3DSurface9 *pSurface, const RECT *pRect, D3DCOLOR color) override;
    HRESULT CreateOffscreenPlainSurface(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
                                        IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle) override;
    HRESULT SetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 *pRenderTarget) override;
    HRESULT GetRenderTarget(DWORD RenderTargetIndex, IDirect3DSurface9 **ppRenderTarget) override;
    HRESULT SetDepthStencilSurface(IDirect3DSurface9 *pNewZStencil) override;
    HRESULT GetDepthStencilSurface(IDirect3DSurface9 **ppZStencilSurface) override;
    HRESULT BeginScene() override;
    HRESULT EndScene() override;
    HRESULT Clear(DWORD Count, const D3DRECT *pRects, DWORD Flags, D3DCOLOR Color, float Z, DWORD Stencil) override;
    HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) override;
    HRESULT GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX *pMatrix) override;
    HRESULT MultiplyTransform(D3DTRANSFORMSTATETYPE State, const D3DMATRIX *pMatrix) override;
    HRESULT SetViewport(const D3DVIEWPORT9 *pViewport) override;
    HRESULT GetViewport(D3DVIEWPORT9 *pViewport) override;
    HRESULT SetMaterial(const D3DMATERIAL9 *pMaterial) override;
    HRESULT GetMaterial(D3DMATERIAL9 *pMaterial) override;
    HRESULT SetLight(DWORD Index, const D3DLIGHT9 *pLight) override;
    HRESULT GetLight(DWORD Index, D3DLIGHT9 *pLight) override;
    HRESULT LightEnable(DWORD Index, BOOL Enable) override;
    HRESULT GetLightEnable(DWORD Index, BOOL *pEnable) override;
    HRESULT SetClipPlane(DWORD Index, const float *pPlane) override;
    HRESULT GetClipPlane(DWORD Index, float *pPlane) override;
    HRESULT SetRenderState(D3DRENDERSTATETYPE State, DWORD Value) override;
    HRESULT GetRenderState(D3DRENDERSTATETYPE State, DWORD *pValue) override;
    HRESULT CreateStateBlock(D3DSTATEBLOCKTYPE Type, IDirect3DStateBlock9 **ppSB) override;
    HRESULT BeginStateBlock() override;
    HRESULT EndStateBlock(IDirect3DStateBlock9 **ppSB) override;
    HRESULT SetClipStatus(const D3DCLIPSTATUS9 *pClipStatus) override;
    HRESULT GetClipStatus(D3DCLIPSTATUS9 *pClipStatus) override;
    HRESULT GetTexture(DWORD Stage, IDirect3DBaseTexture9 **ppTexture) override;
    HRESULT SetTexture(DWORD Stage, IDirect3DBaseTexture9 *pTexture) override;
    HRESULT GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD *pValue) override;
    HRESULT SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value) override;
    HRESULT GetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD *pValue) override;
    HRESULT SetSamplerState(DWORD Sampler, D3DSAMPLERSTATETYPE Type, DWORD Value) override;
    HRESULT ValidateDevice(DWORD *pNumPasses) override;
    HRESULT SetPaletteEntries(UINT PaletteNumber, const struct tagPALETTEENTRY *pEntries) override;
    HRESULT GetPaletteEntries(UINT PaletteNumber, struct tagPALETTEENTRY *pEntries) override;
    HRESULT SetCurrentTexturePalette(UINT PaletteNumber) override;
    HRESULT GetCurrentTexturePalette(UINT *PaletteNumber) override;
    HRESULT SetScissorRect(const RECT *pRect) override;
    HRESULT GetScissorRect(RECT *pRect) override;
    HRESULT SetSoftwareVertexProcessing(BOOL bSoftware) override;
    BOOL GetSoftwareVertexProcessing() override { return FALSE; }
    HRESULT SetNPatchMode(float nSegments) override;
    float GetNPatchMode() override { return 0.f; }
    HRESULT DrawPrimitive(D3DPRIMITIVETYPE PrimitiveType, UINT StartVertex, UINT PrimitiveCount) override;
    HRESULT DrawIndexedPrimitive(D3DPRIMITIVETYPE PrimitiveType, INT BaseVertexIndex, UINT MinVertexIndex,
                                 UINT NumVertices, UINT startIndex, UINT primCount) override;
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT PrimitiveCount, const void *pVertexStreamZeroData,
                            UINT VertexStreamZeroStride) override;
    HRESULT DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE PrimitiveType, UINT MinVertexIndex, UINT NumVertices,
                                   UINT PrimitiveCount, const void *pIndexData, D3DFORMAT IndexDataFormat,
                                   const void *pVertexStreamZeroData, UINT VertexStreamZeroStride) override;
    HRESULT ProcessVertices(UINT SrcStartIndex, UINT DestIndex, UINT VertexCount, IDirect3DVertexBuffer9 *pDestBuffer,
                            IDirect3DVertexDeclaration9 *pVertexDecl, DWORD Flags) override;
    HRESULT CreateVertexDeclaration(const D3DVERTEXELEMENT9 *pVertexElements,
                                    IDirect3DVertexDeclaration9 **ppDecl) override;
    HRESULT SetVertexDeclaration(IDirect3DVertexDeclaration9 *pDecl) override;
    HRESULT GetVertexDeclaration(IDirect3DVertexDeclaration9 **ppDecl) override;
    HRESULT SetFVF(DWORD FVF) override;
    HRESULT GetFVF(DWORD *pFVF) override;
    HRESULT CreateVertexShader(const DWORD *pFunction, IDirect3DVertexShader9 **ppShader) override;
    HRESULT SetVertexShader(IDirect3DVertexShader9 *pShader) override;
    HRESULT GetVertexShader(IDirect3DVertexShader9 **ppShader) override;
    HRESULT SetVertexShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4fCount) override;
    HRESULT GetVertexShaderConstantF(UINT StartRegister, float *pConstantData, UINT Vector4fCount) override;
    HRESULT SetVertexShaderConstantI(UINT StartRegister, const int *pConstantData, UINT Vector4iCount) override;
    HRESULT GetVertexShaderConstantI(UINT StartRegister, int *pConstantData, UINT Vector4iCount) override;
    HRESULT SetVertexShaderConstantB(UINT StartRegister, const BOOL *pConstantData, UINT BoolCount) override;
    HRESULT GetVertexShaderConstantB(UINT StartRegister, BOOL *pConstantData, UINT BoolCount) override;
    HRESULT SetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 *pStreamData, UINT OffsetInBytes,
                            UINT Stride) override;
    HRESULT GetStreamSource(UINT StreamNumber, IDirect3DVertexBuffer9 **ppStreamData, UINT *pOffsetInBytes,
                            UINT *pStride) override;
    HRESULT SetStreamSourceFreq(UINT StreamNumber, UINT Setting) override;
    HRESULT GetStreamSourceFreq(UINT StreamNumber, UINT *pSetting) override;
    HRESULT SetIndices(IDirect3DIndexBuffer9 *pIndexData) override;
    HRESULT GetIndices(IDirect3DIndexBuffer9 **ppIndexData) override;
    HRESULT CreatePixelShader(const DWORD *pFunction, IDirect3DPixelShader9 **ppShader) override;
    HRESULT SetPixelShader(IDirect3DPixelShader9 *pShader) override;
    HRESULT GetPixelShader(IDirect3DPixelShader9 **ppShader) override;
    HRESULT SetPixelShaderConstantF(UINT StartRegister, const float *pConstantData, UINT Vector4fCount) override;
    HRESULT GetPixelShaderConstantF(UINT StartRegister, float *pConstantData, UINT Vector4fCount) override;
    HRESULT SetPixelShaderConstantI(UINT StartRegister, const int *pConstantData, UINT Vector4iCount) override;
    HRESULT GetPixelShaderConstantI(UINT StartRegister, int *pConstantData, UINT Vector4iCount) override;
    HRESULT SetPixelShaderConstantB(UINT StartRegister, const BOOL *pConstantData, UINT BoolCount) override;
    HRESULT GetPixelShaderConstantB(UINT StartRegister, BOOL *pConstantData, UINT BoolCount) override;
    HRESULT DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) override;
    HRESULT DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) override;
    HRESULT DeletePatch(UINT) override;
    HRESULT CreateQuery(D3DQUERYTYPE Type, IDirect3DQuery9 **ppQuery) override;

    // IDirect3DDevice9Ex
    HRESULT SetConvolutionMonoKernel(UINT, UINT, float *, float *) override { return D3DERR_NOTAVAILABLE; }
    HRESULT ComposeRects(IDirect3DSurface9 *, IDirect3DSurface9 *, IDirect3DVertexBuffer9 *, UINT,
                         IDirect3DVertexBuffer9 *, D3DCOMPOSERECTSOP, int, int) override
    {
        return D3DERR_NOTAVAILABLE;
    }
    HRESULT PresentEx(const RECT *pSourceRect, const RECT *pDestRect, HWND hDestWindowOverride,
                      const struct _RGNDATA *pDirtyRegion, DWORD) override
    {
        return Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
    }
    HRESULT GetGPUThreadPriority(INT *p) override
    {
        if (p)
            *p = 0;
        return D3D_OK;
    }
    HRESULT SetGPUThreadPriority(INT) override { return D3D_OK; }
    HRESULT WaitForVBlank(UINT) override { return D3D_OK; }
    HRESULT CheckResourceResidency(IDirect3DResource9 **, UINT) override { return D3D_OK; }
    HRESULT SetMaximumFrameLatency(UINT) override { return D3D_OK; }
    HRESULT GetMaximumFrameLatency(UINT *p) override
    {
        if (p)
            *p = 1;
        return D3D_OK;
    }
    HRESULT CheckDeviceState(HWND) override { return D3D_OK; }
    HRESULT CreateRenderTargetEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                 DWORD MultisampleQuality, BOOL Lockable, IDirect3DSurface9 **ppSurface,
                                 HANDLE *pSharedHandle, DWORD) override
    {
        return CreateRenderTarget(Width, Height, Format, MultiSample, MultisampleQuality, Lockable, ppSurface,
                                  pSharedHandle);
    }
    HRESULT CreateOffscreenPlainSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DPOOL Pool,
                                          IDirect3DSurface9 **ppSurface, HANDLE *pSharedHandle, DWORD) override
    {
        return CreateOffscreenPlainSurface(Width, Height, Format, Pool, ppSurface, pSharedHandle);
    }
    HRESULT CreateDepthStencilSurfaceEx(UINT Width, UINT Height, D3DFORMAT Format, D3DMULTISAMPLE_TYPE MultiSample,
                                        DWORD MultisampleQuality, BOOL Discard, IDirect3DSurface9 **ppSurface,
                                        HANDLE *pSharedHandle, DWORD) override
    {
        return CreateDepthStencilSurface(Width, Height, Format, MultiSample, MultisampleQuality, Discard, ppSurface,
                                         pSharedHandle);
    }
    HRESULT ResetEx(D3DPRESENT_PARAMETERS *pp, D3DDISPLAYMODEEX *) override { return Reset(pp); }
    HRESULT GetDisplayModeEx(UINT iSwapChain, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) override;

    // ---- internal (device thread unless noted)
    const GLCaps &glCaps() const { return m_glcaps; }
    GLStateCache &cache() { return m_cache; }
    bool checkThread(const char *method); // logs once per method when called off the device thread
    void deferDestroy(std::function<void()> fn); // any thread
    void processDeferred();
    // Called by destructors (device thread) so dangling bindings are cleared.
    void onSurfaceDestroyed(Surface *s);
    void purgeFbos(Surface *s); // drop cached FBOs that attach s (GL storage changed)
    void onTextureStorageDestroyed(TexStorage *t);
    void onVertexBufferDestroyed(VertexBuffer *vb);
    void onIndexBufferDestroyed(IndexBuffer *ib);
    void onDeclDestroyed(VertexDecl *d);
    void onVertexShaderDestroyed(VertexShader *s);
    void onPixelShaderDestroyed(PixelShader *s);
    void onQueryDestroyed(Query *q);
    void queueGLDelete(GLenum kind, GLuint name); // any thread: textures/buffers/renderbuffers/queries/shaders

    // GL binding helpers with redundant-call elimination
    void bindArrayBuffer(GLuint b);
    void bindElementBuffer(GLuint b);
    void bindDrawFbo(GLuint f);
    void bindReadFbo(GLuint f);
    void bindTextureForEdit(GLenum target, GLuint tex); // binds on the scratch unit
    void bindTextureUnit(int unit, GLenum target, GLuint tex);
    void bindSamplerUnit(int unit, GLuint sampler);
    void useProgram(GLuint p);
    void setScissorTest(bool on);
    void setScissorBox(GLint x, GLint y, GLint w, GLint h);
    void setFullWriteMasks(); // color/depth/stencil masks all on (Clear/blit)
    void markRasterDirty() { m_rasterDirty = true; }

    GLuint fboFor(Surface *color, Surface *depth); // cached FBO with those attachments
    void bindRenderTargets();                      // binds the FBO for m_rt[0]/m_ds if changed
    Surface *renderTarget0() const { return m_rt[0]; }
    Surface *depthStencil() const { return m_ds; }
    SwapChain *swapChain() const { return m_swapChain; }
    void presentBlit();                            // back buffer -> canvas
    bool readSurfacePixels(Surface *src, const RECT &r, uint8_t *dst, size_t dstPitch, D3DFORMAT dstFmt);
    void beginOcclusion(Query *q);
    void endOcclusion(Query *q);
    Query *activeOcclusionQuery() const { return m_activeOcclusion; }

private:
    ~Device() override;
    void resetState();
    void applyRasterState();
    void applyViewport();
    bool prepareDraw(GLuint upBuffer, UINT upStride, INT baseVertex);
    Program *programFor(VertexShader *vs, PixelShader *ps, uint32_t bgraMask);
    Program *linkProgram(VertexShader *vs, PixelShader *ps, uint32_t bgraMask);
    void uploadUniforms(Program *p);
    void setupVertexAttribs(Program *p, GLuint upBuffer, UINT upStride, INT baseVertex);
    void bindSamplers(Program *p);
    GLuint samplerObjectFor(const SamplerParams &sp);
    void destroyAllGL();
    void clearRect(const RECT &r, DWORD flags, D3DCOLOR color, float z, DWORD stencil);
    void createContext();
    static GLenum primitiveMode(D3DPRIMITIVETYPE t, UINT primCount, GLsizei *vertexCount);

    Direct3D9 *m_d3d;
    D3DDEVICE_CREATION_PARAMETERS m_creation;
    GLCaps m_glcaps;
    GLStateCache m_cache;
    intptr_t m_context = 0;
    SwapChain *m_swapChain = nullptr;
    Surface *m_autoDepth = nullptr;
    bool m_inScene = false;
    unsigned m_frame = 0;

    // D3D state
    DWORD m_rs[256];
    DWORD m_ss[kSamplerSlots][14];
    DWORD m_tss[8][33];
    IDirect3DBaseTexture9 *m_textures[kSamplerSlots] = {};
    TexStorage *m_textureStorage[kSamplerSlots] = {};
    struct Stream {
        VertexBuffer *vb = nullptr;
        UINT offset = 0, stride = 0, freq = 1;
    } m_streams[kMaxStreams];
    IndexBuffer *m_ib = nullptr;
    VertexDecl *m_decl = nullptr;
    DWORD m_fvf = 0;
    VertexShader *m_vs = nullptr;
    PixelShader *m_ps = nullptr;
    float m_vsF[kVSConstF][4];
    int m_vsI[kConstI][4];
    BOOL m_vsB[kConstB];
    float m_psF[kPSConstF][4];
    int m_psI[kConstI][4];
    BOOL m_psB[kConstB];
    uint32_t m_vsFVersion = 1, m_psFVersion = 1, m_vsIVersion = 1, m_psIVersion = 1, m_vsBVersion = 1,
             m_psBVersion = 1;
    D3DVIEWPORT9 m_viewport;
    RECT m_scissor;
    Surface *m_rt[4] = {};
    Surface *m_ds = nullptr;
    D3DMATRIX m_transforms[512];
    D3DGAMMARAMP m_gamma;
    D3DCLIPSTATUS9 m_clipStatus = {};
    float m_clipPlanes[6][4] = {};
    D3DMATERIAL9 m_material = {};

    // dirty tracking
    bool m_rasterDirty = true, m_viewportDirty = true, m_fboDirty = true, m_attribsDirty = true, m_samplersDirty = true;
    uint32_t m_samplerDirtyMask = 0xffffffffu;
    Program *m_curProgram = nullptr;
    uint32_t m_curBgraMask = 0;
    bool m_bgraDirty = true;
    GLuint m_curFbo = 0;
    int m_curDepthBits = 24;

    // caches
    struct ProgKey {
        uint64_t vs, ps;
        uint32_t bgra;
        bool operator==(const ProgKey &o) const { return vs == o.vs && ps == o.ps && bgra == o.bgra; }
    };
    struct ProgKeyHash {
        size_t operator()(const ProgKey &k) const
        {
            uint64_t h = k.vs * 0x9E3779B97F4A7C15ull ^ (k.ps + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full ^ k.bgra;
            return (size_t)(h ^ (h >> 29));
        }
    };
    std::unordered_map<ProgKey, Program *, ProgKeyHash> m_programs;
    float m_posFixup[4] = {1, -1, 0, 0};
    float m_alphaTest[2] = {0, 0};
    bool m_floatTargetNoBlend = false;
    struct FboEntry {
        uint64_t colorId, depthId;
        Surface *color, *depth;
        GLuint fbo;
    };
    std::vector<FboEntry> m_fbos;
    std::unordered_map<uint64_t, GLuint> m_samplerObjects;
    GLuint m_samplerBound[kMaxSamplers] = {};
    TexStorage *m_unitSamplerTex[kMaxSamplers] = {}; // texture the unit's sampler object was chosen for
    GLuint m_vao = 0;
    GLuint m_upVB = 0, m_upIB = 0;
    size_t m_upVBSize = 0, m_upIBSize = 0;
    GLuint m_scratchFbo[2] = {0, 0};
    std::vector<uint8_t> m_scratch;
    Query *m_activeOcclusion = nullptr;
    // gamma ramp applied in Present (fullscreen gamma does not exist on the web)
    bool presentWithGamma(Surface *bb);
    GLuint m_presentProg = 0, m_gammaLut = 0;
    GLint m_presentLocSrc = -1, m_presentLocLut = -1;
    D3DGAMMARAMP m_gammaUploaded;
    bool m_gammaLutValid = false, m_gammaDirty = false, m_presentProgFailed = false;

    // cross-thread queues
    std::mutex m_deferMtx;
    std::vector<std::function<void()>> m_deferred;
    std::vector<std::pair<GLenum, GLuint>> m_glDeletes;
    std::atomic<bool> m_hasDeferred{false};
    std::vector<Query *> m_queries;
};

} // namespace d3d9shim
