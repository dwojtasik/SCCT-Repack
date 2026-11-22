// Mission statistics in the OPSAT ([Gameplay] EnableOpsatStatistics).
// Adds MISSION STATISTICS under EQUIPMENT in the OPSAT main menu (opsat_PC, Lst_Main_Menu). Picking it
// opens the game's own end-of-mission statistics page (mission_review_PC, P_MissionStatistics)
// filled with the counters of the mission being played, the way the mission end does it: a
// CMissionReview is made on the OPSAT's Magma manager, given the level time, and its stats page is
// shown. The page's buttons become the ones of the other OPSAT pages: left OPSAT (back to the
// OPSAT menu), right EXIT (close the OPSAT, back to the game); retry is hidden. Esc / Backspace also
// go back to the OPSAT, and closing the OPSAT (EXIT, its key) closes the page. The page is refilled every second
// while open, so the mission time keeps running. Nothing is written: the fill only reads the
// mission manager's counters and the profile totals.
// The menu text is the page's own localized title (MISSION STATISTICS); the button texts are the
// ones the OPSAT pages use (Keys K_Opsat, Computer Exit).

static const DWORD ST_SITE_ADD_LAST   = 0x10B2F1AE;   // OPSAT load: AddItem(Equipment) on Lst_Main_Menu
static const DWORD ST_VT_CLICK        = 0x110E01C0;   // OPSAT listener slot: click / activate
static const DWORD ST_FUNC_CLICK      = 0x10B31A90;
static const DWORD ST_FUNC_TOGGLE     = 0x10B31490;   // OPSAT open (1) / close (0), thiscall ret 4
static const DWORD ST_SITE_TOGGLE[9]  = {0x10B230A8, 0x10B259A2, 0x10B259E4, 0x10B25A37, 0x10B25A70,
                                         0x10B317E4, 0x10B318F0, 0x10B31C60, 0x10B31F4D};
static const DWORD ST_GLOBAL_MAINMENU = 0x1122D610;   // hash of Lst_Main_Menu
static const DWORD ST_FUNC_CMR_MAKE   = 0x10B32F70;   // cdecl(CMissionReview** slot, manager): new + load
static const DWORD ST_FUNC_CMR_FREE   = 0x10B32520;   // cdecl(CMissionReview** slot): unload + delete
static const DWORD ST_FUNC_CMR_SHOW   = 0x10B32550;   // thiscall(show, page 0/1/2) ret 8
static const DWORD ST_FUNC_CMR_FILL   = 0x10B33DB0;   // thiscall: fill P_MissionStatistics
static const DWORD ST_VT_CMR_SHOWN    = 0x110E0750;   // CMissionReview slot 1: page shown -> fill
static const DWORD ST_FUNC_CMR_SHOWN  = 0x10B353F0;
static const DWORD ST_VT_CMR_CLICK    = 0x110E0724;   // CMissionReview +8 listener slot 3: button click
static const DWORD ST_FUNC_CMR_CLICK  = 0x10B33180;
static const DWORD ST_GLOBAL_B_OBJ    = 0x1122D674;   // hash of B_Objectives
static const DWORD ST_GLOBAL_B_NEXT   = 0x1122D67C;   // hash of B_NextMission
static const DWORD ST_FUNC_BUTTON_TEXT = 0x10B71F10;  // cdecl(page, hash, section, key, package, 1)
static const DWORD ST_FUNC_PLAYER     = 0x10A0E220;   // thiscall: player list (manager +4h)
static const DWORD ST_FUNC_PC         = 0x10A45B50;   // thiscall(index byte) on list +64h: controller
static const DWORD ST_FUNC_NODE_ENABLE = 0x10D32E70;  // node: enabled flag (+4Ch bit 1), thiscall(bool)
// CMissionReview: +14h manager, +18h loaded, +38h P_MissionStatistics, +50h ReplaceButton node
// (the empty middle panel), +54h B_RetryMission node, +64h level time (double; the fill adds the
// mission manager's +A0h). Magma node: +18h bit 0 visible.

