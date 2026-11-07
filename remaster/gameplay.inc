// ------------------------------------------------------------------------------------------------
// gameplay.inc - [Camera] DisableCargoShipSway and [Physics] HighFpsForcedTickrate.
//
// ULevel always ticks with the real frame time. HighFpsForcedTickrate paces hanging lamps and
// swaying cables (ESoftBody / ESBRope / ERope) at N Hz with elapsed dt. 0 = original high-FPS
// tick. NPCs, player and camera keep the real delta. DisableCargoShipSway clears
// ALevelInfo::bCameraRocking (02_CargoShip).
// ------------------------------------------------------------------------------------------------
static const DWORD SITE_LEVEL_TICK1  = 0x10A43AB7;   // push 2; call [edx+6Ch]  GLevel
static const DWORD SITE_LEVEL_TICK2  = 0x10A43AD1;   // push 2; call [eax+6Ch]  GEntry
static const DWORD SITE_LEVEL_TICK3  = 0x10A43AF8;   // push 2; call [edx+6Ch]  other level
static const DWORD SITE_ACTOR_TICK1  = 0x10A792D1;   // call [eax+0B0h]  LevelInfo
static const DWORD SITE_ACTOR_TICK2  = 0x10A7937C;   // call [eax+0B0h]  actor loop
static const DWORD SITE_ACTOR_TICK3  = 0x10A793DB;   // call [eax+0B0h]  newly added actors
static const DWORD SITE_GETFULLNAME  = 0x10CD8F90;   // UObject::GetFullName (thiscall, wchar)

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

static wchar_t* ObjName(void* obj, wchar_t* buf, int n) {
    if (!obj || !g_getFullName || n < 2) return 0;
    buf[0] = 0;
    if (!ObjPtrOk(obj)) return 0;
    DWORD vt = *(DWORD*)obj;
    if (!vt || (vt & 3) || !ObjPtrOk((void*)vt)) return 0;
    wchar_t tmp[1024];
    tmp[0] = 0;
    wchar_t* r = g_getFullName(obj, tmp);
    tmp[1023] = 0;
    if (!r || !tmp[0]) return 0;
    wcsncpy(buf, tmp, n - 1);
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
    if (!LampTickOn() && !g_cfg.disableCargoSway) return true;
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
    return true;
}
