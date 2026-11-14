// ------------------------------------------------------------------------------------------------
// gameplay.inc - [Camera] DisableCargoShipSway / DisableFishEyeVision,
// [Weapons] Pistol/RifleReticleSettleMult, Pistol/RifleMoveSpeedPenaltyMult,
// Pistol/RifleAccuracyBase, and
// [Physics] HighFpsForcedTickrate.
//
// ULevel always ticks with the real frame time. HighFpsForcedTickrate paces hanging lamps and
// swaying cables (ESoftBody / ESBRope / ERope) at N Hz with elapsed dt. 0 = original high-FPS
// tick. NPCs, player and camera keep the real delta. DisableCargoShipSway clears
// ALevelInfo::bCameraRocking (02_CargoShip). DisableFishEyeVision replaces Magma's
// fisheye mesh DrawIndexedPrimitive with a fullscreen UV 0..1 blit (vision PS stays).
// Pistol/RifleReticleSettleMult scale EWeapon settle time (returnMod / mult).
// 0 = always focused, 1 = original. Pistol/RifleMoveSpeedPenaltyMult scale the reticle spread
// from moving (AccuracyMovementModifier * mult). Both are rebuilt from the class defaults.
// Pistol/RifleAccuracyBase set the focused spread floor (AccuracyBase); the native weapon tick
// still scales it by 0.6 while crouched.
// ------------------------------------------------------------------------------------------------
static const DWORD SITE_LEVEL_TICK1  = 0x10A43AB7;   // push 2; call [edx+6Ch]  GLevel
static const DWORD SITE_LEVEL_TICK2  = 0x10A43AD1;   // push 2; call [eax+6Ch]  GEntry
static const DWORD SITE_LEVEL_TICK3  = 0x10A43AF8;   // push 2; call [edx+6Ch]  other level
static const DWORD SITE_ACTOR_TICK1  = 0x10A792D1;   // call [eax+0B0h]  LevelInfo
static const DWORD SITE_ACTOR_TICK2  = 0x10A7937C;   // call [eax+0B0h]  actor loop
static const DWORD SITE_ACTOR_TICK3  = 0x10A793DB;   // call [eax+0B0h]  newly added actors
static const DWORD SITE_GETFULLNAME  = 0x10CD8F90;   // UObject::GetFullName (thiscall, wchar)
static const DWORD SITE_GNAMES       = 0x1130DFC0;   // TArray<FNameEntry*> GNames
static const DWORD SITE_FISHEYE_DIP   = 0x10C83E5D;   // Magma fisheye DrawIndexedPrimitive

static int LampTickOn() {
    return g_cfg.highFpsTickrate > 0;
}

static float LampTickStep() {
    int hz = g_cfg.highFpsTickrate;
    if (hz < 1) hz = 35;
    return 1.0f / (float)hz;
}

typedef wchar_t* (__attribute__((thiscall)) *GetFullNameFn)(void* obj, wchar_t* buf);
typedef void     (__attribute__((thiscall)) *LevelTickOrig)(void* self, int tickType, float dt);
typedef int      (__attribute__((thiscall)) *ActorTickOrig)(void* self, float dt, int tickType);

static GetFullNameFn g_getFullName;
static int   g_slowRun;
static float g_slowDt;
static float g_slowAcc;

static int ObjPtrOk(void* p) {
    if (!p || ((DWORD)p & 3)) return 0;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return 1;
}

static int CopyFName(void* obj, wchar_t* buf, int n) {
    if (!obj || n < 2 || !ObjPtrOk(obj)) return 0;
    DWORD idx = *(DWORD*)((BYTE*)obj + 0x20);
    if (!idx || idx > 400000) return 0;
    DWORD names = *(DWORD*)VA(SITE_GNAMES);
    if (!names || (names & 3) || !ObjPtrOk((void*)names)) return 0;
    BYTE* slot = (BYTE*)names + idx * 4;
    if (!ObjPtrOk(slot)) return 0;
    DWORD ent = *(DWORD*)slot;
    if (!ent || (ent & 3) || !ObjPtrOk((void*)ent)) return 0;
    const wchar_t* s = (const wchar_t*)(ent + 0xC);
    if (!ObjPtrOk((void*)s)) return 0;
    int i = 0;
    while (i < n - 1 && s[i] && i < 120) {
        wchar_t c = s[i];
        if (c < 32 && c != 9) break;
        buf[i++] = c;
    }
    buf[i] = 0;
    return i > 0;
}

