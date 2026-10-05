#include "bloom.h"
#include "fx_bloom_bright.h"
#include "fx_bloom_blur.h"

// Passes: copy bb -> full-res RT (StretchRect; bb can't be sampled); bright -> quarter res,
// under threshold dropped; 5-tap gaussian x/y ping-pong at quarter res, last y additive onto
// frame (hw bilinear = free upscale). Quarter res = whole cost story.
// Intensity on LAST pass: buffers 8 bit, scaling first clips glow >1.0 to flat white.

static IDirect3DPixelShader9* s_psBright  = nullptr;
static IDirect3DPixelShader9* s_psBlur    = nullptr;
static IDirect3DTexture9*     s_sceneTex  = nullptr;   // full res copy of the frame
static IDirect3DSurface9*     s_sceneSurf = nullptr;
static IDirect3DTexture9*     s_texA      = nullptr;   // quarter res, ping
static IDirect3DSurface9*     s_surfA     = nullptr;
static IDirect3DTexture9*     s_texB      = nullptr;   // quarter res, pong
static IDirect3DSurface9*     s_surfB     = nullptr;
static int  s_w = 0, s_h = 0, s_bw = 0, s_bh = 0;
static bool s_failed = false;      // card refused a shader: never ask again

void Bloom_Destroy()
{
    if (s_surfB)     { s_surfB->Release();     s_surfB     = nullptr; }
    if (s_texB)      { s_texB->Release();      s_texB      = nullptr; }
    if (s_surfA)     { s_surfA->Release();     s_surfA     = nullptr; }
    if (s_texA)      { s_texA->Release();      s_texA      = nullptr; }
    if (s_sceneSurf) { s_sceneSurf->Release(); s_sceneSurf = nullptr; }
    if (s_sceneTex)  { s_sceneTex->Release();  s_sceneTex  = nullptr; }
    if (s_psBlur)    { s_psBlur->Release();    s_psBlur    = nullptr; }
    if (s_psBright)  { s_psBright->Release();  s_psBright  = nullptr; }
    s_w = s_h = s_bw = s_bh = 0;
}

// Sized to bb. Res change -> h_Reset destroys all; size test covers swapchain change under us.
static bool BloomEnsure(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    D3DSURFACE_DESC desc{};
    if (FAILED(bb->GetDesc(&desc))) return false;

    if (s_psBright && s_psBlur && s_sceneTex && s_texA && s_texB &&
        s_w == (int)desc.Width && s_h == (int)desc.Height)
        return true;

    Bloom_Destroy();

    // bytecode copied to DWORDs: BYTE array has no alignment guarantee
    {
        std::vector<DWORD> code((sizeof(k_fxBloomBrightBytes) + 3) / 4);
        memcpy(code.data(), k_fxBloomBrightBytes, sizeof(k_fxBloomBrightBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_psBright)) || !s_psBright) {
            s_failed = true;
            DebugLog("bloom: the card refused the bright pass shader -- bloom is off");
            Bloom_Destroy();
            return false;
        }
    }
    {
        std::vector<DWORD> code((sizeof(k_fxBloomBlurBytes) + 3) / 4);
        memcpy(code.data(), k_fxBloomBlurBytes, sizeof(k_fxBloomBlurBytes));
        if (FAILED(dev->CreatePixelShader(code.data(), &s_psBlur)) || !s_psBlur) {
            s_failed = true;
            DebugLog("bloom: the card refused the blur shader -- bloom is off");
            Bloom_Destroy();
            return false;
        }
    }

    // bb format = straight blit; X8R8G8B8 fallback if format can't back an RT texture
    HRESULT hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                    desc.Format, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr))
        hr = dev->CreateTexture(desc.Width, desc.Height, 1, D3DUSAGE_RENDERTARGET,
                                D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &s_sceneTex, nullptr);
    if (FAILED(hr) || FAILED(s_sceneTex->GetSurfaceLevel(0, &s_sceneSurf))) {
        DebugLog("bloom: frame copy target failed 0x%08lx", (unsigned long)hr);
        Bloom_Destroy();
        return false;
    }

    const int bw = ((int)desc.Width  / 4) < 16 ? 16 : (int)desc.Width  / 4;
    const int bh = ((int)desc.Height / 4) < 16 ? 16 : (int)desc.Height / 4;

    if (FAILED(dev->CreateTexture(bw, bh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_texA, nullptr)) ||
        FAILED(s_texA->GetSurfaceLevel(0, &s_surfA)) ||
        FAILED(dev->CreateTexture(bw, bh, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                                  D3DPOOL_DEFAULT, &s_texB, nullptr)) ||
        FAILED(s_texB->GetSurfaceLevel(0, &s_surfB)))
    {
        DebugLog("bloom: glow buffer creation failed");
        Bloom_Destroy();
        return false;
    }

    s_w = (int)desc.Width; s_h = (int)desc.Height;
    s_bw = bw;             s_bh = bh;
    DebugLog("bloom: built %dx%d, glow buffers %dx%d", s_w, s_h, s_bw, s_bh);
    return true;
}

