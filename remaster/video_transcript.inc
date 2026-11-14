// ------------------------------------------------------------------------------------------------
// video_transcript.inc - [Localization] DialogShowVideos. JSON next to the BIK (Data\Videos\<clip>.json).
// Overlay language key; missing/empty → "int". Missing file → no text.
// Times follow Bink 1.6c FrameNum, not wall clock. No speaker names.
// ------------------------------------------------------------------------------------------------
static const int VID_MAX_CUES = 256;

struct VidCue {
    float start, end;
    wchar_t text[512];
};

static void*   g_vidBink;
static VidCue  g_vidCues[VID_MAX_CUES];
static int     g_vidCueN;
static int     g_vidCueAt;

static const char* VidSkipWs(const char* p) {
    while (p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
    return p;
}

static int VidHex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int VidParseString(const char** pp, wchar_t* out, int n) {
    const char* p = VidSkipWs(*pp);
    if (*p != '"') return 0;
    p++;
    char utf8[1024];
    int o = 0;
    while (*p && *p != '"' && o < (int)sizeof(utf8) - 8) {
        if (*p != '\\') {
            utf8[o++] = *p++;
            continue;
        }
        p++;
        char e = *p;
        if (!e) break;
        p++;
        if (e == 'n') utf8[o++] = '\n';
        else if (e == 't') utf8[o++] = ' ';
        else if (e == 'u') {
            int cp = 0;
            for (int i = 0; i < 4; i++) {
                int h = VidHex(p[i]);
                if (h < 0) { *pp = p; return 0; }
                cp = (cp << 4) | h;
            }
            p += 4;
            if (cp < 0x80) utf8[o++] = (char)cp;
            else if (cp < 0x800) {
                utf8[o++] = (char)(0xC0 | (cp >> 6));
                utf8[o++] = (char)(0x80 | (cp & 0x3F));
            } else {
                utf8[o++] = (char)(0xE0 | (cp >> 12));
                utf8[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                utf8[o++] = (char)(0x80 | (cp & 0x3F));
            }
        } else if (e != 'r') utf8[o++] = e;
    }
    if (*p != '"') return 0;
    *pp = p + 1;
    utf8[o] = 0;
    if (!out || n < 2) return 1;
    int m = MultiByteToWideChar(CP_UTF8, 0, utf8, o, out, n - 1);
    if (m <= 0) m = MultiByteToWideChar(CP_ACP, 0, utf8, o, out, n - 1);
    if (m < 0) m = 0;
    out[m < n ? m : n - 1] = 0;
    out[n - 1] = 0;
    return 1;
}

static int VidParseNumber(const char** pp, float* out) {
    const char* p = VidSkipWs(*pp);
    char buf[64];
    int i = 0;
    if (*p == '-' || *p == '+') buf[i++] = *p++;
    while ((*p >= '0' && *p <= '9') || *p == '.') {
        if (i < 63) buf[i++] = *p;
        p++;
    }
    buf[i] = 0;
    if (!i) return 0;
    if (out) *out = (float)atof(buf);
    *pp = p;
    return 1;
}

static int VidSkipValue(const char** pp) {
    const char* p = VidSkipWs(*pp);
    if (*p == '"') return VidParseString(pp, 0, 0);
    if (*p == '-' || *p == '+' || (*p >= '0' && *p <= '9'))
        return VidParseNumber(pp, 0);
    return 0;
}

static int VidParseKey(const char** pp, char* out, int n) {
    const char* p = VidSkipWs(*pp);
    if (*p != '"') return 0;
    p++;
    int i = 0;
    while (*p && *p != '"' && i < n - 1) {
        if (*p == '\\') p++;
        if (!*p) break;
        out[i++] = *p++;
    }
    if (*p != '"') return 0;
    out[i] = 0;
    *pp = p + 1;
    return 1;
}

static int VidParseCue(const char** pp, VidCue* cue) {
    const char* p = VidSkipWs(*pp);
    if (*p != '{') return 0;
    p++;
    memset(cue, 0, sizeof *cue);
    cue->end = -1;
    while (*p) {
        p = VidSkipWs(p);
        if (*p == '}') { *pp = p + 1; return cue->end >= cue->start && cue->text[0]; }
        if (*p == ',') { p++; continue; }
        char key[32] = {};
        if (!VidParseKey(&p, key, 32)) return 0;
        p = VidSkipWs(p);
        if (*p != ':') return 0;
        p++;
        if (!_stricmp(key, "start")) {
            if (!VidParseNumber(&p, &cue->start)) return 0;
        } else if (!_stricmp(key, "end")) {
            if (!VidParseNumber(&p, &cue->end)) return 0;
        } else if (!_stricmp(key, "text")) {
            if (!VidParseString(&p, cue->text, 512)) return 0;
        } else if (!VidSkipValue(&p)) return 0;
    }
    return 0;
}

static int VidParseLangArray(const char* json, const char* lang, VidCue* cues, int cap) {
    if (!json || !lang || !lang[0]) return 0;
    char pat[12];
    snprintf(pat, sizeof pat, "\"%s\"", lang);
    const char* p = json;
    const char* arr = 0;
    while ((p = strstr(p, pat))) {
        const char* q = VidSkipWs(p + strlen(pat));
        if (*q == ':') {
            q = VidSkipWs(q + 1);
            if (*q == '[') { arr = q; break; }
        }
        p++;
    }
    if (!arr) return 0;
    p = arr + 1;
    int n = 0;
    while (*p && n < cap) {
        p = VidSkipWs(p);
        if (*p == ']') break;
        if (*p == ',') { p++; continue; }
        VidCue cue;
        if (!VidParseCue(&p, &cue)) break;
        cues[n++] = cue;
    }
    return n;
}

static char* VidReadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 8 || sz > 4 * 1024 * 1024) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return 0; }
    if ((long)fread(buf, 1, (size_t)sz, f) != sz) { free(buf); fclose(f); return 0; }
    fclose(f);
    buf[sz] = 0;
    if ((unsigned char)buf[0] == 0xEF && (unsigned char)buf[1] == 0xBB && (unsigned char)buf[2] == 0xBF)
        memmove(buf, buf + 3, (size_t)sz - 2);
    return buf;
}