static wchar_t* ObjName(void* obj, wchar_t* buf, int n) {
    if (!obj || n < 2) return 0;
    buf[0] = 0;
    if (!ObjPtrOk(obj)) return 0;
    DWORD vt = *(DWORD*)obj;
    if (!vt || (vt & 3) || !ObjPtrOk((void*)vt)) return 0;
    if (CopyFName(obj, buf, n) && wcsstr(buf, L"Play_"))
        return buf;
    if (!g_getFullName) return buf[0] ? buf : 0;
    wchar_t tmp[1024];
    tmp[0] = 0;
    wchar_t* r = g_getFullName(obj, tmp);
    tmp[1023] = 0;
    const wchar_t* src = tmp[0] ? tmp : r;
    if (!src || !src[0]) return buf[0] ? buf : 0;
    wcsncpy(buf, src, n - 1);
    buf[n - 1] = 0;
    return buf;
}

static const wchar_t* NameLeaf(const wchar_t* s) {
    const wchar_t* leaf = s;
    for (const wchar_t* p = s; *p; p++) if (*p == L'.' || *p == L' ') leaf = p + 1;
    return leaf;
}

static int NameStarts(const wchar_t* s, const wchar_t* type) {
    size_t n = wcslen(type);
    return wcsncmp(s, type, n) == 0 && (s[n] == L' ' || s[n] == 0);
}

static int ClassPrefix(const wchar_t* s, const wchar_t* prefix) {
    size_t n = wcslen(prefix);
    if (wcsncmp(s, prefix, n) != 0) return 0;
    wchar_t c = s[n];
    return c == 0 || c == L' ' || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z');
}

static int IsPlayerSide(const wchar_t* buf) {
    return wcsstr(buf, L"LevelInfo") || ClassPrefix(buf, L"EPlayer") ||
           NameStarts(buf, L"EPawn") || NameStarts(buf, L"ECoopPlayerController") ||
           NameStarts(buf, L"PlayerController") || NameStarts(buf, L"PlayerInput") ||
           NameStarts(buf, L"HUD") || NameStarts(buf, L"EInventory") ||
           NameStarts(buf, L"EchelonGameInfo") || NameStarts(buf, L"GameInfo") ||
           NameStarts(buf, L"Viewport") || ClassPrefix(buf, L"EPlayerCam") ||
           wcsstr(buf, L"Camera");
}

// 0 = every frame, real dt (NPCs, player, camera, everything else).
// 1 = hanging lamps / swaying cables.
static int SimKind(void* actor) {
    if (!actor) return 0;
    void* cls = *(void**)((BYTE*)actor + 0x24);
    static void* cacheCls[96];
    static char  cacheVal[96];
    static int   cacheN;
    for (int i = 0; i < cacheN; i++) if (cacheCls[i] == cls) return cacheVal[i];
    int k = 0;
    wchar_t buf[256];
    if (ObjName(actor, buf, 256) && !IsPlayerSide(buf) &&
        (ClassPrefix(buf, L"ESB") || ClassPrefix(buf, L"ESoft") ||
         NameStarts(buf, L"ERopeActor") || NameStarts(buf, L"ERapelRopeActor") ||
         NameStarts(buf, L"ESoftBodyActor") || NameStarts(buf, L"WindEmitter") ||
         NameStarts(buf, L"EGameplayObjectLight") ||
         wcsstr(buf, L"RopeActor") || wcsstr(buf, L"SoftBody")))
        k = 1;
    if (cacheN < 96) { cacheCls[cacheN] = cls; cacheVal[cacheN] = (char)k; cacheN++; }
    return k;
}

static void* FindProperty(void* obj, const wchar_t* name, const wchar_t* type) {
    if (!obj || !g_getFullName) return 0;
    void* cls = *(void**)((BYTE*)obj + 0x24);
    unsigned total = cls ? *(unsigned short*)((BYTE*)cls + 76) : 0;
    if (!cls || total == 0 || total > 0x100000) return 0;
    wchar_t buf[256];
    for (unsigned depth = 0; cls && depth < 32; depth++, cls = *(void**)((BYTE*)cls + 40)) {
        unsigned count = 0;
        for (void* field = *(void**)((BYTE*)cls + 72); field && ++count <= 2048; ) {
            if (ObjName(field, buf, 256) && NameStarts(buf, type) && wcscmp(NameLeaf(buf), name) == 0) {
                unsigned dim = *(unsigned short*)((BYTE*)field + 52);
                unsigned esz = *(unsigned short*)((BYTE*)field + 54);
                unsigned poff = *(unsigned short*)((BYTE*)field + 68);
                if (dim == 1 && (esz == 1 || esz == 4) && poff + 4 <= total)
                    return field;
            }
            void* next = *(void**)((BYTE*)field + 44);
            if (next == field) break;
            field = next;
        }
    }
    return 0;
}

