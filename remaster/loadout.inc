// Custom loadout ([Gameplay] EnableCustomLoadouts, CustomLoadoutMax*Amount).
// Adds CUSTOM to the mission briefing loadout list (loadout_single_PC). With it picked, the
// equipped-gear list on the same page edits the kit in two tabs; the last row switches tabs:
//   WEAPONS: SC pistol ammo, launcher, foregrip, sniper (attachment + ammo), shotgun
//            (attachment + ammo), wall mine; SC-20K ammo is the rifle panel above the list (B_f2000), which
//            shows the custom total (the game shows the reserve there)
//   GADGETS: sticky camera, sticky shocker, airfoil round, gas grenade, frag / flash / smoke grenade
//   left click = one more, right click = one less (magazines for ammo, on / off for attachments)
// Item and tab names come from the game's localization (Equipments), in capitals like the rest;
// texts the game has no string for (CUSTOM, the click hint) come from Remaster.ini
// [LocalizationKeys] <Key>.<lang>=<text> (UTF-8), for the current text language, else .int.
//   keys: PgUp / PgDn or numpad 8 / 2 = pick (rifle panel, items, tab switch),
//         Left / Right, numpad 4 / 6 or - / + = change it
// Ammo is the total carried. The game gives each weapon / fitted attachment one loaded magazine
// and adds the slot's magazines, so attachments get total - 1; SC-20K and pistol ammo is set
// exactly after the game's weapon setup (loaded clip + total), so it can go down to 0.
// The kit always starts as a copy of the mission's Stealth loadout; changes live in memory only
// (nothing is written to disk) and go back to Stealth whenever CUSTOM is picked again or another
// mission's briefing is shown. CUSTOM is stored as loadout type 7 in
// EMissionManager.m_eLoadoutType (saved with the game): the game reads it as Stealth, then the
// amounts are replaced in the one native that fills the 17 equipment slots, so the mission start
// gives the same kit.

static const DWORD LO_FUNC_LIST_ADD     = 0x10D2FC80;   // list AddItem(text, data), thiscall ret 8
static const DWORD LO_FUNC_LIST_LAYOUT  = 0x10D298A0;   // list relayout (rows from max-visible)
static const DWORD LO_FUNC_LIST_SETTOP  = 0x10D284F0;   // list first visible row
static const DWORD LO_FUNC_LIST_SETHOVER = 0x10D2B180;  // list hover row (restyles old / new row), thiscall(short)
static const DWORD LO_FUNC_LIST_SETPRESS = 0x10D2B210;  // list row under the mouse (+AAh), same shape
static const DWORD LO_FUNC_LIST_ROWSTYLE = 0x10D2A150;  // list: style one row from its state, thiscall(short)
static const DWORD LO_FUNC_SET_CHILD     = 0x10D5B850;  // widget: set text of child label(name, text)
static const DWORD LO_FUNC_MAGMA_ROOT    = 0x10AD4770;  // Magma manager -> screen root (+16h: mouse x, y shorts)
static const DWORD LO_FUNC_WEAPON_SETUP  = 0x10C27AB0;  // EMissionManager: rifle + pistol from the slots (pawn)
static const DWORD LO_SITE_WEAPON_SETUP[2] = {0x10C28090, 0x10C28218};
// weapons: pawn +560h SC-20K, +564h pistol; weapon +434h clip ammo, +438h clip size, +43Ch total ammo
static const DWORD LO_SITE_ADD_LAST     = 0x10B2C4A8;   // page init: third AddItem (LoadOutType[2])
static const DWORD LO_SITE_DISPLACE     = 0x10B2B45D;   // cmp ax,3: some maps drop entry 2 (Assault)
static const DWORD LO_VT_SELCHANGED     = 0x110DFED0;   // page listener slot: list selection
static const DWORD LO_FUNC_SELCHANGED   = 0x10B2BBC0;
static const DWORD LO_FUNC_APPLY        = 0x10B2BB60;   // clear inventory, give [page+28h], refresh
static const DWORD LO_FUNC_REFRESH      = 0x10B2B8B0;   // fill B_f2000 + Lst_equiped, thiscall ret 8
static const DWORD LO_SITE_REFRESH[4]   = {0x10B2BBAF, 0x10B2BE52, 0x10B2BEAC, 0x10B2BF14};
static const DWORD LO_SITE_FILL         = 0x10C28F09;   // EMissionManager: fill slots from section
static const DWORD LO_FUNC_FILL         = 0x10C28680;
static const DWORD LO_SITE_KEY          = 0x10A0E7C1;   // SCMagmaInteraction KeyEvent: call [edx+DCh]
static const DWORD LO_FUNC_CRC          = 0x10D233A0;   // Magma name hash (CRC32), cdecl(out*, name)
static const DWORD LO_FUNC_FIND_CHILD   = 0x10D30670;   // widget child by hash, thiscall
static const DWORD LO_FUNC_TEXT_A       = 0x10AD8C90;   // SetText (type 8 widgets)
static const DWORD LO_FUNC_TEXT_B       = 0x10AD8CC0;   // SetText (types 0, 2, 3, 4)
static const DWORD LO_FUNC_SET_CELL     = 0x10D2DCA0;   // list SetCellText(row, col, text)
static const DWORD LO_FUNC_PAGE_SHOWN   = 0x10AD58D0;   // Magma: page visible(name)
static const DWORD LO_FUNC_LOCALIZE     = 0x10CCC4B0;   // Localize(sec, key, pkg, lang, opt, path)
static const DWORD LO_GLOBAL_SELEVENT   = 0x1122D5B8;   // list selection event class

// Magma list fields
static const int LIST_ITEMS_BEGIN = 0x6C, LIST_ITEMS_END = 0x70, LIST_ITEM_SIZE = 0x3C;
static const int LIST_TOP = 0xA2, LIST_MAX_ROWS = 0xA6, LIST_SELECTED = 0xA8, LIST_HOVER = 0xAC;
static const int LIST_FLAGS = 0xB4, LIST_FLAG_HOVER = 0x02;   // track the row under the mouse

static const int LO_TYPE_CUSTOM  = 7;   // unused ELOADOUTTYPE value
static const int LO_TYPE_STEALTH = 1;
static const int LO_OFF_STARTUP  = 0x30;   // StartupAttach: 1 Foregrip, 2 Launcher, 3 Sniper, 4 Shotgun
static const int LO_OFF_TYPE     = 0x84;   // m_eLoadoutType