typedef void (__cdecl *StMakeFn)(void**, void*);
typedef void (__cdecl *StFreeFn)(void**);
typedef void (__attribute__((thiscall)) *StShowFn)(void*, int, int);
typedef char (__attribute__((thiscall)) *StClickFn)(void*, void*);
typedef char (__attribute__((thiscall)) *StShownFn)(void*, void*);
typedef void (__attribute__((thiscall)) *StToggleFn)(void*, int);
typedef void* (__attribute__((thiscall)) *StPcFn)(void*, int);
typedef void (__attribute__((thiscall)) *StEnableFn)(void*, int);
typedef void (__cdecl *StButtonTextFn)(void*, DWORD, const wchar_t*, const wchar_t*, const wchar_t*, int);

static int GameIsLoading();     // dialog.inc

static int   g_stActive;        // the statistics hooks are in (EnableOpsatStatistics)
static void* g_stList;          // OPSAT Lst_Main_Menu with our entry
static int   g_stIndex = -1;    // our row in it
static void* g_stCmr;           // the statistics page's CMissionReview (kept until a safe point)
static BYTE* g_stCmrMgr;
static BYTE* g_stOpsat;         // the OPSAT it was opened from
static int   g_stShown;         // the page is on screen
static int   g_stSecond = -1;   // mission second last shown
static DWORD g_stSwallowKey;    // key whose release / hold still belongs to the close

// OPSAT main menu ([Gameplay] EnableOpsatCentering, with or without our entry): the game places it
// low in the panel (and ours makes it a row longer), so it is moved up to the middle of the panel (from the ruler to the bottom of the button bar, opsat_PC frame
// lines, Magma units), as if opsat_PC.mgb had it there. A widget's state is {vtable, colour, x, y}: the
// list keeps its live one at +0Ch, and the page keeps the file's two copies (faded in / shown) as
// keyframes that showing the OPSAT applies again. All three are moved, then the list is laid out.
static const int ST_MENU_PANEL_TOP = 116, ST_MENU_PANEL_BOTTOM = 368;
static const DWORD ST_FUNC_POS_SET = 0x10D25900;   // state: SetPosition(point*), vtable +18h

static int StKeysAt(const BYTE* q, const BYTE* list, DWORD vt, DWORD pos) {
    const DWORD* k = (const DWORD*)q;
    return k[0] == vt && k[1] == 0x00FFFFFF && k[2] == pos && k[3] == vt && k[4] == 0xFFFFFFFF &&
           k[5] == pos && q != list + 0xC;
}

static int StWritable(const MEMORY_BASIC_INFORMATION& mi) {
    return mi.State == MEM_COMMIT && (mi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)) &&
           !(mi.Protect & (PAGE_GUARD | PAGE_NOACCESS));
}

enum { ST_MAX_KEYS = 8 };

// Collects the OPSAT open/close keyframes in committed read-write memory in [lo, hi).
static int StScanKeys(BYTE* lo, BYTE* hi, BYTE* list, DWORD vt, DWORD pos, int privateOnly,
                      BYTE** out, int n) {
    for (BYTE* a = lo; a < hi && n < ST_MAX_KEYS;) {
        MEMORY_BASIC_INFORMATION mi;
        if (!VirtualQuery(a, &mi, sizeof mi)) break;
        BYTE* rb = (BYTE*)mi.BaseAddress;
        BYTE* re = rb + mi.RegionSize;
        if (re <= a) break;
        if (StWritable(mi) && (!privateOnly || mi.Type == MEM_PRIVATE)) {
            BYTE* s = rb < lo ? lo : rb;
            BYTE* e = re > hi ? hi : re;
            for (BYTE* q = (BYTE*)(((UINT_PTR)s + 3) & ~(UINT_PTR)3); q + 24 <= e; q += 4)
                if (*(DWORD*)q == vt && StKeysAt(q, list, vt, pos)) {
                    out[n++] = q;
                    if (n == ST_MAX_KEYS) break;
                    q += 20;
                }
        }
        a = re;
    }
    return n;
}

