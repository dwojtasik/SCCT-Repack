// ------------------------------------------------------------------------------------------------
// Remaster.dll - bloom / glow remaster for Splinter Cell: Chaos Theory (PC v1.05.157).
//
// Loaded by splintercell3.exe through an import added by remaster_patch_exe. At load time it:
//   - drops D3DCREATE_PUREDEVICE from CreateDevice so device state can be saved / restored,
//   - hooks Direct3D (glow_fix.inc): anti-aliased glow downsampling and smooth glow upsampling
//     for every post-process path (HDR on / off, vision modes),
//   - optionally replaces the HDR glow by a modern resolution-relative bloom chain,
//   - scales the coarse glow pyramid by [Pyramid] Size (pyramid.inc).
// Settings: Remaster.ini next to the DLL.
//
// :copyright: (c) 2026 by Dominik Wojtasik.
// :license: MIT, see LICENSE for more details.
// ------------------------------------------------------------------------------------------------
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <d3dcompiler.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ------------------------------------------------------------------------------------------------
// Config
// ------------------------------------------------------------------------------------------------
struct Config {
    int   enabled;
    int   fixDownsample;   // anti-aliased reduction of the scene before the game builds its glow
    int   smoothUpsample;  // cubic B-spline upsampling of composited glow levels
    int   modernHDR;       // replace the HDR glow by the bloom chain below
    float intensity;
    float weight[4];       // weights of the 256/128/64/32-equivalent levels
    float fineWeight;      // weight of the two levels finer than the 256-equivalent level
    float radius;          // tent radius in source texels for upsampling
    int   karis;           // Karis average on the first downsample (anti-flicker)
    int   outputDivisor;   // final bloom texture = screen / outputDivisor (2 or 4)
    int   fixLuminance;    // feed the game's 256x256 chain (eye adaptation) from the filtered result
    int   fp16;
    int   pyramidSize;
};
static Config g_cfg;

static const int BASE_WIDTH = 256;   // width of the original glow base level

static float IniF(const char* sec, const char* key, float def, const char* ini) {
    char buf[64], d[64];
    snprintf(d, sizeof d, "%g", def);
    GetPrivateProfileStringA(sec, key, d, buf, sizeof buf, ini);
    return (float)atof(buf);
}

static int IniI(const char* sec, const char* key, int def, const char* ini) {
    return GetPrivateProfileIntA(sec, key, def, ini);
}

static void LoadConfig(const char* dir) {
    char ini[MAX_PATH];
    snprintf(ini, sizeof ini, "%sRemaster.ini", dir);
    g_cfg.enabled        = IniI("Bloom", "Enabled", 1, ini);
    g_cfg.fixDownsample  = IniI("Bloom", "FixDownsample", 1, ini);
    g_cfg.smoothUpsample = IniI("Bloom", "SmoothUpsample", 1, ini);
    g_cfg.modernHDR      = IniI("Bloom", "ModernHDRBloom", 1, ini);
    g_cfg.intensity      = IniF("Bloom", "Intensity", 0.9f, ini);
    g_cfg.weight[0]      = IniF("Bloom", "Weight256", 1.4f, ini);
    g_cfg.weight[1]      = IniF("Bloom", "Weight128", 1.3f, ini);
    g_cfg.weight[2]      = IniF("Bloom", "Weight64", 1.2f, ini);
    g_cfg.weight[3]      = IniF("Bloom", "Weight32", 1.6f, ini);
    g_cfg.fineWeight     = IniF("Bloom", "FineWeight", 0.15f, ini);
    g_cfg.radius         = IniF("Bloom", "UpsampleRadius", 0.75f, ini);
    g_cfg.karis          = IniI("Bloom", "KarisAverage", 1, ini);
    g_cfg.outputDivisor  = IniI("Bloom", "OutputDivisor", 2, ini);
    g_cfg.fixLuminance   = IniI("Bloom", "FixLuminanceSource", 1, ini);
    g_cfg.fp16           = IniI("Bloom", "UseFP16", 1, ini);
    g_cfg.pyramidSize    = IniI("Pyramid", "Size", 512, ini);
    if (g_cfg.outputDivisor != 4) g_cfg.outputDivisor = 2;
}