// Save/restore all touched state. DrawPrimitiveUP nulls stream 0 + indices; matsys cache still
// thinks both bound.
struct BloomSaved
{
    IDirect3DSurface9*           rt;
    IDirect3DSurface9*           ds;
    IDirect3DPixelShader9*       ps;
    IDirect3DVertexShader9*      vs;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DBaseTexture9*       tex0;
    IDirect3DVertexBuffer9*      stream;
    IDirect3DIndexBuffer9*       indices;
    UINT   streamOffset, streamStride;
    DWORD  fvf;
    D3DVIEWPORT9 vp;
    DWORD  rs[16];
    DWORD  samp[6];
    float  psc[2][4];
};

static const D3DRENDERSTATETYPE k_blRS[16] = {
    D3DRS_ZENABLE, D3DRS_ZWRITEENABLE, D3DRS_ALPHABLENDENABLE, D3DRS_SEPARATEALPHABLENDENABLE,
    D3DRS_ALPHATESTENABLE, D3DRS_CULLMODE, D3DRS_SCISSORTESTENABLE, D3DRS_SRGBWRITEENABLE,
    D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE, D3DRS_FOGENABLE, D3DRS_CLIPPLANEENABLE,
    D3DRS_FILLMODE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
};
static const DWORD k_blRSVal[16] = {
    FALSE, FALSE, FALSE, FALSE,
    FALSE, D3DCULL_NONE, FALSE, FALSE,
    D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE |
        D3DCOLORWRITEENABLE_ALPHA,
    FALSE, FALSE, 0,
    D3DFILL_SOLID, D3DBLEND_ONE, D3DBLEND_ONE, D3DBLENDOP_ADD,
};
static const D3DSAMPLERSTATETYPE k_blSamp[6] = {
    D3DSAMP_ADDRESSU, D3DSAMP_ADDRESSV, D3DSAMP_MAGFILTER,
    D3DSAMP_MINFILTER, D3DSAMP_MIPFILTER, D3DSAMP_SRGBTEXTURE,
};