// The keyframes live in the heap, usually near the list, but where depends on the session's
// allocation order, and there can be more than one copy. All copies near the list are taken;
// only when there are none is the whole heap searched, and those hits are kept for next time.
static int StFindMenuKeys(BYTE* list, DWORD vt, DWORD pos, BYTE** out) {
    static BYTE* farKeys[ST_MAX_KEYS];
    static int farCount;
    BYTE* near0 = list > (BYTE*)0x4010000 ? list - 0x4000000 : (BYTE*)0x10000;
    BYTE* near1 = list < (BYTE*)0xFBFF0000 ? list + 0x4000000 : (BYTE*)0xFFFF0000;
    int n = StScanKeys(near0, near1, list, vt, pos, 0, out, 0);
    if (n) return n;
    for (int i = 0; i < farCount; i++) {
        MEMORY_BASIC_INFORMATION mi;
        if (VirtualQuery(farKeys[i], &mi, sizeof mi) && StWritable(mi) && StKeysAt(farKeys[i], list, vt, pos))
            out[n++] = farKeys[i];
    }
    if (n) return n;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    BYTE* top = (BYTE*)si.lpMaximumApplicationAddress;
    n = StScanKeys((BYTE*)si.lpMinimumApplicationAddress, near0, list, vt, pos, 1, out, 0);
    if (near1 < top) n = StScanKeys(near1, top, list, vt, pos, 1, out, n);
    for (farCount = 0; farCount < n; farCount++) farKeys[farCount] = out[farCount];
    return n;
}

static void StCenterMenu(BYTE* list) {
    BYTE* state = list + 0xC;
    DWORD* vt = *(DWORD**)state;
    if (!vt || vt[0x18 / 4] != VA(ST_FUNC_POS_SET)) return;
    short top = *(short*)(list + 0x94), bottom = *(short*)(list + 0x96);
    short x = *(short*)(state + 8), y = *(short*)(state + 0xA);
    int height = bottom - top;
    if (height <= 0 || top != y) return;
    short want = (short)((ST_MENU_PANEL_TOP + ST_MENU_PANEL_BOTTOM - height) / 2);
    if (want == y) return;
    DWORD pos = (WORD)x | ((DWORD)(WORD)y << 16);
    BYTE* keys[ST_MAX_KEYS];
    int n = StFindMenuKeys(list, (DWORD)vt, pos, keys);
    if (!n) return;                                  // without the keyframes the OPSAT would undo it
    for (int i = 0; i < n; i++) {
        *(short*)(keys[i] + 0xA) = want;
        *(short*)(keys[i] + 0x16) = want;
    }
    short pt[2] = {x, want};
    typedef void (__attribute__((thiscall)) *SetPosFn)(void*, const short*);
    ((SetPosFn)vt[0x18 / 4])(state, pt);
    ((LoVoidFn)VA(LO_FUNC_LIST_LAYOUT))(list);
}

// Buttons of P_MissionStatistics: B_Objectives (left) = OPSAT, B_NextMission (right) = EXIT,
// B_RetryMission hidden, ReplaceButton (middle panel) shown. How they were is kept for the real
// mission end (the file stays loaded; the texts are set again by every load).
enum { STB_LEFT, STB_RIGHT, STB_RETRY, STB_MIDDLE, ST_BUTTONS };
struct StButton { BYTE* node; BYTE vis; BYTE en; };
static StButton g_stButtons[ST_BUTTONS];
static int g_stButtonsSaved;

static const wchar_t* StMenuText() {
    static wchar_t name[64];
    LoLocText(L"MISSIONREVIEW", L"StatsTitle", L"Localization\\InGameMenus", L"MISSION STATISTICS", name, 64);
    return name;
}

static void StSetNode(BYTE* n, int vis, int en) {
    if (!n) return;
    n[0x18] = (BYTE)((n[0x18] & 0xFE) | (vis ? 1 : 0));
    ((StEnableFn)VA(ST_FUNC_NODE_ENABLE))(n, en);
}