// ------------------------------------------------------------------------------------------------
// Shared render state
// ------------------------------------------------------------------------------------------------
enum { MAXL = 12 };
struct Level { IDirect3DTexture9* tex; IDirect3DSurface9* surf; UINT w, h; };

struct Bloom {
    IDirect3DDevice9*            dev;
    UINT                         srcW, srcH;
    int                          nLevels;    // D[1..nLevels]
    int                          k0;         // level matching the original 256 glow level
    int                          outLevel;   // level of the final bloom texture (1 = W/2)
    Level                        D[MAXL + 1];
    Level                        U[MAXL + 1];
    IDirect3DVertexShader9*      vs;
    IDirect3DPixelShader9*       psDown, *psDownKaris, *psUp, *psBicubic;
    IDirect3DVertexDeclaration9* decl;
    IDirect3DStateBlock9*        sb;
    D3DFORMAT                    fmt;
    bool                         ready;
    bool                         failed;     // creation failed for this size; retried after a reset
} g_b;

static void SafeRel(IUnknown** p) { if (*p) { (*p)->Release(); *p = 0; } }
#define REL(x) SafeRel((IUnknown**)&(x))

static bool g_internal;   // true while the DLL issues its own draw calls (D3D hooks pass through)

#include "shaders.inc"

static void Chain_Release() {
    for (int i = 0; i <= MAXL; i++) {
        REL(g_b.D[i].surf); REL(g_b.D[i].tex);
        REL(g_b.U[i].surf); REL(g_b.U[i].tex);
    }
    g_b.ready = false; g_b.failed = false; g_b.srcW = g_b.srcH = 0;
}

static void Common_Release() {
    REL(g_b.vs); REL(g_b.psDown); REL(g_b.psDownKaris); REL(g_b.psUp); REL(g_b.psBicubic); REL(g_b.decl); REL(g_b.sb);
    g_b.dev = 0;
}

static void GlowFix_Release();

static void Bloom_Release() { GlowFix_Release(); Chain_Release(); Common_Release(); }

// Shaders, vertex declaration and state block shared by all passes. Retried every 10 s on failure.
static bool  g_commonFailed;
static DWORD g_commonFailTime;

static bool Common_Ensure(IDirect3DDevice9* dev) {
    if (g_b.dev == dev && g_b.vs) return true;
    if (g_commonFailed && g_b.dev == dev && GetTickCount() - g_commonFailTime < 10000) return false;
    Chain_Release(); Common_Release();
    g_b.dev = dev; g_commonFailed = true; g_commonFailTime = GetTickCount();
    D3DDEVICE_CREATION_PARAMETERS cp;
    if (SUCCEEDED(dev->GetCreationParameters(&cp)) && (cp.BehaviorFlags & D3DCREATE_PUREDEVICE)) return false;

    bool wasInternal = g_internal;
    g_internal = true;
    bool ok = MakeVS(dev, HLSL_VS, &g_b.vs) &&
              MakePS(dev, HLSL_DOWN, &g_b.psDown) &&
              MakePS(dev, HLSL_DOWN_KARIS, &g_b.psDownKaris) &&
              MakePS(dev, HLSL_UP, &g_b.psUp) &&
              MakePS(dev, HLSL_BICUBIC, &g_b.psBicubic);
    g_internal = wasInternal;
    D3DVERTEXELEMENT9 el[] = {
        {0, 0, D3DDECLTYPE_FLOAT4, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 16, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()};
    ok = ok && SUCCEEDED(dev->CreateVertexDeclaration(el, &g_b.decl)) &&
         SUCCEEDED(dev->CreateStateBlock(D3DSBT_ALL, &g_b.sb));
    if (!ok) { Common_Release(); g_b.dev = dev; return false; }
    g_commonFailed = false;
    return true;
}

struct Vtx { float x, y, z, w, u, v; };

static void Pass(IDirect3DSurface9* rt, UINT w, UINT h, IDirect3DPixelShader9* ps,
                 IDirect3DTexture9* t0, IDirect3DTexture9* t1, const float* c, UINT nc) {
    IDirect3DDevice9* d = g_b.dev;
    d->SetRenderTarget(0, rt);
    D3DVIEWPORT9 vp = {0, 0, w, h, 0.0f, 1.0f};
    d->SetViewport(&vp);
    d->SetPixelShader(ps);
    d->SetTexture(0, t0);
    d->SetTexture(1, t1 ? t1 : t0);
    d->SetPixelShaderConstantF(0, c, nc);
    const float hx = 1.0f / w, hy = 1.0f / h;   // D3D9 half-pixel offset in clip units
    Vtx v[4] = {
        {-1 - hx,  1 + hy, 0, 1, 0, 0},
        { 1 - hx,  1 + hy, 0, 1, 1, 0},
        {-1 - hx, -1 + hy, 0, 1, 0, 1},
        { 1 - hx, -1 + hy, 0, 1, 1, 1}};
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(Vtx));
}