static void* LevelInfoOf(void* level) {
    if (!level || !g_getFullName) return 0;
    static int actorsOff = -1;
    static void* lastLevel;
    if (level != lastLevel) { actorsOff = -1; lastLevel = level; }
    if (actorsOff < 0) {
        DWORD exe = (DWORD)GetModuleHandleA(NULL);
        wchar_t buf[256];
        for (int off = 0x28; off <= 0x180; off += 4) {
            void** data = *(void***)((BYTE*)level + off);
            int num = *(int*)((BYTE*)level + off + 4);
            int maxn = *(int*)((BYTE*)level + off + 8);
            if (!data || ((DWORD)data & 3) || num < 1 || num > 100000 || maxn < num || maxn > 200000) continue;
            void* first = data[0];
            if (!first || ((DWORD)first & 3)) continue;
            DWORD vt = *(DWORD*)first;
            if (vt < exe || vt > exe + 0x00A00000) continue;
            if (ObjName(first, buf, 256) && wcsstr(buf, L"LevelInfo")) { actorsOff = off; break; }
        }
    }
    if (actorsOff < 0) return 0;
    void** data = *(void***)((BYTE*)level + actorsOff);
    return data ? data[0] : 0;
}

static int   g_rockOff = -2;
static DWORD g_rockMask;

static void ClearCameraRocking(void* level) {
    void* info = LevelInfoOf(level);
    if (!info) return;
    if (g_rockOff == -2) {
        void* prop = FindProperty(info, L"bCameraRocking", L"BoolProperty");
        if (!prop) { g_rockOff = -1; return; }
        g_rockOff = *(unsigned short*)((BYTE*)prop + 68);
        g_rockMask = *(DWORD*)((BYTE*)prop + 84);
        if (!g_rockMask || (g_rockMask & (g_rockMask - 1))) { g_rockOff = -1; return; }
    }
    if (g_rockOff >= 0) *(DWORD*)((BYTE*)info + g_rockOff) &= ~g_rockMask;
}

// Cache only successful offsets (-2 = unknown). Never lock in a miss.
static void* ReadObjectProp(void* obj, const wchar_t* name, int* offCache) {
    int off = *offCache;
    if (off < 0) {
        void* prop = FindProperty(obj, name, L"ObjectProperty");
        if (!prop) return 0;
        off = *(unsigned short*)((BYTE*)prop + 68);
        *offCache = off;
    }
    void* p = *(void**)((BYTE*)obj + off);
    return (p && !((DWORD)p & 3) && ObjPtrOk(p)) ? p : 0;
}

static int g_offCurCtrl = -2, g_offCtrlList = -2, g_offNextCtrl = -2;
static int g_offActiveGun = -2, g_offPawn = -2, g_offCurWeapon = -2, g_offHandItem = -2;

static int ClassIsA(void* obj, const wchar_t* leaf) {
    if (!obj || !ObjPtrOk(obj)) return 0;
    void* cls = *(void**)((BYTE*)obj + 0x24);
    wchar_t buf[256];
    for (int d = 0; cls && d < 32; d++) {
        if (!ObjPtrOk(cls)) break;
        if (CopyFName(cls, buf, 256) && wcscmp(buf, leaf) == 0) return 1;
        if (ObjName(cls, buf, 256) && wcscmp(NameLeaf(buf), leaf) == 0) return 1;
        cls = *(void**)((BYTE*)cls + 40);
    }
    return 0;
}

static int g_accBaseOff = -2;
static int g_accDesOff  = -2;
static int g_accRealOff = -2;
static int g_accCurOff  = -2;   // Accuracy: the shown spread, eases toward AccuracyDesired
static int g_accMoveOff = -2;
static int g_accRetOff  = -2;

// The settle / move values are saved with the gun, so every write is derived from the class
// default (UClass::Defaults, as execDefaultVariable reads it), never from the live value: a gun
// loaded from any save (also ones made by older builds) gets the configured value again.
static const int UCLASS_DEFAULTS = 0x29C;   // TArray<BYTE> Defaults {Data, Num, Max}