typedef void (__attribute__((thiscall)) *LoAddFn)(void*, const wchar_t*, int);
typedef char (__attribute__((thiscall)) *LoSelFn)(void*, void*);
typedef void (__attribute__((thiscall)) *LoVoidFn)(void*);
typedef void (__attribute__((thiscall)) *LoTopFn)(void*, int);
typedef void* (__attribute__((thiscall)) *LoGetFn)(void*);
typedef void (__attribute__((thiscall)) *LoChildFn)(void*, const char*, const wchar_t*);
typedef void (__attribute__((thiscall)) *LoPtrFn)(void*, void*);
typedef void (__attribute__((thiscall)) *LoRefreshFn)(void*, void*, void*);
typedef int  (__attribute__((thiscall)) *LoFillFn)(void*, const wchar_t*);
typedef DWORD (__attribute__((thiscall)) *LoKeyFn)(void*, DWORD, DWORD, DWORD);
typedef void* (__attribute__((thiscall)) *LoFindFn)(void*, DWORD);
typedef int  (__attribute__((thiscall)) *LoTypeFn)(void*);
typedef void (__attribute__((thiscall)) *LoTextFn)(void*, const wchar_t*);
typedef void (__attribute__((thiscall)) *LoCellFn)(void*, int, int, const wchar_t*);
typedef char (__attribute__((thiscall)) *LoShownFn)(void*, const char*);
typedef DWORD* (__cdecl *LoCrcFn)(DWORD*, const char*);
typedef const wchar_t* (__cdecl *LoLocFn)(const wchar_t*, const wchar_t*, const wchar_t*,
                                          const wchar_t*, int, int);

// Item groups: ammo amounts are shown and limited in rounds (clip sizes from the game classes).
enum { LG_RIFLE, LG_PISTOL, LG_SHOTGUN, LG_SNIPER, LG_ATTACH, LG_GADGET, LG_GRENADE, LG_MINE };
static const int kLoClip[] = {30, 20, 7, 6, 1, 1, 1, 1};

// EMissionManager equipment slots, as the fill native reads them from Equipments.ini.
struct LoItem { const char* key; const wchar_t* sec; int off; int group; };   // key: Equipments.ini name
static const LoItem kLoItems[] = {
    {"AmmoClipSC20k",    L"FN2000AMMO",       0x44, LG_RIFLE},
    {"AmmoClip57",       L"FN7AMMO",          0x48, LG_PISTOL},
    {"AmmoClipShotgun",  L"SHOTGUNAMMO",      0x4C, LG_SHOTGUN},
    {"AmmoClipSniper",   L"SCOPEAMMO",        0x50, LG_SNIPER},
    {"AttachForegrip",   L"FOREGRIP",         0x34, LG_ATTACH},
    {"AttachLauncher",   L"LAUNCHER",         0x38, LG_ATTACH},
    {"AttachSniper",     L"SCOPE",            0x3C, LG_ATTACH},
    {"AttachShotgun",    L"SHOTGUN",          0x40, LG_ATTACH},
    {"StickyCamera",     L"STICKYCAMERA",     0x54, LG_GADGET},
    {"StickyShocker",    L"STICKYSHOCKER",    0x58, LG_GADGET},
    {"RingAirfoilRound", L"RINGAIRFOILROUND", 0x60, LG_GADGET},
    {"GazGrenade",       L"GAZGRENADE",       0x5C, LG_GADGET},    // launcher round
    {"FragGrenade",      L"FRAGGRENADE",      0x64, LG_GRENADE},
    {"FlashGrenade",     L"FLASHGRENADE",     0x68, LG_GRENADE},
    {"SmokeGrenade",     L"SMOKEGRENADE",     0x6C, LG_GRENADE},
    {"WallMine",         L"WALLMINE",         0x70, LG_MINE},
};
enum { LO_N = sizeof kLoItems / sizeof kLoItems[0] };

// Editor rows. Lst_equiped shows 8 rows under the rifle panel (B_f2000); each tab fills them.
// Ammo is shown and set as the total the weapon carries: the game gives every weapon and fitted
// attachment one loaded magazine and adds the slot's magazines on top, so slot = total - 1.
enum { LE_BLANK, LE_TAB, LE_AMMO, LE_ATTACH, LE_ATTACH_AMMO, LE_COUNT };
struct LoEntry { int kind; int item; int ammo; const wchar_t* nameSec; };
// item: kLoItems index; ammo: for LE_ATTACH_AMMO; nameSec: Equipments section for the name (0 = item's)
enum { LO_TABS = 2, LO_ROWS = 8 };
static const LoEntry kLoRifle = {LE_AMMO, 0, -1, 0};   // SC-20K ammo: the rifle panel
static const LoEntry kLoTab[LO_TABS][LO_ROWS] = {
    {   // WEAPONS
        {LE_AMMO, 1, -1, L"FN7"},    // pistol ammo, named SC PISTOL like the other loadouts
        {LE_ATTACH, 5, -1},          // launcher
        {LE_ATTACH, 4, -1},          // foregrip
        {LE_ATTACH_AMMO, 6, 3},      // sniper attachment + its ammo
        {LE_ATTACH_AMMO, 7, 2},      // shotgun attachment + its ammo
        {LE_COUNT, 15, -1},          // wall mine
        {LE_BLANK, 0, -1},
        {LE_TAB, 1, -1},             // -> GADGETS
    },
    {   // GADGETS
        {LE_COUNT, 8, -1},           // sticky camera
        {LE_COUNT, 9, -1},           // sticky shocker
        {LE_COUNT, 10, -1},          // ring airfoil round
        {LE_COUNT, 11, -1},          // gas grenade
        {LE_COUNT, 12, -1},          // frag grenade
        {LE_COUNT, 13, -1},          // flash grenade
        {LE_COUNT, 14, -1},          // smoke grenade
        {LE_TAB, 0, -1},             // -> WEAPONS
    },
};

static int   g_loVal[LO_N];          // custom slot value (magazines / count), -1 = Stealth amount
static int   g_loDef[LO_N];          // mission Stealth amount, captured on each custom fill
static wchar_t g_loMap[64];          // map of the briefing the custom kit belongs to
static int   g_loTabNow;             // tab shown
static int   g_loSel[LO_TABS] = {-1, -1};   // keyboard target in each tab: a row, or LO_SEL_RIFLE
enum { LO_SEL_RIFLE = -1 };
static int   g_loStartup = 4;        // item of the attachment the rifle starts on (panel text)
static void* g_loList;               // Lst_Menu holding the CUSTOM entry
static void* g_loPage;               // loadout page seen with CUSTOM active
static DWORD g_loPageVt;
static short g_loHover = -1;         // list row last clicked: kept lit across the redraw
static int g_loLastAttach = -1;      // attachment switched on most recently

static int LoGroupMax(int g) {
    switch (g) {
        case LG_RIFLE:   return g_cfg.customMaxRifleAmmo;
        case LG_PISTOL:  return g_cfg.customMaxPistolAmmo;
        case LG_SHOTGUN: return g_cfg.customMaxShotgunAmmo;
        case LG_SNIPER:  return g_cfg.customMaxSniperAmmo;
        case LG_GADGET:  return g_cfg.customMaxGadgets;
        case LG_GRENADE: return g_cfg.customMaxGrenades;
        case LG_MINE:    return g_cfg.customMaxMines;
        default:         return 1;
    }
}