static void SetCommonState(IDirect3DDevice9* d) {
    d->SetDepthStencilSurface(0);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_CLIPPLANEENABLE, 0);
    d->SetRenderState(D3DRS_FILLMODE, D3DFILL_SOLID);
    for (DWORD s = 0; s < 2; s++) {
        d->SetSamplerState(s, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(s, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
        d->SetSamplerState(s, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(s, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(s, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        d->SetSamplerState(s, D3DSAMP_SRGBTEXTURE, FALSE);
    }
    d->SetVertexShader(g_b.vs);
    d->SetVertexDeclaration(g_b.decl);
}

// ------------------------------------------------------------------------------------------------
// Modern HDR bloom: 13-tap downsample chain W/2 .. W/2^n, tent upsample with the original weights
// ------------------------------------------------------------------------------------------------
static bool MakeRT(Level& L, UINT w, UINT h) {
    L.w = w; L.h = h;
    if (FAILED(g_b.dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, g_b.fmt, D3DPOOL_DEFAULT, &L.tex, 0))) return false;
    L.tex->GetSurfaceLevel(0, &L.surf);
    return true;
}

static bool Bloom_Create(IDirect3DDevice9* dev, UINT W, UINT H) {
    if (!Common_Ensure(dev)) return false;
    Chain_Release();
    g_b.srcW = W; g_b.srcH = H;

    // level k has size ceil(W / 2^k); k0 = level whose width is closest (log scale) to BASE_WIDTH
    int k0 = (int)floorf(log2f((float)W / BASE_WIDTH) + 0.5f);
    if (k0 < 1) k0 = 1;
    int n = k0 + 3;
    if (n > MAXL) { n = MAXL; k0 = n - 3; }
    g_b.k0 = k0; g_b.nLevels = n;
    g_b.outLevel = (g_cfg.outputDivisor == 4) ? 2 : 1;
    if (g_b.outLevel > k0) g_b.outLevel = k0;

    g_b.fmt = g_cfg.fp16 ? D3DFMT_A16B16G16R16F : D3DFMT_A8R8G8B8;
    for (int attempt = 0; attempt < 2; attempt++) {
        bool ok = true;
        UINT w = W, h = H;
        for (int k = 1; k <= n && ok; k++) {
            w = (w + 1) / 2; h = (h + 1) / 2;
            if (!w) w = 1;
            if (!h) h = 1;
            ok = MakeRT(g_b.D[k], w, h);
            if (ok && k >= g_b.outLevel && k < n) ok = MakeRT(g_b.U[k], w, h);
        }
        if (ok) { g_b.ready = true; return true; }
        Chain_Release();
        g_b.srcW = W; g_b.srcH = H;
        if (g_b.fmt == D3DFMT_A8R8G8B8) break;
        g_b.fmt = D3DFMT_A8R8G8B8;   // FP16 render targets unavailable
    }
    g_b.failed = true;
    return false;
}

// Builds the bloom chain from 'scene' (full-res bright-passed scene copy). If 'out256' is given,
// also writes a filtered downsample into it (input of the game's luminance / glow chain).
static bool Bloom_Build(IDirect3DDevice9* d, IDirect3DTexture9* scene, IDirect3DSurface9* out256) {
    D3DSURFACE_DESC sd;
    if (!scene || FAILED(scene->GetLevelDesc(0, &sd))) return false;
    if (!g_b.ready || g_b.dev != d || g_b.srcW != sd.Width || g_b.srcH != sd.Height) {
        if (g_b.failed && g_b.dev == d && g_b.srcW == sd.Width && g_b.srcH == sd.Height) return false;
        if (!Bloom_Create(d, sd.Width, sd.Height)) return false;
    }
    g_internal = true;
    IDirect3DSurface9 *oldRT = 0, *oldDS = 0;
    d->GetRenderTarget(0, &oldRT);
    d->GetDepthStencilSurface(&oldDS);
    g_b.sb->Capture();
    SetCommonState(d);

    float c[8];
    const int n = g_b.nLevels, k0 = g_b.k0;

    // progressive downsample
    for (int k = 1; k <= n; k++) {
        IDirect3DTexture9* src = (k == 1) ? scene : g_b.D[k - 1].tex;
        UINT sw = (k == 1) ? sd.Width : g_b.D[k - 1].w, sh = (k == 1) ? sd.Height : g_b.D[k - 1].h;
        c[0] = c[2] = 1.0f / sw; c[1] = c[3] = 1.0f / sh;
        Pass(g_b.D[k].surf, g_b.D[k].w, g_b.D[k].h, (k == 1 && g_cfg.karis) ? g_b.psDownKaris : g_b.psDown, src, 0, c, 1);
    }

    // filtered input for the game's 256x256 chain (eye adaptation + legacy glow)
    if (out256 && g_cfg.fixLuminance) {
        D3DSURFACE_DESC od;
        out256->GetDesc(&od);
        int j = 0;   // finest level that is still >= target width, else the full-res scene
        for (int k = 1; k <= n; k++) if (g_b.D[k].w >= od.Width) j = k;
        IDirect3DTexture9* src = j ? g_b.D[j].tex : scene;
        UINT sw = j ? g_b.D[j].w : sd.Width, sh = j ? g_b.D[j].h : sd.Height;
        c[0] = c[2] = 1.0f / sw; c[1] = c[3] = 1.0f / sh;
        Pass(out256, od.Width, od.Height, g_b.psDown, src, 0, c, 1);
    }

    // progressive tent upsample, accumulating the 4 glow levels with the original weights
    const float I = g_cfg.intensity;
    float w[MAXL + 1] = {};
    for (int k = k0 - 2; k < k0; k++) if (k >= 1) w[k] = g_cfg.fineWeight * I;
    for (int i = 0; i < 4; i++) if (k0 + i <= MAXL) w[k0 + i] = g_cfg.weight[i] * I;
    for (int k = n - 1; k >= g_b.outLevel; k--) {
        IDirect3DTexture9* lower = (k == n - 1) ? g_b.D[n].tex : g_b.U[k + 1].tex;
        UINT lw = (k == n - 1) ? g_b.D[n].w : g_b.U[k + 1].w, lh = (k == n - 1) ? g_b.D[n].h : g_b.U[k + 1].h;
        c[0] = c[2] = g_cfg.radius / lw; c[1] = c[3] = g_cfg.radius / lh;
        c[4] = (k == n - 1) ? w[n] : 1.0f;   // the coarsest level carries its own weight
        c[5] = w[k]; c[6] = c[7] = 0;
        Pass(g_b.U[k].surf, g_b.U[k].w, g_b.U[k].h, g_b.psUp, lower, g_b.D[k].tex, c, 2);
    }

    d->SetRenderTarget(0, oldRT);
    d->SetDepthStencilSurface(oldDS);
    g_b.sb->Apply();
    REL(oldRT); REL(oldDS);
    g_internal = false;
    return true;
}

static IDirect3DTexture9* Bloom_Result() { return g_b.ready ? g_b.U[g_b.outLevel].tex : 0; }

// ------------------------------------------------------------------------------------------------
// Game sites
// ------------------------------------------------------------------------------------------------
static const DWORD IMAGE_BASE         = 0x10900000;
static const DWORD SITE_CREATE_FLAGS  = 0x10C7FD07;   // push 50h: CreateDevice BehaviorFlags (HWVP | PURE)
static const DWORD SITE_RELEASE       = 0x10C8A3C0;   // jmp to the render-target release (device reset)
static const DWORD FUNC_RELEASE       = 0x10C89850;
static const DWORD SITE_HDR_VPASS     = 0x10C9E851;   // SM3 HDR: 2nd downsample draw
static const DWORD SITE_HDR_FINAL     = 0x10C9F806;   // SM3 HDR: final composite draw
static const DWORD IAT_DIRECT3DCREATE9 = 0x110B7730;

// device object (UD3DRenderDevice) field offsets
static const int OFF_SCENECOPY_TEX = 0x4BA4;
static const int OFF_S_A256        = 0x4BBC;
static const int OFF_S_B256        = 0x4BE0;
static const int OFF_FLAG_4D4D     = 0x4D4D;

static DWORD g_delta;   // relocation delta of splintercell3.exe
#define VA(x) ((DWORD)(x) + g_delta)

static void WriteMem(DWORD addr, const void* data, int n) {
    DWORD old;
    VirtualProtect((void*)addr, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void*)addr, data, n);
    VirtualProtect((void*)addr, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void*)addr, n);
}

#include "glow_fix.inc"
#include "pyramid.inc"

extern "C" {
    BYTE* g_devobj;
    DWORD g_origRelease;
    void  VPassThunk();
    void  FinalThunk();
    void  ReleaseThunk();
}

static bool g_builtThisFrame;

extern "C" HRESULT __stdcall VPassDraw_C(IDirect3DDevice9* dev, D3DPRIMITIVETYPE t, INT bv, UINT mi, UINT nv, UINT si, UINT pc) {
    HRESULT hr = dev->DrawIndexedPrimitive(t, bv, mi, nv, si, pc);
    g_builtThisFrame = false;
    if (g_cfg.enabled && g_cfg.modernHDR && g_devobj) {
        IDirect3DTexture9* scene = *(IDirect3DTexture9**)(g_devobj + OFF_SCENECOPY_TEX);
        IDirect3DSurface9* out = *(IDirect3DSurface9**)(g_devobj + (g_devobj[OFF_FLAG_4D4D] ? OFF_S_B256 : OFF_S_A256));
        g_builtThisFrame = Bloom_Build(dev, scene, out);
    }
    return hr;
}

extern "C" HRESULT __stdcall FinalDraw_C(IDirect3DDevice9* dev, D3DPRIMITIVETYPE t, INT bv, UINT mi, UINT nv, UINT si, UINT pc) {
    IDirect3DTexture9* bloom = g_builtThisFrame ? Bloom_Result() : 0;
    if (!bloom || g_b.dev != dev) return dev->DrawIndexedPrimitive(t, bv, mi, nv, si, pc);

    IDirect3DBaseTexture9* old = 0;
    dev->GetTexture(1, &old);
    float oldC[4];
    dev->GetVertexShaderConstantF(0x67, oldC, 1);
    DWORD smin, smag, smip, au, av;
    dev->GetSamplerState(1, D3DSAMP_MINFILTER, &smin);
    dev->GetSamplerState(1, D3DSAMP_MAGFILTER, &smag);
    dev->GetSamplerState(1, D3DSAMP_MIPFILTER, &smip);
    dev->GetSamplerState(1, D3DSAMP_ADDRESSU, &au);
    dev->GetSamplerState(1, D3DSAMP_ADDRESSV, &av);

    // c103 = glow texture mapping, same formula as the game helper 0x10C91900
    const Level& L = g_b.U[g_b.outLevel];
    float c[4] = {1.0f, -1.0f, 0.5f / L.w, 0.5f / L.h + 1.0f};
    dev->SetTexture(1, bloom);
    dev->SetVertexShaderConstantF(0x67, c, 1);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    dev->SetSamplerState(1, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);

    HRESULT hr = dev->DrawIndexedPrimitive(t, bv, mi, nv, si, pc);

    dev->SetTexture(1, old);
    dev->SetVertexShaderConstantF(0x67, oldC, 1);
    dev->SetSamplerState(1, D3DSAMP_MINFILTER, smin);
    dev->SetSamplerState(1, D3DSAMP_MAGFILTER, smag);
    dev->SetSamplerState(1, D3DSAMP_MIPFILTER, smip);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSU, au);
    dev->SetSamplerState(1, D3DSAMP_ADDRESSV, av);
    if (old) old->Release();
    g_builtThisFrame = false;
    return hr;
}