static int ClassDefaultF(void* gun, int off, float* out) {
    BYTE* cls = *(BYTE**)((BYTE*)gun + 0x24);
    if (off < 0 || !ObjPtrOk(cls)) return 0;
    BYTE* data = *(BYTE**)(cls + UCLASS_DEFAULTS);
    int num = *(int*)(cls + UCLASS_DEFAULTS + 4);
    if (!data || num < off + 4 || !ObjPtrOk(data) || !ObjPtrOk(data + off)) return 0;
    float v = *(float*)(data + off);
    if (!(v >= 0.f && v < 1.0e6f)) return 0;
    *out = v;
    return 1;
}

enum { WPN_NONE, WPN_PISTOL, WPN_RIFLE };

// Pistol / rifle by class chain; cached per class (ClassIsA builds full names).
static int WeaponKind(void* gun) {
    static void* s_cls;
    static int s_kind;
    void* cls = *(void**)((BYTE*)gun + 0x24);
    if (cls == s_cls) return s_kind;
    int kind = WPN_NONE;
    if (ClassIsA(gun, L"EOneHandedWeapon")) kind = WPN_PISTOL;
    else if (ClassIsA(gun, L"EMainGun") || ClassIsA(gun, L"ETwoHandedWeapon")) kind = WPN_RIFLE;
    s_cls = cls;
    s_kind = kind;
    return kind;
}

static int EnsureAccuracyOffs(void* gun) {
    if (g_accBaseOff != -2) return g_accBaseOff >= 0 && g_accRetOff >= 0;
    void* b = FindProperty(gun, L"AccuracyBase", L"FloatProperty");
    void* d = FindProperty(gun, L"AccuracyDesired", L"FloatProperty");
    void* r = FindProperty(gun, L"RealAccuracy", L"FloatProperty");
    void* m = FindProperty(gun, L"AccuracyMovementModifier", L"FloatProperty");
    void* t = FindProperty(gun, L"AccuracyReturnModifier", L"FloatProperty");
    void* c = FindProperty(gun, L"Accuracy", L"FloatProperty");
    if (!b || !d || !r || !t) {
        g_accBaseOff = g_accDesOff = g_accRealOff = g_accCurOff = g_accMoveOff = g_accRetOff = -1;
        return 0;
    }
    g_accBaseOff = *(unsigned short*)((BYTE*)b + 68);
    g_accDesOff  = *(unsigned short*)((BYTE*)d + 68);
    g_accRealOff = *(unsigned short*)((BYTE*)r + 68);
    g_accCurOff  = c ? *(unsigned short*)((BYTE*)c + 68) : -1;
    g_accMoveOff = m ? *(unsigned short*)((BYTE*)m + 68) : -1;
    g_accRetOff  = *(unsigned short*)((BYTE*)t + 68);
    return 1;
}

static void ApplyWeaponStock(void* gun) {
    if (!gun || !ObjPtrOk(gun) || !EnsureAccuracyOffs(gun)) return;
    int kind = WeaponKind(gun);
    if (kind == WPN_NONE) return;
    int pistol = kind == WPN_PISTOL;
    float mult = pistol ? g_cfg.pistolReticleSettleMult : g_cfg.rifleReticleSettleMult;
    float move = pistol ? g_cfg.pistolMovePenaltyMult : g_cfg.rifleMovePenaltyMult;
    float base = pistol ? g_cfg.pistolAccuracyBase : g_cfg.rifleAccuracyBase;
    BYTE* g = (BYTE*)gun;
    float* pBase = (float*)(g + g_accBaseOff);
    if (*pBase != base) *pBase = base;
    if (mult <= 0.f) {
        // Always focused. With no movement penalty the native tick sets AccuracyDesired to the
        // floor (AccuracyBase, x0.6 crouched); snap the shown spread to it (also one loaded from a
        // save). RealAccuracy only drops (keeps the native settled-aim bonus).
        if (g_accMoveOff >= 0) *(float*)(g + g_accMoveOff) = 0.f;
        float floor = *(float*)(g + g_accDesOff);
        if (!(floor >= 0.f && floor <= base)) floor = base;
        *(float*)(g + g_accDesOff) = floor;
        if (g_accCurOff >= 0) *(float*)(g + g_accCurOff) = floor;
        float* real = (float*)(g + g_accRealOff);
        if (!(*real <= floor)) *real = floor;
        return;
    }
    // Targets are resolved once per gun; afterwards a write happens only when the live value
    // differs (first frame after a load, a gun switch, or anything else resetting it).
    static void* s_gun;
    static void* s_cls;
    static float s_ret, s_move;
    static int s_haveRet, s_haveMove;
    void* cls = *(void**)(g + 0x24);
    if (gun != s_gun || cls != s_cls) {
        float def;
        s_haveMove = g_accMoveOff >= 0 && ClassDefaultF(gun, g_accMoveOff, &def);
        if (s_haveMove) s_move = def * move;
        s_haveRet = ClassDefaultF(gun, g_accRetOff, &def);
        if (s_haveRet) s_ret = def / mult;
        s_gun = gun;
        s_cls = cls;
    }
    float* ret = (float*)(g + g_accRetOff);
    if (s_haveRet && *ret != s_ret) *ret = s_ret;
    if (s_haveMove) {
        float* mv = (float*)(g + g_accMoveOff);
        if (*mv != s_move) *mv = s_move;
    }
}