// SC-20K / pistol ammo: kept as the total magazines (set exactly after the weapon setup).
static int LoIsTotal(int i) { int g = kLoItems[i].group; return g == LG_RIFLE || g == LG_PISTOL; }
// Attachment ammo: kept as the slot value, magazines on top of the loaded one.
static int LoIsExtra(int i) { int g = kLoItems[i].group; return g == LG_SHOTGUN || g == LG_SNIPER; }

static int LoMaxUnits(int i) {
    int g = kLoItems[i].group;
    if (g < LG_RIFLE || g > LG_MINE) return 0;
    if (g == LG_ATTACH) return 1;
    int m = LoGroupMax(g) / kLoClip[g];
    return LoIsExtra(i) ? (m > 0 ? m - 1 : 0) : m;
}

// Back to a plain copy of the Stealth loadout.
static void LoReset() {
    for (int i = 0; i < LO_N; i++) g_loVal[i] = -1;
    g_loLastAttach = -1;
}

static int LoEffective(int i) {
    int def = LoIsTotal(i) ? g_loDef[i] + 1 : g_loDef[i];   // Stealth: loaded + slot magazines
    int v = g_loVal[i] >= 0 ? g_loVal[i] : def;
    if (v < 0) v = 0;
    if (v > LoMaxUnits(i)) v = LoMaxUnits(i);
    return v;
}

static const wchar_t* LoLocalize(const wchar_t* sec, const wchar_t* key, const wchar_t* pkg) {
    const wchar_t* s = ((LoLocFn)VA(LO_FUNC_LOCALIZE))(sec, key, pkg, 0, 1, 1);
    return (s && s[0] && s[0] != L'<') ? s : 0;
}

// Localized text in capitals (as the game shows item names on this page); fallback if missing.
// Magma text for pol / svk / hun / rus / cze is not Unicode: the localization files and the
// custom fonts keep those letters in single-byte cells (CP1250 / CP1251 bytes read as CP1252,
// cze letters painted over Western cells; overlay_text.inc decodes it for the subtitles).
// Real Unicode text (Remaster.ini) has to be put into the same cells to show.
static UINT OverlayCpForLang(const char* lang);                    // overlay_text.inc
static void OverlayFixWide(wchar_t* s, int n, const char* lang);   // overlay_text.inc: Magma -> Unicode
static void EffectiveTextLanguage(char* out, int n);               // lang_fallback.inc

static int LoMagmaCellsLang(const char* lang) {
    UINT cp = OverlayCpForLang(lang);
    return cp == 1250 || cp == 1251 || !_stricmp(lang, "cze");
}

static wchar_t LoCzeCell(wchar_t c) {   // inverse of OverlayMapCze
    switch (c) {
        case 0x017D: return 0x00C2; case 0x017E: return 0x00E2;   // Ž ž
        case 0x010C: return 0x00C3; case 0x010D: return 0x00E3;   // Č č
        case 0x016E: return 0x00C5; case 0x016F: return 0x00E5;   // Ů ů
        case 0x0160: return 0x00D4; case 0x0161: return 0x00F4;   // Š š
        case 0x011A: return 0x00D5; case 0x011B: return 0x00F5;   // Ě ě
        case 0x0158: return 0x00DB; case 0x0159: return 0x00FB;   // Ř ř
        case 0x0147: return 0x00D1; case 0x0148: return 0x00F1;   // Ň ň
        case 0x010E: return 0x00D6; case 0x010F: return 0x00FC;   // Ď ď
        default: return c;
    }
}

// Unicode -> the cells the current language's Magma font uses.
static void LoToMagmaCells(wchar_t* s, int cap) {
    char lang[8] = {};
    EffectiveTextLanguage(lang, sizeof lang);
    if (!LoMagmaCellsLang(lang)) return;
    if (!_stricmp(lang, "cze")) {
        for (wchar_t* p = s; *p; p++) *p = LoCzeCell(*p);
        return;
    }
    UINT cp = OverlayCpForLang(lang);
    char bytes[512];
    int n = WideCharToMultiByte(cp, 0, s, -1, bytes, sizeof bytes, 0, 0);
    if (n <= 0) return;
    int k = 0;
    for (int i = 0; bytes[i] && k < cap - 1; i++) {
        unsigned char b = (unsigned char)bytes[i];
        wchar_t c = b;
        if (b >= 0x80 && b <= 0x9F && cp == 1250) MultiByteToWideChar(1252, 0, bytes + i, 1, &c, 1);
        s[k++] = c;
    }
    s[k] = 0;
}

// Capitals for Magma text: back to Unicode first where the font uses byte cells (else the
// cells for ł, ż, ą... would stay lower case), then into the cells again.
static void LoMagmaUpper(wchar_t* s, int cap) {
    char lang[8] = {};
    EffectiveTextLanguage(lang, sizeof lang);
    if (LoMagmaCellsLang(lang)) OverlayFixWide(s, cap, lang);
    CharUpperBuffW(s, (DWORD)wcslen(s));
    LoToMagmaCells(s, cap);
}

static void LoLocText(const wchar_t* sec, const wchar_t* key, const wchar_t* pkg, const wchar_t* fallback,
                      wchar_t* out, int cap) {
    const wchar_t* t = LoLocalize(sec, key, pkg);
    wcsncpy(out, t ? t : fallback, cap - 1);
    out[cap - 1] = 0;
    for (int k = (int)wcslen(out) - 1; k >= 0 && out[k] == L' '; k--) out[k] = 0;
    LoMagmaUpper(out, cap);
}

// [LocalizationKeys]: <Key>.<lang>=<text>, read once at startup (UTF-8, or the ANSI code page).
struct LoLocKey { char key[48]; wchar_t text[128]; };
static LoLocKey g_loKeys[64];
static int g_loKeyN;

// Store one "key=value" line of [LocalizationKeys] (value bytes UTF-8, else the ANSI code page).
static void LoAddLocKey(const char* line, int len) {
    while (len > 0 && (*line == ' ' || *line == '\t')) { line++; len--; }
    while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t' || line[len - 1] == '\r')) len--;
    if (len <= 0 || *line == ';' || *line == '#' || g_loKeyN >= 64) return;
    const char* eq = (const char*)memchr(line, '=', len);
    if (!eq || eq == line) return;
    LoLocKey& k = g_loKeys[g_loKeyN];
    int kl = (int)(eq - line);
    while (kl > 0 && (line[kl - 1] == ' ' || line[kl - 1] == '\t')) kl--;
    if (kl <= 0 || kl >= (int)sizeof k.key) return;
    memcpy(k.key, line, kl);
    k.key[kl] = 0;
    const char* v = eq + 1;
    int vl = len - (int)(v - line);
    while (vl > 0 && (*v == ' ' || *v == '\t')) { v++; vl--; }
    if (vl >= 2 && v[0] == '"' && v[vl - 1] == '"') { v++; vl -= 2; }
    if (vl <= 0) return;
    int m = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, v, vl, k.text, 127);
    if (m <= 0) m = MultiByteToWideChar(CP_ACP, 0, v, vl, k.text, 127);
    if (m <= 0) return;
    k.text[m] = 0;
    g_loKeyN++;
}

