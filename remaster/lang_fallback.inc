// ------------------------------------------------------------------------------------------------
// lang_fallback.inc - Fallbacks follow what actually exists on disk / in the UMD.
//
// Audio: MAPS.LM0 => bank present (may be incomplete). SetCurrentLanguage forced to int when
// missing; CreateFile IAT still rewrites stray LS0/LM0 opens to INT.
// Lipsync (per file, follows the loaded audio language, like loc text):
//   Data\Lipsync\<lang>\... on disk (game root) -> same file in the UMD -> the INT file
//   (disk, then UMD). A disk hit is opened by absolute path so the UMD copy cannot shadow it.
//   Resolved at the reader open in USound::Serialize, which builds the path from GLanguage.
// Text: System\Localization\*.<lang> when present; else Data\System\Localization\ from UMD;
// missing in both => EffectiveTextLanguage = int. Disk/UMD probes are scanned once and cached.
// ------------------------------------------------------------------------------------------------
static const DWORD IAT_MULTIBYTETOWIDECHAR = 0x110B7288;
static const DWORD IAT_WIDECHARTOMULTIBYTE = 0x110B730C;
static const DWORD IAT_FINDFIRSTFILEW     = 0x110B7298;
static const DWORD IAT_FINDFIRSTFILEA     = 0x110B72A0;
static const DWORD IAT_GETFILEATTRIBUTESW = 0x110B72DC;
static const DWORD IAT_CREATEFILEA        = 0x110B73BC;
static const DWORD IAT_CREATEFILEW        = 0x110B73C0;
static const DWORD IAT_GETFILEATTRIBUTESA = 0x110B73DC;
static const DWORD SITE_SET_CUR_LANG      = 0x11046D75;   // mov ebx,[esp+8]; push ebx
static const DWORD SITE_LIP_OPEN          = 0x10A6DB62;   // USound::Serialize: CreateFileReader(lipsync)
static const DWORD SITE_LIP_OPEN_CONT     = 0x10A6DB70;
static const DWORD GERROR_PTR             = 0x111C8898;
static const DWORD DARE_MASTER_DIR        = 0x1132BB58;

typedef HANDLE (WINAPI *CreateFileAFn)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef HANDLE (WINAPI *CreateFileWFn)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
typedef DWORD  (WINAPI *GetFileAttributesAFn)(LPCSTR);
typedef DWORD  (WINAPI *GetFileAttributesWFn)(LPCWSTR);
typedef HANDLE (WINAPI *FindFirstFileAFn)(LPCSTR, LPWIN32_FIND_DATAA);
typedef HANDLE (WINAPI *FindFirstFileWFn)(LPCWSTR, LPWIN32_FIND_DATAW);
typedef int    (WINAPI *MultiByteToWideCharFn)(UINT, DWORD, LPCSTR, int, LPWSTR, int);
typedef int    (WINAPI *WideCharToMultiByteFn)(UINT, DWORD, LPCWSTR, int, LPSTR, int, LPCSTR, LPBOOL);

static CreateFileAFn        o_CreateFileA;
static CreateFileWFn        o_CreateFileW;
static GetFileAttributesAFn o_GetFileAttributesA;
static GetFileAttributesWFn o_GetFileAttributesW;
static FindFirstFileAFn     o_FindFirstFileA;
static FindFirstFileWFn     o_FindFirstFileW;
static MultiByteToWideCharFn o_MultiByteToWideChar;
static WideCharToMultiByteFn o_WideCharToMultiByte;
static int                  g_soundFb;
static char                 g_loadedAudioLang[8];
static std::vector<unsigned> g_umdLipFiles;   // hashes of "<lang>\<dir>\<file>.bin" in the UMD
static char                 g_lipDiskRoot[MAX_PATH];   // <game>\Data\Lipsync\ (ends in '\')
static char                 g_lipDiskLangs[16][4];
static int                  g_lipDiskLangN;
static int                  g_lipDiskScan;
static char                 g_fontLangs[16][4];
static int                  g_fontLangN;
static char                 g_locFileLangs[16][4];
static int                  g_locFileLangN;
static int                  g_umdScan;
static char                 g_locDiskLangs[16][4];
static int                  g_locDiskLangN;
static int                  g_locDiskScan;
static char                 g_fontDiskLangs[16][4];
static int                  g_fontDiskLangN;
static int                  g_fontDiskScan;
static char                 g_effTextLang[8];
static int                  g_effTextReady;
static char                 g_effGameLang[8];
static int                  g_effGameReady;

extern "C" {
    DWORD g_setLangCont;
    const char* FilterLang(const char* lang);
    void H_SetCurrentLanguageEntry();
    DWORD g_lipOpenCont;
    DWORD g_lipGError;
    void H_LipOpenEntry();
    const wchar_t* LipResolvePath(const wchar_t* path);
}

