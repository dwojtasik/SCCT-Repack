// ------------------------------------------------------------------------------------------------
// fonts.inc — Magma HUD fonts + Localize disk path.
//
// Fonts: System\Localization\Fonts\<lang>\ when present; else UMD \Fonts\<lang>\; else INT.
// Localize: clear Magma's "Data\" platform prefix only when System\Localization\ has the
// effective text language; otherwise keep Data\ (UMD) / INT fallback.
// ------------------------------------------------------------------------------------------------
static const DWORD SITE_FONT_DIR1 = 0x10D4D49D;
static const DWORD SITE_FONT_DIR2 = 0x10D5433B;
static const BYTE  PUSH_FONTS[5]  = {0x68, 0x6C, 0xAC, 0x13, 0x11};
static const DWORD SITE_LOC_PREFIX = 0x10CCC52D; // call GetPlatformPrefix
static const BYTE  CALL_LOC_PREFIX[5] = {0xE8, 0x3E, 0xDB, 0xFF, 0xFF};
static const DWORD VA_EMPTY_WSTR = 0x110B7B38; // L""

static char g_fontsDir[80];

static void ApplyLocDiskPrefix() {
    if (!Check(SITE_LOC_PREFIX, CALL_LOC_PREFIX, 5)) return;
    char lang[16];
    EffectiveTextLanguage(lang, 16);
    // No loose files for this language → leave Magma's "Data\" prefix (UMD path).
    if (!LocLangOnDisk(lang)) return;
    BYTE b[5] = {0xB8}; // mov eax, L""
    *(DWORD*)(b + 1) = VA(VA_EMPTY_WSTR);
    WriteMem(VA(SITE_LOC_PREFIX), b, 5);
}

static void ApplyLocFonts() {
    ApplyLocDiskPrefix();
    char lang[16];
    EffectiveTextLanguage(lang, 16);
    if (!lang[0] || !_stricmp(lang, "int")) return;
    int disk = FontDirOnDisk(lang);
    if (!disk && !FontFolderInUmd(lang)) return;
    if (disk)
        snprintf(g_fontsDir, sizeof g_fontsDir,
                 "\\..\\..\\..\\System\\Localization\\Fonts\\%s\\", lang);
    else
        snprintf(g_fontsDir, sizeof g_fontsDir, "\\Fonts\\%s\\", lang);
    if (!Check(SITE_FONT_DIR1, PUSH_FONTS, 5) || !Check(SITE_FONT_DIR2, PUSH_FONTS, 5))
        return;
    DWORD p = (DWORD)g_fontsDir;
    WriteMem(VA(SITE_FONT_DIR1) + 1, &p, 4);
    WriteMem(VA(SITE_FONT_DIR2) + 1, &p, 4);
}