// Read [LocalizationKeys] straight from the file: the Windows INI functions pass a file without
// a BOM through the ANSI code page, which breaks UTF-8 bytes such as the second byte of Ł or Ę.
// UTF-8 (with or without BOM) and UTF-16 LE files are understood.
static void LoLoadLocKeys(const char* ini) {
    g_loKeyN = 0;
    FILE* f = fopen(ini, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > (1 << 20)) { fclose(f); return; }
    char* raw = (char*)malloc(size + 2);
    if (!raw) { fclose(f); return; }
    size_t got = fread(raw, 1, size, f);
    fclose(f);
    raw[got] = raw[got + 1] = 0;
    char* text = raw;
    char* conv = 0;
    long n = (long)got;
    if (n >= 2 && (unsigned char)raw[0] == 0xFF && (unsigned char)raw[1] == 0xFE) {   // UTF-16 LE
        int wn = (int)(n - 2) / 2;
        int cb = WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)(raw + 2), wn, 0, 0, 0, 0);
        conv = (char*)malloc(cb + 1);
        if (!conv) { free(raw); return; }
        WideCharToMultiByte(CP_UTF8, 0, (const wchar_t*)(raw + 2), wn, conv, cb, 0, 0);
        conv[cb] = 0;
        text = conv;
        n = cb;
    } else if (n >= 3 && (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB &&
               (unsigned char)raw[2] == 0xBF) {
        text += 3;
        n -= 3;
    }
    int inSection = 0;
    const char* end = text + n;
    for (const char* p = text; p < end;) {
        const char* e = p;
        while (e < end && *e != '\n') e++;
        const char* q = p;
        while (q < e && (*q == ' ' || *q == '\t')) q++;
        if (q < e && *q == '[') {
            const char* c = (const char*)memchr(q, ']', e - q);
            inSection = c && (c - q - 1) == 16 && !_strnicmp(q + 1, "LocalizationKeys", 16);
        } else if (inSection) {
            LoAddLocKey(p, (int)(e - p));
        }
        p = e < end ? e + 1 : e;
    }
    free(conv);
    free(raw);
}

// Text for <name> in the current text language, else English (.int), else 0.
static const wchar_t* LoKeyText(const char* name) {
    char lang[8] = {};
    EffectiveTextLanguage(lang, sizeof lang);
    const char* tries[2] = {lang, "int"};
    for (int t = 0; t < 2; t++) {
        if (!tries[t][0]) continue;
        char want[64];
        snprintf(want, sizeof want, "%s.%s", name, tries[t]);
        for (int i = 0; i < g_loKeyN; i++)
            if (!_stricmp(g_loKeys[i].key, want)) return g_loKeys[i].text;
    }
    return 0;
}

static const wchar_t* LoCustomName() {
    static wchar_t name[64];   // read each time: the language can change in the options
    const wchar_t* ini = LoKeyText("CustomLoadout");
    if (ini) {
        wcsncpy(name, ini, 63);
        name[63] = 0;
        CharUpperBuffW(name, (DWORD)wcslen(name));
        LoToMagmaCells(name, 64);
    } else {
        LoLocText(L"LOADOUT", L"LoadOutType[5]", L"Localization\\PreGameMenus", L"CUSTOM", name, 64);
    }
    return name;
}

// Tab names: WEAPONS = "<SC-20K> / <SC PISTOL>", GADGETS = the gadget category, both localized.
static void LoTabName(int tab, wchar_t* out, int cap) {
    if (tab == 0) {
        wchar_t a[48], b[48];
        LoLocText(L"FN2000", L"NameShort", L"Localization\\Equipments", L"SC-20K", a, 48);
        LoLocText(L"FN7", L"NameShort", L"Localization\\Equipments", L"SC PISTOL", b, 48);
        _snwprintf(out, cap, L"%ls / %ls", a, b);
        out[cap - 1] = 0;
    } else {
        LoLocText(L"CATEGORIES", L"Category[3]", L"Localization\\Equipments", L"GADGETS", out, cap);
    }
}

static void LoItemName(int i, wchar_t* out, int cap) {
    LoLocText(kLoItems[i].sec, L"Name", L"Localization\\Equipments", L"?", out, cap);
}

// The rifle holds two attachments: it spawns them in Foregrip, Launcher, Sniper, Shotgun order
// and drops the rest (0x10BF7360 gives up once the current and alternate slots are taken).
enum { LO_ATTACH_FIRST = 4, LO_ATTACH_COUNT = 4, LO_ATTACH_MAX = 2 };

static int LoAttachOn(int skip) {
    int n = 0;
    for (int i = LO_ATTACH_FIRST; i < LO_ATTACH_FIRST + LO_ATTACH_COUNT; i++)
        if (i != skip && LoEffective(i)) n++;
    return n;
}

// Items (kLoItems): 4 foregrip, 5 launcher, 6 sniper, 7 shotgun; 8..11 launcher rounds.
enum { LO_FOREGRIP = 4, LO_LAUNCHER = 5, LO_SNIPER = 6, LO_SHOTGUN = 7 };
static int LoIsLauncherRound(int i) { return i >= 8 && i <= 11; }   // sticky cam / shocker, airfoil, gas
static int LoLauncherOn() { return LoEffective(LO_LAUNCHER) != 0; }

// Which fitted attachment to switch off to make room (never `keep`):
// the launcher only ever goes by hand; the foregrip goes first; sniper and shotgun are equal,
// so of the two the one switched on earlier goes.
static int LoAttachToDrop(int keep) {
    if (keep != LO_FOREGRIP && LoEffective(LO_FOREGRIP)) return LO_FOREGRIP;
    int s = keep != LO_SNIPER && LoEffective(LO_SNIPER);
    int g = keep != LO_SHOTGUN && LoEffective(LO_SHOTGUN);
    if (s && g) return g_loLastAttach == LO_SNIPER ? LO_SHOTGUN : LO_SNIPER;
    if (s) return LO_SNIPER;
    if (g) return LO_SHOTGUN;
    return -1;
}

// Switching an attachment on with two already fitted switches one off (LoAttachToDrop).
static void LoMakeRoomFor(int item) {
    while (LoAttachOn(item) >= LO_ATTACH_MAX) {
        int drop = LoAttachToDrop(item);
        if (drop < 0) return;
        g_loVal[drop] = 0;
    }
}

// More than two fitted (a Stealth loadout never has that): drop by the same priority.
static void LoLimitAttachments() {
    while (LoAttachOn(-1) > LO_ATTACH_MAX) {
        int drop = LoAttachToDrop(-1);
        if (drop < 0) return;
        g_loVal[drop] = 0;
    }
}

static void LoSetAttach(int i, int on) {
    if (on && !LoEffective(i)) { LoMakeRoomFor(i); g_loLastAttach = i; }
    g_loVal[i] = on ? 1 : 0;
}

