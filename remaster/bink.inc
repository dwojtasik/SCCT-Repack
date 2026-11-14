// ------------------------------------------------------------------------------------------------
// bink.inc - 4K Bink blit target and language audio track.
//
// Stock Bink language tracks: int=0, fra=4, esp=8, ita=12, deu=16. Polish is 20.
// Russian is 24. Czech, Portuguese, Slovak and Hungarian have no Bink track — LangTrackBase is 0.
// BinkSetSoundTrack is called before BinkOpen. If that track is missing, fall back to track 0.
// ------------------------------------------------------------------------------------------------
static const DWORD SITE_BINK_TEX1 = 0x10C86200;
static const DWORD SITE_BINK_TEX2 = 0x10C8C015;
static const DWORD SITE_BINK_TEX3 = 0x10CA4134;
static const DWORD SITE_BINK_UV_H = 0x10C86490;   // fmul 1/480
static const DWORD SITE_BINK_UV_W = 0x10C864AB;   // fmul 1/640
static const DWORD IAT_BINKCLOSE          = 0x110B76F4;
static const DWORD IAT_BINKOPEN           = 0x110B770C;
static const DWORD IAT_BINKSETSOUNDTRACK  = 0x110B7710;

typedef void (__stdcall *BinkSetSoundTrackFn)(unsigned nTracks, unsigned* tracks);
typedef void* (__stdcall *BinkOpenFn)(const char* name, unsigned flags);
typedef void (__stdcall *BinkCloseFn)(void* bink);

static BinkSetSoundTrackFn o_BinkSetSoundTrack;
static BinkOpenFn          o_BinkOpen;
static BinkCloseFn         o_BinkClose;
static unsigned            g_binkTracks[4];

static void VideoOnBinkOpen(void* bink, const char* name);
static void VideoOnBinkClose(void* bink);

static int LangTrackBase(const char* lang) {
    if (!lang || !lang[0]) return 0;
    if (!_stricmp(lang, "fra")) return 4;
    if (!_stricmp(lang, "esp")) return 8;
    if (!_stricmp(lang, "ita")) return 12;
    if (!_stricmp(lang, "deu")) return 16;
    if (!_stricmp(lang, "pol")) return 20;
    if (!_stricmp(lang, "rus")) return 24;
    return 0;
}

static void FillLangTracks(unsigned* out, int base) {
    for (int i = 0; i < 4; i++) out[i] = (unsigned)(base + i);
}

static const char* PathLeaf(const char* path) {
    if (!path || !path[0]) return path;
    const char* a = strrchr(path, '\\');
    const char* b = strrchr(path, '/');
    if (a && b) return (a > b ? a : b) + 1;
    if (a) return a + 1;
    if (b) return b + 1;
    return path;
}

static FILE* BikTryOpen(const char* path) {
    return (path && path[0]) ? fopen(path, "rb") : 0;
}

static FILE* BikOpenHeader(const char* name) {
    if (!name || !name[0]) return 0;
    FILE* f = BikTryOpen(name);
    if (f) return f;
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (!slash) return 0;
    *slash = 0;
    char path[MAX_PATH];
    snprintf(path, sizeof path, "%s\\%s", exe, name);
    f = BikTryOpen(path);
    if (f) return f;
    snprintf(path, sizeof path, "%s\\..\\Data\\Videos\\%s", exe, PathLeaf(name));
    return BikTryOpen(path);
}

// 1 = track present, 0 = header read but track missing, -1 = file unreadable.
static int BikHasTrack(const char* name, unsigned id) {
    FILE* f = BikOpenHeader(name);
    if (!f) return -1;
    unsigned char hdr[44];
    if (fread(hdr, 1, 44, f) != 44 || hdr[0] != 'B' || hdr[1] != 'I' || hdr[2] != 'K') {
        fclose(f);
        return -1;
    }
    unsigned ntracks = 0;
    memcpy(&ntracks, hdr + 40, 4);
    if (ntracks == 0 || ntracks > 64) {
        fclose(f);
        return -1;
    }
    if (fseek(f, (long)(ntracks * 8), SEEK_CUR) != 0) {
        fclose(f);
        return -1;
    }
    int found = 0;
    for (unsigned i = 0; i < ntracks; i++) {
        unsigned tid = 0;
        if (fread(&tid, 4, 1, f) != 1) break;
        if (tid == id) { found = 1; break; }
    }
    fclose(f);
    return found;
}

