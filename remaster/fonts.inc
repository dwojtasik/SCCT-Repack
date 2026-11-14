// ------------------------------------------------------------------------------------------------
// fonts.inc - Magma HUD bitmaps live in Data\Magma\DataPC\Fonts\<lang>\ when that folder
// exists (pol/cze/rus/svk on remaster UMDs). Missing folder → keep stock \Fonts\ (INT).
// Patches the two "\Fonts\" string pushes. Safe at DllMain — Magma loads fonts later.
// ------------------------------------------------------------------------------------------------
static const DWORD SITE_FONT_DIR1 = 0x10D4D49D;
static const DWORD SITE_FONT_DIR2 = 0x10D5433B;
static const BYTE  PUSH_FONTS[5]  = {0x68, 0x6C, 0xAC, 0x13, 0x11};

static char g_fontsDir[16];

static void ApplyLocFonts() {
    char lang[16];
    ReadTextLanguage(lang, 16);
    if (!lang[0] || !_stricmp(lang, "int")) return;
    if (!FontDirExists(lang)) return;
    snprintf(g_fontsDir, sizeof g_fontsDir, "\\Fonts\\%s\\", lang);
    if (!Check(SITE_FONT_DIR1, PUSH_FONTS, 5) || !Check(SITE_FONT_DIR2, PUSH_FONTS, 5))
        return;
    DWORD p = (DWORD)g_fontsDir;
    WriteMem(VA(SITE_FONT_DIR1) + 1, &p, 4);
    WriteMem(VA(SITE_FONT_DIR2) + 1, &p, 4);
}