// Entry value in clicks: magazines carried (ammo), 0/1 (attachment), 0 = off / n magazines
// (attachment with ammo), count (gadgets).
static int LoEntryUnits(const LoEntry& e) {
    switch (e.kind) {
        case LE_AMMO:        return LoEffective(e.item);
        case LE_ATTACH:      return LoEffective(e.item);
        case LE_ATTACH_AMMO: return LoEffective(e.item) ? LoEffective(e.ammo) + 1 : 0;
        case LE_COUNT:       return LoEffective(e.item);
        default:             return 0;
    }
}

static int LoEntryMin(const LoEntry&) { return 0; }

static int LoEntryMax(const LoEntry& e) {
    switch (e.kind) {
        case LE_AMMO:        return LoMaxUnits(e.item);
        case LE_ATTACH:      return 1;
        case LE_ATTACH_AMMO: return LoMaxUnits(e.ammo) + 1;
        case LE_COUNT:       return LoMaxUnits(e.item);
        default:             return 0;
    }
}

static void LoEntrySet(const LoEntry& e, int u) {
    switch (e.kind) {
        case LE_AMMO:   g_loVal[e.item] = u; break;
        case LE_ATTACH: LoSetAttach(e.item, u); break;
        case LE_ATTACH_AMMO:
            LoSetAttach(e.item, u > 0);
            if (u > 0) g_loVal[e.ammo] = u - 1;
            break;
        case LE_COUNT:  g_loVal[e.item] = u; break;
    }
}

// Rough width of capitals text in the list font, in spaces (measured on the briefing page:
// a space is ~0.5 of a capital; widths scale with the resolution, so the ratio holds).
static float LoTextSpaces(const wchar_t* t) {
    float w = 0;
    for (; *t; t++) {
        wchar_t c = *t;
        if (c == L' ') w += 1.0f;
        else if (c == L'I' || c == L'1' || c == L'.' || c == L',' || c == L'\'' || c == L'!') w += 0.9f;
        else if (c == L'[' || c == L']' || c == L'(' || c == L')') w += 0.9f;
        else if (c == L'M' || c == L'W' || c == L'@') w += 2.3f;
        else if (c == L'/' || c == L'-') w += 1.25f;
        else if (c == L'<' || c == L'>') w += 1.6f;
        else if (c >= L'0' && c <= L'9') w += 1.8f;
        else w += 1.97f;
    }
    return w;
}

// Pad with spaces so the text sits in the middle of a list row (row text starts after the
// two-character marker; the row's middle is ~57 spaces further on).
static void LoCenterInRow(const wchar_t* text, wchar_t* out, int cap) {
    const float kHalfRow = 57.1f;
    int n = (int)(kHalfRow - LoTextSpaces(text) / 2 + 0.5f);
    if (n < 0) n = 0;
    if (n > cap - 1 - (int)wcslen(text)) n = cap - 1 - (int)wcslen(text);
    if (n < 0) n = 0;
    for (int k = 0; k < n; k++) out[k] = L' ';
    wcsncpy(out + n, text, cap - 1 - n);
    out[cap - 1] = 0;
}

static void LoEntryName(const LoEntry& e, wchar_t* out, int cap) {
    if (e.kind == LE_TAB) {
        wchar_t t[96], line[112];
        LoTabName(e.item, t, 96);
        _snwprintf(line, 112, e.item ? L"[ >> %ls ]" : L"[ << %ls ]", t);
        line[111] = 0;
        LoCenterInRow(line, out, cap);
    } else if (e.kind == LE_BLANK) {
        out[0] = 0;
    } else if (e.nameSec) {
        LoLocText(e.nameSec, L"Name", L"Localization\\Equipments", L"?", out, cap);
    } else {
        LoItemName(e.item, out, cap);
    }
}

// ON / OFF as the settings menu shows them (PreGame_PC [MenuSettings]), in the current language.
static void LoOnOff(int on, wchar_t* out, int cap) {
    LoLocText(L"MenuSettings", on ? L"ON" : L"OFF", L"Localization\\PreGame_PC", on ? L"ON" : L"OFF", out, cap);
}

static void LoEntryValue(const LoEntry& e, wchar_t* out, int cap) {
    int u = LoEntryUnits(e);
    out[0] = 0;
    switch (e.kind) {
        case LE_AMMO:        _snwprintf(out, cap, L"%.2i", u * kLoClip[kLoItems[e.item].group]); break;
        case LE_ATTACH:      LoOnOff(u, out, cap); break;
        case LE_ATTACH_AMMO:
            if (u) _snwprintf(out, cap, L"%.2i", u * kLoClip[kLoItems[e.ammo].group]);
            else LoOnOff(0, out, cap);
            break;
        case LE_COUNT:
            if (LoIsLauncherRound(e.item) && !LoLauncherOn()) LoOnOff(0, out, cap);   // no launcher fitted
            else _snwprintf(out, cap, L"%.2i", u);
            break;
        default: break;
    }
    out[cap - 1] = 0;
}

static int LoListCount(void* list) {
    BYTE* b = *(BYTE**)((BYTE*)list + LIST_ITEMS_BEGIN);
    BYTE* e = *(BYTE**)((BYTE*)list + LIST_ITEMS_END);
    return b ? (int)((e - b) / LIST_ITEM_SIZE) : 0;
}

static int LoPageOk(void* page) {
    return page && ObjPtrOk(page) && *(DWORD*)page == g_loPageVt;
}

static void* LoGrid() {
    void* g = *(void**)((BYTE*)g_loPage + 0x3C);   // Lst_equiped
    return (g && ObjPtrOk(g)) ? g : 0;
}

static void LoDirty() {
    void* mgr = *(void**)((BYTE*)g_loPage + 0x10);
    if (mgr && ObjPtrOk(mgr)) *((BYTE*)mgr + 0x14) = 1;
}

static int LoEditorActive() {
    if (!g_cfg.enableCustomLoadouts || !LoPageOk(g_loPage)) return 0;
    BYTE* p = (BYTE*)g_loPage;
    if (*(int*)(p + 0x28) != LO_TYPE_CUSTOM) return 0;
    void* mgr = *(void**)(p + 0x10);
    return mgr && ObjPtrOk(mgr) && ((LoShownFn)VA(LO_FUNC_PAGE_SHOWN))(mgr, "P_LoadOut");
}

// A briefing for another mission starts the custom kit over (page +1Ch: map name FString).
// Only checked while the briefing page is up, never at mission start.
static void LoCheckMap(void* page) {
    const wchar_t* map = *(const wchar_t**)((BYTE*)page + 0x1C);
    if (!map || IsBadReadPtr(map, 2)) return;
    if (wcsncmp(map, g_loMap, 63) == 0) return;
    wcsncpy(g_loMap, map, 63);
    g_loMap[63] = 0;
    LoReset();
}