extern "C" void __stdcall OnRelease_C(BYTE*) {
    Bloom_Release();
    g_builtThisFrame = false;
}

// Thunks (GNU as, AT&T syntax). The patched call sites keep the stack layout of
// IDirect3DDevice9::DrawIndexedPrimitive (this + 6 args, stdcall).
asm(
    ".text\n"
    ".globl _VPassThunk\n"
    "_VPassThunk:\n"
    "    movl %edi, _g_devobj\n"
    "    jmp _VPassDraw_C@28\n"
    ".globl _FinalThunk\n"
    "_FinalThunk:\n"
    "    jmp _FinalDraw_C@28\n"
    ".globl _ReleaseThunk\n"
    "_ReleaseThunk:\n"
    "    pushl %ecx\n"
    "    pushl %ecx\n"
    "    call _OnRelease_C@4\n"
    "    popl %ecx\n"
    "    jmp *_g_origRelease\n"
);

static bool Check(DWORD va, const BYTE* b, int n) { return memcmp((void*)VA(va), b, n) == 0; }

static DWORD Rel32Target(DWORD va) { return VA(va) + 5 + *(int*)(VA(va) + 1); }

static void PatchCall6(DWORD va, void* target) {   // 6-byte indirect call -> call rel32 + nop
    BYTE b[6] = {0xE8, 0, 0, 0, 0, 0x90};
    *(DWORD*)(b + 1) = (DWORD)target - (VA(va) + 5);
    WriteMem(VA(va), b, 6);
}

