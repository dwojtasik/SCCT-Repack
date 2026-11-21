// overlay_text.inc - overlay loc decode. No Magma / game state.
// pol/svk/hun CP1250, rus CP1251, cze custom 1252 slots, prt and Western 1252 as-is.
static UINT OverlayCpForLang(const char* lang) {
    if (!lang || !lang[0]) return 0;
    if (!_stricmp(lang, "pol") || !_stricmp(lang, "svk") || !_stricmp(lang, "hun")) return 1250;
    if (!_stricmp(lang, "rus")) return 1251;
    if (!_stricmp(lang, "cze") || !_stricmp(lang, "prt")) return 1252;
    return 0;
}

static wchar_t OverlayFromByte(UINT cp, unsigned char b) {
    char src[2] = { (char)b, 0 };
    wchar_t dst[2] = {};
    if (cp && MultiByteToWideChar(cp, 0, src, 1, dst, 1) == 1 && dst[0])
        return dst[0];
    return (wchar_t)b;
}

// SamFisher.cz Magma fonts: ČŘŠŽĚŮŇĎ painted into Western CP1252 cells.
static wchar_t OverlayMapCze(wchar_t c) {
    switch (c) {
        case 0x00C2: return 0x017D;                 // Â → Ž
        case 0x00E2: return 0x017E;                 // â → ž
        case 0x00C3: case 0x0102: return 0x010C;    // Ã/Ă → Č
        case 0x00E3: case 0x0103: return 0x010D;    // ã/ă → č
        case 0x00C5: case 0x0139: return 0x016E;    // Å/Ĺ → Ů
        case 0x00E5: case 0x013A: return 0x016F;    // å/ĺ → ů
        case 0x00D4: return 0x0160;                 // Ô → Š
        case 0x00F4: return 0x0161;                 // ô → š
        case 0x00D5: case 0x0150: return 0x011A;    // Õ/Ő → Ě
        case 0x00F5: case 0x0151: return 0x011B;    // õ/ő → ě
        case 0x00DB: case 0x0170: return 0x0158;    // Û/Ű → Ř
        case 0x00FB: case 0x0171: return 0x0159;    // û/ű → ř
        case 0x00D1: case 0x0143: return 0x0147;    // Ñ/Ń → Ň
        case 0x00F1: case 0x0144: return 0x0148;    // ñ/ń → ň
        case 0x00D6: return 0x010E;                 // Ö → Ď
        case 0x00FC: return 0x010F;                 // ü → ď
        default:     return c;
    }
}

static wchar_t OverlayFixChar(wchar_t c, const char* lang) {
    UINT cp = OverlayCpForLang(lang);
    if (lang && !_stricmp(lang, "cze")) {
        c = OverlayMapCze(c);
        if (c >= 0x80 && c <= 0x9F) return OverlayFromByte(1252, (unsigned char)c);
        return c;
    }
    if (cp == 1251) {
        if (c >= 0x80 && c <= 0xFF) return OverlayFromByte(1251, (unsigned char)c);
        return c;
    }
    if (cp == 1250) {
        if (c >= 0x80 && c <= 0xFF) return OverlayFromByte(1250, (unsigned char)c);
        if (c == 0x0152) return 0x015A; // Œ → Ś
        if (c == 0x0153) return 0x015B; // œ → ś
        if (c == 0x0178) return 0x017A; // Ÿ → ź
        return c;
    }
    // prt / fra / deu / esp / ita / int: Magma byte-cast of 1252 C1 (’ • – œ)
    if (c >= 0x80 && c <= 0x9F) return OverlayFromByte(1252, (unsigned char)c);
    return c;
}

static void OverlayFixWide(wchar_t* s, int n, const char* lang) {
    if (!s || n < 2) return;
    for (int i = 0; i < n && s[i]; i++)
        s[i] = OverlayFixChar(s[i], lang);
}