// L_LoadOutType shows the picked loadout's name.
static void LoSetTypeLabel(void* page, const wchar_t* text) {
    void* root = *(void**)((BYTE*)page + 0x2C);
    if (!root || !ObjPtrOk(root)) return;
    DWORD h = 0;
    ((LoCrcFn)VA(LO_FUNC_CRC))(&h, "L_LoadOutType");
    void* node = ((LoFindFn)VA(LO_FUNC_FIND_CHILD))(root, h);
    if (!node || !ObjPtrOk(node)) return;
    void* w = *(void**)((BYTE*)node + 8);
    if (!w || !ObjPtrOk(w)) return;
    int type = ((LoTypeFn)(*(DWORD**)w)[3])(w);
    if (type == 8) ((LoTextFn)VA(LO_FUNC_TEXT_A))(w, text);
    else if (type == 0 || type == 2 || type == 3 || type == 4) ((LoTextFn)VA(LO_FUNC_TEXT_B))(w, text);
}

static void LoSetTitle(void* page) {
    wchar_t t[160];
    const wchar_t* ini = LoKeyText("CustomLoadoutHint");
    wchar_t hint[128];
    wcsncpy(hint, ini ? ini : L"LEFT CLICK: MORE   RIGHT CLICK: LESS", 127);
    hint[127] = 0;
    LoToMagmaCells(hint, 128);
    _snwprintf(t, 160, L"%ls   -   %ls", LoCustomName(), hint);
    t[159] = 0;
    LoSetTypeLabel(page, t);
}

// Put the list's light back on a row: its own setters restyle the old and the new row.
// row -1 = no row lit.
static void LoLightRow(void* grid, short row) {
    ((LoTopFn)VA(LO_FUNC_LIST_SETPRESS))(grid, row);
    ((LoTopFn)VA(LO_FUNC_LIST_SETHOVER))(grid, row);
    if (row >= 0) ((LoTopFn)VA(LO_FUNC_LIST_ROWSTYLE))(grid, row);   // rows rebuilt by the refresh start unstyled
}

// Rifle panel: the game fills it with the rifle, its attachments and the SC-20K reserve; show
// the custom total instead, and the keyboard marker. Same labels the game writes (0x10B2B4F0).
static void LoDrawPanel() {
    void* panel = *(void**)((BYTE*)g_loPage + 0x40);   // B_f2000
    if (!panel || !ObjPtrOk(panel)) return;
    wchar_t text[200], part[64], num[16];
    wcscpy(text, g_loSel[g_loTabNow] == LO_SEL_RIFLE ? L"> " : L"  ");   // indented like the rows
    LoLocText(L"FN2000", L"Name", L"Localization\\Equipments", L"SC-20K", part, 64);
    wcscat(text, part);
    int first = 1;
    for (int pass = 0; pass < 2; pass++)          // the attachment it starts on first, like the game
        for (int i = LO_ATTACH_FIRST; i < LO_ATTACH_FIRST + LO_ATTACH_COUNT; i++) {
            if (!LoEffective(i) || (pass == 0) != (i == g_loStartup)) continue;
            LoItemName(i, part, 64);
            if (wcslen(text) + wcslen(part) + 4 >= 200) continue;
            wcscat(text, first ? L" " : L" / ");
            wcscat(text, part);
            first = 0;
        }
    _snwprintf(num, 16, L"%.2i", LoEffective(0) * kLoClip[LG_RIFLE]);
    num[15] = 0;
    LoChildFn set = (LoChildFn)VA(LO_FUNC_SET_CHILD);
    set(panel, "l_ITEM", text);
    set(panel, "l_total2", num);                    // the SC-20K ammo box
}

// Lst_equiped as the editor: the LO_ROWS rows of the current tab.
static void LoDraw() {
    void* grid = LoGrid();
    if (grid) {
        LoAddFn add = (LoAddFn)VA(LO_FUNC_LIST_ADD);
        for (int n = LoListCount(grid); n < LO_ROWS; n++) add(grid, L"", 0);
        int count = LoListCount(grid);
        if (count >= LO_ROWS) {
            *((BYTE*)grid + LIST_FLAGS) |= LIST_FLAG_HOVER;
            LoCellFn cell = (LoCellFn)VA(LO_FUNC_SET_CELL);
            for (int r = 0; r < LO_ROWS; r++) {
                const LoEntry& e = kLoTab[g_loTabNow][r];
                wchar_t name[100], val[16];
                name[0] = (e.kind != LE_BLANK && r == g_loSel[g_loTabNow]) ? L'>' : L' ';
                name[1] = L' ';
                LoEntryName(e, name + 2, 96);
                LoEntryValue(e, val, 16);
                cell(grid, r, 0, name);
                cell(grid, r, 1, val);
            }
            for (int r = LO_ROWS; r < count; r++) {
                cell(grid, r, 0, L"");
                cell(grid, r, 1, L"");
            }
            ((LoTopFn)VA(LO_FUNC_LIST_SETTOP))(grid, 0);
            // the refresh rebuilds the rows and drops the light: keep it on the row just used
            // (it follows the mouse again as soon as the mouse moves)
            if (g_loHover >= 0 && g_loHover < LO_ROWS) LoLightRow(grid, g_loHover);
            else LoLightRow(grid, -1);   // the rifle panel is the target: no row stays lit
        }
    }
    LoDrawPanel();
    LoSetTitle(g_loPage);   // the page puts the plain loadout name back when it is shown again
    LoDirty();
}

static void LoShowTab(int tab) {
    if (tab < 0 || tab >= LO_TABS) return;
    g_loTabNow = tab;
    LoDraw();
}

// +1 / -1 on an entry (one magazine / one item / on-off), then give the kit again.
static void LoStepEntry(const LoEntry& e, int step) {
    int u = LoEntryUnits(e) + step;
    if (u < LoEntryMin(e) || u > LoEntryMax(e)) { LoDraw(); return; }
    LoEntrySet(e, u);
    ((LoVoidFn)VA(LO_FUNC_APPLY))(g_loPage);   // the refresh redraws the editor
}

// Left / right click (or keys) on a row: +1 / -1; the tab row switches tabs.
static void LoUseRow(int tab, int row, int step) {
    const LoEntry& e = kLoTab[tab][row];
    if (e.kind == LE_TAB) { g_loSel[e.item] = row; LoShowTab(e.item); return; }   // stays on the switch
    if (e.kind == LE_BLANK) return;
    g_loSel[tab] = row;
    if (e.kind == LE_COUNT && LoIsLauncherRound(e.item) && !LoLauncherOn()) { LoDraw(); return; }   // locked
    LoStepEntry(e, step);
}

// PgUp / PgDn: rifle panel, then the tab's rows (not the blank one), then around.
static void LoSelect(int dir) {
    int order[LO_ROWS + 1], n = 0, at = 0;
    order[n++] = LO_SEL_RIFLE;
    for (int r = 0; r < LO_ROWS; r++)
        if (kLoTab[g_loTabNow][r].kind != LE_BLANK) order[n++] = r;
    for (int k = 0; k < n; k++)
        if (order[k] == g_loSel[g_loTabNow]) at = k;
    int sel = order[(at + dir + n) % n];
    g_loSel[g_loTabNow] = sel;
    g_loHover = (short)(sel >= 0 ? sel : -1);
    LoDraw();
}

