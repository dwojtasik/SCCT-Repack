// ------------------------------------------------------------------------------------------------
// loc_paths.inc — Loose layout next to splintercell3.exe (System\):
//   Localization\                 language files (.int, .pol, …)
//   Localization\Fonts\<lang>\    HUD fonts
//   Localization\Barks\           UTF-8 P_Barks.<lang>
//   Localization\Transcripts\     Bink JSON
// CreateFile remap uses the loose tree when it exists; otherwise Magma keeps Data\ (UMD).
// ------------------------------------------------------------------------------------------------

static char g_systemDir[MAX_PATH];
static int  g_systemDirReady;

static void SystemDir(char* out, int n) {
    if (!g_systemDirReady) {
        GetModuleFileNameA(NULL, g_systemDir, MAX_PATH);
        char* slash = strrchr(g_systemDir, '\\');
        if (slash) slash[1] = 0;
        else g_systemDir[0] = 0;
        g_systemDirReady = 1;
    }
    if (!out || n < 2) return;
    snprintf(out, n, "%s", g_systemDir);
}

static int PathIsDirA(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

static int PathIsFileA(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static char* FindPathMarkerA(char* hay, const char* needle) {
    if (!hay || !needle || !needle[0]) return 0;
    int n = (int)strlen(needle);
    for (char* p = hay; *p; p++) {
        if (_strnicmp(p, needle, n) == 0) return p;
    }
    return 0;
}

static const char* FindPathMarkerCA(const char* hay, const char* needle) {
    return FindPathMarkerA((char*)hay, needle);
}

static void SlashNormalizeA(char* s) {
    for (; s && *s; s++)
        if (*s == '/') *s = '\\';
}

static int RemapWithSuffix(char* dst, int n, const char* mid, const char* marker, const char* src) {
    const char* mark = FindPathMarkerCA(src, marker);
    if (!mark) return 0;
    char sys[MAX_PATH];
    SystemDir(sys, MAX_PATH);
    if (!sys[0]) return 0;
    snprintf(dst, n, "%s%s%s", sys, mid, mark + (int)strlen(marker));
    SlashNormalizeA(dst);
    return 1;
}

// DataPC-relative "Fonts\<lang>\…" → System\Localization\Fonts\<lang>\…
static int RemapFontsShortA(const char* src, char* dst, int n) {
    if (!src || !dst || n < 32) return 0;
    if (FindPathMarkerCA(src, "Localization\\Fonts") || FindPathMarkerCA(src, "Localization/Fonts"))
        return 0;
    if (FindPathMarkerCA(src, "Data\\Magma\\DataPC\\Fonts") ||
        FindPathMarkerCA(src, "Data/Magma/DataPC/Fonts"))
        return 0;
    const char* p = src;
    while (p[0] == '.' && (p[1] == '\\' || p[1] == '/')) p += 2;
    if (p[0] == '\\' || p[0] == '/') p++;
    if (_strnicmp(p, "Fonts\\", 6) != 0 && _strnicmp(p, "Fonts/", 6) != 0) {
        p = FindPathMarkerCA(src, "Fonts\\");
        if (!p) p = FindPathMarkerCA(src, "Fonts/");
        if (!p) return 0;
        if (p != src && p[-1] != '\\' && p[-1] != '/' && p[-1] != ':' && p != src + 1)
            return 0;
        if (src[0] && src[1] == ':') return 0;
        if (FindPathMarkerCA(src, "Data\\") || FindPathMarkerCA(src, "Data/")) return 0;
    }
    char sys[MAX_PATH];
    SystemDir(sys, MAX_PATH);
    if (!sys[0]) return 0;
    snprintf(dst, n, "%sLocalization\\Fonts\\%s", sys, p + 6);
    SlashNormalizeA(dst);
    return 1;
}

// Relative "Localization\…" → System\Localization\…
static int RemapLocRelativeA(const char* src, char* dst, int n) {
    if (!src || !dst || n < 32) return 0;
    const char* p = src;
    while (p[0] == '.' && (p[1] == '\\' || p[1] == '/')) p += 2;
    if (p[0] == '\\' || p[0] == '/') p++;
    if (_strnicmp(p, "Localization\\", 13) != 0 && _strnicmp(p, "Localization/", 13) != 0)
        return 0;
    if (src[0] && src[1] == ':') return 0;
    char sys[MAX_PATH];
    SystemDir(sys, MAX_PATH);
    if (!sys[0]) return 0;
    snprintf(dst, n, "%s%s", sys, p);
    SlashNormalizeA(dst);
    return 1;
}

// Any "...\Localization\<leaf>" (not Fonts/Barks/Transcripts) → System\Localization\<leaf>
static int RemapLocLeafA(const char* src, char* dst, int n) {
    if (!src || !dst || n < 32) return 0;
    const char* mark = FindPathMarkerCA(src, "\\Localization\\");
    if (!mark) mark = FindPathMarkerCA(src, "/Localization/");
    if (!mark) return 0;
    const char* rest = mark + 14;
    if (!rest[0] || strchr(rest, '\\') || strchr(rest, '/')) return 0;
    if (!_stricmp(rest, "Fonts") || !_stricmp(rest, "Barks") || !_stricmp(rest, "Transcripts"))
        return 0;
    char sys[MAX_PATH];
    SystemDir(sys, MAX_PATH);
    if (!sys[0]) return 0;
    int sl = (int)strlen(sys);
    if (src[0] && src[1] == ':' && !_strnicmp(src, sys, sl) &&
        !_strnicmp(src + sl, "Localization\\", 13))
        return 0;
    snprintf(dst, n, "%sLocalization\\%s", sys, rest);
    SlashNormalizeA(dst);
    return 1;
}

static int RemapLegacyLocPathA(const char* src, char* dst, int n) {
    if (!src || !dst || n < 32) return 0;
    if (RemapWithSuffix(dst, n, "Localization\\", "Data\\System\\Localization\\", src)) return 1;
    if (RemapWithSuffix(dst, n, "Localization\\", "Data/System/Localization/", src)) return 1;
    if (RemapWithSuffix(dst, n, "Localization\\Fonts\\", "Data\\Magma\\DataPC\\Fonts\\", src)) return 1;
    if (RemapWithSuffix(dst, n, "Localization\\Fonts\\", "Data/Magma/DataPC/Fonts/", src)) return 1;
    if (RemapLocLeafA(src, dst, n)) return 1;
    if (RemapLocRelativeA(src, dst, n)) return 1;
    if (RemapFontsShortA(src, dst, n)) return 1;
    return 0;
}

static int DirHasFilesA(const char* dir) {
    if (!PathIsDirA(dir)) return 0;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int ok = 0;
    do {
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0)))
            continue;
        ok = 1;
        break;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return ok;
}