static void StSetupButtons() {
    BYTE* cmr = (BYTE*)g_stCmr;
    if (!cmr) return;
    BYTE* page = *(BYTE**)(cmr + 0x38);
    if (!page) return;
    LoFindFn find = (LoFindFn)VA(LO_FUNC_FIND_CHILD);
    DWORD hObj = *(DWORD*)VA(ST_GLOBAL_B_OBJ), hNext = *(DWORD*)VA(ST_GLOBAL_B_NEXT);
    BYTE* nodes[ST_BUTTONS];
    nodes[STB_LEFT] = hObj ? (BYTE*)find(page, hObj) : 0;
    nodes[STB_RIGHT] = hNext ? (BYTE*)find(page, hNext) : 0;
    nodes[STB_RETRY] = *(BYTE**)(cmr + 0x54);
    nodes[STB_MIDDLE] = *(BYTE**)(cmr + 0x50);
    if (!g_stButtonsSaved) {
        for (int i = 0; i < ST_BUTTONS; i++) {
            BYTE* n = nodes[i];
            g_stButtons[i].node = n;
            g_stButtons[i].vis = n ? (n[0x18] & 1) : 0;
            g_stButtons[i].en = n ? ((n[0x4C] >> 1) & 1) : 0;
        }
        g_stButtonsSaved = 1;
    }
    StButtonTextFn text = (StButtonTextFn)VA(ST_FUNC_BUTTON_TEXT);
    if (hObj) text(page, hObj, L"Keys", L"K_Opsat", L"Localization\\PreGame_PC", 1);
    if (hNext) text(page, hNext, L"Computer", L"Exit", L"Localization\\PreGame_PC", 1);
    StSetNode(nodes[STB_LEFT], 1, 1);
    StSetNode(nodes[STB_RIGHT], 1, 1);
    StSetNode(nodes[STB_RETRY], 0, 0);
    if (nodes[STB_MIDDLE]) nodes[STB_MIDDLE][0x18] |= 1;
}

static void StRestoreButtons() {
    if (!g_stButtonsSaved) return;
    for (int i = 0; i < ST_BUTTONS; i++)
        StSetNode(g_stButtons[i].node, g_stButtons[i].vis, g_stButtons[i].en);
    g_stButtonsSaved = 0;
}

// Take the page off screen; the review itself stays until StClose (not safe from its own click).
static void StHidePage() {
    if (!g_stCmr || !g_stShown) return;
    ((StShowFn)VA(ST_FUNC_CMR_SHOW))(g_stCmr, 0, 1);
    g_stShown = 0;
}

static void StClose() {
    if (!g_stCmr) return;
    StHidePage();
    StRestoreButtons();
    ((StFreeFn)VA(ST_FUNC_CMR_FREE))(&g_stCmr);
    g_stCmr = 0;
    g_stCmrMgr = 0;
}

// A new level or a load: the old review went with the old Magma pages; forget it.
static void StForget() {
    g_stCmr = 0;
    g_stCmrMgr = 0;
    g_stOpsat = 0;
    g_stShown = 0;
    g_stButtonsSaved = 0;
}

// The level time the mission end gives the review: player controller -> Level (+7Ch) -> +C4h.
static int StLevelTime(BYTE* mgr, double* out) {
    void* players = *(void**)(mgr + 4);
    if (!players) return 0;
    BYTE* slot = (BYTE*)((LoGetFn)VA(ST_FUNC_PLAYER))(players);
    if (!slot || !*(void**)(slot + 0x64)) return 0;
    BYTE* pc = (BYTE*)((StPcFn)VA(ST_FUNC_PC))(*(void**)(slot + 0x64), mgr[0xC]);
    if (!pc || !*(void**)(pc + 0x5C0)) return 0;   // no mission manager: the fill would show nothing
    BYTE* level = *(BYTE**)(pc + 0x7C);
    if (!level) return 0;
    memcpy(out, level + 0xC4, sizeof *out);
    return 1;
}