static int LangEndOk(char c) { return c == 0 || c == '\\' || c == '/' || c == '.'; }

static int IsLetter3(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

static int LooksLikeLang3(const char* s) {
    return s && IsLetter3(s[0]) && IsLetter3(s[1]) && IsLetter3(s[2]) && LangEndOk(s[3]);
}

static int IsDubLang3(const char* s) {
    return LooksLikeLang3(s) && _strnicmp(s, "int", 3);
}

static int PathExistsA(const char* path) {
    if (!path || !path[0]) return 0;
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

static void JoinSlash(char* dst, int n, const char* a, const char* b, const char* c) {
    dst[0] = 0;
    if (!a) a = "";
    if (!b) b = "";
    if (!c) c = "";
    int la = (int)strlen(a), lb = (int)strlen(b), lc = (int)strlen(c);
    int sep1 = (la && a[la - 1] != '\\' && a[la - 1] != '/' && b[0] && b[0] != '\\' && b[0] != '/') ? 1 : 0;
    int sep2 = (lb && b[lb - 1] != '\\' && b[lb - 1] != '/' && c[0] && c[0] != '\\' && c[0] != '/') ? 1 : 0;
    if (la + sep1 + lb + sep2 + lc >= n) return;
    memcpy(dst, a, la);
    int o = la;
    if (sep1) dst[o++] = '\\';
    memcpy(dst + o, b, lb); o += lb;
    if (sep2) dst[o++] = '\\';
    memcpy(dst + o, c, lc);
    dst[o + lc] = 0;
}

static int LangBankExists(const char* lang) {
    if (!lang || !lang[0] || !lang[1] || !lang[2]) return 0;
    char leaf[16];
    memcpy(leaf, lang, 3);
    leaf[3] = 0;
    char probe[MAX_PATH];

    const char* master = (const char*)VA(DARE_MASTER_DIR);
    if (master && master[0] && master[0] >= 32 && master[0] < 127) {
        JoinSlash(probe, MAX_PATH, master, leaf, "MAPS.LM0");
        if (PathExistsA(probe)) return 1;
        JoinSlash(probe, MAX_PATH, master, leaf, "MAPS");
        if (PathExistsA(probe)) return 1;
    }

    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    static const char* kRel[] = {
        "\\..\\Data\\Sounds\\",
        "\\..\\Sounds\\",
        "\\Data\\Sounds\\",
        "\\Sounds\\",
    };
    for (int i = 0; i < 4; i++) {
        snprintf(probe, sizeof probe, "%s%s%s\\MAPS.LM0", exe, kRel[i], leaf);
        if (PathExistsA(probe)) return 1;
    }
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
        snprintf(probe, sizeof probe, "%s\\..\\Data\\Sounds\\%s\\MAPS.LM0", cwd, leaf);
        if (PathExistsA(probe)) return 1;
        snprintf(probe, sizeof probe, "%s\\Data\\Sounds\\%s\\MAPS.LM0", cwd, leaf);
        if (PathExistsA(probe)) return 1;
    }
    return 0;
}

static void EffectiveAudioLanguage(char* out, int n) {
    char want[16] = {};
    ReadAudioLanguage(want, 16);
    if (IsDubLang3(want) && !LangBankExists(want) && LangBankExists("int"))
        snprintf(out, n, "int");
    else
        snprintf(out, n, "%s", want[0] ? want : "int");
}

static void StoreLang3(char* dst, const char* src) {
    dst[0] = src[0];
    dst[1] = src[1];
    dst[2] = src[2];
    dst[3] = 0;
}

static void RememberLoadedAudio(const char* lang);

static void LoadedAudioLanguage(char* out, int n) {
    if (g_loadedAudioLang[0]) {
        snprintf(out, n, "%s", g_loadedAudioLang);
        return;
    }
    EffectiveAudioLanguage(out, n);
}

static int RdUmdIndex(FILE* f) {
    int c = fgetc(f);
    if (c < 0) return -1;
    int neg = c & 0x80;
    unsigned value = (unsigned)(c & 0x3F);
    if (c & 0x40) {
        int shift = 6;
        for (;;) {
            c = fgetc(f);
            if (c < 0) return -1;
            value |= (unsigned)(c & 0x7F) << shift;
            if (!(c & 0x80)) break;
            shift += 7;
            if (shift > 28) return -1;
        }
    }
    return neg ? -(int)value : (int)value;
}

static char FoldAz(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static void RememberLang3(char table[][4], int* n, int cap, const char* lang) {
    if (!LooksLikeLang3(lang) || *n >= cap) return;
    for (int i = 0; i < *n; i++)
        if (!_strnicmp(table[i], lang, 3)) return;
    table[*n][0] = FoldAz(lang[0]);
    table[*n][1] = FoldAz(lang[1]);
    table[*n][2] = FoldAz(lang[2]);
    table[*n][3] = 0;
    (*n)++;
}

static int Lang3InTable(const char table[][4], int n, const char* lang) {
    if (!lang || !lang[0]) return 0;
    for (int i = 0; i < n; i++)
        if (!_strnicmp(table[i], lang, 3)) return 1;
    return 0;
}

// FNV-1a over the path below "Lipsync\", lower case, '\' separators.
static unsigned LipKeyHashW(const wchar_t* s) {
    unsigned h = 2166136261u;
    for (; *s; s++) {
        wchar_t c = *s == L'/' ? L'\\' : *s;
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        h = (h ^ (unsigned)(c & 0xFFFF)) * 16777619u;
    }
    return h;
}

static unsigned LipKeyHashA(const char* s) {
    unsigned h = 2166136261u;
    for (; *s; s++) {
        char c = *s == '/' ? '\\' : *s;
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        h = (h ^ (unsigned)(unsigned char)c) * 16777619u;
    }
    return h;
}

static void ScanUmdNames(FILE* f) {
    int count = RdUmdIndex(f);
    if (count < 1 || count > 1000000) return;
    char name[1024];
    for (int i = 0; i < count; i++) {
        int nlen = RdUmdIndex(f);
        if (nlen < 1 || nlen > 1023) return;
        if ((int)fread(name, 1, nlen, f) != nlen) return;
        name[nlen] = 0;
        unsigned char skip[12];   // offset, size, flags (fread: no 32-bit seek past 2 GiB)
        if (fread(skip, 1, 12, f) != 12) return;
        for (char* s = name; *s; s++)
            if (*s == '/') *s = '\\';
        int hasLip = 0, hasFont = 0, hasLoc = 0;
        for (char* p = name; *p; p++) {
            if (!hasLip && !_strnicmp(p, "Lipsync\\", 8)) {
                if (LooksLikeLang3(p + 8)) g_umdLipFiles.push_back(LipKeyHashA(p + 8));
                hasLip = 1;
            }
            if (!hasFont && !_strnicmp(p, "Fonts\\", 6)) {
                char* rest = p + 6;
                if (LooksLikeLang3(rest) && (rest[3] == '\\' || rest[3] == '/'))
                    RememberLang3(g_fontLangs, &g_fontLangN, 16, rest);
                hasFont = 1;
            }
            if (!hasLoc && !_strnicmp(p, "Localization\\", 13)) {
                char* dot = strrchr(p, '.');
                if (dot && LooksLikeLang3(dot + 1))
                    RememberLang3(g_locFileLangs, &g_locFileLangN, 16, dot + 1);
                hasLoc = 1;
            }
            if (hasLip && hasFont && hasLoc) break;
        }
    }
}

static void EnsureUmdScan() {
    if (g_umdScan) return;
    g_umdScan = 1;
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    char path[MAX_PATH];
    static const char* kRel[] = { "\\dynamic-pc.umd", "\\..\\System\\dynamic-pc.umd" };
    FILE* f = 0;
    for (int i = 0; i < 2 && !f; i++) {
        snprintf(path, sizeof path, "%s%s", exe, kRel[i]);
        f = fopen(path, "rb");
    }
    if (!f) return;
    // 64-bit offsets: remaster UMDs (UMD3) are past 4 GiB; 32-bit ftell fails on them and
    // the scan would see an empty archive.
    if (_fseeki64(f, 0, SEEK_END) != 0) { fclose(f); return; }
    long long fileSize = _ftelli64(f);
    if (fileSize < 16) { fclose(f); return; }

    unsigned long long toc = 0;
    int haveToc = 0;
    if (fileSize >= 32) {
        unsigned char tr[32];
        if (_fseeki64(f, -32, SEEK_END) == 0 && fread(tr, 1, 32, f) == 32) {
            unsigned magic = 0, version = 0;
            unsigned long long size64 = 0;
            memcpy(&toc, tr, 8);
            memcpy(&size64, tr + 8, 8);
            memcpy(&version, tr + 16, 4);
            memcpy(&magic, tr + 24, 4);
            if (magic == 0x33444D55 && version == 3 && size64 == (unsigned long long)fileSize &&
                toc < size64)
                haveToc = 1;
        }
    }
    if (!haveToc) {
        unsigned char tr16[16];
        if (_fseeki64(f, -16, SEEK_END) != 0 || fread(tr16, 1, 16, f) != 16) {
            fclose(f);
            return;
        }
        unsigned toc32 = 0, size32 = 0, version = 0;
        memcpy(&toc32, tr16, 4);
        memcpy(&size32, tr16 + 4, 4);
        memcpy(&version, tr16 + 8, 4);
        if ((long long)size32 != fileSize || (version != 1 && version != 2) ||
            toc32 == 0 || (long long)toc32 >= fileSize) {
            fclose(f);
            return;
        }
        toc = toc32;
        haveToc = 1;
    }
    if (!haveToc || _fseeki64(f, (long long)toc, SEEK_SET) != 0) { fclose(f); return; }
    ScanUmdNames(f);
    fclose(f);
    std::sort(g_umdLipFiles.begin(), g_umdLipFiles.end());
}

static int LipFileInUmd(const wchar_t* key) {
    EnsureUmdScan();
    return std::binary_search(g_umdLipFiles.begin(), g_umdLipFiles.end(), LipKeyHashW(key));
}

static int FontFolderInUmd(const char* lang) {
    EnsureUmdScan();
    return Lang3InTable(g_fontLangs, g_fontLangN, lang);
}

static int LocFileInUmd(const char* lang) {
    EnsureUmdScan();
    return Lang3InTable(g_locFileLangs, g_locFileLangN, lang);
}

// <game>\Data\Lipsync\<lang>\ folders that hold files: scanned once, so a language without a
// loose folder costs no disk probe per line.
static void EnsureLipDiskScan() {
    if (g_lipDiskScan) return;
    g_lipDiskScan = 1;
    char sys[MAX_PATH];
    SystemDir(sys, MAX_PATH);
    if (!sys[0]) return;
    snprintf(g_lipDiskRoot, sizeof g_lipDiskRoot, "%s..\\Data\\Lipsync\\", sys);
    char full[MAX_PATH];
    if (GetFullPathNameA(g_lipDiskRoot, MAX_PATH, full, 0)) snprintf(g_lipDiskRoot, sizeof g_lipDiskRoot, "%s", full);
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%s*", g_lipDiskRoot);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !LooksLikeLang3(fd.cFileName)) continue;
        char dir[MAX_PATH];
        snprintf(dir, sizeof dir, "%s%s", g_lipDiskRoot, fd.cFileName);
        if (DirHasFilesA(dir)) RememberLang3(g_lipDiskLangs, &g_lipDiskLangN, 16, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

// key = "<lang>\<dir>\<file>.bin"; writes the absolute disk path when the loose file exists.
static int LipFileOnDisk(const wchar_t* key, wchar_t* out, int n) {
    EnsureLipDiskScan();
    char lang[4] = { (char)key[0], (char)key[1], (char)key[2], 0 };
    if (!g_lipDiskLangN || !Lang3InTable(g_lipDiskLangs, g_lipDiskLangN, lang)) return 0;
    wchar_t root[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, g_lipDiskRoot, -1, root, MAX_PATH)) return 0;
    if (_snwprintf(out, n, L"%ls%ls", root, key) < 0) return 0;
    out[n - 1] = 0;
    for (wchar_t* p = out; *p; p++) if (*p == L'/') *p = L'\\';
    DWORD attr = GetFileAttributesW(out);   // DLL import: not routed through the EXE IAT hooks
    return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

static void NormLang3(const char* src, char* dst, int n) {
    if (!dst || n < 4) return;
    dst[0] = 0;
    if (!src) return;
    while (*src == ' ' || *src == '\t' || *src == '\r' || *src == '\n') src++;
    if (!IsLetter3(src[0]) || !IsLetter3(src[1]) || !IsLetter3(src[2])) return;
    dst[0] = (char)(src[0] >= 'A' && src[0] <= 'Z' ? src[0] - 'A' + 'a' : src[0]);
    dst[1] = (char)(src[1] >= 'A' && src[1] <= 'Z' ? src[1] - 'A' + 'a' : src[1]);
    dst[2] = (char)(src[2] >= 'A' && src[2] <= 'Z' ? src[2] - 'A' + 'a' : src[2]);
    dst[3] = 0;
}

static void EnsureLocDiskScan() {
    if (g_locDiskScan) return;
    g_locDiskScan = 1;
    char exe[MAX_PATH];
    SystemDir(exe, MAX_PATH);
    if (!exe[0]) return;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%sLocalization\\*", exe);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const char* dot = strrchr(fd.cFileName, '.');
        if (!dot || !LooksLikeLang3(dot + 1)) continue;
        RememberLang3(g_locDiskLangs, &g_locDiskLangN, 16, dot + 1);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static int LocDiskHasAny() {
    EnsureLocDiskScan();
    return g_locDiskLangN > 0;
}

static int LocLangOnDisk(const char* lang) {
    char leaf[4];
    NormLang3(lang, leaf, 4);
    if (!leaf[0]) return 0;
    EnsureLocDiskScan();
    return Lang3InTable(g_locDiskLangs, g_locDiskLangN, leaf);
}

static void EnsureFontDiskScan() {
    if (g_fontDiskScan) return;
    g_fontDiskScan = 1;
    char exe[MAX_PATH];
    SystemDir(exe, MAX_PATH);
    if (!exe[0]) return;
    char pat[MAX_PATH];
    snprintf(pat, sizeof pat, "%sLocalization\\Fonts\\*", exe);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == '.' &&
            (fd.cFileName[1] == 0 || (fd.cFileName[1] == '.' && fd.cFileName[2] == 0)))
            continue;
        if (!LooksLikeLang3(fd.cFileName)) continue;
        char dir[MAX_PATH];
        snprintf(dir, sizeof dir, "%sLocalization\\Fonts\\%s", exe, fd.cFileName);
        if (DirHasFilesA(dir))
            RememberLang3(g_fontDiskLangs, &g_fontDiskLangN, 16, fd.cFileName);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

static int FontDirOnDisk(const char* lang) {
    char leaf[4];
    NormLang3(lang, leaf, 4);
    if (!leaf[0]) return 0;
    EnsureFontDiskScan();
    return Lang3InTable(g_fontDiskLangs, g_fontDiskLangN, leaf);
}

static int LocLangExists(const char* lang) {
    char leaf[4];
    NormLang3(lang, leaf, 4);
    if (!leaf[0]) return 0;
    if (LocLangOnDisk(leaf)) return 1;
    if (LocFileInUmd(leaf)) return 1;
    // Stock English always available from the UMD even if the TOC scan missed it.
    if (!_stricmp(leaf, "int")) return 1;
    return 0;
}

static void FallbackLocLang(const char* want, char* out, int n) {
    char leaf[4];
    NormLang3(want, leaf, 4);
    if (leaf[0] && LocLangExists(leaf))
        snprintf(out, n, "%s", leaf);
    else
        snprintf(out, n, "int");
}

// System\Localization\<pkg>.<lang> with no loose files → Data\System\Localization\ (UMD).
static int PreferUmdLocPathA(const char** name, char* buf, int n) {
    if (!name || !*name || !buf || n < 32) return 0;
    const char* src = *name;
    if (FindPathMarkerCA(src, "Data\\System\\Localization") ||
        FindPathMarkerCA(src, "Data/System/Localization"))
        return 0;
    const char* mark = FindPathMarkerCA(src, "Localization\\");
    if (!mark) mark = FindPathMarkerCA(src, "Localization/");
    if (!mark) return 0;
    const char* rest = mark + 13;
    if (!rest[0] || LocRestIsSpecial(rest) || strpbrk(rest, "\\/")) return 0;
    const char* dot = strrchr(rest, '.');
    if (!dot || !LooksLikeLang3(dot + 1)) return 0;

    char leaf[4];
    NormLang3(dot + 1, leaf, 4);
    if (!leaf[0] || LocLangOnDisk(leaf)) return 0;
    if (!LocFileInUmd(leaf) && _stricmp(leaf, "int")) return 0;

    snprintf(buf, n, "..\\Data\\System\\Localization\\%s", rest);
    *name = buf;
    return 1;
}

static void EffectiveTextLanguage(char* out, int n) {
    if (!g_effTextReady) {
        char want[16] = {};
        ReadTextLanguage(want, 16);
        FallbackLocLang(want, g_effTextLang, sizeof g_effTextLang);
        g_effTextReady = 1;
    }
    snprintf(out, n, "%s", g_effTextLang);
}

static void EffectiveGameLanguage(char* out, int n) {
    if (!g_effGameReady) {
        char want[16] = {};
        ReadGameLanguage(want, 16);
        FallbackLocLang(want, g_effGameLang, sizeof g_effGameLang);
        g_effGameReady = 1;
    }
    snprintf(out, n, "%s", g_effGameLang);
}

static void RememberLoadedAudio(const char* lang) {
    if (!lang || !lang[0] || !lang[1] || !lang[2]) return;
    StoreLang3(g_loadedAudioLang, lang);
}

extern "C" const char* FilterLang(const char* lang) {
    static char buf[16];
    const char* use;
    if (lang && !_strnicmp(lang, "org", 3) && LangEndOk(lang[3]))
        use = LangBankExists("org") ? lang : (LangBankExists("int") ? "int" : lang);
    else {
        EffectiveAudioLanguage(buf, sizeof buf);
        use = buf;
    }
    RememberLoadedAudio(use);
    if (use == buf) return buf;
    snprintf(buf, sizeof buf, "%s", use);
    return buf;
}

static char* FindLangFolderA(char* path, const char* folder) {
    if (!path || !folder) return 0;
    int fl = (int)strlen(folder);
    for (char* p = path; *p; p++) {
        if (_strnicmp(p, folder, fl)) continue;
        if (p[fl] != '\\' && p[fl] != '/') continue;
        char* lang = p + fl + 1;
        if (LooksLikeLang3(lang)) return lang;
    }
    return 0;
}

static int ApplyEffectiveAudioA(char* path) {
    if (!path) return 0;
    char* lang = FindLangFolderA(path, "Sounds");
    if (!lang) return 0;
    char want[16] = {};
    LoadedAudioLanguage(want, 16);
    if (!want[0] || !_strnicmp(lang, want, 3)) return 0;
    lang[0] = want[0];
    lang[1] = want[1];
    lang[2] = want[2];
    return 1;
}

// USound::Serialize opens "..\\Data\\lipsync\\<GLanguage>\\<dir>\\<file>.bin" through the file
// manager (UMD first, then disk). Resolve it per file for the loaded audio language, then INT.
// Returns the path to open: the original, a copy with another language, or an absolute disk path.
extern "C" const wchar_t* LipResolvePath(const wchar_t* path) {
    static thread_local wchar_t out[1024];
    if (!path) return path;
    const wchar_t* key = 0;
    for (const wchar_t* p = path; *p; p++)
        if (!_wcsnicmp(p, L"Lipsync\\", 8) || !_wcsnicmp(p, L"Lipsync/", 8)) { key = p + 8; break; }
    if (!key) return path;
    char cur[4] = { (char)key[0], (char)key[1], (char)key[2], 0 };
    if (!LooksLikeLang3(cur) || (key[3] != L'\\' && key[3] != L'/')) return path;
    size_t len = wcslen(path);
    if (len >= 1000) return path;

    // probe key "<lang>\\<dir>\\<file>.bin" (single separators, whatever the engine joined)
    const wchar_t* rest = key + 3;
    while (*rest == L'\\' || *rest == L'/') rest++;
    wchar_t probe[1024];
    _snwprintf(probe, 1024, L"xxx\\%ls", rest);
    probe[1023] = 0;

    char want[16] = {};
    LoadedAudioLanguage(want, 16);
    if (!LooksLikeLang3(want)) snprintf(want, sizeof want, "int");
    const char* order[2] = { want, "int" };
    int count = _strnicmp(want, "int", 3) ? 2 : 1;
    const char* pick = "int";   // stock INT lipsync ships in every UMD: beats none (rus, cze, ...)
    for (int i = 0; i < count; i++) {
        for (int k = 0; k < 3; k++) probe[k] = (wchar_t)(unsigned char)FoldAz(order[i][k]);
        if (LipFileOnDisk(probe, out, 1024) && wcslen(out) < 1000)
            return out;   // absolute: the UMD lookup misses and the disk reader opens it
        if (LipFileInUmd(probe)) { pick = order[i]; break; }
    }
    if (!_strnicmp(cur, pick, 3)) return path;
    memcpy(out, path, (len + 1) * sizeof(wchar_t));
    wchar_t* lang = out + (key - path);
    for (int k = 0; k < 3; k++) lang[k] = (wchar_t)(unsigned char)pick[k];
    return out;
}

// Replaces "mov esi,[GError]; mov edx,[ecx]; push esi; push ebp; push eax; call [edx+8]"
// (ecx = file manager, eax = path, ebp = 0); resumes at "mov esi,eax".
asm(
    ".text\n"
    ".globl _H_LipOpenEntry\n"
    "_H_LipOpenEntry:\n"
    "    pushl %ecx\n"
    "    pushl %eax\n"
    "    call _LipResolvePath\n"
    "    addl $4, %esp\n"
    "    popl %ecx\n"
    "    movl _g_lipGError, %esi\n"
    "    movl (%esi), %esi\n"
    "    movl (%ecx), %edx\n"
    "    pushl %esi\n"
    "    pushl %ebp\n"
    "    pushl %eax\n"
    "    call *8(%edx)\n"
    "    jmp *_g_lipOpenCont\n"
);

static int WINAPI H_MultiByteToWideChar(UINT cp, DWORD flags, LPCSTR src, int cb, LPWSTR dst, int cch) {
    if (src && (cp == CP_ACP || cp == CP_THREAD_ACP || cp == 1252 || cp == 0)) {
        int n = (cb < 0) ? (int)strlen(src) : cb;
        UINT want = CodePageForAnsi(src, n);
        if (want) cp = want;
    }
    return o_MultiByteToWideChar(cp, flags, src, cb, dst, cch);
}

static int WINAPI H_WideCharToMultiByte(UINT cp, DWORD flags, LPCWSTR src, int cch, LPSTR dst, int cb,
                                       LPCSTR def, LPBOOL used) {
    if (cp == CP_ACP || cp == CP_THREAD_ACP || cp == 1252) {
        char lang[16];
        EffectiveTextLanguage(lang, 16);
        UINT want = CodePageForLangToken(lang);
        if (want) {
            int r = o_WideCharToMultiByte(want, flags, src, cch, dst, cb, def, used);
            if (r > 0) return r;
        }
    }
    return o_WideCharToMultiByte(cp, flags, src, cch, dst, cb, def, used);
}

asm(
    ".text\n"
    ".globl _H_SetCurrentLanguageEntry\n"
    "_H_SetCurrentLanguageEntry:\n"
    "    pushl 8(%esp)\n"
    "    call _FilterLang\n"
    "    addl $4, %esp\n"
    "    movl %eax, %ebx\n"
    "    pushl %ebx\n"
    "    movl _g_setLangCont, %eax\n"
    "    jmpl *%eax\n"
);

static int LooksLikeLocAudio(const char* rest) {
    if (!rest || !rest[0]) return 1;
    if (rest[0] == '\\' || rest[0] == '/') rest++;
    if (!rest[0]) return 1;
    if (!_strnicmp(rest, "MAPS", 4) && LangEndOk(rest[4])) return 1;
    const char* d = strrchr(rest, '.');
    if (!d) return 0;
    return !_stricmp(d, ".LS0") || !_stricmp(d, ".LM0") || !_stricmp(d, ".SM0");
}

static char* FindDubLangA(char* path) {
    if (!path || !path[0]) return 0;
    for (char* p = path; *p; p++) {
        if (_strnicmp(p, "Sounds\\", 7) && _strnicmp(p, "Sounds/", 7)) continue;
        char* lang = p + 7;
        if (IsDubLang3(lang)) return lang;
    }
    char* s = path;
    if (s[0] == '.' && (s[1] == '\\' || s[1] == '/')) s += 2;
    if (!IsDubLang3(s)) return 0;
    const char* rest = s + 3;
    if (*rest == '\\' || *rest == '/') rest++;
    return LooksLikeLocAudio(rest) ? s : 0;
}

static void ProbeDirA(char* probe) {
    char* star = strpbrk(probe, "*?");
    if (!star) return;
    while (star > probe && *star != '\\' && *star != '/') star--;
    if (star > probe) *star = 0;
}

static int FallbackMissingA(char* path) {
    if (!path || !o_GetFileAttributesA) return 0;
    char* lang = FindDubLangA(path);
    if (!lang) return 0;
    char probe[MAX_PATH];
    int n = (int)strlen(path);
    if (n <= 0 || n >= MAX_PATH) return 0;
    memcpy(probe, path, n + 1);
    int glob = strpbrk(probe, "*?") != 0;
    if (glob) ProbeDirA(probe);
    if (o_GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES) return 0;
    lang[0] = 'i';
    lang[1] = 'n';
    lang[2] = 't';
    return 1;
}

// Most CreateFile traffic is unrelated; only rewrite loc / fonts / audio paths (lipsync is
// resolved at its reader open, in LipResolvePath).
static int PathNeedsFallbackA(const char* path) {
    if (!path || !path[0]) return 0;
    for (const char* p = path; *p; p++) {
        char c = *p | 0x20;
        if (c == 'l' && !_strnicmp(p, "Localization", 12)) return 1;
        if (c == 'f' && !_strnicmp(p, "Fonts", 5)) return 1;
        if (c == 's' && !_strnicmp(p, "Sounds", 6)) return 1;
    }
    return 0;
}

static int RemapLocPathA(char* path, int n) {
    char loc[MAX_PATH];
    const char* prefer = path;
    if (PreferLooseLocPathA(&prefer, loc, MAX_PATH) && prefer == loc) {
        snprintf(path, n, "%s", loc);
        return 1;
    }
    if (PreferUmdLocPathA(&prefer, loc, MAX_PATH) && prefer == loc) {
        snprintf(path, n, "%s", loc);
        return 1;
    }
    return 0;
}

static int CopyFallbackA(const char* src, char* dst, int n) {
    if (!src || !dst || n < 16 || !PathNeedsFallbackA(src)) return 0;
    int len = (int)strlen(src);
    if (len <= 0 || len >= n) return 0;
    memcpy(dst, src, len + 1);
    int remapped = RemapLocPathA(dst, n);
    int forced = ApplyEffectiveAudioA(dst);
    return FallbackMissingA(dst) || forced || remapped;
}

static int CopyFallbackW(const wchar_t* src, wchar_t* dst, int n) {
    if (!src || !dst || n < 16) return 0;
    char a[MAX_PATH], b[MAX_PATH];
    if (!WideCharToMultiByte(CP_ACP, 0, src, -1, a, MAX_PATH, 0, 0)) return 0;
    if (!PathNeedsFallbackA(a)) return 0;
    memcpy(b, a, strlen(a) + 1);
    int remapped = RemapLocPathA(b, (int)sizeof b);
    int forced = ApplyEffectiveAudioA(b);
    if (!FallbackMissingA(b) && !forced && !remapped) return 0;
    return MultiByteToWideChar(CP_ACP, 0, b, -1, dst, n) > 0;
}

static HANDLE WINAPI H_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                   DWORD disp, DWORD flags, HANDLE tmpl) {
    char buf[MAX_PATH];
    if (CopyFallbackA(name, buf, MAX_PATH)) name = buf;
    return o_CreateFileA(name, acc, share, sa, disp, flags, tmpl);
}

static HANDLE WINAPI H_CreateFileW(LPCWSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                   DWORD disp, DWORD flags, HANDLE tmpl) {
    wchar_t buf[MAX_PATH];
    if (CopyFallbackW(name, buf, MAX_PATH)) name = buf;
    return o_CreateFileW(name, acc, share, sa, disp, flags, tmpl);
}

static DWORD WINAPI H_GetFileAttributesA(LPCSTR name) {
    char buf[MAX_PATH];
    if (CopyFallbackA(name, buf, MAX_PATH)) name = buf;
    return o_GetFileAttributesA(name);
}

static DWORD WINAPI H_GetFileAttributesW(LPCWSTR name) {
    wchar_t buf[MAX_PATH];
    if (CopyFallbackW(name, buf, MAX_PATH)) name = buf;
    return o_GetFileAttributesW(name);
}

static HANDLE WINAPI H_FindFirstFileA(LPCSTR name, LPWIN32_FIND_DATAA data) {
    char buf[MAX_PATH];
    if (CopyFallbackA(name, buf, MAX_PATH)) name = buf;
    return o_FindFirstFileA(name, data);
}

static HANDLE WINAPI H_FindFirstFileW(LPCWSTR name, LPWIN32_FIND_DATAW data) {
    wchar_t buf[MAX_PATH];
    if (CopyFallbackW(name, buf, MAX_PATH)) name = buf;
    return o_FindFirstFileW(name, data);
}

static void HookIat(DWORD iatVa, void* hook, void** orig) {
    DWORD iat = VA(iatVa);
    void* cur = *(void**)iat;
    if (!cur || cur == hook) return;
    *orig = cur;
    WriteMem(iat, &hook, 4);
}

static void ApplySoundFallback() {
    if (g_soundFb) return;
    EnsureUmdScan();
    EnsureLocDiskScan();
    EnsureFontDiskScan();
    EnsureLipDiskScan();
    char warm[16];
    EffectiveTextLanguage(warm, 16);
    EffectiveGameLanguage(warm, 16);
    const BYTE setLang[5] = {0x8B, 0x5C, 0x24, 0x08, 0x53};
    if (Check(SITE_SET_CUR_LANG, setLang, 5)) {
        g_setLangCont = VA(SITE_SET_CUR_LANG + 5);
        PatchRel32(SITE_SET_CUR_LANG, 0xE9, (void*)H_SetCurrentLanguageEntry);
    }
    const BYTE lipOpen[14] = {0x8B, 0x35, 0x98, 0x88, 0x1C, 0x11, 0x8B, 0x11, 0x56, 0x55, 0x50,
                              0xFF, 0x52, 0x08};
    if (!g_lipOpenCont && Check(SITE_LIP_OPEN, lipOpen, 2) &&
        *(DWORD*)VA(SITE_LIP_OPEN + 2) == VA(GERROR_PTR) && Check(SITE_LIP_OPEN + 6, lipOpen + 6, 8)) {
        g_lipGError = VA(GERROR_PTR);
        g_lipOpenCont = VA(SITE_LIP_OPEN_CONT);
        BYTE b[14];
        memset(b, 0x90, sizeof b);
        b[0] = 0xE9;
        *(DWORD*)(b + 1) = (DWORD)(void*)H_LipOpenEntry - (VA(SITE_LIP_OPEN) + 5);
        WriteMem(VA(SITE_LIP_OPEN), b, sizeof b);
    }
    HookIat(IAT_MULTIBYTETOWIDECHAR, (void*)H_MultiByteToWideChar, (void**)&o_MultiByteToWideChar);
    HookIat(IAT_WIDECHARTOMULTIBYTE, (void*)H_WideCharToMultiByte, (void**)&o_WideCharToMultiByte);
    HookIat(IAT_GETFILEATTRIBUTESA, (void*)H_GetFileAttributesA, (void**)&o_GetFileAttributesA);
    HookIat(IAT_GETFILEATTRIBUTESW, (void*)H_GetFileAttributesW, (void**)&o_GetFileAttributesW);
    HookIat(IAT_CREATEFILEA, (void*)H_CreateFileA, (void**)&o_CreateFileA);
    HookIat(IAT_CREATEFILEW, (void*)H_CreateFileW, (void**)&o_CreateFileW);
    HookIat(IAT_FINDFIRSTFILEA, (void*)H_FindFirstFileA, (void**)&o_FindFirstFileA);
    HookIat(IAT_FINDFIRSTFILEW, (void*)H_FindFirstFileW, (void**)&o_FindFirstFileW);
    if (g_setLangCont || (o_CreateFileA && o_GetFileAttributesA)) g_soundFb = 1;
}