static void __stdcall H_BinkSetSoundTrack(unsigned nTracks, unsigned* tracks) {
    char lang[16];
    ReadAudioLanguage(lang, 16);
    FillLangTracks(g_binkTracks, LangTrackBase(lang));
    (void)nTracks;
    (void)tracks;
    if (o_BinkSetSoundTrack) o_BinkSetSoundTrack(4, g_binkTracks);
}

static void* __stdcall H_BinkOpen(const char* name, unsigned flags) {
    if (o_BinkSetSoundTrack && name) {
        char lang[16];
        ReadAudioLanguage(lang, 16);
        int base = LangTrackBase(lang);
        if (base != 0) {
            int has = BikHasTrack(name, (unsigned)base);
            if (has == 1) FillLangTracks(g_binkTracks, base);
            else FillLangTracks(g_binkTracks, 0);
            o_BinkSetSoundTrack(4, g_binkTracks);
        }
    }
    void* bink = o_BinkOpen ? o_BinkOpen(name, flags) : 0;
    if (bink) VideoOnBinkOpen(bink, name);
    return bink;
}

static void __stdcall H_BinkClose(void* bink) {
    VideoOnBinkClose(bink);
    if (o_BinkClose) o_BinkClose(bink);
}

static void PatchBinkUv(DWORD va, const BYTE* stock, DWORD scaleBits) {
    if (!Check(va, stock, 14)) return;
    BYTE p[14] = {0xEB, 0x06, 0, 0, 0, 0, 0x90, 0x90, 0xD8, 0x0D, 0, 0, 0, 0};
    memcpy(p + 2, &scaleBits, 4);
    DWORD abs = VA(va + 2);
    memcpy(p + 10, &abs, 4);
    WriteMem(VA(va), p, 14);
}

static void ApplyBinkLanguage() {
    DWORD iatTrack = VA(IAT_BINKSETSOUNDTRACK);
    DWORD iatOpen  = VA(IAT_BINKOPEN);
    DWORD iatClose = VA(IAT_BINKCLOSE);
    o_BinkSetSoundTrack = *(BinkSetSoundTrackFn*)iatTrack;
    o_BinkOpen          = *(BinkOpenFn*)iatOpen;
    o_BinkClose         = *(BinkCloseFn*)iatClose;
    if (!o_BinkSetSoundTrack || !o_BinkOpen) return;
    void* pTrack = (void*)H_BinkSetSoundTrack;
    void* pOpen  = (void*)H_BinkOpen;
    WriteMem(iatTrack, &pTrack, 4);
    WriteMem(iatOpen, &pOpen, 4);
    if (o_BinkClose) {
        void* pClose = (void*)H_BinkClose;
        WriteMem(iatClose, &pClose, 4);
    }
}

static void ApplyBink4K() {
    const BYTE stockWH[10] = {0x68, 0xE0, 0x01, 0x00, 0x00, 0x68, 0x80, 0x02, 0x00, 0x00};
    const BYTE hiWH[10]    = {0x68, 0x70, 0x08, 0x00, 0x00, 0x68, 0x00, 0x0F, 0x00, 0x00};
    static const DWORD texSites[] = {SITE_BINK_TEX1, SITE_BINK_TEX2, SITE_BINK_TEX3};
    for (DWORD va : texSites)
        if (Check(va, stockWH, 10))
            WriteMem(VA(va), hiWH, 10);

    const BYTE stockUvH[14] = {0x7D, 0x06, 0xD8, 0x05, 0x10, 0xE3, 0x0B, 0x11,
                               0xD8, 0x0D, 0xB4, 0xCE, 0x0C, 0x11};
    const BYTE stockUvW[14] = {0x7D, 0x06, 0xD8, 0x05, 0x10, 0xE3, 0x0B, 0x11,
                               0xD8, 0x0D, 0xB8, 0x5C, 0x0D, 0x11};
    PatchBinkUv(SITE_BINK_UV_H, stockUvH, 0x39F2B9D6);   // 1/2160
    PatchBinkUv(SITE_BINK_UV_W, stockUvW, 0x39888888);   // 1/3840
    ApplyBinkLanguage();
}