static int VidTryJsonPath(const char* path) {
    char* raw = VidReadFile(path);
    if (!raw) return 0;
    char lang[16] = {};
    ReadGameLanguage(lang, 16);
    int n = VidParseLangArray(raw, lang, g_vidCues, VID_MAX_CUES);
    if (n <= 0 && _stricmp(lang, "int"))
        n = VidParseLangArray(raw, "int", g_vidCues, VID_MAX_CUES);
    free(raw);
    return n;
}

static int VidLoadJson(const char* bik) {
    if (!bik || !bik[0]) return 0;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s", bik);
    char* ext = strrchr(path, '.');
    if (ext) {
        snprintf(ext, (size_t)(path + sizeof path - ext), ".json");
        int n = VidTryJsonPath(path);
        if (n > 0) return n;
    }
    char stem[MAX_PATH];
    snprintf(stem, sizeof stem, "%s", PathLeaf(bik));
    ext = strrchr(stem, '.');
    if (ext) *ext = 0;
    if (!stem[0]) return 0;
    char exe[MAX_PATH];
    ExeDir(exe, MAX_PATH);
    static const char* kRel[] = {
        "..\\Data\\Videos\\",
        "Data\\Videos\\",
        "..\\Videos\\",
        "Videos\\",
    };
    for (int i = 0; i < 4; i++) {
        snprintf(path, sizeof path, "%s%s%s.json", exe, kRel[i], stem);
        int n = VidTryJsonPath(path);
        if (n > 0) return n;
    }
    return 0;
}

static void VidClear() {
    g_vidBink = 0;
    g_vidCueN = 0;
    g_vidCueAt = -1;
}

static void VideoOnBinkOpen(void* bink, const char* name) {
    VidClear();
    g_vidBink = bink;
    if (!g_cfg.dialogShowVideos || !bink) return;
    g_vidCueN = VidLoadJson(name);
}

static void VideoOnBinkClose(void* bink) {
    if (bink && bink == g_vidBink) VidClear();
}

static int VideoIsActive() {
    return g_vidBink != 0;
}

static int VidFindCue(float t) {
    for (int i = 0; i < g_vidCueN; i++)
        if (t >= g_vidCues[i].start && t < g_vidCues[i].end)
            return i;
    return -1;
}

static int VidTime(void* bink, float* out) {
    if (!bink) return 0;
    unsigned* h = (unsigned*)bink;
    unsigned w = h[0], ht = h[1], frames = h[2], frame = h[3], rate = h[5], div = h[6];
    if (w < 16 || w > 8192 || ht < 16 || ht > 8192) return 0;
    if (frames < 1 || frames > 200000) return 0;
    // 4K RAD encodes use unreduced fps rationals (e.g. 10000000/333759).
    // Width/height/frames already reject a bad handle.
    if (rate < 1 || div < 1) return 0;
    unsigned num = frame > 0 ? frame - 1 : 0;
    *out = (float)((double)num * (double)div / (double)rate);
    return 1;
}

static int Video_OnPresent(IDirect3DDevice9* d) {
    if (!g_cfg.dialogShowVideos || !d || !g_vidBink || g_vidCueN <= 0) return 0;
    if (g_cfg.dialogVideoAlpha <= 0) return 1;
    float t = 0;
    if (!VidTime(g_vidBink, &t)) return 1;
    int idx = VidFindCue(t);
    if (idx < 0) {
        g_vidCueAt = -1;
        return 1;
    }
    const VidCue* c = &g_vidCues[idx];
    UINT sw = 0, sh = 0;
    if (!DlgBackSize(d, &sw, &sh)) return 1;
    if (!g_dlgTex || g_dlgDev != d || g_vidCueAt != idx) {
        if (!BuildDialogBitmap(d, c->text, 0, 0, sw, sh, 1))
            return 1;
        g_vidCueAt = idx;
    }
    if (!g_dlgTex) return 1;
    DrawDialogQuad(d, sw, sh);
    return 1;
}
