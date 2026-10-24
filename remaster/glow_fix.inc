// ------------------------------------------------------------------------------------------------
// glow_fix.inc - Direct3D-level glow fix for every post-process path (SM3 HDR on / off, vision modes).
//
// The game's glow render targets are identified when they are created (return address of the
// CreateTexture call inside the render-target setup at 0x10C8ADE0):
//   SOURCE : full-res scene copies the glow is built from
//   TOP    : the 256x256 glow base levels and the 256x600 intermediate
//   CHAIN  : all glow levels
// Two fixes are applied on the fly:
//   PREFILTER   - a draw into a TOP target sampling a SOURCE texture (or a CHAIN level much larger than
//                 the target) gets an anti-aliased copy already reduced to the target size.
//   RECONSTRUCT - a draw into a large target (> 512 px) sampling a CHAIN texture gets a cubic B-spline
//                 upsampled copy at half the target resolution.
// The game's own glow shaders keep running on the replaced textures.
// ------------------------------------------------------------------------------------------------
#include <map>

typedef IDirect3D9* (WINAPI* Direct3DCreate9_t)(UINT);
typedef HRESULT (__stdcall* CreateDevice_t)(IDirect3D9*, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS*, IDirect3DDevice9**);
typedef HRESULT (__stdcall* Present_t)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
typedef HRESULT (__stdcall* CreateTexture_t)(IDirect3DDevice9*, UINT, UINT, UINT, DWORD, D3DFORMAT, D3DPOOL, IDirect3DTexture9**, HANDLE*);
typedef HRESULT (__stdcall* StretchRect_t)(IDirect3DDevice9*, IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
typedef HRESULT (__stdcall* SetRenderTarget_t)(IDirect3DDevice9*, DWORD, IDirect3DSurface9*);
typedef HRESULT (__stdcall* SetTexture_t)(IDirect3DDevice9*, DWORD, IDirect3DBaseTexture9*);
typedef HRESULT (__stdcall* DrawPrimitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
typedef HRESULT (__stdcall* DrawIndexedPrimitive_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
typedef HRESULT (__stdcall* DrawPrimitiveUP_t)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
typedef HRESULT (__stdcall* CreatePS_t)(IDirect3DDevice9*, const DWORD*, IDirect3DPixelShader9**);
typedef HRESULT (__stdcall* SetPS_t)(IDirect3DDevice9*, IDirect3DPixelShader9*);

static Direct3DCreate9_t      o_Direct3DCreate9;
static CreateDevice_t         o_CreateDevice;
static Present_t              o_Present;
static CreateTexture_t        o_CreateTexture;
static StretchRect_t          o_StretchRect;
static SetRenderTarget_t      o_SetRenderTarget;
static SetTexture_t           o_SetTexture;
static DrawPrimitive_t        o_DrawPrimitive;
static DrawIndexedPrimitive_t o_DrawIndexedPrimitive;
static DrawPrimitiveUP_t      o_DrawPrimitiveUP;
static CreatePS_t             o_CreatePS;
static SetPS_t                o_SetPS;

// ---------------------------------------------------------------------------- render target roles
enum Role { R_SOURCE = 1, R_TOP = 2, R_CHAIN = 4 };

struct RTInfo {
    DWORD caller; int role;
    IDirect3DTexture9* tex; IDirect3DSurface9* surf; UINT w, h; D3DFORMAT fmt; DWORD version;
};

static RTInfo g_rts[] = {
    {0x10C8B2D8, R_SOURCE},            // scene copy
    {0x10C8AFE8, R_SOURCE},            // HDR scene
    {0x10C8B046, R_SOURCE},            // SM2 scene copies
    {0x10C8B078, R_SOURCE},
    {0x10C8B385, R_TOP},               // 256x600 intermediate
    {0x10C8B3BD, R_TOP | R_CHAIN},     // chain A: 256, 128, 64, 32, 16
    {0x10C8B3E4, R_CHAIN},
    {0x10C8B405, R_CHAIN},
    {0x10C8B426, R_CHAIN},
    {0x10C8B447, R_CHAIN},
    {0x10C8B46E, R_TOP | R_CHAIN},     // chain B: 256, 128, 64, 32
    {0x10C8B495, R_CHAIN},
    {0x10C8B4B6, R_CHAIN},
    {0x10C8B4D7, R_CHAIN},
    {0x10C8B339, R_TOP | R_CHAIN},     // 256x256 FP16 target of the vision modes
};
enum { NRT = sizeof(g_rts) / sizeof(g_rts[0]) };

static RTInfo* RTByTex(IDirect3DBaseTexture9* t) {
    if (!t) return 0;
    for (RTInfo& r : g_rts) if (r.tex == (IDirect3DTexture9*)t) return &r;
    return 0;
}

static RTInfo* RTBySurf(IDirect3DSurface9* s) {
    if (!s) return 0;
    for (RTInfo& r : g_rts) if (r.surf == s) return &r;
    return 0;
}

// ---------------------------------------------------------------------------- caches
struct PrefOut { RTInfo* src; UINT tw, th; DWORD frame, ver; Level L; };
struct Pyramid { RTInfo* src; DWORD frame, ver; int n; Level P[MAXL]; };
struct UpCache { RTInfo* src; UINT tw, th; DWORD frame, ver; Level L; };
enum { MAXPYR = 4, MAXPREF = 8, MAXUP = 16 };
static Pyramid g_pyr[MAXPYR];
static PrefOut g_pref[MAXPREF];
static UpCache g_up[MAXUP];

static DWORD                  g_frameNo;
static IDirect3DSurface9*     g_curRT;
static UINT                   g_curRTw, g_curRTh;
static IDirect3DBaseTexture9* g_curTex[8];
static void*                  g_curPS;
static bool                   g_upFp16Failed;
static std::map<void*, DWORD> g_psSamplers;   // pixel shader -> mask of sampler stages it reads

static void FreeLevel(Level& L) { REL(L.surf); REL(L.tex); L.w = L.h = 0; }

static void GlowFix_Release() {
    for (Pyramid& p : g_pyr) { for (Level& L : p.P) FreeLevel(L); p.src = 0; p.n = 0; }
    for (PrefOut& p : g_pref) { FreeLevel(p.L); p.src = 0; }
    for (UpCache& u : g_up) { FreeLevel(u.L); u.src = 0; }
}

static bool EnsureLevel(Level& L, UINT w, UINT h, D3DFORMAT fmt) {
    if (L.tex && L.w == w && L.h == h) {
        D3DSURFACE_DESC d;
        L.tex->GetLevelDesc(0, &d);
        if (d.Format == fmt) return true;
    }
    FreeLevel(L);
    if (FAILED(g_b.dev->CreateTexture(w, h, 1, D3DUSAGE_RENDERTARGET, fmt, D3DPOOL_DEFAULT, &L.tex, 0))) { L.tex = 0; return false; }
    L.tex->GetSurfaceLevel(0, &L.surf);
    L.w = w; L.h = h;
    return true;
}

// ---------------------------------------------------------------------------- own passes
static IDirect3DSurface9 *g_saveRT, *g_saveDS;

static bool BeginOwnPasses(IDirect3DDevice9* d) {
    if (!Common_Ensure(d)) return false;
    g_internal = true;
    d->GetRenderTarget(0, &g_saveRT);
    d->GetDepthStencilSurface(&g_saveDS);
    g_b.sb->Capture();
    SetCommonState(d);
    return true;
}

static void EndOwnPasses(IDirect3DDevice9* d) {
    d->SetRenderTarget(0, g_saveRT);
    d->SetDepthStencilSurface(g_saveDS);
    g_b.sb->Apply();
    REL(g_saveRT); REL(g_saveDS);
    g_internal = false;
}

// Anti-aliased reduction of 'src' to exactly tw x th (progressive 13-tap).
static IDirect3DTexture9* GetPrefiltered(IDirect3DDevice9* d, RTInfo* src, UINT tw, UINT th, bool& begun) {
    for (PrefOut& p : g_pref)
        if (p.src == src && p.tw == tw && p.th == th && p.frame == g_frameNo && p.ver == src->version && p.L.tex) return p.L.tex;
    if (!begun) { if (!BeginOwnPasses(d)) return 0; begun = true; }

    Pyramid* py = 0;
    for (Pyramid& p : g_pyr) if (p.src == src) { py = &p; break; }
    if (!py) for (Pyramid& p : g_pyr) if (!p.src) { py = &p; break; }
    if (!py) py = &g_pyr[0];
    if (py->src != src || py->frame != g_frameNo || py->ver != src->version) {
        py->src = src; py->n = 0;
        UINT w = src->w, h = src->h, iw = w, ih = h;
        IDirect3DTexture9* in = src->tex;
        // halve while the next level is still >= 1.5x the target
        while (py->n < MAXL && (w + 1) / 2 * 2 >= tw * 3 && (h + 1) / 2 >= th / 2 && (w + 1) / 2 >= 2) {
            w = (w + 1) / 2; h = (h + 1) / 2;
            Level& L = py->P[py->n];
            if (!EnsureLevel(L, w, h, src->fmt)) { py->src = 0; return 0; }
            float c[4] = {1.0f / iw, 1.0f / ih, 1.0f / iw, 1.0f / ih};
            Pass(L.surf, w, h, g_b.psDown, in, 0, c, 1);
            in = L.tex; iw = w; ih = h; py->n++;
        }
        py->frame = g_frameNo; py->ver = src->version;
    }

    PrefOut* po = 0;
    for (PrefOut& p : g_pref) if (p.src == src && p.tw == tw && p.th == th) { po = &p; break; }
    if (!po) for (PrefOut& p : g_pref) if (!p.src) { po = &p; break; }
    if (!po) po = &g_pref[0];
    if (!EnsureLevel(po->L, tw, th, src->fmt)) { po->src = 0; return 0; }
    IDirect3DTexture9* in = py->n ? py->P[py->n - 1].tex : src->tex;
    UINT iw = py->n ? py->P[py->n - 1].w : src->w, ih = py->n ? py->P[py->n - 1].h : src->h;
    float c[4] = {1.0f / iw, 1.0f / ih, 1.0f / iw, 1.0f / ih};
    Pass(po->L.surf, tw, th, g_b.psDown, in, 0, c, 1);
    po->src = src; po->tw = tw; po->th = th; po->frame = g_frameNo; po->ver = src->version;
    return po->L.tex;
}

// Cubic B-spline upsample of a glow level to tw x th.
static IDirect3DTexture9* GetUpsampled(IDirect3DDevice9* d, RTInfo* src, UINT tw, UINT th, bool& begun) {
    for (UpCache& u : g_up)
        if (u.src == src && u.tw == tw && u.th == th && u.frame == g_frameNo && u.ver == src->version && u.L.tex) return u.L.tex;
    if (!begun) { if (!BeginOwnPasses(d)) return 0; begun = true; }

    UpCache* uc = 0;
    for (UpCache& u : g_up) if (u.src == src && u.tw == tw && u.th == th) { uc = &u; break; }
    if (!uc) for (UpCache& u : g_up) if (!u.src) { uc = &u; break; }
    if (!uc) uc = &g_up[0];
    D3DFORMAT uf = g_cfg.fp16 && !g_upFp16Failed ? D3DFMT_A16B16G16R16F : src->fmt;
    if (!EnsureLevel(uc->L, tw, th, uf)) {
        if (uf != src->fmt) g_upFp16Failed = true;
        if (uf == src->fmt || !EnsureLevel(uc->L, tw, th, src->fmt)) { uc->src = 0; return 0; }
    }
    float c[4] = {(float)src->w, (float)src->h, 1.0f / src->w, 1.0f / src->h};
    Pass(uc->L.surf, tw, th, g_b.psBicubic, src->tex, 0, c, 1);
    uc->src = src; uc->tw = tw; uc->th = th; uc->frame = g_frameNo; uc->ver = src->version;
    return uc->L.tex;
}

// Which texture stages does a pixel shader sample? (stale bindings on unused stages are ignored)
static DWORD SamplerMask(const DWORD* f) {
    DWORD ver = f[0], major = (ver >> 8) & 0xFF, mask = 0;
    if ((ver >> 16) != 0xFFFF) return 0xFF;
    int p = 1;
    for (int guard = 0; guard < 8192; guard++) {
        DWORD tok = f[p], op = tok & 0xFFFF;
        if (tok == 0x0000FFFF) break;
        if (op == 0xFFFE) { p += 1 + ((tok >> 16) & 0x7FFF); continue; }   // comment
        if (major >= 2) {
            DWORD len = (tok >> 24) & 0xF;
            if (op == 31 && len >= 2) {                                    // dcl s#
                DWORD r = f[p + 2], type = ((r >> 28) & 7) | ((r >> 8) & 0x18);
                if (type == 10) mask |= 1u << (r & 0x7FF);
            }
            p += 1 + len;
        } else {
            if (op == 81) { p += 6; continue; }                            // def: 4 raw floats
            int q = p + 1;
            while (f[q] >> 31) q++;
            if (op >= 64 && op <= 89 && q > p + 1) mask |= 1u << (f[p + 1] & 0x7);   // tex* ops name their stage
            p = q;
        }
    }
    return mask;
}

// ---------------------------------------------------------------------------- draw interception
struct Swap { DWORD stage; IDirect3DBaseTexture9* orig; DWORD mn, mg, mp; bool samp; };

template <class F>
static HRESULT GlowDraw(IDirect3DDevice9* d, UINT prims, F draw) {
    if (g_internal) return draw();
    RTInfo* rt = RTBySurf(g_curRT);
    if (rt) rt->version++;   // contents of a tracked target change -> cached copies are stale
    // post-process passes are single quads; mesh / scene draws are never touched
    if (!g_cfg.enabled || prims > 4 || !(g_cfg.fixDownsample || g_cfg.smoothUpsample)) return draw();
    Swap sw[8];
    IDirect3DBaseTexture9* repl[8];
    int ns = 0;
    bool begun = false;
    bool top = rt && (rt->role & R_TOP);
    bool large = (!rt && g_curRTw > 512) || (rt && (rt->role & R_SOURCE));
    DWORD used = 0xFF;
    if (g_curPS) { auto it = g_psSamplers.find(g_curPS); if (it != g_psSamplers.end()) used = it->second; }

    for (DWORD s = 0; s < 8; s++) {
        if (!(used & (1u << s))) continue;
        RTInfo* t = RTByTex(g_curTex[s]);
        if (!t || t == rt) continue;
        IDirect3DTexture9* rep = 0;
        bool samp = false;
        // big reductions: scene -> glow base, or glow level -> much smaller level (large Pyramid Size)
        bool bigStep = rt && (rt->role & (R_TOP | R_CHAIN)) && (t->role & R_CHAIN) &&
                       (t->w * 2 > rt->w * 5 || t->h * 2 > rt->h * 5);
        if (g_cfg.fixDownsample && ((top && (t->role & R_SOURCE)) || bigStep)) {
            rep = GetPrefiltered(d, t, g_curRTw, g_curRTh, begun);
        } else if (large && g_cfg.smoothUpsample && (t->role & R_CHAIN)) {
            UINT tw = (g_curRTw + 1) / 2, th = (g_curRTh + 1) / 2;
            if (tw > t->w || th > t->h) { rep = GetUpsampled(d, t, tw, th, begun); samp = true; }
        }
        if (rep) { sw[ns] = {s, g_curTex[s], 0, 0, 0, samp}; repl[ns] = rep; ns++; }
    }
    if (begun) EndOwnPasses(d);

    for (int i = 0; i < ns; i++) {
        if (sw[i].samp) {
            d->GetSamplerState(sw[i].stage, D3DSAMP_MINFILTER, &sw[i].mn);
            d->GetSamplerState(sw[i].stage, D3DSAMP_MAGFILTER, &sw[i].mg);
            d->GetSamplerState(sw[i].stage, D3DSAMP_MIPFILTER, &sw[i].mp);
            d->SetSamplerState(sw[i].stage, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
            d->SetSamplerState(sw[i].stage, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
            d->SetSamplerState(sw[i].stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        }
        o_SetTexture(d, sw[i].stage, repl[i]);
    }
    HRESULT hr = draw();
    for (int i = ns - 1; i >= 0; i--) {
        o_SetTexture(d, sw[i].stage, sw[i].orig);
        if (sw[i].samp) {
            d->SetSamplerState(sw[i].stage, D3DSAMP_MINFILTER, sw[i].mn);
            d->SetSamplerState(sw[i].stage, D3DSAMP_MAGFILTER, sw[i].mg);
            d->SetSamplerState(sw[i].stage, D3DSAMP_MIPFILTER, sw[i].mp);
        }
    }
    return hr;
}

static HRESULT __stdcall H_DrawPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT s, UINT c) {
    return GlowDraw(d, c, [&] { return o_DrawPrimitive(d, t, s, c); });
}

static HRESULT __stdcall H_DrawIndexedPrimitive(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, INT b, UINT mi, UINT nv, UINT si, UINT pc) {
    return GlowDraw(d, pc, [&] { return o_DrawIndexedPrimitive(d, t, b, mi, nv, si, pc); });
}

static HRESULT __stdcall H_DrawPrimitiveUP(IDirect3DDevice9* d, D3DPRIMITIVETYPE t, UINT c, const void* v, UINT st) {
    return GlowDraw(d, c, [&] { return o_DrawPrimitiveUP(d, t, c, v, st); });
}

// ---------------------------------------------------------------------------- state tracking
static HRESULT __stdcall H_Present(IDirect3DDevice9* d, const RECT* a, const RECT* b, HWND w, const RGNDATA* r) {
    g_frameNo++;
    return o_Present(d, a, b, w, r);
}

static HRESULT __stdcall H_CreateTexture(IDirect3DDevice9* d, UINT w, UINT h, UINT l, DWORD u, D3DFORMAT f, D3DPOOL p, IDirect3DTexture9** t, HANDLE* s) {
    HRESULT hr = o_CreateTexture(d, w, h, l, u, f, p, t, s);
    DWORD caller = (DWORD)__builtin_return_address(0);
    if (SUCCEEDED(hr) && *t && (u & D3DUSAGE_RENDERTARGET) && !g_internal)
        for (RTInfo& r : g_rts)
            if (VA(r.caller) == caller) {
                r.tex = *t; r.w = w; r.h = h; r.fmt = f; r.version = 0;
                IDirect3DSurface9* sf = 0;
                (*t)->GetSurfaceLevel(0, &sf);
                r.surf = sf;
                if (sf) sf->Release();   // the texture keeps its level-0 surface alive
            }
    return hr;
}

static HRESULT __stdcall H_StretchRect(IDirect3DDevice9* d, IDirect3DSurface9* s, const RECT* sr, IDirect3DSurface9* ds, const RECT* dr, D3DTEXTUREFILTERTYPE f) {
    if (!g_internal) { RTInfo* r = RTBySurf(ds); if (r) r->version++; }
    return o_StretchRect(d, s, sr, ds, dr, f);
}

static HRESULT __stdcall H_SetRenderTarget(IDirect3DDevice9* d, DWORD i, IDirect3DSurface9* s) {
    if (!g_internal && i == 0) {
        g_curRT = s; g_curRTw = g_curRTh = 0;
        if (s) { D3DSURFACE_DESC dd; s->GetDesc(&dd); g_curRTw = dd.Width; g_curRTh = dd.Height; }
    }
    return o_SetRenderTarget(d, i, s);
}

static HRESULT __stdcall H_SetTexture(IDirect3DDevice9* d, DWORD st, IDirect3DBaseTexture9* t) {
    if (!g_internal && st < 8) g_curTex[st] = t;
    return o_SetTexture(d, st, t);
}

static HRESULT __stdcall H_CreatePS(IDirect3DDevice9* d, const DWORD* f, IDirect3DPixelShader9** p) {
    HRESULT hr = o_CreatePS(d, f, p);
    if (SUCCEEDED(hr)) g_psSamplers[*p] = SamplerMask(f);
    return hr;
}

static HRESULT __stdcall H_SetPS(IDirect3DDevice9* d, IDirect3DPixelShader9* p) {
    if (!g_internal) g_curPS = p;
    return o_SetPS(d, p);
}

// ---------------------------------------------------------------------------- installation
static void HookVtbl(void** vt, int idx, void* fn, void** orig) {
    if (vt[idx] == fn) return;
    *orig = vt[idx];
    DWORD old;
    VirtualProtect(&vt[idx], 4, PAGE_EXECUTE_READWRITE, &old);
    vt[idx] = fn;
    VirtualProtect(&vt[idx], 4, old, &old);
}

static HRESULT __stdcall H_CreateDevice(IDirect3D9* d3d, UINT a, D3DDEVTYPE t, HWND w, DWORD flags, D3DPRESENT_PARAMETERS* pp, IDirect3DDevice9** dev) {
    HRESULT hr = o_CreateDevice(d3d, a, t, w, flags, pp, dev);
    if (SUCCEEDED(hr) && *dev) {
        void** vt = *(void***)*dev;
        HookVtbl(vt, 17, (void*)H_Present, (void**)&o_Present);
        HookVtbl(vt, 23, (void*)H_CreateTexture, (void**)&o_CreateTexture);
        HookVtbl(vt, 34, (void*)H_StretchRect, (void**)&o_StretchRect);
        HookVtbl(vt, 37, (void*)H_SetRenderTarget, (void**)&o_SetRenderTarget);
        HookVtbl(vt, 65, (void*)H_SetTexture, (void**)&o_SetTexture);
        HookVtbl(vt, 81, (void*)H_DrawPrimitive, (void**)&o_DrawPrimitive);
        HookVtbl(vt, 82, (void*)H_DrawIndexedPrimitive, (void**)&o_DrawIndexedPrimitive);
        HookVtbl(vt, 83, (void*)H_DrawPrimitiveUP, (void**)&o_DrawPrimitiveUP);
        HookVtbl(vt, 106, (void*)H_CreatePS, (void**)&o_CreatePS);
        HookVtbl(vt, 107, (void*)H_SetPS, (void**)&o_SetPS);
        g_frameNo = 0;
        g_commonFailed = false;
    }
    return hr;
}

static IDirect3D9* WINAPI H_Direct3DCreate9(UINT sdk) {
    IDirect3D9* d = o_Direct3DCreate9(sdk);
    if (d) HookVtbl(*(void***)d, 16, (void*)H_CreateDevice, (void**)&o_CreateDevice);
    return d;
}

static void InstallD3DHooks() {
    DWORD iat = VA(IAT_DIRECT3DCREATE9);
    Direct3DCreate9_t real = (Direct3DCreate9_t)GetProcAddress(GetModuleHandleA("d3d9.dll"), "Direct3DCreate9");
    o_Direct3DCreate9 = real ? real : *(Direct3DCreate9_t*)iat;
    DWORD p = (DWORD)H_Direct3DCreate9;
    WriteMem(iat, &p, 4);
}