static void StOpen(BYTE* opsat) {
    BYTE* mgr = *(BYTE**)(opsat + 0x18);
    if (!mgr) return;
    StClose();
    double t;
    if (!StLevelTime(mgr, &t)) return;
    ((StMakeFn)VA(ST_FUNC_CMR_MAKE))(&g_stCmr, mgr);
    BYTE* cmr = (BYTE*)g_stCmr;
    if (!cmr) return;
    if (!cmr[0x18] || !*(void**)(cmr + 0x38)) {     // mission_review_PC did not load
        ((StFreeFn)VA(ST_FUNC_CMR_FREE))(&g_stCmr);
        g_stCmr = 0;
        return;
    }
    g_stCmrMgr = mgr;
    g_stOpsat = opsat;
    memcpy(cmr + 0x64, &t, sizeof t);
    g_stSecond = (int)t;
    g_stShown = 1;
    ((StShowFn)VA(ST_FUNC_CMR_SHOW))(cmr, 1, 1);   // shows P_MissionStatistics; its listener fills it
    StSetupButtons();
}

// Called every frame (Present): refill once a second so the mission time runs.
static void Stats_OnPresent() {
    if (!g_stCmr) return;
    if (GameIsLoading()) { StForget(); return; }
    if (!g_stShown) return;
    BYTE* cmr = (BYTE*)g_stCmr;
    BYTE* mgr = g_stCmrMgr;
    if (!ObjPtrOk(cmr) || !ObjPtrOk(mgr) || *(DWORD*)cmr != VA(0x110E074C)) { StForget(); return; }
    // The OPSAT closed by a path that skips its toggle (its key, F1): the page goes with it.
    if (g_stOpsat && ObjPtrOk(g_stOpsat) && !g_stOpsat[0x1D]) { StClose(); return; }
    double t;
    if (!StLevelTime(mgr, &t) || (int)t == g_stSecond) return;
    g_stSecond = (int)t;
    memcpy(cmr + 0x64, &t, sizeof t);
    ((LoVoidFn)VA(ST_FUNC_CMR_FILL))(cmr);
    StSetupButtons();
}

// ---- hooks ---------------------------------------------------------------------------------------

extern "C" void StatsAddLast_Hook();
extern "C" void __cdecl OnStatsAddLast(void* list, const wchar_t* text, int data) {
    LoAddFn add = (LoAddFn)VA(LO_FUNC_LIST_ADD);
    add(list, text, data);
    if (!list) return;
    if (g_stActive) {
        if (g_stCmr) StForget();                    // a new level: the old page is gone
        g_stIndex = LoListCount(list);
        add(list, StMenuText(), 0);
        g_stList = list;
        int n = LoListCount(list);
        BYTE* maxRows = (BYTE*)list + LIST_MAX_ROWS;
        if (n > 0 && *maxRows < n) {
            *maxRows = (BYTE)n;
            ((LoVoidFn)VA(LO_FUNC_LIST_LAYOUT))(list);
        }
    }
    if (g_cfg.opsatCentering) StCenterMenu((BYTE*)list);
}

asm(
    ".text\n"
    ".globl _StatsAddLast_Hook\n"
    "_StatsAddLast_Hook:\n"             // ecx = Lst_Main_Menu, [esp+4] text, [esp+8] data
    "    pushl 8(%esp)\n"
    "    pushl 8(%esp)\n"
    "    pushl %ecx\n"
    "    call _OnStatsAddLast\n"
    "    addl $12, %esp\n"
    "    ret $8\n"
);

static char __attribute__((thiscall)) H_StClick(void* opsat, void* ev) {
    BYTE* o = (BYTE*)opsat;
    void* list = *(void**)(o + 0x40);
    if (ev && list && list == g_stList && g_stIndex >= 0 &&
        *(DWORD*)((BYTE*)ev + 4) == *(DWORD*)VA(ST_GLOBAL_MAINMENU) &&
        *(short*)((BYTE*)list + LIST_SELECTED) == g_stIndex) {
        StOpen(o);
        BYTE* mgr = *(BYTE**)(o + 0x18);
        if (mgr) mgr[0x14] = 1;                     // handled
        return 1;
    }
    return ((StClickFn)VA(ST_FUNC_CLICK))(opsat, ev);
}

// The review fills its stats page when the page is shown; set our buttons again after it.
static char __attribute__((thiscall)) H_StCmrShown(void* cmr, void* page) {
    char r = ((StShownFn)VA(ST_FUNC_CMR_SHOWN))(cmr, page);
    if (cmr && cmr == g_stCmr) StSetupButtons();
    return r;
}