// Cached probes from lang_fallback.inc.
static int LocLangOnDisk(const char* lang);
static int LocDiskHasAny();
static int LooksLikeLang3(const char* s);

static int LocRestIsSpecial(const char* rest) {
    return rest &&
           (!_strnicmp(rest, "Fonts", 5) || !_strnicmp(rest, "Barks", 5) ||
            !_strnicmp(rest, "Transcripts", 11));
}

// Skip disk I/O when System\Localization\ has no files for this language (keep UMD path).
static int SkipMissingLooseLoc(const char* mapped) {
    const char* mark = FindPathMarkerCA(mapped, "Localization\\");
    if (!mark) mark = FindPathMarkerCA(mapped, "Localization/");
    if (!mark) return 0;
    const char* rest = mark + 13;
    if (LocRestIsSpecial(rest)) return 0;
    if (!LocDiskHasAny()) return 1;
    if (strpbrk(rest, "\\/")) return 0;
    const char* dot = strrchr(rest, '.');
    return dot && LooksLikeLang3(dot + 1) && !LocLangOnDisk(dot + 1);
}

static int PreferLooseLocPathA(const char** name, char* buf, int n) {
    if (!name || !*name) return 0;
    if (!RemapLegacyLocPathA(*name, buf, n)) return 0;
    if (SkipMissingLooseLoc(buf)) return 0;
    if (PathIsFileA(buf)) {
        *name = buf;
        return 1;
    }
    char probe[MAX_PATH];
    snprintf(probe, sizeof probe, "%s", buf);
    char* star = strpbrk(probe, "*?");
    if (star) {
        while (star > probe && *star != '\\' && *star != '/') star--;
        if (star > probe) *star = 0;
        if (!DirHasFilesA(probe)) return 0;
        *name = buf;
        return 1;
    }
    if (DirHasFilesA(buf)) {
        *name = buf;
        return 1;
    }
    return 0;
}