// Magma's mouse position (screen units): SCMagmaInteraction +40h = Magma manager; the manager's
// screen root keeps the last mouse point at +16h (written by the mouse dispatch, 0x10D23CC0).
static int LoMousePoint(void* interaction, short* x, short* y) {
    if (!interaction || !ObjPtrOk(interaction)) return 0;
    void* mgr = *(void**)((BYTE*)interaction + 0x40);
    if (!mgr || !ObjPtrOk(mgr)) return 0;
    BYTE* root = (BYTE*)((LoGetFn)VA(LO_FUNC_MAGMA_ROOT))(mgr);
    if (!root || IsBadReadPtr(root + 0x16, 4)) return 0;
    *x = *(short*)(root + 0x16);
    *y = *(short*)(root + 0x18);
    return 1;
}

// The rifle panel sits right above Lst_equiped, one row high: is the mouse on it?
// Lst_equiped's rect: +90h left, +92h right, +94h top, +96h bottom (same screen units).
static int LoMouseOnRiflePanel(void* grid, void* interaction) {
    short x, y;
    if (!grid || !LoMousePoint(interaction, &x, &y)) return 0;
    BYTE* g = (BYTE*)grid;
    short left = *(short*)(g + 0x90), right = *(short*)(g + 0x92);
    short top = *(short*)(g + 0x94), bottom = *(short*)(g + 0x96);
    int rows = *(g + 0xA0) ? *(g + 0xA0) : LO_ROWS;
    int rowH = (bottom - top + 1) / rows;
    return rowH > 0 && x >= left && x <= right && y < top && y >= top - rowH - rowH / 4;
}

// ---- hooks ---------------------------------------------------------------------------------------

extern "C" void LoadoutAddLast_Hook();
extern "C" void __cdecl OnLoadoutAddLast(void* page, void* list, const wchar_t* text, int data) {
    LoAddFn add = (LoAddFn)VA(LO_FUNC_LIST_ADD);
    add(list, text, data);
    if (!g_cfg.enableCustomLoadouts || !page || *(int*)((BYTE*)page + 0x28) != 0) return;   // solo only
    add(list, LoCustomName(), 0);
    g_loList = list;
    // the list is built for 3 rows; show all 4
    int n = LoListCount(list);
    BYTE* maxRows = (BYTE*)list + LIST_MAX_ROWS;
    if (n > 0 && *maxRows < n) {
        *maxRows = (BYTE)n;
        ((LoVoidFn)VA(LO_FUNC_LIST_LAYOUT))(list);
    }
}

asm(
    ".text\n"
    ".globl _LoadoutAddLast_Hook\n"
    "_LoadoutAddLast_Hook:\n"           // ecx = Lst_Menu, esi = page, [esp+4] text, [esp+8] data
    "    pushl 8(%esp)\n"
    "    pushl 8(%esp)\n"
    "    pushl %ecx\n"
    "    pushl %esi\n"
    "    call _OnLoadoutAddLast\n"
    "    addl $16, %esp\n"
    "    ret $8\n"
);

// CUSTOM is the last entry of Lst_Menu.
static char __attribute__((thiscall)) H_LoSelChanged(void* page, void* ev) {
    LoSelFn orig = (LoSelFn)VA(LO_FUNC_SELCHANGED);
    BYTE* p = (BYTE*)page;
    void* list = *(void**)(p + 0x38);
    if (!g_cfg.enableCustomLoadouts || !list || list != g_loList || !ev ||
        *(DWORD*)((BYTE*)ev + 4) != *(DWORD*)VA(LO_GLOBAL_SELEVENT))
        return orig(page, ev);
    int n = LoListCount(list);
    short idx = *(short*)((BYTE*)list + LIST_SELECTED);
    if (n <= 0 || idx != n - 1) return orig(page, ev);
    g_loPage = page;
    g_loPageVt = *(DWORD*)page;
    if (*(int*)(p + 0x28) != LO_TYPE_CUSTOM) {
        LoCheckMap(page);                        // remember this briefing's map
        LoReset();                               // picking CUSTOM starts from Stealth
        *(int*)(p + 0x28) = LO_TYPE_CUSTOM;
        g_loTabNow = 0;
        g_loSel[0] = g_loSel[1] = LO_SEL_RIFLE;   // the rifle panel starts selected
        g_loHover = -1;
        LoSetTitle(page);
        ((LoVoidFn)VA(LO_FUNC_APPLY))(page);
    }
    void* mgr = *(void**)(p + 0x10);
    if (mgr) *((BYTE*)mgr + 0x14) = 1;
    return 0;
}

static void __attribute__((thiscall)) H_LoRefresh(void* page, void* panel, void* grid) {
    ((LoRefreshFn)VA(LO_FUNC_REFRESH))(page, panel, grid);
    if (g_cfg.enableCustomLoadouts && *(int*)((BYTE*)page + 0x28) == LO_TYPE_CUSTOM) {
        g_loPage = page;
        g_loPageVt = *(DWORD*)page;
        LoDraw();
    }
}

// CUSTOM: read the slots as the Stealth loadout, keep them as the base, then apply the amounts.
static int __attribute__((thiscall)) H_LoFill(void* mm, const wchar_t* file) {
    BYTE* m = (BYTE*)mm;
    if (!g_cfg.enableCustomLoadouts || m[LO_OFF_TYPE] != LO_TYPE_CUSTOM)
        return ((LoFillFn)VA(LO_FUNC_FILL))(mm, file);
    if (LoEditorActive()) LoCheckMap(g_loPage);
    m[LO_OFF_TYPE] = LO_TYPE_STEALTH;
    int r = ((LoFillFn)VA(LO_FUNC_FILL))(mm, file);
    m[LO_OFF_TYPE] = LO_TYPE_CUSTOM;
    for (int i = 0; i < LO_N; i++)
        g_loDef[i] = *(int*)(m + kLoItems[i].off);
    LoLimitAttachments();
    for (int i = 0; i < LO_N; i++) {
        int v = LoEffective(i);
        if (LoIsLauncherRound(i) && !LoLauncherOn()) v = 0;   // no launcher: no launcher rounds
        if (LoIsTotal(i)) v = v > 0 ? v - 1 : 0;   // on top of the loaded magazine; exact after setup
        *(int*)(m + kLoItems[i].off) = v;
    }
    // the rifle starts on an attachment that is fitted (1 Foregrip, 2 Launcher, 3 Sniper, 4 Shotgun)
    static const int attachOff[5] = {0, 0x34, 0x38, 0x3C, 0x40};
    BYTE s = m[LO_OFF_STARTUP];
    if (s < 1 || s > 4 || !*(int*)(m + attachOff[s])) {
        for (BYTE k = 1; k <= 4; k++)
            if (*(int*)(m + attachOff[k])) { m[LO_OFF_STARTUP] = k; break; }
    }
    if (m[LO_OFF_STARTUP] >= 1 && m[LO_OFF_STARTUP] <= 4) g_loStartup = LO_ATTACH_FIRST + m[LO_OFF_STARTUP] - 1;
    return r;
}

