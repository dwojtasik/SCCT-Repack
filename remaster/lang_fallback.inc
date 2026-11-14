// ------------------------------------------------------------------------------------------------
// lang_fallback.inc - Fallbacks follow what actually exists on disk / in the UMD.
//
// Audio: MAPS.LM0 means that language's bank is present (the set can be incomplete).
// DARE still uses that code; CreateFile IAT rewrites only the missing LS0/LM0 files to INT.
// Lipsync: one directory check (UMD Data\Lipsync\<lang>\ or a loose folder). Result is stored
// in g_loadedLipLang when audio language is set — not per viseme file.
// Fonts: Data\Magma\DataPC\Fonts\<lang>\ in the UMD (or a loose folder). Missing → stock \Fonts\.
// Text: Localization\*.<lang> in the UMD (or loose). Missing → Magma Localize / overlay use int.
//
// CreateFile IAT rewrite is not enough for a missing bank: Magma calls
// _SND_fn_vSetCurrentLanguage with "fra"/"pol"/… and DARE then searches DataDirectories
// for that code. Intercept the language set (after DARE's cookie prolog) and pass "int"
// when MAPS.LM0 is not on disk. IAT rewrite stays as a backstop for partial banks.
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
static const DWORD SITE_LIP_SPRINTF       = 0x10AD0769;   // swprintf of Data/Lipsync/<lang>/
static const DWORD FUNC_LIP_SPRINTF       = 0x10CCB0D0;
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
static char                 g_loadedLipLang[8];
static char                 g_lipLangs[16][4];
static int                  g_lipLangN;
static char                 g_fontLangs[16][4];
static int                  g_fontLangN;
static char                 g_locFileLangs[16][4];
static int                  g_locFileLangN;
static int                  g_umdScan;