// Name check builds a full name; remember the last match (pointer + class) instead.
static int IsLocalPlayerController(void* ctrl) {
    static void* s_ctrl;
    static void* s_cls;
    void* cls = *(void**)((BYTE*)ctrl + 0x24);
    if (ctrl == s_ctrl && cls == s_cls) return 1;
    wchar_t buf[256];
    if (!ObjName(ctrl, buf, 256)) return 0;
    if (!wcsstr(buf, L"EPlayerController") && !NameStarts(buf, L"PlayerController") &&
        !ClassPrefix(buf, L"EPlayer"))
        return 0;
    s_ctrl = ctrl;
    s_cls = cls;
    return 1;
}

static void ApplyWeaponReticle(void* level) {
    if (!g_getFullName) return;
    void* info = LevelInfoOf(level);
    if (!info) return;

    void* ctrl = ReadObjectProp(info, L"CurrentController", &g_offCurCtrl);
    if (!ctrl) ctrl = ReadObjectProp(info, L"ControllerList", &g_offCtrlList);

    for (int n = 0; ctrl && n < 64; n++) {
        if (IsLocalPlayerController(ctrl)) {
            void* gun = ReadObjectProp(ctrl, L"ActiveGun", &g_offActiveGun);
            if (!gun) {
                void* pawn = ReadObjectProp(ctrl, L"Pawn", &g_offPawn);
                if (pawn) {
                    gun = ReadObjectProp(pawn, L"CurrentWeapon", &g_offCurWeapon);
                    if (!gun) gun = ReadObjectProp(pawn, L"HandItem", &g_offHandItem);
                }
            }
            if (gun) ApplyWeaponStock(gun);
            return;
        }
        void* next = ReadObjectProp(ctrl, L"nextController", &g_offNextCtrl);
        if (!next || next == ctrl) break;
        ctrl = next;
    }
}

// After Magma Setup + vision PS, replace the warped-mesh DIP with an identity blit.
extern "C" void __stdcall FishEyeBlit_C(void* magma) {
    IDirect3DDevice9* dev = magma ? *(IDirect3DDevice9**)((BYTE*)magma + 0x4D3C) : 0;
    if (!dev) return;

    D3DVIEWPORT9 vp;
    if (FAILED(dev->GetViewport(&vp)) || !vp.Width || !vp.Height) return;

    IDirect3DVertexShader9* vs = 0;
    IDirect3DVertexDeclaration9* vd = 0;
    DWORD fvf = 0;
    dev->GetVertexShader(&vs);
    dev->GetVertexDeclaration(&vd);
    dev->GetFVF(&fvf);

    dev->SetVertexShader(NULL);
    dev->SetVertexDeclaration(NULL);
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);

    const float w = (float)vp.Width, h = (float)vp.Height;
    struct { float x, y, z, rhw, u, v; } q[4] = {
        {-0.5f,  -0.5f,  0, 1, 0, 0},
        {w-0.5f, -0.5f,  0, 1, 1, 0},
        {-0.5f,  h-0.5f, 0, 1, 0, 1},
        {w-0.5f, h-0.5f, 0, 1, 1, 1},
    };
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof q[0]);

    if (vs) { dev->SetVertexShader(vs); vs->Release(); }
    if (vd) { dev->SetVertexDeclaration(vd); vd->Release(); }
    else if (fvf) dev->SetFVF(fvf);
}

extern "C" void FishEyeDip_Hook();

asm(
    ".text\n"
    ".globl _FishEyeDip_Hook\n"
    "_FishEyeDip_Hook:\n"
    "    pushl %esi\n"
    "    call _FishEyeBlit_C@4\n"
    "    popl %esi\n"
    "    ret $4\n"
);