static void PatchRel32(DWORD va, BYTE op, void* target) {
    BYTE b[5] = {op};
    *(DWORD*)(b + 1) = (DWORD)target - (VA(va) + 5);
    WriteMem(VA(va), b, 5);
}

static void InstallHooks() {
    g_delta = (DWORD)GetModuleHandleA(NULL) - IMAGE_BASE;

    // verify every site before touching anything (SCCT PC v1.05.157 only)
    const BYTE DIP_CALL[6] = {0xFF, 0x91, 0x48, 0x01, 0x00, 0x00};   // call [ecx+148h]
    const BYTE JMP_REL[1] = {0xE9};
    const BYTE PUSH_50[2] = {0x6A, 0x50};
    if (!Check(SITE_HDR_VPASS, DIP_CALL, 6) || !Check(SITE_HDR_FINAL, DIP_CALL, 6) ||
        !Check(SITE_RELEASE, JMP_REL, 1) || Rel32Target(SITE_RELEASE) != VA(FUNC_RELEASE) ||
        !Check(SITE_CREATE_FLAGS, PUSH_50, 2))
        return;

    // The game creates a PURE device (no Get* / state-block capture). Drop D3DCREATE_PUREDEVICE so
    // the passes above can save and restore device state exactly.
    const BYTE push40[2] = {0x6A, D3DCREATE_HARDWARE_VERTEXPROCESSING};
    WriteMem(VA(SITE_CREATE_FLAGS), push40, 2);
    PatchCall6(SITE_HDR_VPASS, (void*)VPassThunk);
    PatchCall6(SITE_HDR_FINAL, (void*)FinalThunk);
    g_origRelease = VA(FUNC_RELEASE);
    PatchRel32(SITE_RELEASE, 0xE9, (void*)ReleaseThunk);

    ApplyPyramidSize(g_cfg.pyramidSize);
    InstallD3DHooks();
}

// ------------------------------------------------------------------------------------------------
// Exports
// ------------------------------------------------------------------------------------------------
// Imported by the patched splintercell3.exe (keeps the DLL in its import table). Version 1.3.0.
extern "C" __declspec(dllexport) int Remaster_Version() { return 10300; }

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        char dir[MAX_PATH];
        GetModuleFileNameA(h, dir, MAX_PATH);
        char* s = strrchr(dir, '\\');
        if (s) s[1] = 0; else dir[0] = 0;
        LoadConfig(dir);
        char exe[MAX_PATH];
        GetModuleFileNameA(NULL, exe, MAX_PATH);
        const char* name = strrchr(exe, '\\');
        name = name ? name + 1 : exe;
        if (_stricmp(name, "splintercell3.exe") == 0) InstallHooks();
    }
    return TRUE;
}