static void BloomSave(IDirect3DDevice9* dev, BloomSaved& s)
{
    if (FAILED(dev->GetRenderTarget(0, &s.rt)))        s.rt = nullptr;
    if (FAILED(dev->GetDepthStencilSurface(&s.ds)))    s.ds = nullptr;
    dev->GetPixelShader(&s.ps);
    dev->GetVertexShader(&s.vs);
    dev->GetVertexDeclaration(&s.decl);
    dev->GetTexture(0, &s.tex0);
    dev->GetStreamSource(0, &s.stream, &s.streamOffset, &s.streamStride);
    dev->GetIndices(&s.indices);
    dev->GetFVF(&s.fvf);
    dev->GetViewport(&s.vp);
    for (int i = 0; i < 16; ++i) dev->GetRenderState(k_blRS[i], &s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->GetSamplerState(0, k_blSamp[i], &s.samp[i]);
    dev->GetPixelShaderConstantF(0, s.psc[0], 2);
}

static void BloomRestore(IDirect3DDevice9* dev, BloomSaved& s)
{
    // RT FIRST: SetRenderTarget resets viewport; viewport first = engine clipped to quarter screen
    dev->SetRenderTarget(0, s.rt);
    dev->SetDepthStencilSurface(s.ds);
    dev->SetViewport(&s.vp);
    for (int i = 0; i < 16; ++i) dev->SetRenderState(k_blRS[i], s.rs[i]);
    for (int i = 0; i < 6;  ++i) dev->SetSamplerState(0, k_blSamp[i], s.samp[i]);
    dev->SetPixelShaderConstantF(0, s.psc[0], 2);
    dev->SetTexture(0, s.tex0);
    dev->SetPixelShader(s.ps);
    dev->SetVertexShader(s.vs);
    // FVF + decl share slot: restore only the one in force
    if (s.decl) dev->SetVertexDeclaration(s.decl); else dev->SetFVF(s.fvf);
    dev->SetStreamSource(0, s.stream, s.streamOffset, s.streamStride);
    dev->SetIndices(s.indices);

    if (s.rt)     s.rt->Release();
    if (s.ds)     s.ds->Release();
    if (s.tex0)   s.tex0->Release();
    if (s.ps)     s.ps->Release();
    if (s.vs)     s.vs->Release();
    if (s.decl)   s.decl->Release();
    if (s.stream)  s.stream->Release();
    if (s.indices) s.indices->Release();
}

struct BloomVert { float x, y, z, rhw, u, v; };

static void BloomQuad(BloomVert (&q)[4], int w, int h)
{
    const float W = (float)w, H = (float)h;
    q[0] = { -0.5f,    -0.5f,     0.f, 1.f, 0.f, 0.f };
    q[1] = { W - 0.5f, -0.5f,     0.f, 1.f, 1.f, 0.f };
    q[2] = { -0.5f,     H - 0.5f, 0.f, 1.f, 0.f, 1.f };
    q[3] = { W - 0.5f,  H - 0.5f, 0.f, 1.f, 1.f, 1.f };
}

static void BloomDraw(IDirect3DDevice9* dev, IDirect3DSurface9* bb)
{
    if (FAILED(dev->StretchRect(bb, nullptr, s_sceneSurf, nullptr, D3DTEXF_NONE))) {
        static bool logged = false;
        if (!logged) { logged = true; DebugLog("bloom: the frame copy blit failed -- bloom draws nothing"); }
        return;
    }

    BloomVert quadFull[4], quadGlow[4];
    BloomQuad(quadFull, s_w,  s_h);
    BloomQuad(quadGlow, s_bw, s_bh);

    const D3DVIEWPORT9 vpFull{ 0, 0, (DWORD)s_w,  (DWORD)s_h,  0.f, 1.f };
    const D3DVIEWPORT9 vpGlow{ 0, 0, (DWORD)s_bw, (DWORD)s_bh, 0.f, 1.f };

    BloomSaved st{};
    BloomSave(dev, st);

    for (int i = 0; i < 16; ++i) dev->SetRenderState(k_blRS[i], k_blRSVal[i]);
    dev->SetDepthStencilSurface(nullptr);   // no pass here tests or writes depth
    dev->SetVertexShader(nullptr);          // a bound vertex shader would ignore the FVF
    dev->SetVertexDeclaration(nullptr);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSU,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_ADDRESSV,    D3DTADDRESS_CLAMP);
    dev->SetSamplerState(0, D3DSAMP_MAGFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MINFILTER,   D3DTEXF_LINEAR);
    dev->SetSamplerState(0, D3DSAMP_MIPFILTER,   D3DTEXF_NONE);
    dev->SetSamplerState(0, D3DSAMP_SRGBTEXTURE, 0);   // raw, else glow measured off other image

    // bright pass -> glow buffer
    {
        const float thr = (g_bloomThreshold < 0.f) ? 0.f : (g_bloomThreshold > 1.f ? 1.f : g_bloomThreshold);
        const float c0[4] = { 1.f / (float)s_w, 1.f / (float)s_h, thr, 0.f };
        dev->SetPixelShaderConstantF(0, c0, 1);
        dev->SetRenderTarget(0, s_surfA);
        dev->SetViewport(&vpGlow);
        dev->SetTexture(0, s_sceneTex);
        dev->SetPixelShader(s_psBright);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadGlow, sizeof(BloomVert));
    }

    // blur pairs x,y with widening step (not more taps): prior pass smoothed what wide step would alias
    const float rad    = (g_bloomRadius < 0.f) ? 0.f : (g_bloomRadius > 1.f ? 1.f : g_bloomRadius);
    const int   passes = 1 + (int)(rad * 5.f + 0.5f);          // 1..6
    const float sat    = (g_bloomSat < 0.f) ? 0.f : (g_bloomSat > 3.f ? 3.f : g_bloomSat);
    const float amount = (g_bloomIntensity < 0.f) ? 0.f : g_bloomIntensity;

    const float neutral[4] = { 1.f, 1.f, 1.f, 1.f };
    dev->SetPixelShader(s_psBlur);

    for (int i = 0; i < passes; ++i)
    {
        const float step = (float)(i + 1);
        const bool  last = (i == passes - 1);

        // x: A -> B
        const float cx[4] = { step / (float)s_bw, 0.f, 0.f, 0.f };
        dev->SetPixelShaderConstantF(0, cx, 1);
        dev->SetPixelShaderConstantF(1, neutral, 1);
        dev->SetRenderTarget(0, s_surfB);
        dev->SetViewport(&vpGlow);
        dev->SetTexture(0, s_texA);
        dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadGlow, sizeof(BloomVert));

        // y: B -> A, last pass B -> frame, additive
        const float cy[4] = { 0.f, step / (float)s_bh, 0.f, 0.f };
        dev->SetPixelShaderConstantF(0, cy, 1);
        dev->SetTexture(0, s_texB);

        if (!last) {
            dev->SetPixelShaderConstantF(1, neutral, 1);
            dev->SetRenderTarget(0, s_surfA);
            dev->SetViewport(&vpGlow);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadGlow, sizeof(BloomVert));
        } else {
            const float c1[4] = { amount, amount, amount, sat };
            dev->SetPixelShaderConstantF(1, c1, 1);
            dev->SetRenderTarget(0, bb);
            dev->SetViewport(&vpFull);
            // ONE+ONE; alpha untouched: integer hdr bb alpha = engine dest-alpha depth, still read
            dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
            dev->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
            dev->SetRenderState(D3DRS_COLORWRITEENABLE,
                                D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE);
            dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, quadFull, sizeof(BloomVert));
        }
    }

    BloomRestore(dev, st);

    static bool logged = false;
    if (!logged) {
        logged = true;
        DebugLog("^FX: bloom drew %dx%d glow %dx%d passes=%d intensity=%.2f threshold=%.2f",
                 s_w, s_h, s_bw, s_bh, passes, amount, g_bloomThreshold);
    }
}

bool Bloom_Frame(IDirect3DDevice9* dev)
{
    if (!dev || !g_bloomEnable || s_failed) return false;
    if (g_bloomIntensity <= 0.001f) return false;
    if (!IsInGame()) return false;              // main menu left alone

    IDirect3DSurface9* bb = nullptr;
    if (FAILED(dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return false;
    bool drew = false;
    if (BloomEnsure(dev, bb)) {
        BloomDraw(dev, bb);
        drew = true;
    }
    bb->Release();
    return drew;
}