static bool ApplyFishEyeDisable() {
    const BYTE stock[6] = {0x8B, 0x8E, 0xD8, 0x56, 0x00, 0x00};  // mov ecx, [esi+0x56D8]
    if (!Check(SITE_FISHEYE_DIP, stock, 6)) return false;
    PatchRel32(SITE_FISHEYE_DIP, 0xE9, (void*)FishEyeDip_Hook);
    return true;
}

static void SimAccum(float dt, float step, float* acc, int* run, float* outDt) {
    *run = 1;
    *outDt = dt;
    if (dt > 0 && dt < step) {
        *acc += dt;
        if (*acc >= step) {
            *outDt = *acc;
            if (*outDt > 0.1f) *outDt = 0.1f;
            *acc = 0;
            *run = 1;
        } else {
            *run = 0;
        }
    } else {
        *acc = 0;
    }
}

extern "C" int __stdcall ActorTick_C(void* actor, float dt, int tickType) {
    if (!actor) return 0;
    ActorTickOrig orig = *(ActorTickOrig*)(*(DWORD*)actor + 0xB0);
    int kind = LampTickOn() ? SimKind(actor) : 0;
    if (kind == 0)
        return orig(actor, dt, tickType);
    if (!g_slowRun) return 0;
    return orig(actor, g_slowDt, tickType);
}

extern "C" void __stdcall LevelTick_C(void* level, float dt) {
    if (!level) return;
    g_slowRun = 1;
    g_slowDt = dt;
    if (LampTickOn())
        SimAccum(dt, LampTickStep(), &g_slowAcc, &g_slowRun, &g_slowDt);
    else
        g_slowAcc = 0;

    LevelTickOrig orig = *(LevelTickOrig*)(*(DWORD*)level + 0x6C);
    orig(level, 2, dt);
    if (g_cfg.disableCargoSway) ClearCameraRocking(level);
    ApplyWeaponReticle(level);   // always: also resets values saved by older builds
}

asm(
    ".text\n"
    ".globl _LevelTickThunk\n"
    "_LevelTickThunk:\n"
    "    pushl 4(%esp)\n"
    "    pushl %ecx\n"
    "    call _LevelTick_C@8\n"
    "    ret $4\n"
    ".globl _ActorTickThunk\n"
    "_ActorTickThunk:\n"
    "    pushl 8(%esp)\n"
    "    pushl 8(%esp)\n"
    "    pushl %ecx\n"
    "    call _ActorTick_C@12\n"
    "    ret $8\n"
);

extern "C" void LevelTickThunk();
extern "C" void ActorTickThunk();

static bool ApplyGameplayHooks() {
    const BYTE tick1[5] = {0x6A, 0x02, 0xFF, 0x52, 0x6C};
    const BYTE tick2[5] = {0x6A, 0x02, 0xFF, 0x50, 0x6C};
    const BYTE tick3[5] = {0x6A, 0x02, 0xFF, 0x52, 0x6C};
    const BYTE atick[6] = {0xFF, 0x90, 0xB0, 0x00, 0x00, 0x00};
    const BYTE gfn[7]   = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6};
    if (!Check(SITE_LEVEL_TICK1, tick1, 5) || !Check(SITE_LEVEL_TICK2, tick2, 5) ||
        !Check(SITE_LEVEL_TICK3, tick3, 5))
        return false;
    if (Check(SITE_GETFULLNAME, gfn, 7))
        g_getFullName = (GetFullNameFn)VA(SITE_GETFULLNAME);
    PatchRel32(SITE_LEVEL_TICK1, 0xE8, (void*)LevelTickThunk);
    PatchRel32(SITE_LEVEL_TICK2, 0xE8, (void*)LevelTickThunk);
    PatchRel32(SITE_LEVEL_TICK3, 0xE8, (void*)LevelTickThunk);
    if (LampTickOn() &&
        Check(SITE_ACTOR_TICK1, atick, 6) && Check(SITE_ACTOR_TICK2, atick, 6) &&
        Check(SITE_ACTOR_TICK3, atick, 6)) {
        PatchCall6(SITE_ACTOR_TICK1, (void*)ActorTickThunk);
        PatchCall6(SITE_ACTOR_TICK2, (void*)ActorTickThunk);
        PatchCall6(SITE_ACTOR_TICK3, (void*)ActorTickThunk);
    }
    if (g_cfg.disableFishEye && !ApplyFishEyeDisable())
        return false;
    return true;
}