// SC-20K / pistol: after the game's weapon setup, set the ammo to the custom total.
// Actor.stAmmoPool: ClipAmmo, ClipCapacity, TotalAmmo; TotalAmmo counts the loaded rounds too
// (the HUD shows TotalAmmo / ClipAmmo, firing takes from both).
static void LoSetWeaponAmmo(BYTE* weapon, int rounds) {
    if (!weapon || !ObjPtrOk(weapon)) return;
    int clip = *(int*)(weapon + 0x438);
    if (clip <= 0 || clip > 1000) return;
    if (rounds < 0) rounds = 0;
    *(int*)(weapon + 0x434) = rounds < clip ? rounds : clip;
    *(int*)(weapon + 0x43C) = rounds;
}

static void __attribute__((thiscall)) H_LoWeaponSetup(void* mm, void* pawn) {
    ((LoPtrFn)VA(LO_FUNC_WEAPON_SETUP))(mm, pawn);
    if (!g_cfg.enableCustomLoadouts || !mm || !pawn || ((BYTE*)mm)[LO_OFF_TYPE] != LO_TYPE_CUSTOM) return;
    BYTE* p = (BYTE*)pawn;
    LoSetWeaponAmmo(*(BYTE**)(p + 0x560), LoEffective(0) * kLoClip[LG_RIFLE]);
    LoSetWeaponAmmo(*(BYTE**)(p + 0x564), LoEffective(1) * kLoClip[LG_PISTOL]);
}

static int LoHandleInput(void* interaction, DWORD key) {
    void* grid = LoGrid();
    short hover = grid ? *(short*)((BYTE*)grid + LIST_HOVER) : -1;
    int onRow = hover >= 0 && hover < LO_ROWS;
    int sel = g_loSel[g_loTabNow];
    switch (key) {
        case 0x01: case 0x02: {                      // left click = more, right click = less
            int step = key == 0x01 ? 1 : -1;
            if (onRow) { g_loHover = hover; LoUseRow(g_loTabNow, hover, step); return 1; }
            if (LoMouseOnRiflePanel(grid, interaction)) {   // the rifle panel: SC-20K ammo
                g_loHover = -1;
                g_loSel[g_loTabNow] = LO_SEL_RIFLE;
                LoStepEntry(kLoRifle, step);
                return 1;
            }
            return 0;
        }
        case 0x21: case 0x68: LoSelect(-1); return 1;                            // PgUp, numpad 8
        case 0x22: case 0x62: LoSelect(1); return 1;                             // PgDn, numpad 2
        case 0x25: case 0x64: case 0x6D: case 0xBD:                              // Left, numpad 4, -
        case 0x27: case 0x66: case 0x6B: case 0xBB: {                            // Right, numpad 6, +
            int step = (key == 0x25 || key == 0x64 || key == 0x6D || key == 0xBD) ? -1 : 1;
            if (sel == LO_SEL_RIFLE) { g_loHover = -1; LoStepEntry(kLoRifle, step); }
            else { g_loHover = (short)sel; LoUseRow(g_loTabNow, sel, step); }
            return 1;
        }
        default: return 0;
    }
}

static int StHandleKey(DWORD key, DWORD action);   // stats.inc

static DWORD __attribute__((thiscall)) H_LoMagmaKey(void* self, DWORD key, DWORD action, DWORD delta) {
    if ((key & 0xFF) && StHandleKey(key & 0xFF, action & 0xFF))
        return 1;
    if ((key & 0xFF) && (action & 0xFF) == 1 && LoEditorActive() && LoHandleInput(self, key & 0xFF))
        return 1;
    return ((LoKeyFn)(*(DWORD**)self)[0xDC / 4])(self, key, action, delta);
}

// Magma key events: custom loadout editor and the OPSAT statistics page (stats.inc).
static void ApplyMagmaKeyHook() {
    if (!g_cfg.enableCustomLoadouts && !g_cfg.opsatStatistics) return;
    const BYTE keyCall[6] = {0xFF, 0x92, 0xDC, 0x00, 0x00, 0x00};
    if (Check(LO_SITE_KEY, keyCall, 6)) PatchCall6(LO_SITE_KEY, (void*)H_LoMagmaKey);
}

static void ApplyLoadoutHooks() {
    if (!g_cfg.enableCustomLoadouts) return;
    const BYTE call[1] = {0xE8};
    const BYTE keyCall[6] = {0xFF, 0x92, 0xDC, 0x00, 0x00, 0x00};
    const BYTE disp[4] = {0x66, 0x3D, 0x03, 0x00};
    if (!Check(LO_SITE_ADD_LAST, call, 1) || Rel32Target(LO_SITE_ADD_LAST) != VA(LO_FUNC_LIST_ADD) ||
        !Check(LO_SITE_FILL, call, 1) || Rel32Target(LO_SITE_FILL) != VA(LO_FUNC_FILL) ||
        !Check(LO_SITE_KEY, keyCall, 6) || !Check(LO_SITE_DISPLACE, disp, 4) ||
        *(DWORD*)VA(LO_VT_SELCHANGED) != VA(LO_FUNC_SELCHANGED))
        return;
    for (int k = 0; k < 4; k++)
        if (!Check(LO_SITE_REFRESH[k], call, 1) || Rel32Target(LO_SITE_REFRESH[k]) != VA(LO_FUNC_REFRESH))
            return;
    PatchRel32(LO_SITE_ADD_LAST, 0xE8, (void*)LoadoutAddLast_Hook);
    PatchRel32(LO_SITE_FILL, 0xE8, (void*)H_LoFill);
    for (int k = 0; k < 2; k++)
        if (Check(LO_SITE_WEAPON_SETUP[k], call, 1) && Rel32Target(LO_SITE_WEAPON_SETUP[k]) == VA(LO_FUNC_WEAPON_SETUP))
            PatchRel32(LO_SITE_WEAPON_SETUP[k], 0xE8, (void*)H_LoWeaponSetup);
    for (int k = 0; k < 4; k++) PatchRel32(LO_SITE_REFRESH[k], 0xE8, (void*)H_LoRefresh);
    DWORD sel = (DWORD)(void*)H_LoSelChanged;
    WriteMem(VA(LO_VT_SELCHANGED), &sel, 4);
    // On some maps the page drops entry 2 (Assault) once the list has 3 entries; with CUSTOM
    // it has 4, so it still drops Assault, and only once.
    const BYTE n[1] = {4};
    WriteMem(VA(LO_SITE_DISPLACE + 2), n, 1);
}
