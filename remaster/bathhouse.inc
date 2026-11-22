// Bathhouse vent ([Gameplay] FixBathhouseVentEnemySpotted).
// The AI counts "times identified as intruder" (mission stat 7) when it identifies the player
// (EAIController perception, 0x10BB3EF0 -> EMissionManager AddStats(7, npc pawn)). In 10_BathHouse
// the night vision elite below the vent room identifies Sam while he crawls in the vent; that one
// is not counted. The guard still reacts as before; only the count is skipped.

static const DWORD BH_SITE_IDENT  = 0x10BB3F66;   // call EMissionManager::AddStats(7, npc)
static const DWORD BH_FUNC_STATS  = 0x10C290D0;   // thiscall(type, reference)

typedef void (__attribute__((thiscall)) *BhStatsFn)(void*, int, void*);

static int g_bhLocOff = -2;

// Offset of a property of any type (Location is a 12-byte struct), by its leaf name.
static int BhPropOffset(void* obj, const wchar_t* name, const wchar_t* type) {
    if (!obj || !g_getFullName) return -1;
    void* cls = *(void**)((BYTE*)obj + 0x24);
    wchar_t buf[256];
    for (unsigned depth = 0; cls && depth < 32; depth++, cls = *(void**)((BYTE*)cls + 40)) {
        unsigned count = 0;
        for (void* field = *(void**)((BYTE*)cls + 72); field && ++count <= 2048; ) {
            if (ObjName(field, buf, 256) && NameStarts(buf, type) && wcscmp(NameLeaf(buf), name) == 0)
                return *(unsigned short*)((BYTE*)field + 68);
            void* next = *(void**)((BYTE*)field + 44);
            if (next == field) break;
            field = next;
        }
    }
    return -1;
}

static int BhLocation(void* actor, float out[3]) {
    if (!actor || !ObjPtrOk(actor)) return 0;
    if (g_bhLocOff == -2) {
        int off = BhPropOffset(actor, L"Location", L"StructProperty");
        if (off < 0) return 0;
        g_bhLocOff = off;
    }
    memcpy(out, (BYTE*)actor + g_bhLocOff, 12);
    return 1;
}

extern "C" void BhIdent_Hook();
// The vent above the vent room: Sam crawls in it at z -120 around x 5763, y 4034..4043 when the
// night vision elite (EDisplaceElite0, on the floor below at 6562 4758 -359) identifies him.
// The box keeps clear of the room floor (z -359 and lower).
static const float BH_VENT_MIN[3] = {5600.0f, 3700.0f, -200.0f};
static const float BH_VENT_MAX[3] = {5930.0f, 4400.0f,  -40.0f};

static int BhInVent(void* target) {
    float l[3];
    if (!BhLocation(target, l)) return 0;
    for (int k = 0; k < 3; k++)
        if (l[k] < BH_VENT_MIN[k] || l[k] > BH_VENT_MAX[k]) return 0;
    return 1;
}

static int BhIsVentSighting(void* npc, void* target) {
    wchar_t buf[256];
    if (!ObjName(npc, buf, 256) || wcscmp(buf, L"EDisplaceElite 10_BathHouse.EDisplaceElite0") != 0) return 0;
    if (!ObjName(target, buf, 256) || !NameStarts(buf, L"ESam")) return 0;
    return BhInVent(target);
}

extern "C" void __cdecl OnBhIdent(void* mm, int type, void* npc, BYTE* ai, int entryOff) {
    void* target = 0;
    if (ai && ObjPtrOk(ai)) {
        BYTE* arr = *(BYTE**)(ai + 0x4F0);
        if (arr && ObjPtrOk(arr + entryOff)) target = *(void**)(arr + entryOff);
    }
    if (npc && target && BhIsVentSighting(npc, target)) return;
    ((BhStatsFn)VA(BH_FUNC_STATS))(mm, type, npc);
}

asm(
    ".text\n"
    ".globl _BhIdent_Hook\n"
    "_BhIdent_Hook:\n"                  // ecx = mission manager, [esp+4] 7, [esp+8] npc pawn,
    "    pushl %edi\n"                  // esi = AI controller, edi = target entry offset
    "    pushl %esi\n"
    "    pushl 16(%esp)\n"
    "    pushl 16(%esp)\n"
    "    pushl %ecx\n"
    "    call _OnBhIdent\n"
    "    addl $20, %esp\n"
    "    ret $8\n"
);

static void ApplyBathhouseHooks() {
    if (!g_cfg.fixBathhouseVent) return;
    const BYTE gfn[7] = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6};
    if (!g_getFullName && Check(SITE_GETFULLNAME, gfn, 7))
        g_getFullName = (GetFullNameFn)VA(SITE_GETFULLNAME);
    const BYTE call[1] = {0xE8};
    if (!Check(BH_SITE_IDENT, call, 1) || Rel32Target(BH_SITE_IDENT) != VA(BH_FUNC_STATS)) return;
    PatchRel32(BH_SITE_IDENT, 0xE8, (void*)BhIdent_Hook);
}