extern "C" {
    DWORD g_setLangCont;
    const char* FilterLang(const char* lang);
    void H_SetCurrentLanguageEntry();
    void H_LipSprintf(wchar_t* dest, const wchar_t* fmt, const wchar_t* a, const wchar_t* b);
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
static void RememberLoadedLip(const char* audioLang);

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

static void ScanUmdNames(FILE* f) {
    int count = RdUmdIndex(f);
    if (count < 1 || count > 1000000) return;
    char name[1024];
    for (int i = 0; i < count; i++) {
        int nlen = RdUmdIndex(f);
        if (nlen < 1 || nlen > 1023) return;
        if ((int)fread(name, 1, nlen, f) != nlen) return;
        name[nlen] = 0;
        if (fseek(f, 12, SEEK_CUR) != 0) return;
        for (char* s = name; *s; s++)
            if (*s == '/') *s = '\\';
        int hasLip = 0, hasFont = 0, hasLoc = 0;
        for (char* p = name; *p; p++) {
            if (!hasLip && !_strnicmp(p, "Lipsync\\", 8)) {
                RememberLang3(g_lipLangs, &g_lipLangN, 16, p + 8);
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
    if (fseek(f, -32, SEEK_END) != 0) { fclose(f); return; }
    unsigned char tr[32];
    if (fread(tr, 1, 32, f) != 32) { fclose(f); return; }
    unsigned magic = 0, version = 0;
    memcpy(&magic, tr + 24, 4);
    memcpy(&version, tr + 16, 4);
    unsigned long long toc = 0;
    memcpy(&toc, tr, 8);
    if (magic != 0x33444D55 || version != 3) { fclose(f); return; }
    if (fseek(f, (long)toc, SEEK_SET) != 0) { fclose(f); return; }
    ScanUmdNames(f);
    fclose(f);
}

static int LipFolderInUmd(const char* lang) {
    EnsureUmdScan();
    return Lang3InTable(g_lipLangs, g_lipLangN, lang);
}

static int FontFolderInUmd(const char* lang) {
    EnsureUmdScan();
    return Lang3InTable(g_fontLangs, g_fontLangN, lang);
}

static int LocFileInUmd(const char* lang) {
    EnsureUmdScan();
    return Lang3InTable(g_locFileLangs, g_locFileLangN, lang);
}

static int LipDirOnDisk(const char* lang) {
    if (!lang || !lang[0] || !lang[1] || !lang[2]) return 0;
    char leaf[4] = { lang[0], lang[1], lang[2], 0 };
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    char probe[MAX_PATH];
    static const char* kRel[] = {
        "\\..\\Data\\Lipsync\\",
        "\\Data\\Lipsync\\",
        "\\..\\Lipsync\\",
        "\\Lipsync\\",
    };
    for (int i = 0; i < 4; i++) {
        snprintf(probe, sizeof probe, "%s%s%s", exe, kRel[i], leaf);
        DWORD attr = GetFileAttributesA(probe);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return 1;
    }
    char cwd[MAX_PATH];
    if (GetCurrentDirectoryA(MAX_PATH, cwd)) {
        snprintf(probe, sizeof probe, "%s\\..\\Data\\Lipsync\\%s", cwd, leaf);
        DWORD attr = GetFileAttributesA(probe);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return 1;
        snprintf(probe, sizeof probe, "%s\\Data\\Lipsync\\%s", cwd, leaf);
        attr = GetFileAttributesA(probe);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return 1;
    }
    return 0;
}

static int LipDirExists(const char* lang) {
    return LipFolderInUmd(lang) || LipDirOnDisk(lang);
}

static int FontDirOnDisk(const char* lang) {
    if (!lang || !lang[0] || !lang[1] || !lang[2]) return 0;
    char leaf[4] = { lang[0], lang[1], lang[2], 0 };
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    char probe[MAX_PATH];
    static const char* kRel[] = {
        "\\..\\Data\\Magma\\DataPC\\Fonts\\",
        "\\Data\\Magma\\DataPC\\Fonts\\",
        "\\..\\Fonts\\",
        "\\Fonts\\",
    };
    for (int i = 0; i < 4; i++) {
        snprintf(probe, sizeof probe, "%s%s%s", exe, kRel[i], leaf);
        DWORD attr = GetFileAttributesA(probe);
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY))
            return 1;
    }
    return 0;
}

static int FontDirExists(const char* lang) {
    return FontFolderInUmd(lang) || FontDirOnDisk(lang);
}

static int LocLangOnDisk(const char* lang) {
    if (!lang || !lang[0] || !lang[1] || !lang[2]) return 0;
    char leaf[4] = { lang[0], lang[1], lang[2], 0 };
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) *slash = 0;
    char probe[MAX_PATH];
    static const char* kRel[] = {
        "\\..\\Data\\System\\Localization\\P_01_Lighthouse.",
        "\\Data\\System\\Localization\\P_01_Lighthouse.",
        "\\Localization\\P_01_Lighthouse.",
    };
    for (int i = 0; i < 3; i++) {
        snprintf(probe, sizeof probe, "%s%s%s", exe, kRel[i], leaf);
        if (GetFileAttributesA(probe) != INVALID_FILE_ATTRIBUTES)
            return 1;
    }
    return 0;
}

static int LocLangExists(const char* lang) {
    if (!lang || !lang[0]) return 0;
    if (!_strnicmp(lang, "int", 3) && LangEndOk(lang[3])) return 1;
    return LocFileInUmd(lang) || LocLangOnDisk(lang);
}

static void FallbackLocLang(const char* want, char* out, int n) {
    if (want && want[0] && LocLangExists(want))
        snprintf(out, n, "%s", want);
    else
        snprintf(out, n, "int");
}

static void EffectiveTextLanguage(char* out, int n) {
    char want[16] = {};
    ReadTextLanguage(want, 16);
    FallbackLocLang(want, out, n);
}

static void EffectiveGameLanguage(char* out, int n) {
    char want[16] = {};
    ReadGameLanguage(want, 16);
    FallbackLocLang(want, out, n);
}

static void RememberLoadedLip(const char* audioLang) {
    if (g_loadedLipLang[0]) return;
    if (audioLang && audioLang[0] && LipDirExists(audioLang))
        StoreLang3(g_loadedLipLang, audioLang);
    else if (LipDirExists("int"))
        StoreLang3(g_loadedLipLang, "int");
    else if (audioLang && audioLang[0])
        StoreLang3(g_loadedLipLang, audioLang);
    else
        StoreLang3(g_loadedLipLang, "int");
}

static void RememberLoadedAudio(const char* lang) {
    if (!lang || !lang[0] || !lang[1] || !lang[2]) return;
    StoreLang3(g_loadedAudioLang, lang);
    RememberLoadedLip(lang);
}

static void EffectiveLipLanguage(char* out, int n) {
    if (!g_loadedLipLang[0]) {
        char want[16] = {};
        LoadedAudioLanguage(want, 16);
        RememberLoadedLip(want);
    }
    snprintf(out, n, "%s", g_loadedLipLang[0] ? g_loadedLipLang : "int");
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
    char* lip = FindLangFolderA(path, "Lipsync");
    char* lang = lip ? lip : FindLangFolderA(path, "Sounds");
    if (!lang) return 0;
    char want[16] = {};
    if (lip) EffectiveLipLanguage(want, 16);
    else LoadedAudioLanguage(want, 16);
    if (!want[0] || !_strnicmp(lang, want, 3)) return 0;
    lang[0] = want[0];
    lang[1] = want[1];
    lang[2] = want[2];
    return 1;
}

static void ApplyEffectiveAudioW(wchar_t* path) {
    if (!path) return;
    for (wchar_t* p = path; *p; p++) {
        int lip = !_wcsnicmp(p, L"Lipsync\\", 8) || !_wcsnicmp(p, L"Lipsync/", 8);
        int snd = !_wcsnicmp(p, L"Sounds\\", 7) || !_wcsnicmp(p, L"Sounds/", 7);
        if (!lip && !snd) continue;
        wchar_t* lang = p + (lip ? 8 : 7);
        char a[4] = { (char)lang[0], (char)lang[1], (char)lang[2], 0 };
        if (!LooksLikeLang3(a)) continue;
        char want[16] = {};
        if (lip) EffectiveLipLanguage(want, 16);
        else LoadedAudioLanguage(want, 16);
        if (!want[0] || !_strnicmp(a, want, 3)) return;
        lang[0] = (wchar_t)(unsigned char)want[0];
        lang[1] = (wchar_t)(unsigned char)want[1];
        lang[2] = (wchar_t)(unsigned char)want[2];
        return;
    }
}

extern "C" void H_LipSprintf(wchar_t* dest, const wchar_t* fmt, const wchar_t* a, const wchar_t* b) {
    typedef void (__cdecl *Fn)(wchar_t*, const wchar_t*, const wchar_t*, const wchar_t*);
    ((Fn)VA(FUNC_LIP_SPRINTF))(dest, fmt, a, b);
    if (dest) ApplyEffectiveAudioW(dest);
}

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

static int CopyFallbackA(const char* src, char* dst, int n) {
    if (!src || !dst || n < 16) return 0;
    int len = (int)strlen(src);
    if (len <= 0 || len >= n) return 0;
    memcpy(dst, src, len + 1);
    int forced = ApplyEffectiveAudioA(dst);
    return FallbackMissingA(dst) || forced;
}

static int CopyFallbackW(const wchar_t* src, wchar_t* dst, int n) {
    if (!src || !dst || n < 16) return 0;
    char a[MAX_PATH], b[MAX_PATH];
    if (!WideCharToMultiByte(CP_ACP, 0, src, -1, a, MAX_PATH, 0, 0)) return 0;
    memcpy(b, a, strlen(a) + 1);
    int forced = ApplyEffectiveAudioA(b);
    if (!FallbackMissingA(b) && !forced) return 0;
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
    const BYTE setLang[5] = {0x8B, 0x5C, 0x24, 0x08, 0x53};
    if (Check(SITE_SET_CUR_LANG, setLang, 5)) {
        g_setLangCont = VA(SITE_SET_CUR_LANG + 5);
        PatchRel32(SITE_SET_CUR_LANG, 0xE9, (void*)H_SetCurrentLanguageEntry);
    }
    const BYTE lipCall[1] = {0xE8};
    if (Check(SITE_LIP_SPRINTF, lipCall, 1) && Rel32Target(SITE_LIP_SPRINTF) == VA(FUNC_LIP_SPRINTF))
        PatchRel32(SITE_LIP_SPRINTF, 0xE8, (void*)H_LipSprintf);
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