// Clicks on our page: OPSAT = back to the menu, EXIT = close the OPSAT; nothing else (no retry /
// next mission / objectives). The review is freed later: this runs inside its own listener.
static char __attribute__((thiscall)) H_StCmrClick(void* sub, void* ev) {
    BYTE* cmr = (BYTE*)sub - 8;
    if (!g_stCmr || cmr != (BYTE*)g_stCmr || !ev) return ((StClickFn)VA(ST_FUNC_CMR_CLICK))(sub, ev);
    if (g_stCmrMgr) g_stCmrMgr[0x14] = 1;          // handled
    DWORD h = *(DWORD*)((BYTE*)ev + 4);
    if (h == *(DWORD*)VA(ST_GLOBAL_B_OBJ)) {
        StHidePage();
    } else if (h == *(DWORD*)VA(ST_GLOBAL_B_NEXT)) {
        StHidePage();
        if (g_stOpsat) ((StToggleFn)VA(ST_FUNC_TOGGLE))(g_stOpsat, 0);
    }
    return 0;
}

// The OPSAT key on our page asks to open the OPSAT again: like on the other OPSAT pages it closes
// everything instead.
static void __attribute__((thiscall)) H_StToggle(void* opsat, int open) {
    if ((open & 0xFF) && g_stCmr && g_stShown) {
        StClose();
        ((StToggleFn)VA(ST_FUNC_TOGGLE))(opsat, 0);
        return;
    }
    if (!(open & 0xFF)) StClose();
    ((StToggleFn)VA(ST_FUNC_TOGGLE))(opsat, open);
}

// Magma key events (H_LoMagmaKey): while the page is open, Esc / Backspace go back to the OPSAT.
// action: 1 press, 2 hold, 3 release. The release of that key is eaten too.
static int StHandleKey(DWORD key, DWORD action) {
    if (!g_cfg.opsatStatistics) return 0;
    if (g_stSwallowKey && key == g_stSwallowKey) {
        if (action != 1) {
            if (action == 3) g_stSwallowKey = 0;
            return 1;
        }
        g_stSwallowKey = 0;                           // a new press: the old release never came
    }
    if (action != 1 || !g_stCmr || !g_stShown) return 0;
    if (key != 0x1B && key != 0x08) return 0;
    StClose();
    g_stSwallowKey = key;
    return 1;
}

// The OPSAT menu hook serves both options; the rest only the statistics page.
static void ApplyStatsHooks() {
    if (!g_cfg.opsatStatistics && !g_cfg.opsatCentering) return;
    const BYTE call[1] = {0xE8};
    if (!Check(ST_SITE_ADD_LAST, call, 1) || Rel32Target(ST_SITE_ADD_LAST) != VA(LO_FUNC_LIST_ADD)) return;
    int stats = g_cfg.opsatStatistics &&
        *(DWORD*)VA(ST_VT_CLICK) == VA(ST_FUNC_CLICK) &&
        *(DWORD*)VA(ST_VT_CMR_SHOWN) == VA(ST_FUNC_CMR_SHOWN) &&
        *(DWORD*)VA(ST_VT_CMR_CLICK) == VA(ST_FUNC_CMR_CLICK);
    for (int k = 0; stats && k < 9; k++)
        if (!Check(ST_SITE_TOGGLE[k], call, 1) || Rel32Target(ST_SITE_TOGGLE[k]) != VA(ST_FUNC_TOGGLE))
            stats = 0;
    PatchRel32(ST_SITE_ADD_LAST, 0xE8, (void*)StatsAddLast_Hook);
    if (!stats) return;
    g_stActive = 1;
    for (int k = 0; k < 9; k++) PatchRel32(ST_SITE_TOGGLE[k], 0xE8, (void*)H_StToggle);
    DWORD fn = (DWORD)(void*)H_StClick;
    WriteMem(VA(ST_VT_CLICK), &fn, 4);
    fn = (DWORD)(void*)H_StCmrShown;
    WriteMem(VA(ST_VT_CMR_SHOWN), &fn, 4);
    fn = (DWORD)(void*)H_StCmrClick;
    WriteMem(VA(ST_VT_CMR_CLICK), &fn, 4);
}
