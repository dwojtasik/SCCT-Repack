// ------------------------------------------------------------------------------------------------
// dialog.inc - [Localization] DisplayDialogs overlay. Spoken lines from P_*.<lang> loc files.
// CNV/INT/SPC section = EchelonPattern bytecode Play_* stem (dialog_cues.inc).
// Line = Dare 01..N mapped through loc file order. Pattern class disambiguates
// colliding stems. COM from Magma Localize. Colour identity uses the Play_*
// speaker code (Ner vs Emi) so duplicate display names still get different colours.
// NPC barks: Magma PlaySound Play_random_* -> Dare type-4 pick -> type-1 leaf.
// Overlay the leaf only (dialog_hids.inc hid->Play_* is compiled in).
// Loc text: UTF-8 P_Barks.<lang> from ..\\Data\\Sounds\\localization (DLL in System\\).
// ------------------------------------------------------------------------------------------------
#include <vector>
#include <string>

static const DWORD SITE_NOTIFY_CONV     = 0x10A82CA0;   // AActor::execNotifyAudioEndConversation
static const DWORD SITE_ACTOR_PLAYSOUND = 0x10A7BCB0;   // AActor::PlaySound (thiscall, ret 18h)
static const DWORD SITE_EPATTERN_TALK   = 0x10B98800;   // EPattern Talk iNative 0x580
static const DWORD SITE_EPATTERN_SPEECH = 0x10B98CA0;   // EPattern Speech iNative 0x57F
static const DWORD SITE_GNATIVES        = 0x113127B8;   // GNatives[4096]
static const DWORD SITE_SPEECH_PLAY     = 0x10B91750;   // EPattern plays stored Speech USound via vtable+0x80
static const DWORD SITE_LOCALIZE        = 0x10CCC4B0;   // Localize(section, key, package, lang, optional, pathFlag) cdecl
static const DWORD SITE_MAGMA_TICK      = 0x10B252D0;   // Magma C++ init/tick (saves this)
static const DWORD SITE_LOADING_UI      = 0x10B64B40;   // Magma loading HUD show/hide (arg0=1 shows LoadingMsg)
static const DWORD SITE_LOADING_DONE    = 0x10B64BF0;   // Magma LoadingMsg <-> LoadingDone (arg0=1 is done)
static const DWORD SITE_LOADING_HUD_VT  = 0x110E5F40;   // loading HUD C++ vtable
static const DWORD MAGMA_LOADING_HUD    = 0x120;        // Magma+0x120 -> loading HUD
static const DWORD LOADING_DONE         = 0x5D;         // HUD+0x5D: 0=LoadingMsg, 1=LoadingDone
static const DWORD SITE_PAUSE_CTRL_VT   = 0x110E0DC0;   // pause menu controller vtable
static const DWORD SITE_DARE_SAMPLE     = 0x11044300;   // Dare plays a type-1 event (ecx=event, eax=live)
static const DWORD SITE_DARE_GETRES     = 0x11045590;   // Dare resource id -> sample object (stdcall)
static const DWORD SITE_DARE_STREAM1    = 0x1104413F;   // streamed play reads instance+0xC sample
static const DWORD SITE_DARE_STREAM2    = 0x11044181;
static const DWORD SITE_DARE_STREAM3    = 0x110441C3;
static const DWORD SITE_DARE_VOICE      = 0x11044475;   // non-streamed play reads instance+0xC sample
static const DWORD SITE_DARE_PICK       = 0x11055540;   // type-4 container picks the child that will play
static const DWORD MAGMA_PAUSE_CTRL     = 0xC50;        // Magma+0xC50 -> pause controller
static const DWORD PAUSE_SHOWN          = 0x58;         // controller+0x58: 1 while pause_PC is up

typedef void (__attribute__((thiscall)) *NativeFn)(void* self, void* stack, void* result);

static NativeFn o_Talk;
static NativeFn o_Speech;
static NativeFn o_NotifyConv;
static NativeFn o_SpeechPlay;
static NativeFn o_ObjectConst;
static int      g_inDlgNative;
extern "C" {
    DWORD g_notifyCont;
    DWORD g_talkCont;
    DWORD g_speechCont;
    DWORD g_speechPlayCont;
    DWORD g_actorPlayCont;
    DWORD o_AudioPlay;
    DWORD g_localizeCont;
    DWORD g_magmaTickCont;
    DWORD g_loadingUiCont;
    DWORD g_loadingDoneCont;
    DWORD g_dareSampleCont;
    DWORD g_dareStreamCont1;
    DWORD g_dareStreamCont2;
    DWORD g_dareStreamCont3;
    DWORD g_dareVoiceCont;
    DWORD g_darePickCont;
    void* g_magmaCpp;
    void* g_loadingHud;
}

static void* g_pendingDlg[32];
static int   g_pendingDlgN;

struct LocLine {
    char     section[96];
    int      id;
    wchar_t  text[768];
    wchar_t  speaker[64];
};

struct CueBind {
    char play[80];
    char section[96];
    int  id;
};

struct BarkLine {
    char     play[80];
    wchar_t  text[768];
};

static std::vector<LocLine> g_loc;
static std::vector<CueBind> g_cues;
static std::vector<BarkLine> g_barks;
static int      g_cueMapReady;
static char     g_locLang[16];
static char     g_locDir[MAX_PATH];
static CRITICAL_SECTION g_dlgLock;
static bool     g_dlgLockInit;
static wchar_t  g_dlgText[768];
static wchar_t  g_dlgSpeaker[64];
static int      g_dlgSpeakerIdx;
static char     g_lastCueCode[12];
static DWORD    g_lastCueCodeAt;
static char     g_colorSec[96];
static char     g_colorExtra[12][12];
static int      g_colorExtraN;
static DWORD    g_dlgHideAt;
static char     g_patternSection[96];
static char     g_localizeSec[96];
static int      g_dlgFromLocalize;
static IDirect3DTexture9* g_dlgTex;
static UINT     g_dlgTexW, g_dlgTexH;
static IDirect3DDevice9*  g_dlgDev;
static HFONT    g_dlgFont, g_vidFont;
static int      g_dlgFontSize, g_dlgFontBold;
static int      g_vidFontSize, g_vidFontBold;
static DWORD    g_dlgDirty;
static int      g_locLoading;
static int      g_locInited;
static int      g_pauseTrustShown = 1;
static int      g_loadStarted;
static int      g_dlgWasPaused;
static DWORD    g_dlgPauseStart;
static int      g_missionLocLoaded[12];
static DWORD    g_pendingRandomAt;
static DWORD    g_pendingRandomHid;
static char     g_lastBarkKey[80];
static DWORD    g_lastBarkAt;

extern "C" void __attribute__((thiscall)) SpeechPlay_Trampoline();
extern "C" void __attribute__((thiscall)) NotifyConv_Trampoline();
extern "C" void __attribute__((thiscall)) Talk_Trampoline();
extern "C" void __attribute__((thiscall)) Speech_Trampoline();
extern "C" void MagmaTick_Hook();
extern "C" void LoadingUi_Hook();
extern "C" void LoadingDone_Hook();
extern "C" void OnGameLoad();
extern "C" void OnLoadingUi(int show);
extern "C" void OnLoadingDone(int done);
extern "C" void ShowIfDialogSound(void* obj);
extern "C" void QueueDialogSound(void* obj);
extern "C" void EnqueuePlayedSound(void* obj);
extern "C" void EnqueuePlayedSoundEx(void* actor, void* obj);
extern "C" void ActorPlaySound_Hook();
extern "C" void AudioPlay_Hook();
extern "C" void DareSample_Hook();
extern "C" void DareStream_Hook1();
extern "C" void DareStream_Hook2();
extern "C" void DareStream_Hook3();
extern "C" void DareVoice_Hook();
extern "C" void* DarePick_Trampoline(void* container);
extern "C" void OnDareSampleEvent(void* event);
extern "C" void OnDareStreamInstance(void* inst);
extern "C" const wchar_t* __cdecl Localize_Trampoline(const wchar_t*, const wchar_t*,
    const wchar_t*, const wchar_t*, int, int);

asm(
    ".text\n"
    ".globl _SpeechPlay_Trampoline\n"
    "_SpeechPlay_Trampoline:\n"
    "    pushl %esi\n"
    "    movl %ecx, %esi\n"
    "    movl 0x7C(%esi), %eax\n"
    "    movl _g_speechPlayCont, %edx\n"
    "    jmpl *%edx\n"
    ".globl _ActorPlaySound_Hook\n"
    "_ActorPlaySound_Hook:\n"
    "    pushal\n"
    "    movl 36(%esp), %eax\n"
    "    pushl %eax\n"
    "    movl 28(%esp), %eax\n"
    "    pushl %eax\n"
    "    call _EnqueuePlayedSoundEx\n"
    "    addl $8, %esp\n"
    "    popal\n"
    "    movl 0x7C(%ecx), %eax\n"
    "    movl 0x38(%eax), %edx\n"
    "    movl _g_actorPlayCont, %eax\n"
    "    jmpl *%eax\n"
    ".globl _AudioPlay_Hook\n"
    "_AudioPlay_Hook:\n"
    "    pushal\n"
    "    movl 40(%esp), %eax\n"
    "    pushl %eax\n"
    "    call _EnqueuePlayedSound\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl _o_AudioPlay, %eax\n"
    "    jmpl *%eax\n"
    ".globl _DareSample_Hook\n"
    "_DareSample_Hook:\n"
    "    pushal\n"
    "    pushl %ecx\n"
    "    call _OnDareSampleEvent\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    subl $0xe4, %esp\n"
    "    jmp *_g_dareSampleCont\n"
    ".globl _DareStream_Hook1\n"
    "_DareStream_Hook1:\n"
    "    pushal\n"
    "    pushl %esi\n"
    "    call _OnDareStreamInstance\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl 0xC(%esi), %eax\n"
    "    movl 0x54(%eax), %ecx\n"
    "    jmp *_g_dareStreamCont1\n"
    ".globl _DareStream_Hook2\n"
    "_DareStream_Hook2:\n"
    "    pushal\n"
    "    pushl %esi\n"
    "    call _OnDareStreamInstance\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl 0xC(%esi), %eax\n"
    "    movl 0x54(%eax), %ecx\n"
    "    jmp *_g_dareStreamCont2\n"
    ".globl _DareStream_Hook3\n"
    "_DareStream_Hook3:\n"
    "    pushal\n"
    "    pushl %esi\n"
    "    call _OnDareStreamInstance\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl 0xC(%esi), %eax\n"
    "    movl 0x54(%eax), %ecx\n"
    "    jmp *_g_dareStreamCont3\n"
    ".globl _DareVoice_Hook\n"
    "_DareVoice_Hook:\n"
    "    pushal\n"
    "    pushl %ebx\n"
    "    call _OnDareStreamInstance\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movl 0xC(%ebx), %eax\n"
    "    movl 0x54(%eax), %ecx\n"
    "    jmp *_g_dareVoiceCont\n"
    ".globl _DarePick_Trampoline\n"
    "_DarePick_Trampoline:\n"
    "    pushl %ecx\n"
    "    pushl %esi\n"
    "    pushl %edi\n"
    "    movl 0x10(%esp), %edi\n"
    "    jmp *_g_darePickCont\n"
    ".globl _Talk_Trampoline\n"
    "_Talk_Trampoline:\n"
    "    subl $0x20, %esp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    movl _g_talkCont, %eax\n"
    "    jmpl *%eax\n"
    ".globl _Speech_Trampoline\n"
    "_Speech_Trampoline:\n"
    "    subl $0x20, %esp\n"
    "    pushl %ebx\n"
    "    pushl %esi\n"
    "    movl _g_speechCont, %eax\n"
    "    jmpl *%eax\n"
    ".globl _NotifyConv_Trampoline\n"
    "_NotifyConv_Trampoline:\n"
    "    movl 4(%esp), %eax\n"
    "    pushl %esi\n"
    "    movl _g_notifyCont, %edx\n"
    "    jmpl *%edx\n"
    ".globl _Localize_Trampoline\n"
    "_Localize_Trampoline:\n"
    "    subl $0x204, %esp\n"
    "    movl _g_localizeCont, %eax\n"
    "    jmpl *%eax\n"
    ".globl _MagmaTick_Hook\n"
    "_MagmaTick_Hook:\n"
    "    movl %ecx, _g_magmaCpp\n"
    "    subl $0xC, %esp\n"
    "    pushl %ebx\n"
    "    pushl %ebp\n"
    "    pushl %esi\n"
    "    movl _g_magmaTickCont, %eax\n"
    "    jmpl *%eax\n"
    ".globl _LoadingUi_Hook\n"
    "_LoadingUi_Hook:\n"
    "    movl %ecx, _g_loadingHud\n"
    "    pushal\n"
    "    movzbl 36(%esp), %eax\n"
    "    pushl %eax\n"
    "    call _OnLoadingUi\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    movb 4(%esp), %al\n"
    "    testb %al, %al\n"
    "    movl _g_loadingUiCont, %edx\n"
    "    jmpl *%edx\n"
    ".globl _LoadingDone_Hook\n"
    "_LoadingDone_Hook:\n"
    "    movl %ecx, _g_loadingHud\n"
    "    pushal\n"
    "    movzbl 36(%esp), %eax\n"
    "    pushl %eax\n"
    "    call _OnLoadingDone\n"
    "    addl $4, %esp\n"
    "    popal\n"
    "    pushl %ebx\n"
    "    movb 8(%esp), %bl\n"
    "    movl _g_loadingDoneCont, %edx\n"
    "    jmpl *%edx\n"
);

static DWORD ParseHexColor(const char* s, DWORD def) {
    while (*s == ' ' || *s == '\t' || *s == '#') s++;
    if (!*s) return def;
    unsigned v = 0;
    for (int i = 0; i < 6 && s[i]; i++) {
        char c = s[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else { v >>= 4; break; }
    }
    return v & 0xFFFFFFu;
}

enum { MAX_DLG_SPEAKERS = 12 };

static void ParseSpeakerColors(const char* s) {
    static const DWORD kDef[MAX_DLG_SPEAKERS] = {
        0xFFFF00, 0x00FFFF, 0xFF7F00, 0x7FFF00, 0xFF77FF, 0x7F7FFF,
        0xFF4444, 0x44FFAA, 0xAACCFF, 0xFFAA44, 0xDD88FF, 0x88FF88
    };
    for (int i = 0; i < MAX_DLG_SPEAKERS; i++) g_cfg.dialogSpeakerColor[i] = kDef[i];
    int n = 0;
    const char* p = s;
    while (*p && n < MAX_DLG_SPEAKERS) {
        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (!*p) break;
        g_cfg.dialogSpeakerColor[n] = ParseHexColor(p, kDef[n]);
        n++;
        while (*p && *p != ',') p++;
    }
    g_cfg.dialogSpeakerColorN = n > 0 ? n : MAX_DLG_SPEAKERS;
}

static int ParseLangToken(const char* s, char* out, int n) {
    if (!out || n < 4) return 0;
    out[0] = 0;
    if (!s) return 0;
    while (*s == ' ' || *s == '\t') s++;
    char tmp[16] = {};
    int i = 0;
    while (s[i] && s[i] != ' ' && s[i] != '\t' && i < 15) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        tmp[i++] = c;
    }
    tmp[i] = 0;
    static const char* kLang[] = { "cze", "deu", "esp", "fra", "hun", "int", "ita", "pol", "prt", "rus", "svk" };
    for (int k = 0; k < (int)(sizeof kLang / sizeof kLang[0]); k++) {
        if (_stricmp(tmp, kLang[k])) continue;
        snprintf(out, n, "%s", kLang[k]);
        return 1;
    }
    return 0;
}

static void LoadLangOverride(const char* ini, const char* sec, const char* key, char* out, int n) {
    char ov[16] = {};
    GetPrivateProfileStringA(sec, key, "", ov, sizeof ov, ini);
    ParseLangToken(ov, out, n);
}

static int IniHas(const char* sec, const char* key, const char* ini) {
    char buf[8] = {};
    GetPrivateProfileStringA(sec, key, "\x01", buf, sizeof buf, ini);
    return !(buf[0] == 1 && buf[1] == 0);
}

static int IniILoc(const char* key, int def, const char* ini) {
    if (IniHas("Localization", key, ini)) return IniI("Localization", key, def, ini);
    return IniI("UI", key, def, ini);
}

static float IniFLoc(const char* key, float def, const char* ini) {
    if (IniHas("Localization", key, ini)) return IniF("Localization", key, def, ini);
    return IniF("UI", key, def, ini);
}

static void IniStrLoc(const char* key, const char* def, char* out, int n, const char* ini) {
    if (IniHas("Localization", key, ini))
        GetPrivateProfileStringA("Localization", key, def, out, n, ini);
    else
        GetPrivateProfileStringA("UI", key, def, out, n, ini);
}

static int ClampI(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static void LoadDialogConfig(const char* ini) {
    int display = -1;
    if (IniHas("Localization", "DisplayDialogs", ini))
        display = IniI("Localization", "DisplayDialogs", 1, ini);
    else {
        display = GetPrivateProfileIntA("UI", "DisplayDialogs", -1, ini);
        if (display < 0) display = GetPrivateProfileIntA("UI", "ShowAllDialogs", -1, ini);
        if (display < 0) display = 1;
    }
    g_cfg.displayDialogs = display ? 1 : 0;
    LoadLangOverride(ini, "Localization", "OverrideTextLanguage", g_cfg.textLangOverride, sizeof g_cfg.textLangOverride);
    LoadLangOverride(ini, "Localization", "OverrideAudioLanguage", g_cfg.audioLangOverride, sizeof g_cfg.audioLangOverride);
    LoadLangOverride(ini, "Localization", "OverrideDialogsLanguage", g_cfg.dialogLangOverride, sizeof g_cfg.dialogLangOverride);
    if (!g_cfg.dialogLangOverride[0])
        LoadLangOverride(ini, "UI", "OverrideDialogsLanguage", g_cfg.dialogLangOverride, sizeof g_cfg.dialogLangOverride);
    g_cfg.hideSpeakerName  = IniILoc("HideSpeakerName", 0, ini);
    g_cfg.dialogSpeakerTrim = IniILoc("DialogSpeakerTrim", 1, ini) ? 1 : 0;
    int cueGuess = -1;
    if (IniHas("Localization", "DisplaySpeakerCueGuess", ini))
        cueGuess = IniI("Localization", "DisplaySpeakerCueGuess", 1, ini);
    else {
        cueGuess = GetPrivateProfileIntA("UI", "DisplaySpeakerCueGuess", -1, ini);
        if (cueGuess < 0) cueGuess = GetPrivateProfileIntA("UI", "DisplaySpeakerQueGuess", 1, ini);
    }
    g_cfg.displaySpeakerCueGuess = cueGuess ? 1 : 0;
    g_cfg.dialogShowEchelonComms = IniILoc("DialogShowEchelonComms", 0, ini) ? 1 : 0;
    g_cfg.dialogShowInterrogations = IniILoc("DialogShowInterrogations", 1, ini) ? 1 : 0;
    g_cfg.dialogShowVideos = IniILoc("DialogShowVideos", 1, ini) ? 1 : 0;
    g_cfg.dialogShowBarks = IniILoc("DialogShowBarks", 1, ini) ? 1 : 0;
    g_cfg.dialogPadPct     = IniFLoc("DialogBottomPaddingPercent", 5.0f, ini);
    g_cfg.dialogMaxWidthPct = IniILoc("DialogMaxWidthSizePercent", 50, ini);
    g_cfg.dialogFontSize   = IniILoc("DialogFontSize", 38, ini);
    g_cfg.dialogBold       = IniILoc("DialogBold", 1, ini) ? 1 : 0;
    g_cfg.dialogAlpha      = IniILoc("DialogAlpha", 255, ini);
    char col[64];
    IniStrLoc("DialogColor", "FFFFFF", col, sizeof col, ini);
    g_cfg.dialogColor = ParseHexColor(col, 0xFFFFFF);
    IniStrLoc("DialogBackgroundColor", "000000", col, sizeof col, ini);
    g_cfg.dialogBgColor = ParseHexColor(col, 0x000000);
    g_cfg.dialogBgAlpha = IniILoc("DialogBackgroundAlpha", 0, ini);
    g_cfg.dialogVideoFontSize = IniILoc("DialogVideoFontSize", g_cfg.dialogFontSize, ini);
    g_cfg.dialogVideoBold = IniILoc("DialogVideoBold", g_cfg.dialogBold, ini) ? 1 : 0;
    g_cfg.dialogVideoAlpha = IniILoc("DialogVideoAlpha", g_cfg.dialogAlpha, ini);
    IniStrLoc("DialogVideoColor", "FFFFFF", col, sizeof col, ini);
    g_cfg.dialogVideoColor = ParseHexColor(col, g_cfg.dialogColor);
    IniStrLoc("DialogVideoBackgroundColor", "000000", col, sizeof col, ini);
    g_cfg.dialogVideoBgColor = ParseHexColor(col, g_cfg.dialogBgColor);
    g_cfg.dialogVideoBgAlpha = IniILoc("DialogVideoBackgroundAlpha", 128, ini);
    char sp[256];
    IniStrLoc("DialogSpeakerColors",
        "FFFF00,00FFFF,FF7F00,7FFF00,FF77FF,7F7FFF,FF4444,44FFAA,AACCFF,FFAA44,DD88FF,88FF88",
        sp, sizeof sp, ini);
    ParseSpeakerColors(sp);
    g_cfg.dialogSpeakerColorsEnabled = IniILoc("DialogSpeakerColorsEnabled", 1, ini) ? 1 : 0;
    g_cfg.dialogSpeakerColors = IniILoc("DialogSpeakerUseDifferentColors", 1, ini) ? 1 : 0;
    g_cfg.dialogMatchLineColor = IniILoc("DialogMatchEntireLineColorWithSpeaker", 0, ini) ? 1 : 0;
    g_cfg.dialogFontSize = ClampI(g_cfg.dialogFontSize, 8, 96);
    g_cfg.dialogVideoFontSize = ClampI(g_cfg.dialogVideoFontSize, 8, 96);
    if (g_cfg.dialogPadPct < 0) g_cfg.dialogPadPct = 0;
    if (g_cfg.dialogPadPct > 40) g_cfg.dialogPadPct = 40;
    g_cfg.dialogMaxWidthPct = ClampI(g_cfg.dialogMaxWidthPct, 10, 100);
    g_cfg.dialogBgAlpha = ClampI(g_cfg.dialogBgAlpha, 0, 255);
    g_cfg.dialogAlpha = ClampI(g_cfg.dialogAlpha, 0, 255);
    g_cfg.dialogVideoBgAlpha = ClampI(g_cfg.dialogVideoBgAlpha, 0, 255);
    g_cfg.dialogVideoAlpha = ClampI(g_cfg.dialogVideoAlpha, 0, 255);
}

static int Video_OnPresent(IDirect3DDevice9* d);
static int VideoIsActive();

static void EnsureDlgLock() {
    if (g_dlgLockInit) return;
    InitializeCriticalSection(&g_dlgLock);
    g_dlgLockInit = true;
}

static void ReadSettingsLanguage(char* out, int n);
static void ReadTextLanguage(char* out, int n);
static void ReadGameLanguage(char* out, int n);
static int LocLangExists(const char* lang);
static void FallbackLocLang(const char* want, char* out, int n);
static void EffectiveTextLanguage(char* out, int n);
static void EffectiveGameLanguage(char* out, int n);

// Magma Localize converts loc with MultiByteToWideChar(CP_ACP). Western ACP is 1252, which
// misreads CP1250/1251/... loc. Pick the ANSI code page from the text language (extend this
// table for new langs) or from bytes when Polish was packed into .int.
static UINT g_detectedLocCp;

static UINT CodePageForLangToken(const char* lang) {
    return OverlayCpForLang(lang);
}

static int LooksLikeCp1250A(const char* s, int n) {
    if (!s) return 0;
    if (n < 0) n = (int)strlen(s);
    for (int i = 0; i < n; i++) {
        unsigned char b = (unsigned char)s[i];
        // Unique vs CP1252/French: Ą ą ł Ź Ż
        if (b == 0xA5 || b == 0xB9 || b == 0xB3 || b == 0x8F || b == 0xAF)
            return 1;
    }
    return 0;
}

static int LooksLikeCp1251A(const char* s, int n) {
    if (!s) return 0;
    if (n < 0) n = (int)strlen(s);
    for (int i = 0; i < n; i++) {
        unsigned char b = (unsigned char)s[i];
        if (b == 0xA8 || b == 0xB8 || b == 0xAA || b == 0xBA)
            return 1;
    }
    return 0;
}

static UINT DetectLocCodePage(const char* s, int n) {
    if (LooksLikeCp1250A(s, n)) return 1250;
    if (LooksLikeCp1251A(s, n)) return 1251;
    return 0;
}

static UINT CodePageForAnsi(const char* s, int n) {
    char lang[16];
    EffectiveTextLanguage(lang, 16);
    UINT langCp = CodePageForLangToken(lang);
    if (langCp) return langCp;
    UINT det = DetectLocCodePage(s, n);
    if (det) {
        g_detectedLocCp = det;
        return det;
    }
    return g_detectedLocCp;
}

static UINT LocCodePage() {
    char lang[16];
    EffectiveTextLanguage(lang, 16);
    UINT cp = CodePageForLangToken(lang);
    if (cp) return cp;
    if (g_detectedLocCp) return g_detectedLocCp;
    return CP_ACP;
}

static UINT CodePageForBytes(const char* s, int n, UINT hint) {
    UINT cp = CodePageForAnsi(s, n);
    if (cp) return cp;
    return hint ? hint : CP_ACP;
}

static void FixOverlayWide(wchar_t* s, int n) {
    char lang[16];
    EffectiveGameLanguage(lang, 16);
    OverlayFixWide(s, n, lang);
}

static void ToWide(const char* src, wchar_t* dst, int n) {
    dst[0] = 0;
    if (!src || n < 2) return;
    UINT cp = CodePageForBytes(src, -1, LocCodePage());
    int m = MultiByteToWideChar(cp, 0, src, -1, dst, n);
    if (m <= 0) dst[0] = 0;
    dst[n - 1] = 0;
    char lang[16];
    EffectiveTextLanguage(lang, 16);
    OverlayFixWide(dst, n, lang);
}

static void ToWideUtf8(const char* src, wchar_t* dst, int n) {
    dst[0] = 0;
    if (!src || n < 2) return;
    int m = MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, n);
    if (m <= 0) {
        ToWide(src, dst, n);
        return;
    }
    dst[n - 1] = 0;
}

static void StripTrailing(char* s) {
    int n = (int)strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) s[--n] = 0;
}

static int ContainsI(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return 0;
    size_t n = strlen(needle);
    for (const char* p = hay; *p; p++) {
        size_t i = 0;
        while (i < n) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
            if (!a || a != b) break;
            i++;
        }
        if (i == n) return 1;
    }
    return 0;
}

enum { DLG_NORMAL = 0, DLG_INTERRO = 1, DLG_ECHELON = 2, DLG_SKIP = 3, DLG_PROP = 4 };

static int ContainsAny(const char* s, const char* const* pats, int n) {
    for (int i = 0; i < n; i++)
        if (ContainsI(s, pats[i])) return 1;
    return 0;
}

static int LocKind(const char* s) {
    if (!s || !*s) return DLG_NORMAL;
    static const char* kInterro[] = {
        "_Int", "P_01_Panama_VPOffice", "_backalleyinterro", "_interro",
        "_Jonginterro", "_NDTinterog"
    };
    static const char* kEchelon[] = {
        "_Communications", "_LambertComms", "_GCS_Timer",
        "_Penthouse_AlarmState", "_Penthouse_CamerasRestantes",
        "_Penthouse_Gnome_killed", "_Penthouse_Gnome_Lambert",
        "_Penthouse_Gnome_stunned", "_Lambert_Comm", "_Penthouse_SamIsAKiller",
        "_Displace01_AlarmCheck", "_Displace01_DHackSequence",
        "_Displace01_DHackSuccess", "_Displace01_Lambert", "_Displace01_NoKill",
        "_Displace01_Objectives", "_LambertConv", "_NuclearPlant_OBJ",
        "_Hokkaido_AlarmComms", "_Hokkaido_CommSummaries", "_Hokkaido_Helico",
        "_UNhq_BloodMessage", "_UNhq_Objectives", "_UNhq_Startingmessage",
        "_UNhq_Ventilationroomsg", "_HQcom", "_SeoulTwo_Laser", "_ComSummaries"
    };
    static const char* kSkip[] = {
        "_Popup", "_Objectives", "_Master", "_AlarmSystem", "_AlarmManager",
        "_AlarmCheck", "_Briefings"
    };
    static const char* kProp[] = {
        "_Propaganda", "_NK_Snippets", "_RadioPlay", "_RadioTV"
    };
    static const char* kInterroExclude[] = {
        "InteractiveObjects", "InteriorGardenChat", "Gnome_IntheDvorak"
    };
    if (ContainsAny(s, kInterro, (int)(sizeof kInterro / sizeof kInterro[0])) &&
        !ContainsAny(s, kInterroExclude, (int)(sizeof kInterroExclude / sizeof kInterroExclude[0])))
        return DLG_INTERRO;
    if (ContainsAny(s, kEchelon, (int)(sizeof kEchelon / sizeof kEchelon[0]))) return DLG_ECHELON;
    if (ContainsAny(s, kSkip, (int)(sizeof kSkip / sizeof kSkip[0]))) return DLG_SKIP;
    if (ContainsAny(s, kProp, (int)(sizeof kProp / sizeof kProp[0]))) return DLG_PROP;
    return DLG_NORMAL;
}

static int SkipDialogSection(const char* s) {
    if (!s || !*s) return 0;
    int k = LocKind(s);
    if (k == DLG_SKIP) return 1;
    if (k == DLG_ECHELON && !g_cfg.dialogShowEchelonComms) return 1;
    if (k == DLG_INTERRO && !g_cfg.dialogShowInterrogations) return 1;
    return 0;
}

static int ParseSpeechId(const char* key) {
    if (_strnicmp(key, "Speech_", 7) != 0) return -1;
    const char* p = key + 7;
    if (*p == '-') return -1;
    int v = 0;
    if (*p < '0' || *p > '9') return -1;
    while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
    if (*p == 'L' || *p == 'l') p++;
    if (*p && *p != ' ' && *p != '\t') return -1;
    return v;
}

static int SkipJunkValue(const char* v) {
    while (*v == ' ' || *v == '\t') v++;
    if (!*v) return 1;
    if (!_stricmp(v, "(null)")) return 1;
    if (!_strnicmp(v, "POPUP", 5)) return 1;
    return 0;
}

static void LoadLocBuffer(const char* data, size_t sz) {
    if (!data || !sz) return;
    const char* p = data;
    const char* end = data + sz;
    if (sz >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF) p += 3;
    char section[96] = {};
    while (p < end) {
        const char* e = p;
        while (e < end && *e && *e != '\n' && *e != '\r') e++;
        char line[1024];
        int n = (int)(e - p);
        if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
        memcpy(line, p, n); line[n] = 0;
        while (e < end && (*e == '\n' || *e == '\r')) e++;
        p = e;
        StripTrailing(line);
        if (line[0] == ';' || line[0] == '#' || !line[0]) continue;
        if (line[0] == '[') {
            char* rb = strchr(line, ']');
            if (!rb) continue;
            *rb = 0;
            snprintf(section, sizeof section, "%s", line + 1);
            continue;
        }
        if (!section[0] || SkipDialogSection(section)) continue;
        if (!_strnicmp(line, "POPUP", 5)) continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        StripTrailing(line);
        int id = ParseSpeechId(line);
        if (id < 0) continue;
        const char* val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        if (SkipJunkValue(val)) continue;
        LocLine L = {};
        snprintf(L.section, sizeof L.section, "%s", section);
        L.id = id;
        ToWide(val, L.text, 768);
        if (!L.text[0]) continue;
        g_loc.push_back(L);
    }
}

static void LoadLocFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return;
    std::string raw;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        raw.append(buf, n);
        if (raw.size() > 8 * 1024 * 1024) break;
    }
    fclose(f);
    if (raw.empty()) return;
    LoadLocBuffer(raw.c_str(), raw.size());
}

static void ExeDir(char* out, int n);
static void DllDir(char* out, int n);

static void LowerPlayKey(char* s) {
    for (; *s; s++)
        if (*s >= 'A' && *s <= 'Z') *s = (char)(*s - 'A' + 'a');
}

static void LoadBarkBuffer(const char* data, size_t sz) {
    if (!data || !sz) return;
    const char* p = data;
    const char* end = data + sz;
    if (sz >= 3 && (unsigned char)p[0] == 0xEF && (unsigned char)p[1] == 0xBB && (unsigned char)p[2] == 0xBF)
        p += 3;
    char section[96] = {};
    while (p < end) {
        const char* e = p;
        while (e < end && *e && *e != '\n' && *e != '\r') e++;
        char line[1024];
        int n = (int)(e - p);
        if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
        memcpy(line, p, n); line[n] = 0;
        while (e < end && (*e == '\n' || *e == '\r')) e++;
        p = e;
        StripTrailing(line);
        if (line[0] == ';' || line[0] == '#' || !line[0]) continue;
        if (line[0] == '[') {
            char* rb = strchr(line, ']');
            if (!rb) continue;
            *rb = 0;
            snprintf(section, sizeof section, "%s", line + 1);
            continue;
        }
        if (section[0] && _stricmp(section, "P_Barks") != 0) continue;
        char* eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        StripTrailing(line);
        if (_strnicmp(line, "Play_", 5) != 0) continue;
        const char* val = eq + 1;
        while (*val == ' ' || *val == '\t') val++;
        if (SkipJunkValue(val)) continue;
        BarkLine L = {};
        snprintf(L.play, sizeof L.play, "%s", line);
        LowerPlayKey(L.play);
        ToWideUtf8(val, L.text, 768);
        if (!L.text[0] || !wcscmp(L.text, L"(null)")) continue;
        g_barks.push_back(L);
    }
}

#include "dialog_hids.inc"

static int HidIdCmp(const void* a, const void* b) {
    DWORD ha = *(const DWORD*)a;
    DWORD hb = *(const DWORD*)b;
    return (ha > hb) - (ha < hb);
}

static const char* HidToPlay(DWORD hid) {
    if (!hid || kBarkHidN < 1) return 0;
    const DWORD* hit = (const DWORD*)bsearch(&hid, kBarkHid, (size_t)kBarkHidN,
                                             sizeof(DWORD), HidIdCmp);
    if (!hit) return 0;
    int i = (int)(hit - kBarkHid);
    return kBarkHidStr + kBarkHidOff[i];
}

static int LoadBarkPath(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    std::string raw;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        raw.append(buf, n);
        if (raw.size() > 8 * 1024 * 1024) break;
    }
    fclose(f);
    if (raw.empty()) return 0;
    size_t before = g_barks.size();
    LoadBarkBuffer(raw.c_str(), raw.size());
    return g_barks.size() > before ? 1 : 0;
}

static void LoadBarkFiles(const char* lang) {
    g_barks.clear();
    const char* use = (lang && lang[0]) ? lang : "int";
    char dll[MAX_PATH] = {}, dir[MAX_PATH] = {}, path[MAX_PATH] = {};
    DllDir(dll, MAX_PATH);
    if (!dll[0]) ExeDir(dll, MAX_PATH);
    snprintf(dir, sizeof dir, "%s..\\Data\\Sounds\\localization\\", dll);
    snprintf(path, sizeof path, "%sP_Barks.%s", dir, use);
    if (!LoadBarkPath(path) && _stricmp(use, "int") != 0) {
        snprintf(path, sizeof path, "%sP_Barks.int", dir);
        LoadBarkPath(path);
    }
}

static void ReadSettingsLanguage(char* out, int n) {
    snprintf(out, n, "int");
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    char* slash = strrchr(exe, '\\');
    if (slash) slash[1] = 0;
    const char* files[] = {"Settings.ini", "SplinterCell3User.ini", "SplinterCell3.ini", "Default.ini"};
    for (int i = 0; i < 4; i++) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s%s", exe, files[i]);
        char buf[32] = {};
        GetPrivateProfileStringA("Localization", "Language", "", buf, sizeof buf, path);
        if (!buf[0] || !_stricmp(buf, "None"))
            GetPrivateProfileStringA("Engine.Engine", "Language", "", buf, sizeof buf, path);
        if (!buf[0] || !_stricmp(buf, "None"))
            GetPrivateProfileStringA("Core.System", "Language", "", buf, sizeof buf, path);
        if (buf[0] && _stricmp(buf, "None")) {
            snprintf(out, n, "%s", buf);
            break;
        }
    }
    for (char* q = out; *q; q++) {
        if (*q >= 'A' && *q <= 'Z') *q = (char)(*q - 'A' + 'a');
    }
}

static void ReadTextLanguage(char* out, int n) {
    if (g_cfg.textLangOverride[0]) {
        snprintf(out, n, "%s", g_cfg.textLangOverride);
        return;
    }
    ReadSettingsLanguage(out, n);
}

static void ReadAudioLanguage(char* out, int n) {
    if (g_cfg.audioLangOverride[0]) {
        snprintf(out, n, "%s", g_cfg.audioLangOverride);
        return;
    }
    ReadSettingsLanguage(out, n);
}

static void ReadGameLanguage(char* out, int n) {
    if (g_cfg.dialogLangOverride[0]) {
        snprintf(out, n, "%s", g_cfg.dialogLangOverride);
        return;
    }
    ReadTextLanguage(out, n);
}

static void ExeDir(char* out, int n) {
    GetModuleFileNameA(NULL, out, n);
    char* slash = strrchr(out, '\\');
    if (slash) slash[1] = 0;
    else out[0] = 0;
}

static void DllDir(char* out, int n) {
    HMODULE mods[3] = {
        GetModuleHandleA("AVIFIL32.dll"),
        GetModuleHandleA("Remaster.dll"),
        GetModuleHandleA(NULL)
    };
    for (int i = 0; i < 3; i++) {
        if (!mods[i]) continue;
        GetModuleFileNameA(mods[i], out, n);
        char* slash = strrchr(out, '\\');
        if (slash) {
            slash[1] = 0;
            return;
        }
    }
    out[0] = 0;
}

static int LocFileReadable(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) return 1;
    FILE* f = fopen(path, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static int ProbeLocDir(const char* dir, const char* lang) {
    char test[MAX_PATH];
    snprintf(test, sizeof test, "%sP_01_Lighthouse.%s", dir, lang);
    if (LocFileReadable(test)) return 1;
    snprintf(test, sizeof test, "%sP_01_Lighthouse.int", dir);
    if (LocFileReadable(test)) return 1;
    snprintf(test, sizeof test, "%sP_00_Training.int", dir);
    return LocFileReadable(test);
}

static int FindLocDir(char* out, int n, const char* lang) {
    const char* rel[] = {
        "..\\Data\\System\\Localization\\",
        "Data\\System\\Localization\\",
        "Localization\\",
        "..\\Localization\\",
    };
    for (int i = 0; i < 4; i++) {
        if (ProbeLocDir(rel[i], lang)) {
            snprintf(out, n, "%s", rel[i]);
            return 1;
        }
    }
    char cur[MAX_PATH];
    ExeDir(cur, MAX_PATH);
    for (int up = 0; up < 4 && cur[0]; up++) {
        for (int i = 0; i < 4; i++) {
            char cand[MAX_PATH];
            snprintf(cand, sizeof cand, "%s%s", cur, rel[i]);
            if (ProbeLocDir(cand, lang)) {
                snprintf(out, n, "%s", cand);
                return 1;
            }
        }
        char* slash = strrchr(cur, '\\');
        if (!slash) break;
        *slash = 0;
        slash = strrchr(cur, '\\');
        if (!slash) break;
        slash[1] = 0;
    }
    return 0;
}

static const char* kLocStem[] = {
    "P_00_Training", "P_00_Training_COOP", "P_01_Lighthouse", "P_01_Panama",
    "P_02_CargoShip", "P_02_seoulthree", "P_03_Bank", "P_03_ChemBunker",
    "P_04_GCS", "P_04_Penthouse", "P_05_Displace01", "P_05_NuclearPlant",
    "P_06_Hokkaido", "P_07_Battery", "P_07_UNhq", "P_08_SeoulOne",
    "P_09_SeoulTwo", "P_10_Bathhouse", "P_10_BathHouse", "P_11_KokuboSosho",
};

#include "dialog_sections.inc"
#include "dialog_speakers.inc"
#include "dialog_cues.inc"

static int LocSectionIndex(const char* section) {
    if (!section || !section[0]) return -1;
    int nsec = (int)(sizeof kLocSection / sizeof kLocSection[0]);
    for (int i = 0; i < nsec; i++)
        if (!_stricmp(kLocSection[i], section)) return i;
    return -1;
}

static int SectionHasSpeech(const char* section) {
    int idx = LocSectionIndex(section);
    if (idx < 0) return 0;
    return kLocFileOrder[kLocFileOrderOff[idx]] != 0;
}

static int IntSpeakerLookup(const char* section, int id, wchar_t* out, int n) {
    out[0] = 0;
    if (!section || id < 0 || n < 2) return 0;
    int sec = LocSectionIndex(section);
    if (sec < 0) return 0;
    int lo = 0, hi = kIntSpkN - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        int c = (int)kIntSpkSec[mid] - sec;
        if (!c) c = (int)kIntSpkId[mid] - id;
        if (c < 0) lo = mid + 1;
        else if (c > 0) hi = mid - 1;
        else {
            ToWide(kIntSpkPool[kIntSpkName[mid]], out, n);
            return 1;
        }
    }
    return 0;
}

static int LocalizeReady();
static void FillLocSpeakersFromInt();
static int GameIsPaused();
static void TrimSpeakerName(wchar_t* name, int n);
static void BuildCueMap();
static const CueBind* FindCue(const wchar_t* soundName);
static void SpeakerFromIntLine(const wchar_t* intText, wchar_t* speaker, int sn);
static void BodyFromLocLine(const wchar_t* locText, wchar_t* body, int bn);

static void LoadLocLanguage(const char* lang) {
    g_loc.clear();
    g_cues.clear();
    g_barks.clear();
    g_cueMapReady = 0;
    memset(g_missionLocLoaded, 0, sizeof g_missionLocLoaded);
    g_locLang[0] = 0;
    g_locDir[0] = 0;
    FindLocDir(g_locDir, MAX_PATH, lang);
    snprintf(g_locLang, sizeof g_locLang, "%s", lang);
    LoadBarkFiles(lang);
}

static void EnsureLoc() {
    if (g_locInited) return;
    char lang[16];
    EffectiveGameLanguage(lang, 16);
    if (!lang[0]) snprintf(lang, sizeof lang, "int");
    LoadLocLanguage(lang);
    g_locInited = 1;
}

static void EnsureBarks() {
    EnsureLoc();
    if (!g_barks.empty()) return;
    char lang[16];
    EffectiveGameLanguage(lang, 16);
    if (!lang[0]) snprintf(lang, sizeof lang, "int");
    LoadBarkFiles(lang);
}

static void SplitSpeaker(const wchar_t* in, wchar_t* speaker, int sn, wchar_t* body, int bn);

static int SoundMission(const wchar_t* name) {
    const wchar_t* leaf = NameLeaf(name);
    static const wchar_t* tags[] = { L"CNV_", L"INT_", L"SPC_", L"COM_", L"SRC_" };
    for (int t = 0; t < 5; t++) {
        const wchar_t* p = wcsstr(leaf, tags[t]);
        if (!p) continue;
        p += 4;
        if (p[0] >= L'0' && p[0] <= L'9' && p[1] >= L'0' && p[1] <= L'9')
            return (p[0] - L'0') * 10 + (p[1] - L'0');
    }
    return -1;
}

static int SoundCueStem(const wchar_t* name, char* out, int n) {
    out[0] = 0;
    if (!name || n < 8) return 0;
    const wchar_t* leaf = NameLeaf(name);
    static const wchar_t* tags[] = { L"CNV_", L"INT_", L"SPC_", L"COM_", L"SRC_" };
    const wchar_t* p = 0;
    for (int t = 0; t < 5; t++) {
        p = wcsstr(leaf, tags[t]);
        if (p) break;
    }
    if (!p) return 0;
    int i = 0;
    while (p[i] && i < n - 1) {
        wchar_t c = p[i];
        if (c == L'_') {
            int und = 0;
            for (int k = 0; k < i; k++) if (out[k] == '_') und++;
            if (und >= 2) break;
        }
        if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
            (c >= L'0' && c <= L'9') || c == L'_')
            out[i++] = (char)c;
        else break;
    }
    out[i] = 0;
    return out[0] && strchr(out, '_') ? 1 : 0;
}

static const char* LookupCueSection(const char* stem, const char* hint) {
    if (!stem || !stem[0]) return 0;
    int n = (int)(sizeof kCueStem / sizeof kCueStem[0]);
    const char* hit = 0;
    int nhit = 0;
    int hintOk = 0;
    for (int i = 0; i < n; i++) {
        if (_stricmp(kCueStem[i], stem) != 0) continue;
        nhit++;
        hit = kCueSection[i];
        if (hint && hint[0] && !_stricmp(kCueSection[i], hint)) hintOk = 1;
    }
    if (nhit == 1) return hit;
    if (nhit > 1 && hintOk) return hint;
    return 0;
}

static int FileOrderSpeechId(const char* section, int line1) {
    if (!section || line1 < 1) return -1;
    int idx = LocSectionIndex(section);
    if (idx < 0) return -1;
    const unsigned short* p = kLocFileOrder + kLocFileOrderOff[idx];
    int n = 0;
    while (p[n]) n++;
    if (n < 1) return -1;
    if (line1 > n) return -1;
    return p[line1 - 1];
}

static int  g_dlgPri;
enum { PRI_BARK = 0, PRI_COM = 1, PRI_PROP = 2, PRI_CNV = 3, PRI_INT = 4 };

static int OverlayPriNow() {
    EnsureDlgLock();
    DWORD now = GetTickCount();
    EnterCriticalSection(&g_dlgLock);
    int pri = (now < g_dlgHideAt) ? g_dlgPri : 0;
    LeaveCriticalSection(&g_dlgLock);
    return pri;
}

static const wchar_t* WcsFindPlay(const wchar_t* name) {
    if (!name) return 0;
    for (const wchar_t* s = name; *s; s++) {
        if ((s[0] == L'P' || s[0] == L'p') && (s[1] == L'l' || s[1] == L'L') &&
            (s[2] == L'a' || s[2] == L'A') && (s[3] == L'y' || s[3] == L'Y') && s[4] == L'_')
            return s;
    }
    return 0;
}

static int IsBarkPlayName(const wchar_t* name) {
    if (!name) return 0;
    if (wcsstr(name, L"Stop_") || wcsstr(name, L"Org_")) return 0;
    if (wcsstr(name, L"CNV_") || wcsstr(name, L"INT_") || wcsstr(name, L"SPC_") ||
        wcsstr(name, L"SRC_") || wcsstr(name, L"COM_") || wcsstr(name, L"BRF_"))
        return 0;
    return WcsFindPlay(name) != 0;
}

static int BarkNatLetter(wchar_t c) {
    return c == L'U' || c == L'u' || c == L'J' || c == L'j' ||
           c == L'K' || c == L'k' || c == L'S' || c == L's';
}

static int IsNpcBarkName(const wchar_t* name) {
    if (!name || wcsstr(name, L"Stop_") || wcsstr(name, L"Org_")) return 0;
    if (wcsstr(name, L"SCX_Barks") || wcsstr(name, L"Coop_Npc_Barks")) return 1;
    const wchar_t* p = WcsFindPlay(name);
    if (!p) return 0;
    if (!_wcsnicmp(p, L"Play_random_", 12)) p += 12;
    else p += 5;
    int ex = (p[0] == L'E' || p[0] == L'e') && (p[1] == L'X' || p[1] == L'x');
    int in = (p[0] == L'I' || p[0] == L'i') && (p[1] == L'N' || p[1] == L'n');
    return (ex || in) && BarkNatLetter(p[2]) && p[3] == L'_';
}

static int DialogPri(const wchar_t* sname, const char* section) {
    int k = LocKind(section);
    if (k == DLG_INTERRO || (sname && wcsstr(sname, L"INT_"))) return PRI_INT;
    if (k == DLG_ECHELON || (sname && wcsstr(sname, L"COM_"))) return PRI_COM;
    if (k == DLG_PROP) return PRI_PROP;
    if (sname && (wcsstr(sname, L"CNV_") || wcsstr(sname, L"SPC_") || wcsstr(sname, L"SRC_")))
        return PRI_CNV;
    if (sname && IsBarkPlayName(sname)) return PRI_BARK;
    return PRI_CNV;
}

static int SectionMission(const char* section) {
    if (!section) return -1;
    if ((section[0] == 'P' || section[0] == 'p') && section[1] == '_' &&
        section[2] >= '0' && section[2] <= '9' && section[3] >= '0' && section[3] <= '9' &&
        section[4] == '_')
        return (section[2] - '0') * 10 + (section[3] - '0');
    return -1;
}

static int SoundLineId(const wchar_t* name);
static void ResolveLocSection(char* section, int n);

static const LocLine* FindLocBySectionId(const char* section, int id) {
    if (!section || !section[0] || id < 0) return 0;
    for (size_t i = 0; i < g_loc.size(); i++) {
        if (g_loc[i].id == id && !_stricmp(g_loc[i].section, section))
            return &g_loc[i];
    }
    return 0;
}

static int CueSpeakerCode(const char* play, char* out, int n) {
    out[0] = 0;
    if (!play || n < 3) return 0;
    const char* us = 0;
    for (const char* p = play; *p; p++)
        if (*p == '_') us = p;
    if (!us || !us[1]) return 0;
    const char* p = us + 1;
    while (*p >= '0' && *p <= '9') p++;
    int i = 0;
    while (p[i] && i < n - 1) {
        char c = p[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) i++;
        else if ((c >= '0' && c <= '9') && i > 0) i++;
        else break;
    }
    if (i < 2 || i > 8) return 0;
    memcpy(out, p, (size_t)i);
    out[i] = 0;
    return 1;
}

static int GuessCueSpeaker(const char* code, wchar_t* out, int n) {
    out[0] = 0;
    if (!code || !code[0] || n < 2) return 0;
    static const struct { const char* code; const wchar_t* name; } kMap[] = {
        { "Fis", L"FISHER" },
        { "Lam", L"LAMBERT" },
        { "Gri", L"GRIMSDOTTIR" },
        { "Red", L"REDDING" },
        { "Tur", L"TURTLE" },
        { "Par", L"PARTRIDGE" },
        { "Pil", L"PILOT" },
        { "She", L"SHETLAND" },
        { "Oto", L"OTOMO" },
    };
    for (int i = 0; i < (int)(sizeof kMap / sizeof kMap[0]); i++) {
        if (_stricmp(kMap[i].code, code) != 0) continue;
        wcsncpy(out, kMap[i].name, n - 1);
        out[n - 1] = 0;
        return 1;
    }
    return 0;
}

static int SpeakerColorIndex(const char* section, const wchar_t* name, const char* cueCode) {
    int n = g_cfg.dialogSpeakerColorN;
    if (n < 1) n = MAX_DLG_SPEAKERS;
    if (n > MAX_DLG_SPEAKERS) n = MAX_DLG_SPEAKERS;
    if (cueCode && cueCode[0]) {
        char codes[MAX_DLG_SPEAKERS][12];
        int nc = 0;
        if (section && section[0]) {
            int nplay = (int)(sizeof kPlayName / sizeof kPlayName[0]);
            for (int i = 0; i < nplay && nc < MAX_DLG_SPEAKERS; i++) {
                if (_stricmp(kPlaySection[i], section) != 0) continue;
                char code[12];
                if (!CueSpeakerCode(kPlayName[i], code, 12)) continue;
                int seen = 0;
                for (int k = 0; k < nc; k++)
                    if (!_stricmp(codes[k], code)) { seen = 1; break; }
                if (seen) continue;
                snprintf(codes[nc], 12, "%s", code);
                nc++;
            }
        }
        for (int i = 0; i < nc; i++)
            if (!_stricmp(codes[i], cueCode)) return i % n;
        const char* sec = section && section[0] ? section : "";
        if (_stricmp(g_colorSec, sec) != 0) {
            snprintf(g_colorSec, sizeof g_colorSec, "%s", sec);
            g_colorExtraN = 0;
        }
        for (int i = 0; i < g_colorExtraN; i++)
            if (!_stricmp(g_colorExtra[i], cueCode))
                return (nc + i) % n;
        if (g_colorExtraN < MAX_DLG_SPEAKERS) {
            snprintf(g_colorExtra[g_colorExtraN], 12, "%s", cueCode);
            int idx = (nc + g_colorExtraN) % n;
            g_colorExtraN++;
            return idx;
        }
        return nc % n;
    }
    if (!name || !name[0]) return 0;
    wchar_t want[64];
    wcsncpy(want, name, 63);
    want[63] = 0;
    TrimSpeakerName(want, 64);
    if (!want[0]) return 0;
    wchar_t names[MAX_DLG_SPEAKERS][64];
    int nnames = 0;
    if (section && section[0]) {
        for (int line = 1; line <= 160 && nnames < MAX_DLG_SPEAKERS; line++) {
            int sid = FileOrderSpeechId(section, line);
            if (sid < 0) break;
            const LocLine* L = FindLocBySectionId(section, sid);
            wchar_t sp[64], body[8];
            const wchar_t* spn = 0;
            wchar_t locSp[64] = {};
            if (L && L->speaker[0]) {
                wcsncpy(locSp, L->speaker, 63);
                locSp[63] = 0;
                TrimSpeakerName(locSp, 64);
                spn = locSp;
            }
            if ((!spn || !spn[0]) && L && L->text[0]) {
                SplitSpeaker(L->text, sp, 64, body, 8);
                if (sp[0]) spn = sp;
            }
            if (!spn || !spn[0]) {
                IntSpeakerLookup(section, sid, sp, 64);
                if (sp[0]) spn = sp;
            }
            if (!spn || !spn[0]) continue;
            wchar_t got[64];
            wcsncpy(got, spn, 63);
            got[63] = 0;
            TrimSpeakerName(got, 64);
            if (!got[0]) continue;
            int seen = 0;
            for (int i = 0; i < nnames; i++)
                if (!_wcsicmp(names[i], got)) { seen = 1; break; }
            if (seen) continue;
            wcsncpy(names[nnames], got, 63);
            names[nnames][63] = 0;
            nnames++;
        }
        if (!nnames) {
            for (size_t i = 0; i < g_loc.size() && nnames < MAX_DLG_SPEAKERS; i++) {
                if (_stricmp(g_loc[i].section, section) != 0) continue;
                wchar_t sp[64], body[8];
                const wchar_t* spn = g_loc[i].speaker;
                if (!spn[0]) {
                    SplitSpeaker(g_loc[i].text, sp, 64, body, 8);
                    if (sp[0]) spn = sp;
                }
                if (!spn[0]) {
                    IntSpeakerLookup(section, g_loc[i].id, sp, 64);
                    if (sp[0]) spn = sp;
                }
                if (!spn[0]) continue;
                wchar_t got[64];
                wcsncpy(got, spn, 63);
                got[63] = 0;
                TrimSpeakerName(got, 64);
                if (!got[0]) continue;
                int seen = 0;
                for (int k = 0; k < nnames; k++)
                    if (!_wcsicmp(names[k], got)) { seen = 1; break; }
                if (seen) continue;
                wcsncpy(names[nnames], got, 63);
                names[nnames][63] = 0;
                nnames++;
            }
        }
    }
    for (int i = 0; i < nnames; i++)
        if (!_wcsicmp(names[i], want)) return i % n;
    return nnames % n;
}

static int EndsWithI(const char* s, const char* suf) {
    if (!s || !suf || !*suf) return 0;
    int ns = (int)strlen(s), n = (int)strlen(suf);
    if (n < 1 || ns < n) return 0;
    return !_stricmp(s + ns - n, suf);
}

static int UniqueSectionByNeedle(int mission, const char* needle, char* out, int n) {
    if (!needle || !needle[0] || !out || n < 8) return 0;
    int nlen = (int)strlen(needle);
    if (nlen < 5) return 0;
    int nsec = (int)(sizeof kLocSection / sizeof kLocSection[0]);
    int found = -1, nfound = 0;
    for (int i = 0; i < nsec; i++) {
        if (mission >= 0 && SectionMission(kLocSection[i]) != mission) continue;
        if (!SectionHasSpeech(kLocSection[i])) continue;
        if (LocKind(kLocSection[i]) == DLG_SKIP) continue;
        if (!ContainsI(kLocSection[i], needle) && !EndsWithI(kLocSection[i], needle))
            continue;
        found = i;
        nfound++;
    }
    if (nfound != 1) return 0;
    snprintf(out, n, "%s", kLocSection[found]);
    return 1;
}

static int PickCnvSection(char* sec, int n, const char* hint, const wchar_t* soundName) {
    if (!sec || n < 8 || !soundName) return 0;
    sec[0] = 0;
    char stem[32] = {};
    char hintSec[96] = {};
    if (hint && hint[0]) {
        snprintf(hintSec, sizeof hintSec, "%s", hint);
        ResolveLocSection(hintSec, 96);
    }
    if (!SoundCueStem(soundName, stem, 32)) return 0;
    const char* byCue = LookupCueSection(stem, hintSec[0] ? hintSec : 0);
    if (!byCue) return 0;
    if (SkipDialogSection(byCue)) return 0;
    snprintf(sec, n, "%s", byCue);
    return 1;
}

static void TrimSpeakerName(wchar_t* name, int n) {
    if (!g_cfg.dialogSpeakerTrim || !name || !name[0] || n < 2) return;
    wchar_t* last = 0;
    for (wchar_t* p = name; *p; p++)
        if (*p == L'_') last = p;
    if (!last || !last[1]) return;
    wchar_t tmp[64];
    wcsncpy(tmp, last + 1, 63);
    tmp[63] = 0;
    wcsncpy(name, tmp, n - 1);
    name[n - 1] = 0;
}

static void SplitSpeaker(const wchar_t* in, wchar_t* speaker, int sn, wchar_t* body, int bn) {
    speaker[0] = 0; body[0] = 0;
    if (!in || !in[0] || sn < 2 || bn < 2) return;
    if (in[0] == L'-') {
        const wchar_t* end = 0;
        int ok = 1;
        for (const wchar_t* p = in + 1; *p && (p - in) < 64; p++) {
            if (*p == L'-') { end = p; break; }
            wchar_t c = *p;
            if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
                  (c >= L'0' && c <= L'9') || c == L'_' || c == L' ')) {
                ok = 0;
                break;
            }
        }
        if (ok && end && end > in + 1) {
            int n = (int)(end - (in + 1));
            if (n < sn - 1) {
                wcsncpy(speaker, in + 1, n);
                speaker[n] = 0;
                const wchar_t* rest = end + 1;
                while (*rest == L' ' || *rest == L'\t') rest++;
                wcsncpy(body, rest, bn - 1);
                body[bn - 1] = 0;
                return;
            }
        }
    }
    const wchar_t* colon = 0;
    for (const wchar_t* p = in; *p && (p - in) < 48; p++) {
        if (*p == L':') { colon = p; break; }
        if (*p == L'-' && p > in && p[1] == L' ') { colon = p; break; }
    }
    if (!colon) { wcsncpy(body, in, bn - 1); body[bn - 1] = 0; return; }
    int n = (int)(colon - in);
    while (n > 0 && (in[n - 1] == L' ' || in[n - 1] == L'\t')) n--;
    int ok = n > 0 && n < sn - 1;
    for (int i = 0; ok && i < n; i++) {
        wchar_t c = in[i];
        if (!((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
              (c >= L'0' && c <= L'9') || c == L'_' || c == L' '))
            ok = 0;
    }
    if (!ok) { wcsncpy(body, in, bn - 1); body[bn - 1] = 0; return; }
    wcsncpy(speaker, in, n); speaker[n] = 0;
    const wchar_t* rest = colon + 1;
    while (*rest == L' ' || *rest == L'\t') rest++;
    wcsncpy(body, rest, bn - 1); body[bn - 1] = 0;
}

static int SoundLineId(const wchar_t* name) {
    const wchar_t* leaf = NameLeaf(name);
    if (!leaf || !leaf[0]) return -1;
    const wchar_t* us = 0;
    for (const wchar_t* p = leaf; *p; p++)
        if (*p == L'_') us = p;
    if (!us || !us[1] || us[1] < L'0' || us[1] > L'9') return -1;
    int v = 0;
    for (const wchar_t* p = us + 1; *p >= L'0' && *p <= L'9'; p++)
        v = v * 10 + (*p - L'0');
    return v > 0 ? v : -1;
}

static int IsDialogSoundName(const wchar_t* name) {
    if (!name) return 0;
    if (wcsstr(name, L"Stop_") || wcsstr(name, L"BRF_")) return 0;
    if (wcsstr(name, L"COM_")) return g_cfg.dialogShowEchelonComms;
    if (wcsstr(name, L"INT_")) return g_cfg.dialogShowInterrogations;
    if (wcsstr(name, L"CNV_") || wcsstr(name, L"SPC_") || wcsstr(name, L"SRC_")) return 1;
    return wcsstr(name, L"SCX_Voice") != 0;
}

static void SectionFromPattern(void* obj, char* out, int n) {
    out[0] = 0;
    if (!obj) return;
    wchar_t buf[256];
    if (!ObjName(obj, buf, 256) || !buf[0]) return;
    const wchar_t* sp = wcschr(buf, L' ');
    if (sp && sp > buf) {
        char cls[96] = {};
        int cn = (int)(sp - buf);
        if (cn > 95) cn = 95;
        WideCharToMultiByte(CP_ACP, 0, buf, cn, cls, 96, 0, 0);
        cls[cn] = 0;
        if (LocSectionIndex(cls) >= 0) {
            snprintf(out, n, "%s", cls);
            return;
        }
    }
    const wchar_t* path = buf;
    if (sp) path = sp + 1;
    wchar_t pkg[80] = {}, inst[80] = {};
    const wchar_t* dot = wcschr(path, L'.');
    if (dot) {
        int pn = (int)(dot - path);
        if (pn > 79) pn = 79;
        wcsncpy(pkg, path, pn); pkg[pn] = 0;
        wcsncpy(inst, dot + 1, 79); inst[79] = 0;
        wchar_t* d2 = wcschr(inst, L'.');
        if (d2) wcsncpy(inst, d2 + 1, 79);
    } else {
        wcsncpy(inst, path, 79); inst[79] = 0;
    }
    int il = (int)wcslen(inst);
    while (il > 0 && inst[il - 1] >= L'0' && inst[il - 1] <= L'9') inst[--il] = 0;
    char ap[80], ai[80];
    *ap = 0; *ai = 0;
    WideCharToMultiByte(CP_ACP, 0, pkg, -1, ap, 80, 0, 0);
    WideCharToMultiByte(CP_ACP, 0, inst, -1, ai, 80, 0, 0);
    if (ap[0] && ai[0]) snprintf(out, n, "%s_%s", ap, ai);
    else if (ai[0]) snprintf(out, n, "%s", ai);
}

static int StartsI(const char* s, const char* pfx) {
    if (!s || !pfx || !*pfx) return 0;
    for (; *pfx; s++, pfx++) {
        char a = *s, b = *pfx;
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (!a || a != b) return 0;
    }
    return 1;
}

static const char* NameTail(const char* s) {
    const char* leaf = s ? s : "";
    for (const char* p = leaf; *p; p++)
        if (*p == '_' || *p == '.') leaf = p + 1;
    return leaf;
}

static void StripTrailingDigitsA(char* s) {
    int n = (int)strlen(s);
    while (n > 0 && s[n - 1] >= '0' && s[n - 1] <= '9') s[--n] = 0;
}

static int SectionHintMission(const char* hint) {
    if (!hint) return -1;
    const char* p = hint;
    if ((p[0] == 'P' || p[0] == 'p') && p[1] == '_') p += 2;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' &&
        (p[2] == '_' || p[2] == 0))
        return (p[0] - '0') * 10 + (p[1] - '0');
    return -1;
}

static void ResolveLocSection(char* section, int n) {
    if (!section || n < 2 || !section[0]) return;
    if (LocSectionIndex(section) >= 0) return;
    char tmp[96];
    if ((section[0] != 'P' && section[0] != 'p') || section[1] != '_') {
        snprintf(tmp, sizeof tmp, "P_%s", section);
        if (LocSectionIndex(tmp) >= 0) {
            snprintf(section, n, "%s", tmp);
            return;
        }
    }
    int hintMis = SectionHintMission(section);
    int nsec = (int)(sizeof kLocSection / sizeof kLocSection[0]);
    int found = -1, nfound = 0;
    int hintLen = (int)strlen(section);
    if (hintLen >= 8) {
        for (int i = 0; i < nsec; i++) {
            if (hintMis >= 0 && SectionMission(kLocSection[i]) != hintMis) continue;
            if (!SectionHasSpeech(kLocSection[i])) continue;
            if (!ContainsI(kLocSection[i], section)) continue;
            found = i;
            nfound++;
        }
        if (nfound == 1) {
            snprintf(section, n, "%s", kLocSection[found]);
            return;
        }
    }
    const char* leaf = NameTail(section);
    char leafA[80];
    snprintf(leafA, sizeof leafA, "%s", leaf);
    StripTrailingDigitsA(leafA);
    int leafLen = (int)strlen(leafA);
    if (leafLen >= 5) {
        found = -1;
        nfound = 0;
        for (int i = 0; i < nsec; i++) {
            if (hintMis >= 0 && SectionMission(kLocSection[i]) != hintMis) continue;
            if (!SectionHasSpeech(kLocSection[i])) continue;
            if (LocKind(kLocSection[i]) == DLG_SKIP) continue;
            const char* sl = NameTail(kLocSection[i]);
            if (StartsI(sl, leafA) || StartsI(leafA, sl)) {
                found = i;
                nfound++;
            }
        }
        if (nfound == 1 && found >= 0) {
            snprintf(section, n, "%s", kLocSection[found]);
            return;
        }
    }
    const char* p = section;
    if ((p[0] == 'P' || p[0] == 'p') && p[1] == '_') p += 2;
    if (p[0] >= '0' && p[0] <= '9' && p[1] >= '0' && p[1] <= '9' && p[2] == '_')
        p += 3;
    const char* us = strchr(p, '_');
    if (us && us[1] && UniqueSectionByNeedle(hintMis, us + 1, tmp, sizeof tmp)) {
        snprintf(section, n, "%s", tmp);
        return;
    }
    for (const char* q = section; *q; q++) {
        int skip = 0;
        if (!_strnicmp(q, "_Conv", 5) || !_strnicmp(q, "_Cnv", 4)) skip = 1;
        else if (!_strnicmp(q, "_Interro", 8) || !_strnicmp(q, "_Interog", 8)) skip = 1;
        else if (!_strnicmp(q, "_Int", 4) && q[4] >= 'A' && q[4] <= 'Z') skip = 1;
        if (!skip) continue;
        if (UniqueSectionByNeedle(hintMis, q + 1, tmp, sizeof tmp)) {
            snprintf(section, n, "%s", tmp);
            return;
        }
    }
}

static void RememberPattern(void* obj) {
    if (!obj) return;
    char sec[96] = {};
    SectionFromPattern(obj, sec, 96);
    if (!sec[0]) return;
    char raw[96];
    snprintf(raw, sizeof raw, "%s", sec);
    ResolveLocSection(sec, 96);
    if (LocSectionIndex(sec) < 0)
        snprintf(sec, sizeof sec, "%s", raw);
    if (SkipDialogSection(sec) && LocKind(sec) == DLG_SKIP) return;
    EnterCriticalSection(&g_dlgLock);
    snprintf(g_patternSection, sizeof g_patternSection, "%s", sec);
    LeaveCriticalSection(&g_dlgLock);
}

typedef const wchar_t* (__cdecl *GameLocalizeFn)(const wchar_t* section, const wchar_t* key,
    const wchar_t* package, const wchar_t* lang, int optional, int pathFlag);
static GameLocalizeFn o_Localize;

static void SectionStem(const char* section, char* out, int n) {
    out[0] = 0;
    if (!section || n < 2) return;
    int best = 0;
    const char* stem = 0;
    int nstem = (int)(sizeof kLocStem / sizeof kLocStem[0]);
    for (int i = 0; i < nstem; i++) {
        int L = (int)strlen(kLocStem[i]);
        if (L <= best || _strnicmp(section, kLocStem[i], L) != 0) continue;
        char next = section[L];
        if (next && next != '_') continue;
        best = L;
        stem = kLocStem[i];
    }
    if (stem) {
        snprintf(out, n, "%s", stem);
        return;
    }
    snprintf(out, n, "%s", section);
    for (char* q = out; *q; q++) {
        if (!_strnicmp(q, "_Cnv", 4) || !_strnicmp(q, "_Communication", 14) ||
            !_strnicmp(q, "_Briefing", 9) || !_strnicmp(q, "_Interro", 8) ||
            !_strnicmp(q, "_Interog", 8) ||
            (!_strnicmp(q, "_Int", 4) && q[4] >= 'A' && q[4] <= 'Z')) {
            *q = 0;
            break;
        }
    }
}

static int LocalizeReady() {
    if (o_Localize) return 1;
    const BYTE p[6] = {0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
    return Check(SITE_LOCALIZE, p, 6) ? 1 : 0;
}

static int TryGameLocalizeLang(const char* section, int id, const wchar_t* lang, wchar_t* out, int n) {
    if (!section || !section[0] || id < 0 || n < 2) return 0;
    if (!LocalizeReady()) return 0;
    char keyA[32], stem[80], pkgPath[96];
    snprintf(keyA, sizeof keyA, "Speech_%04dL", id);
    SectionStem(section, stem, 80);
    if (!stem[0]) return 0;
    wchar_t sec[96], key[32], pkg[96];
    ToWide(section, sec, 96);
    ToWide(keyA, key, 32);
    snprintf(pkgPath, sizeof pkgPath, "Localization\\%s", stem);
    ToWide(pkgPath, pkg, 96);
    GameLocalizeFn fn = o_Localize ? o_Localize : (GameLocalizeFn)VA(SITE_LOCALIZE);
    const wchar_t* s = fn(sec, key, pkg, lang, 1, 1);
    if (!s || !s[0] || s[0] == L'<' || !wcscmp(s, L"(null)")) return 0;
    wcsncpy(out, s, n - 1);
    out[n - 1] = 0;
    FixOverlayWide(out, n);
    return 1;
}

static int TryDialogLocalize(const char* section, int id, wchar_t* out, int n) {
    wchar_t lw[8] = {};
    char langA[16] = {};
    EffectiveGameLanguage(langA, 16);
    ToWide(langA, lw, 8);
    return TryGameLocalizeLang(section, id, lw, out, n);
}

static int PlayLeaf(const wchar_t* name, char* out, int n) {
    out[0] = 0;
    if (!name || n < 8) return 0;
    const wchar_t* p = WcsFindPlay(name);
    if (!p) return 0;
    int i = 0;
    while (p[i] && p[i] != L' ' && p[i] != L'.' && i < n - 1) {
        wchar_t c = p[i];
        out[i] = (c < 128) ? (char)c : '_';
        i++;
    }
    out[i] = 0;
    return out[0] != 0;
}

static const BarkLine* FindBarkExact(const char* key) {
    if (!key || !key[0] || g_barks.empty()) return 0;
    for (size_t i = 0; i < g_barks.size(); i++)
        if (!strcmp(g_barks[i].play, key)) return &g_barks[i];
    return 0;
}

static int IsRandomPlayKey(const char* play) {
    if (!play) return 0;
    return !_strnicmp(play, "play_random_", 12);
}

static int IsSeqPlayKey(const char* play) {
    if (!play) return 0;
    return !_strnicmp(play, "play_seq_", 9);
}

static int IsRandomPlayName(const wchar_t* name) {
    const wchar_t* p = WcsFindPlay(name);
    return p && !_wcsnicmp(p, L"Play_random_", 12);
}

static int IsSeqPlayName(const wchar_t* name) {
    const wchar_t* p = WcsFindPlay(name);
    return p && !_wcsnicmp(p, L"Play_seq_", 9);
}

static const BarkLine* FindBark(const char* play) {
    if (!play || !play[0] || g_barks.empty()) return 0;
    char key[80];
    snprintf(key, sizeof key, "%s", play);
    LowerPlayKey(key);
    if (IsSeqPlayKey(key) || IsRandomPlayKey(key)) return 0;
    return FindBarkExact(key);
}

static const BarkLine* FindBarkName(const wchar_t* name) {
    char play[80];
    if (!PlayLeaf(name, play, 80)) return 0;
    return FindBark(play);
}

static int SoundSpeakerCode(const wchar_t* name, char* out, int n) {
    out[0] = 0;
    char play[80];
    if (!PlayLeaf(name, play, 80)) return 0;
    return CueSpeakerCode(play, out, n);
}

static void RememberCueCode(const wchar_t* name) {
    char code[12];
    if (!SoundSpeakerCode(name, code, 12)) return;
    snprintf(g_lastCueCode, sizeof g_lastCueCode, "%s", code);
    g_lastCueCodeAt = GetTickCount();
}

static const CueBind* FindCue(const wchar_t* soundName) {
    if (!soundName || !soundName[0] || g_cues.empty()) return 0;
    char play[80] = {};
    if (!PlayLeaf(soundName, play, 80)) return 0;
    char hint[96] = {};
    EnterCriticalSection(&g_dlgLock);
    snprintf(hint, sizeof hint, "%s", g_patternSection);
    LeaveCriticalSection(&g_dlgLock);
    const CueBind* hit = 0;
    const CueBind* hinted = 0;
    int n = 0;
    for (size_t i = 0; i < g_cues.size(); i++) {
        if (_stricmp(g_cues[i].play, play) != 0) continue;
        n++;
        hit = &g_cues[i];
        if (hint[0] && !_stricmp(g_cues[i].section, hint)) hinted = &g_cues[i];
    }
    if (hinted) return hinted;
    if (n == 1) return hit;
    return hit;
}

static void BuildCueMap() {
    if (g_cueMapReady) return;
    g_cueMapReady = 1;
    g_cues.clear();
    int nplay = (int)(sizeof kPlayName / sizeof kPlayName[0]);
    g_cues.reserve((size_t)nplay);
    for (int i = 0; i < nplay; i++) {
        const char* sec = kPlaySection[i];
        if (SkipDialogSection(sec) && LocKind(sec) == DLG_SKIP) continue;
        int sid = FileOrderSpeechId(sec, kPlayLine[i]);
        if (sid < 0) continue;
        CueBind b = {};
        snprintf(b.play, sizeof b.play, "%s", kPlayName[i]);
        snprintf(b.section, sizeof b.section, "%s", sec);
        b.id = sid;
        g_cues.push_back(b);
    }
}

static void SpeakerFromIntLine(const wchar_t* intText, wchar_t* speaker, int sn) {
    speaker[0] = 0;
    if (!intText || !intText[0]) return;
    wchar_t body[8];
    SplitSpeaker(intText, speaker, sn, body, 8);
}

static void BodyFromLocLine(const wchar_t* locText, wchar_t* body, int bn) {
    body[0] = 0;
    if (!locText || !locText[0]) return;
    wchar_t sp[64];
    SplitSpeaker(locText, sp, 64, body, bn);
    if (!body[0]) wcsncpy(body, locText, bn - 1);
    body[bn - 1] = 0;
}

static void FillLocSpeakersFromInt() {
    wchar_t dummy[8];
    for (size_t i = 0; i < g_loc.size(); i++) {
        if (g_loc[i].speaker[0]) continue;
        SplitSpeaker(g_loc[i].text, g_loc[i].speaker, 64, dummy, 8);
        if (g_loc[i].speaker[0]) continue;
        IntSpeakerLookup(g_loc[i].section, g_loc[i].id, g_loc[i].speaker, 64);
    }
}

static const LocLine* GetOrLoadLoc(const char* section, int id) {
    if (!section || !section[0] || id < 0) return 0;
    const LocLine* L = FindLocBySectionId(section, id);
    if (L) return L;
    wchar_t buf[768] = {};
    g_locLoading++;
    int ok = TryDialogLocalize(section, id, buf, 768);
    g_locLoading--;
    if (!ok || !buf[0]) return 0;
    LocLine row = {};
    snprintf(row.section, sizeof row.section, "%s", section);
    row.id = id;
    IntSpeakerLookup(section, id, row.speaker, 64);
    if (!row.speaker[0]) {
        wchar_t dummy[8];
        SplitSpeaker(buf, row.speaker, 64, dummy, 8);
    }
    BodyFromLocLine(buf, row.text, 768);
    if (!row.text[0]) wcsncpy(row.text, buf, 767);
    row.text[767] = 0;
    g_loc.push_back(row);
    return &g_loc.back();
}

static void LoadMissionLoc(int mission) {
    if (mission < 0 || mission > 11) return;
    if (g_missionLocLoaded[mission]) return;
    EnsureLoc();
    if (!g_locDir[0]) return;
    g_missionLocLoaded[mission] = 1;
    const char* lang = g_locLang[0] ? g_locLang : "int";
    char pattern[MAX_PATH];
    snprintf(pattern, sizeof pattern, "%sP_%02d_*.%s", g_locDir, mission, lang);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            char path[MAX_PATH];
            snprintf(path, sizeof path, "%s%s", g_locDir, fd.cFileName);
            LoadLocFile(path);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    FillLocSpeakersFromInt();
}

static void* g_dlgLastSound;
static DWORD g_dlgLastSoundAt;

static int DialogSoundSeen(void* sound) {
    if (!sound) return 0;
    DWORD now = GetTickCount();
    if (sound == g_dlgLastSound && now - g_dlgLastSoundAt < 400)
        return 1;
    g_dlgLastSound = sound;
    g_dlgLastSoundAt = now;
    return 0;
}

extern "C" void OnGameLoad() {
    EnsureDlgLock();
    EnterCriticalSection(&g_dlgLock);
    g_dlgText[0] = 0;
    g_dlgSpeaker[0] = 0;
    g_dlgHideAt = 0;
    g_patternSection[0] = 0;
    g_localizeSec[0] = 0;
    g_dlgFromLocalize = 0;
    g_dlgLastSound = 0;
    g_dlgLastSoundAt = 0;
    g_pendingDlgN = 0;
    g_dlgDirty++;
    g_dlgWasPaused = 0;
    g_pendingRandomAt = 0;
    g_pendingRandomHid = 0;
    LeaveCriticalSection(&g_dlgLock);
}

static void SetLoading(int on) {
    g_loadStarted = on ? 1 : 0;
    OnGameLoad();
    g_pauseTrustShown = 0;
}

extern "C" void OnLoadingUi(int show) { SetLoading(show); }
extern "C" void OnLoadingDone(int done) { SetLoading(!done); }

static void* LoadingHud() {
    void* m = g_magmaCpp;
    if (m && !((DWORD)m & 3)) {
        void* h = *(void**)((BYTE*)m + MAGMA_LOADING_HUD);
        if (h && !((DWORD)h & 3) && *(DWORD*)h == VA(SITE_LOADING_HUD_VT))
            return h;
    }
    void* h = g_loadingHud;
    if (h && !((DWORD)h & 3) && *(DWORD*)h == VA(SITE_LOADING_HUD_VT))
        return h;
    return 0;
}

static int GameIsLoading() {
    if (!g_loadStarted) return 0;
    void* h = LoadingHud();
    if (h && *((BYTE*)h + LOADING_DONE)) {
        g_loadStarted = 0;
        return 0;
    }
    return 1;
}

static void ShowBarkPlay(const char* play);

static void ShowDialogLine(void* sound, const wchar_t* scriptText, int locId = -1,
                           const wchar_t* nameOver = 0) {
    EnsureDlgLock();
    EnsureLoc();
    EnsureBarks();
    int id = -1;
    wchar_t sname[256] = {};
    if (nameOver && nameOver[0]) wcsncpy(sname, nameOver, 255);
    else if (sound && ObjName(sound, sname, 256)) {
        id = SoundLineId(sname);
        int mission = SoundMission(sname);
        if (mission >= 0 && mission <= 11) LoadMissionLoc(mission);
    }
    if (IsSeqPlayName(sname) || IsRandomPlayName(sname)) return;
    if (GameIsLoading() || GameIsPaused()) return;
    if (sound && !scriptText && !IsNpcBarkName(sname) && DialogSoundSeen(sound)) return;
    char section[96] = {};
    int fromSound = sname[0] && !(scriptText && scriptText[0]);
    int soundCom = sname[0] && wcsstr(sname, L"COM_") != 0;
    int soundCnv = sname[0] && (wcsstr(sname, L"CNV_") || wcsstr(sname, L"SPC_") || wcsstr(sname, L"SRC_"));
    int soundInt = sname[0] && wcsstr(sname, L"INT_") != 0;
    // COM_* is not in pattern kPlayName; show from P_Barks / Localize when enabled.
    if (fromSound && soundCom && !g_cfg.dialogShowEchelonComms) return;
    if (fromSound && (soundCnv || soundInt)) {
        const CueBind* cue = FindCue(sname);
        if (cue) {
            snprintf(section, sizeof section, "%s", cue->section);
            id = cue->id;
        } else {
            char hint[96] = {};
            EnterCriticalSection(&g_dlgLock);
            snprintf(hint, sizeof hint, "%s", g_patternSection);
            LeaveCriticalSection(&g_dlgLock);
            if (PickCnvSection(section, sizeof section, hint, sname))
                id = FileOrderSpeechId(section, SoundLineId(sname));
        }
    } else if (locId >= 0 && g_localizeSec[0]) {
        snprintf(section, sizeof section, "%s", g_localizeSec);
        id = locId;
    }
    if (section[0] && SkipDialogSection(section)) {
        if (LocKind(section) == DLG_SKIP) return;
        if (!sname[0] || (!soundInt && !soundCnv && !soundCom))
            return;
        section[0] = 0;
    }
    if (fromSound && g_dlgFromLocalize && OverlayPriNow() &&
        DialogPri(sname, section[0] ? section : 0) <= OverlayPriNow())
        return;
    const LocLine* line = 0;
    if (section[0] && id >= 0) line = GetOrLoadLoc(section, id);
    if (line) {
        snprintf(section, sizeof section, "%s", line->section);
        id = line->id;
    }
    const wchar_t* src = 0;
    if (line && line->text[0] && wcscmp(line->text, L"(null)") != 0)
        src = line->text;
    else if (scriptText && scriptText[0] && wcscmp(scriptText, L"(null)") != 0 &&
             wcsncmp(scriptText, L"POPUP", 5) != 0)
        src = scriptText;
    else {
        const BarkLine* bark = sname[0] ? FindBarkName(sname) : 0;
        if (bark && bark->text[0]) {
            src = bark->text;
            if (!section[0]) snprintf(section, sizeof section, "P_Barks");
        }
    }
    if (!src) return;
    const char* locsec = line ? line->section : section;
    int pri = DialogPri(sname, locsec);
    if (pri < OverlayPriNow()) {
        return;
    }

    wchar_t speaker[64] = {}, body[768] = {};
    if (line && line->speaker[0]) wcsncpy(speaker, line->speaker, 63);
    BodyFromLocLine(src, body, 768);
    if (!speaker[0]) {
        wchar_t sp[64], dummy[8];
        SplitSpeaker(src, sp, 64, dummy, 8);
        if (sp[0]) wcsncpy(speaker, sp, 63);
    }
    if (!speaker[0] && locsec && locsec[0]) {
        int sid = line ? line->id : id;
        if (sid >= 0) IntSpeakerLookup(locsec, sid, speaker, 64);
        if (!speaker[0]) {
            wchar_t intBuf[768];
            if (sid >= 0 && TryGameLocalizeLang(locsec, sid, L"int", intBuf, 768))
                SpeakerFromIntLine(intBuf, speaker, 64);
        }
    }
    if (!body[0]) wcsncpy(body, src, 767);
    body[767] = 0;
    speaker[63] = 0;
    TrimSpeakerName(speaker, 64);

    char cueCode[12] = {};
    if (sname[0]) SoundSpeakerCode(sname, cueCode, 12);
    if (!cueCode[0] && g_lastCueCode[0] &&
        GetTickCount() - g_lastCueCodeAt < 3000)
        snprintf(cueCode, sizeof cueCode, "%s", g_lastCueCode);
    if (cueCode[0]) {
        snprintf(g_lastCueCode, sizeof g_lastCueCode, "%s", cueCode);
        g_lastCueCodeAt = GetTickCount();
    }
    int kind = LocKind(locsec);
    int comms = soundCom || kind == DLG_ECHELON;
    int interro = soundInt || kind == DLG_INTERRO;
    if (cueCode[0] && interro && !_stricmp(cueCode, "Fis")) {
        wcsncpy(speaker, L"FISHER", 63);
        speaker[63] = 0;
    } else if (cueCode[0] && comms && !speaker[0]) {
        GuessCueSpeaker(cueCode, speaker, 64);
    }
    if (!speaker[0] && g_cfg.displaySpeakerCueGuess && cueCode[0] && pri != PRI_BARK) {
        ToWide(cueCode, speaker, 64);
        for (wchar_t* p = speaker; *p; p++)
            if (*p >= L'a' && *p <= L'z') *p = (wchar_t)(*p - L'a' + L'A');
    }

    wchar_t shown[768];
    if (g_cfg.hideSpeakerName || !speaker[0])
        wcsncpy(shown, body, 767);
    else {
        int n = (int)wcslen(speaker);
        if (n > 700) n = 700;
        memcpy(shown, speaker, (size_t)n * sizeof(wchar_t));
        shown[n++] = L':';
        shown[n++] = L' ';
        shown[n] = 0;
        wcsncat(shown, body, 767 - n);
    }
    shown[767] = 0;
    FixOverlayWide(shown, 768);
    FixOverlayWide(body, 768);
    FixOverlayWide(speaker, 64);
    if (!shown[0]) return;
    if (!wcscmp(shown, L"(null)")) return;

    int dur = (int)wcslen(shown) * 70 + 1500;
    if (dur < 2500) dur = 2500;
    if (dur > 30000) dur = 30000;
    EnterCriticalSection(&g_dlgLock);
    wcsncpy(g_dlgText, shown, 767); g_dlgText[767] = 0;
    wcsncpy(g_dlgSpeaker, speaker, 63); g_dlgSpeaker[63] = 0;
    int colorSpeaker = g_cfg.dialogSpeakerColorsEnabled &&
        (cueCode[0] || (speaker[0] && (!g_cfg.hideSpeakerName || g_cfg.dialogMatchLineColor)));
    g_dlgSpeakerIdx = (colorSpeaker && g_cfg.dialogSpeakerColors)
        ? SpeakerColorIndex(locsec, speaker, cueCode) : 0;
    g_dlgPri = pri;
    g_dlgHideAt = GetTickCount() + (DWORD)dur;
    g_dlgFromLocalize = !fromSound;
    g_dlgDirty++;
    LeaveCriticalSection(&g_dlgLock);
}

static int BarkShownRecently(const char* key) {
    DWORD now = GetTickCount();
    if (key && g_lastBarkKey[0] && !strcmp(g_lastBarkKey, key) && now - g_lastBarkAt < 350)
        return 1;
    if (key) snprintf(g_lastBarkKey, sizeof g_lastBarkKey, "%s", key);
    g_lastBarkAt = now;
    return 0;
}

static void ShowBarkPlay(const char* play) {
    if (!play || !play[0] || !g_cfg.dialogShowBarks) return;
    char key[80];
    snprintf(key, sizeof key, "%s", play);
    LowerPlayKey(key);
    if (IsRandomPlayKey(key) || IsSeqPlayKey(key)) return;
    if (BarkShownRecently(key)) return;
    wchar_t name[96] = {};
    ToWide(key, name, 96);
    ShowDialogLine(0, 0, -1, name);
}

static int TryShowBarkFromHid(DWORD hid) {
    if (g_pendingRandomHid && GetTickCount() - g_pendingRandomAt < 600 &&
        (hid >> 16) != (g_pendingRandomHid >> 16))
        return 0;
    const char* play = HidToPlay(hid);
    if (!play) return 0;
    if (IsRandomPlayKey(play) || IsSeqPlayKey(play)) return 0;
    wchar_t wname[96] = {};
    ToWide(play, wname, 96);
    if (!IsNpcBarkName(wname)) return 0;
    if (!FindBark(play)) return 0;
    ShowBarkPlay(play);
    return 1;
}

typedef void* (__attribute__((stdcall)) *DareGetResFn)(DWORD);

static void* DareGetResource(DWORD id) {
    if (!id) return 0;
    DareGetResFn fn = (DareGetResFn)VA(SITE_DARE_GETRES);
    return fn(id);
}

static int TryShowBarkFromSample(void* sample) {
    if (!sample || ((DWORD)sample & 3) || !ObjPtrOk(sample) || !ObjPtrOk((BYTE*)sample + 8))
        return 0;
    DWORD hid = *(DWORD*)sample;
    DWORD typ = *((DWORD*)sample + 1);
    if (!hid || typ > 16 || typ != 1) return 0;
    return TryShowBarkFromHid(hid);
}

static int TryShowPickedChild(void* child) {
    if (!child) return 0;
    DWORD v = (DWORD)child;
    if ((v & 0x40000000) && v < 0x41000000)
        return TryShowBarkFromSample(DareGetResource(v));
    return TryShowBarkFromSample(child);
}

extern "C" void* __cdecl H_DarePick(void* container) {
    void* child = DarePick_Trampoline(container);
    if (g_cfg.displayDialogs && g_cfg.dialogShowBarks)
        TryShowPickedChild(child);
    return child;
}

extern "C" void OnDareSampleEvent(void* event) {
    if (!g_cfg.displayDialogs || !g_cfg.dialogShowBarks || !event) return;
    if (((DWORD)event & 3) || !ObjPtrOk(event) || !ObjPtrOk((BYTE*)event + 8)) return;
    DWORD hid = *(DWORD*)event;
    DWORD typ = *((DWORD*)event + 1);
    DWORD res = *((DWORD*)event + 2);
    if (!hid || typ > 16) return;
    EnsureBarks();
    const char* play = HidToPlay(hid);
    if (play && (IsRandomPlayKey(play) || IsSeqPlayKey(play))) {
        g_pendingRandomAt = GetTickCount();
        g_pendingRandomHid = hid;
        if (res > 0x10000 && !(res & 3))
            TryShowBarkFromSample((void*)res);
        else if (res)
            TryShowBarkFromSample(DareGetResource(res));
        return;
    }
    if (res > 0x10000 && !(res & 3) && TryShowBarkFromSample((void*)res))
        return;
    if (res && TryShowBarkFromSample(DareGetResource(res))) return;
    if (!play) return;
    wchar_t wname[96] = {};
    ToWide(play, wname, 96);
    if (!IsNpcBarkName(wname) || !FindBark(play)) return;
    ShowBarkPlay(play);
}

extern "C" void OnDareStreamInstance(void* inst) {
    if (!g_cfg.displayDialogs || !g_cfg.dialogShowBarks || !inst) return;
    if (((DWORD)inst & 3) || !ObjPtrOk(inst) || !ObjPtrOk((BYTE*)inst + 0xC)) return;
    void* sample = *(void**)((BYTE*)inst + 0xC);
    if (TryShowBarkFromSample(sample)) return;
    void* nested = 0;
    if (ObjPtrOk((BYTE*)inst + 0x30)) nested = *(void**)((BYTE*)inst + 0x30);
    if (nested && ObjPtrOk(nested) && ObjPtrOk((BYTE*)nested + 0xC))
        TryShowBarkFromSample(*(void**)((BYTE*)nested + 0xC));
}

static const wchar_t* __cdecl H_Localize(const wchar_t* section, const wchar_t* key,
    const wchar_t* package, const wchar_t* lang, int optional, int pathFlag) {
    GameLocalizeFn fn = o_Localize ? o_Localize : (GameLocalizeFn)VA(SITE_LOCALIZE);
    wchar_t forced[8] = {};
    char req[16] = {}, eff[16] = {};
    if (g_cfg.textLangOverride[0])
        snprintf(req, sizeof req, "%s", g_cfg.textLangOverride);
    else if (lang && lang[0])
        WideCharToMultiByte(CP_ACP, 0, lang, -1, req, (int)sizeof req, 0, 0);
    else
        ReadTextLanguage(req, 16);
    FallbackLocLang(req, eff, 16);
    ToWide(eff, forced, 8);
    lang = forced;
    const wchar_t* s = fn(section, key, package, lang, optional, pathFlag);
    // Load (and other loc scans) call Localize for every Speech_ key. That is not a spoken
    // line — only Talk/Speech natives mean the line is actually playing.
    if (!g_inDlgNative || g_locLoading || !g_cfg.displayDialogs || !s || !s[0] || s[0] == L'<')
        return s;
    if (!key) return s;
    char keyA[40] = {}, secA[96] = {};
    WideCharToMultiByte(CP_ACP, 0, key, -1, keyA, 40, 0, 0);
    int speechId = ParseSpeechId(keyA);
    if (speechId < 0) return s;
    if (section) WideCharToMultiByte(CP_ACP, 0, section, -1, secA, 96, 0, 0);
    if (secA[0] && SkipDialogSection(secA)) return s;
    if (OverlayPriNow() > PRI_COM) return s;
    wchar_t tmp[768];
    wcsncpy(tmp, s, 767); tmp[767] = 0;
    FixOverlayWide(tmp, 768);
    char tmpA[160] = {};
    WideCharToMultiByte(CP_ACP, 0, tmp, -1, tmpA, 160, 0, 0);
    if (SkipJunkValue(tmpA)) return s;
    EnterCriticalSection(&g_dlgLock);
    snprintf(g_localizeSec, sizeof g_localizeSec, "%s", secA);
    LeaveCriticalSection(&g_dlgLock);
    ShowDialogLine(0, tmp, speechId);
    return s;
}

static int IsSafeUObject(void* obj) {
    if (!obj || ((DWORD)obj & 3)) return 0;
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(obj, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    DWORD vt = *(DWORD*)obj;
    if (!vt || (vt & 3)) return 0;
    if (!VirtualQuery((void*)vt, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return 0;
    return 1;
}

static void EnqueueSound(void* obj) {
    if (!g_cfg.displayDialogs || !g_dlgLockInit || GameIsLoading()) return;
    if (!obj || ((DWORD)obj & 3)) return;
    EnterCriticalSection(&g_dlgLock);
    for (int i = 0; i < g_pendingDlgN; i++)
        if (g_pendingDlg[i] == obj) { LeaveCriticalSection(&g_dlgLock); return; }
    if (g_pendingDlgN >= 0 && g_pendingDlgN < 32)
        g_pendingDlg[g_pendingDlgN++] = obj;
    LeaveCriticalSection(&g_dlgLock);
}

static void PeekPatternSound(void* self) {
    if (!self || !IsSafeUObject(self)) return;
    BYTE* slot = (BYTE*)self + 0x4DC;
    if (ObjPtrOk(slot)) EnqueueSound(*(void**)slot);
}

static void __attribute__((thiscall)) H_ObjectConst(void* self, void* stack, void* result) {
    o_ObjectConst(self, stack, result);
    if (!g_inDlgNative || !result || ((DWORD)result & 3) || !ObjPtrOk(result)) return;
    QueueDialogSound(*(void**)result);
}

static void __attribute__((thiscall)) H_Talk(void* self, void* stack, void* result) {
    if (g_cfg.displayDialogs && self && IsSafeUObject(self))
        RememberPattern(self);
    g_inDlgNative++;
    o_Talk(self, stack, result);
    g_inDlgNative--;
}

static void __attribute__((thiscall)) H_Speech(void* self, void* stack, void* result) {
    if (g_cfg.displayDialogs && self && IsSafeUObject(self))
        RememberPattern(self);
    // Match Talk: Localize during Speech may drive COM_* / Communications overlays.
    g_inDlgNative++;
    o_Speech(self, stack, result);
    g_inDlgNative--;
    if (g_cfg.displayDialogs) PeekPatternSound(self);
}

static void HookAudioFromActor(void* actor) {
    static int hooked;
    if (hooked || !actor || !ObjPtrOk(actor)) return;
    DWORD level = *(DWORD*)((BYTE*)actor + 0x7C);
    if (!level || (level & 3) || !ObjPtrOk((void*)level)) return;
    DWORD eng = *(DWORD*)((BYTE*)level + 0x38);
    if (!eng || (eng & 3) || !ObjPtrOk((void*)eng)) return;
    DWORD audio = *(DWORD*)((BYTE*)eng + 0x48);
    if (!audio || (audio & 3) || !ObjPtrOk((void*)audio)) return;
    DWORD* vt = *(DWORD**)audio;
    if (!vt || ((DWORD)vt & 3) || !ObjPtrOk(vt) || !ObjPtrOk(vt + 32)) return;
    DWORD fn = vt[0x7C / 4];
    if (!fn || fn == (DWORD)(void*)AudioPlay_Hook) return;
    o_AudioPlay = fn;
    DWORD hook = (DWORD)(void*)AudioPlay_Hook;
    WriteMem((DWORD)&vt[0x7C / 4], &hook, 4);
    hooked = 1;
}

extern "C" void EnqueuePlayedSoundEx(void* actor, void* obj) {
    HookAudioFromActor(actor);
    EnqueuePlayedSound(obj);
}

extern "C" void QueueDialogSound(void* obj) {
    if (!g_inDlgNative) return;
    wchar_t buf[256] = {};
    if (obj && IsSafeUObject(obj) && ObjName(obj, buf, 256) && buf[0])
        RememberCueCode(buf);
    EnqueueSound(obj);
}

extern "C" void EnqueuePlayedSound(void* obj) {
    if (!g_cfg.displayDialogs || !g_dlgLockInit) return;
    if (!obj || !IsSafeUObject(obj)) return;
    wchar_t buf[256] = {};
    if (!ObjName(obj, buf, 256) || !buf[0]) return;
    int npc = IsNpcBarkName(buf);
    int dlg = IsDialogSoundName(buf);
    const BarkLine* hit = 0;
    if (!dlg && g_cfg.dialogShowBarks) {
        EnsureBarks();
        hit = FindBarkName(buf);
    }
    if (dlg) {
        EnqueueSound(obj);
        return;
    }
    if (!g_cfg.dialogShowBarks) return;
    if (IsSeqPlayName(buf)) return;
    if (IsRandomPlayName(buf)) {
        g_pendingRandomAt = GetTickCount();
        if (ObjPtrOk((BYTE*)obj + 0x2C))
            g_pendingRandomHid = *(DWORD*)((BYTE*)obj + 0x2C);
        return;
    }
    if (!hit && !npc) return;
    if (GameIsLoading()) EnqueueSound(obj);
    else ShowIfDialogSound(obj);
}

static void DrainDialogSounds() {
    void* pending[32];
    int n = 0;
    if (!g_dlgLockInit) return;
    EnterCriticalSection(&g_dlgLock);
    n = g_pendingDlgN;
    if (n > 32) n = 32;
    if (n > 0) memcpy(pending, g_pendingDlg, (size_t)n * sizeof(void*));
    g_pendingDlgN = 0;
    LeaveCriticalSection(&g_dlgLock);
    for (int i = 0; i < n; i++)
        ShowIfDialogSound(pending[i]);
}

extern "C" void ShowIfDialogSound(void* obj) {
    if (!g_cfg.displayDialogs || !obj || ((DWORD)obj & 3)) return;
    if (!IsSafeUObject(obj)) return;
    wchar_t buf[256] = {};
    if (!ObjName(obj, buf, 256) || !buf[0]) return;
    RememberCueCode(buf);
    EnsureBarks();
    int dlg = IsDialogSoundName(buf);
    const BarkLine* bark = FindBarkName(buf);
    if (!dlg && !bark && !IsNpcBarkName(buf)) return;
    if (!dlg && !g_cfg.dialogShowBarks) return;
    if (!dlg && bark) {
        char play[80] = {};
        if (PlayLeaf(buf, play, 80)) {
            LowerPlayKey(play);
            if (BarkShownRecently(play)) return;
        }
    }
    int mission = SoundMission(buf);
    if (mission >= 0 && mission <= 11) LoadMissionLoc(mission);
    if (wcsstr(buf, L"COM_") && !g_cfg.dialogShowEchelonComms) return;
    ShowDialogLine(obj, 0);
}

static void __attribute__((thiscall)) H_SpeechPlay(void* self, void* a, void* b) {
    if (g_cfg.displayDialogs && self && IsSafeUObject(self)) {
        RememberPattern(self);
        BYTE* slot = (BYTE*)self + 0x4DC;
        if (ObjPtrOk(slot)) PeekPatternSound(self);
    }
    o_SpeechPlay(self, a, b);
}

static void __attribute__((thiscall)) H_NotifyConv(void* self, void* stack, void* result) {
    if (g_cfg.displayDialogs) {
        EnterCriticalSection(&g_dlgLock);
        DWORD now = GetTickCount();
        if (g_dlgHideAt > now + 1500) g_dlgHideAt = now + 1500;
        LeaveCriticalSection(&g_dlgLock);
    }
    o_NotifyConv(self, stack, result);
}

static void Dialog_Release() {
    if (g_dlgTex) { g_dlgTex->Release(); g_dlgTex = 0; }
    g_dlgTexW = g_dlgTexH = 0;
    g_dlgDev = 0;
    if (g_dlgFont) { DeleteObject(g_dlgFont); g_dlgFont = 0; }
    if (g_vidFont) { DeleteObject(g_vidFont); g_vidFont = 0; }
}

static HFONT MakeOverlayFont(int size, int bold, HFONT* slot, int* haveSize, int* haveBold) {
    if (*slot && *haveSize == size && *haveBold == bold) return *slot;
    if (*slot) DeleteObject(*slot);
    *haveSize = size;
    *haveBold = bold;
    int h = size;
    if (h < 8) h = 8;
    if (h > 96) h = 96;
    *slot = CreateFontA(-h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
                        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                        "Tahoma");
    return *slot;
}

static HFONT DlgFont() {
    return MakeOverlayFont(g_cfg.dialogFontSize, g_cfg.dialogBold, &g_dlgFont, &g_dlgFontSize, &g_dlgFontBold);
}

static HFONT VidFont() {
    return MakeOverlayFont(g_cfg.dialogVideoFontSize, g_cfg.dialogVideoBold, &g_vidFont, &g_vidFontSize, &g_vidFontBold);
}

static void DrawOutlined(HDC hdc, RECT* r, const wchar_t* s, int fmt, COLORREF rgb) {
    SetBkMode(hdc, TRANSPARENT);
    RECT o;
    const int d[8][2] = {{-1,0},{1,0},{0,-1},{0,1},{-1,-1},{1,-1},{-1,1},{1,1}};
    SetTextColor(hdc, RGB(0, 0, 0));
    for (int i = 0; i < 8; i++) {
        o = *r;
        OffsetRect(&o, d[i][0], d[i][1]);
        DrawTextW(hdc, s, -1, &o, fmt);
    }
    SetTextColor(hdc, rgb);
    DrawTextW(hdc, s, -1, r, fmt);
}

static int BuildDialogBitmap(IDirect3DDevice9* d, const wchar_t* full, const wchar_t* speaker,
                             int speakerIdx, UINT sw, UINT sh, int video) {
    HFONT font = video ? VidFont() : DlgFont();
    int fontSize = video ? g_cfg.dialogVideoFontSize : g_cfg.dialogFontSize;
    DWORD textColor = video ? g_cfg.dialogVideoColor : g_cfg.dialogColor;
    DWORD bgColor = video ? g_cfg.dialogVideoBgColor : g_cfg.dialogBgColor;
    int textAlpha = video ? g_cfg.dialogVideoAlpha : g_cfg.dialogAlpha;
    int bgAlpha = video ? g_cfg.dialogVideoBgAlpha : g_cfg.dialogBgAlpha;
    if (!font) return 0;
    if (!full) return 0;
    int maxW = (int)(sw * g_cfg.dialogMaxWidthPct / 100);
    if (maxW < 200) maxW = (int)sw;
    if (maxW < 32) return 0;
    if ((int)sw > 0 && maxW > (int)sw) maxW = (int)sw;
    const int padX = 12, padY = 8;

    wchar_t name[64] = {}, body[768] = {}, draw[768] = {};
    int showName = !video && !g_cfg.hideSpeakerName && speaker && speaker[0];
    if (showName) {
        wcsncpy(name, speaker, 63);
        SplitSpeaker(full, name, 64, body, 768);
        if (!name[0]) wcsncpy(name, speaker, 63);
        if (!body[0]) wcsncpy(body, full, 767);
        wcsncpy(draw, full, 767);
    } else {
        wcsncpy(draw, full, 767);
        wcsncpy(body, full, 767);
    }
    name[63] = 0; body[767] = 0; draw[767] = 0;

    HDC screen = GetDC(0);
    HDC hdc = screen ? CreateCompatibleDC(screen) : 0;
    if (!hdc) {
        if (screen) ReleaseDC(0, screen);
        return 0;
    }
    HGDIOBJ oldFont = SelectObject(hdc, font);

    int split = showName;
    wchar_t head[96] = {};
    SIZE hs = {}, bs = {};
    int wrap = 0;
    int textW = 8, textH = fontSize;
    const wchar_t* bodyStr = body[0] ? body : full;
    if (split) {
        const wchar_t* sep = L": ";
        int nl = (int)wcslen(name);
        if (nl > 88) nl = 88;
        memcpy(head, name, (size_t)nl * sizeof(wchar_t));
        head[nl] = 0;
        wcsncat(head, sep, 95 - nl);
        GetTextExtentPoint32W(hdc, head, (int)wcslen(head), &hs);
        GetTextExtentPoint32W(hdc, bodyStr, (int)wcslen(bodyStr), &bs);
        wrap = (hs.cx + bs.cx) > (maxW - padX * 2);
        if (!wrap) {
            textW = hs.cx + bs.cx;
            textH = hs.cy > bs.cy ? hs.cy : bs.cy;
        } else {
            RECT br = {0, 0, maxW - padX * 2, 0};
            DrawTextW(hdc, bodyStr, -1, &br, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            int bw = br.right - br.left;
            int bh = br.bottom - br.top;
            textW = hs.cx > bw ? hs.cx : bw;
            textH = hs.cy + 4 + bh;
        }
    } else {
        GetTextExtentPoint32W(hdc, draw, (int)wcslen(draw), &bs);
        if (bs.cx <= maxW - padX * 2) {
            textW = bs.cx;
            textH = bs.cy > 0 ? bs.cy : fontSize;
        } else {
            RECT mr = {0, 0, maxW - padX * 2, 0};
            DrawTextW(hdc, draw, -1, &mr, DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
            textW = mr.right - mr.left;
            textH = mr.bottom - mr.top;
        }
    }
    if (textW < 8) textW = 8;
    if (textH < fontSize) textH = fontSize;
    int boxW = textW + padX * 2;
    int boxH = textH + padY * 2;
    if (boxW > (int)sw) boxW = (int)sw;
    if (boxH > (int)sh / 3) boxH = (int)sh / 3;
    if (boxW < 16) boxW = 16;
    if (boxH < 16) boxH = 16;
    if (boxW > 2048) boxW = 2048;
    if (boxH > 1024) boxH = 1024;

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = boxW;
    bi.bmiHeader.biHeight = -boxH;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = 0;
    HBITMAP bmp = CreateDIBSection(hdc, &bi, DIB_RGB_COLORS, &bits, 0, 0);
    if (!bmp || !bits) {
        SelectObject(hdc, oldFont); DeleteDC(hdc); ReleaseDC(0, screen);
        return 0;
    }
    HGDIOBJ oldBmp = SelectObject(hdc, bmp);
    unsigned* fill = (unsigned*)bits;
    for (int i = 0; i < boxW * boxH; i++) fill[i] = 0x00FF00FFu;

    DWORD bodyRgb = textColor;
    DWORD nameRgb = bodyRgb;
    int colorSpeaker = !video && (showName || (speaker && speaker[0] && g_cfg.dialogMatchLineColor));
    if (colorSpeaker && g_cfg.dialogSpeakerColorsEnabled) {
        int n = g_cfg.dialogSpeakerColorN;
        if (n < 1) n = MAX_DLG_SPEAKERS;
        int si = g_cfg.dialogSpeakerColors ? speakerIdx : 0;
        if (si < 0 || si >= n) si = 0;
        nameRgb = g_cfg.dialogSpeakerColor[si];
        if (g_cfg.dialogMatchLineColor) bodyRgb = nameRgb;
    }

    COLORREF nameCol = RGB((nameRgb >> 16) & 255, (nameRgb >> 8) & 255, nameRgb & 255);
    COLORREF bodyCol = RGB((bodyRgb >> 16) & 255, (bodyRgb >> 8) & 255, bodyRgb & 255);
    if (!split) {
        RECT rc = {padX, padY, boxW - padX, boxH - padY};
        DrawOutlined(hdc, &rc, draw, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX, bodyCol);
    } else if (!wrap) {
        int x = (boxW - (hs.cx + bs.cx)) / 2;
        if (x < padX) x = padX;
        RECT nr = {x, padY, x + hs.cx + 2, boxH - padY};
        RECT brc = {x + hs.cx, padY, boxW - padX, boxH - padY};
        DrawOutlined(hdc, &nr, head, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX, nameCol);
        DrawOutlined(hdc, &brc, bodyStr, DT_WORDBREAK | DT_LEFT | DT_NOPREFIX, bodyCol);
    } else {
        RECT nr = {padX, padY, boxW - padX, padY + hs.cy + 2};
        RECT brc = {padX, padY + hs.cy + 4, boxW - padX, boxH - padY};
        DrawOutlined(hdc, &nr, head, DT_CENTER | DT_SINGLELINE | DT_NOPREFIX, nameCol);
        DrawOutlined(hdc, &brc, bodyStr, DT_WORDBREAK | DT_CENTER | DT_NOPREFIX, bodyCol);
    }

    unsigned* px = (unsigned*)bits;
    int count = boxW * boxH;
    unsigned bgA = (unsigned)bgAlpha;
    unsigned textA = (unsigned)textAlpha;
    unsigned bg = (bgA << 24) | (bgColor & 0x00FFFFFFu);
    for (int i = 0; i < count; i++) {
        unsigned v = px[i] & 0x00FFFFFFu;
        if (v == 0x00FF00FFu) px[i] = bgA ? bg : 0;
        else px[i] = (textA << 24) | v;
    }

    if (!g_dlgTex || g_dlgDev != d || g_dlgTexW != (UINT)boxW || g_dlgTexH != (UINT)boxH) {
        if (g_dlgTex) { g_dlgTex->Release(); g_dlgTex = 0; }
        g_internal = true;
        if (FAILED(d->CreateTexture(boxW, boxH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_dlgTex, 0))) {
            g_dlgTex = 0;
            g_internal = false;
            SelectObject(hdc, oldFont); SelectObject(hdc, oldBmp);
            DeleteObject(bmp); DeleteDC(hdc); ReleaseDC(0, screen);
            return 0;
        }
        g_internal = false;
        g_dlgTexW = boxW; g_dlgTexH = boxH; g_dlgDev = d;
    }
    D3DLOCKED_RECT lr;
    if (SUCCEEDED(g_dlgTex->LockRect(0, &lr, 0, 0))) {
        unsigned char* srcp = (unsigned char*)bits;
        unsigned char* dstp = (unsigned char*)lr.pBits;
        int srcPitch = boxW * 4;
        for (int y = 0; y < boxH; y++) memcpy(dstp + y * lr.Pitch, srcp + y * srcPitch, srcPitch);
        g_dlgTex->UnlockRect(0);
    }

    SelectObject(hdc, oldFont);
    SelectObject(hdc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(hdc);
    ReleaseDC(0, screen);
    return 1;
}

struct DlgVtx { float x, y, z, rhw; DWORD color; float u, v; };

static int GameIsPaused() {
    void* m = g_magmaCpp;
    if (!m || ((DWORD)m & 3)) return 0;
    void* ctrl = *(void**)((BYTE*)m + MAGMA_PAUSE_CTRL);
    if (!ctrl || ((DWORD)ctrl & 3)) return 0;
    if (*(DWORD*)ctrl != VA(SITE_PAUSE_CTRL_VT)) return 0;
    BYTE shown = *((BYTE*)ctrl + PAUSE_SHOWN);
    if (!g_pauseTrustShown) {
        if (shown) return 0;
        g_pauseTrustShown = 1;
    }
    return shown != 0;
}

static int DlgBackSize(IDirect3DDevice9* d, UINT* sw, UINT* sh) {
    static UINT w, h;
    static IDirect3DDevice9* dev;
    if (w && h && dev == d) {
        *sw = w;
        *sh = h;
        return 1;
    }
    IDirect3DSurface9* bb = 0;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb)) || !bb) return 0;
    D3DSURFACE_DESC bd;
    bb->GetDesc(&bd);
    bb->Release();
    if (!bd.Width || !bd.Height) return 0;
    w = bd.Width;
    h = bd.Height;
    dev = d;
    *sw = w;
    *sh = h;
    return 1;
}

static void DrawDialogQuad(IDirect3DDevice9* d, UINT sw, UINT sh) {
    DWORD zEn, zWr, ab, src, dst, at, cull, lit, fog, sten, scis, cwe, srgb;
    DWORD cop, ca1, ca2, aop, aa1, aa2, smin, smag, smip, fvf;
    D3DVIEWPORT9 vp = {};
    IDirect3DVertexShader9* vs = 0;
    IDirect3DPixelShader9* ps = 0;
    IDirect3DBaseTexture9* tex = 0;
    IDirect3DVertexDeclaration9* decl = 0;
    IDirect3DVertexBuffer9* vb = 0;
    IDirect3DIndexBuffer9* ib = 0;
    UINT vbOff = 0, vbStride = 0;
    d->GetRenderState(D3DRS_ZENABLE, &zEn);
    d->GetRenderState(D3DRS_ZWRITEENABLE, &zWr);
    d->GetRenderState(D3DRS_ALPHABLENDENABLE, &ab);
    d->GetRenderState(D3DRS_SRCBLEND, &src);
    d->GetRenderState(D3DRS_DESTBLEND, &dst);
    d->GetRenderState(D3DRS_ALPHATESTENABLE, &at);
    d->GetRenderState(D3DRS_CULLMODE, &cull);
    d->GetRenderState(D3DRS_LIGHTING, &lit);
    d->GetRenderState(D3DRS_FOGENABLE, &fog);
    d->GetRenderState(D3DRS_STENCILENABLE, &sten);
    d->GetRenderState(D3DRS_SCISSORTESTENABLE, &scis);
    d->GetRenderState(D3DRS_COLORWRITEENABLE, &cwe);
    d->GetRenderState(D3DRS_SRGBWRITEENABLE, &srgb);
    d->GetViewport(&vp);
    d->GetTextureStageState(0, D3DTSS_COLOROP, &cop);
    d->GetTextureStageState(0, D3DTSS_COLORARG1, &ca1);
    d->GetTextureStageState(0, D3DTSS_COLORARG2, &ca2);
    d->GetTextureStageState(0, D3DTSS_ALPHAOP, &aop);
    d->GetTextureStageState(0, D3DTSS_ALPHAARG1, &aa1);
    d->GetTextureStageState(0, D3DTSS_ALPHAARG2, &aa2);
    d->GetSamplerState(0, D3DSAMP_MINFILTER, &smin);
    d->GetSamplerState(0, D3DSAMP_MAGFILTER, &smag);
    d->GetSamplerState(0, D3DSAMP_MIPFILTER, &smip);
    d->GetVertexShader(&vs);
    d->GetPixelShader(&ps);
    d->GetTexture(0, &tex);
    d->GetFVF(&fvf);
    d->GetVertexDeclaration(&decl);
    d->GetStreamSource(0, &vb, &vbOff, &vbStride);
    d->GetIndices(&ib);

    int pad = (int)(sh * (g_cfg.dialogPadPct / 100.0f));
    float x0 = (sw - g_dlgTexW) * 0.5f;
    float y1 = (float)(sh - pad);
    float y0 = y1 - (float)g_dlgTexH;
    float x1 = x0 + (float)g_dlgTexW;
    DlgVtx v[4] = {
        {x0, y0, 0, 1, 0xFFFFFFFF, 0, 0},
        {x1, y0, 0, 1, 0xFFFFFFFF, 1, 0},
        {x0, y1, 0, 1, 0xFFFFFFFF, 0, 1},
        {x1, y1, 0, 1, 0xFFFFFFFF, 1, 1},
    };

    bool wasInternal = g_internal;
    g_internal = true;
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ZWRITEENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    d->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
    d->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, FALSE);
    D3DVIEWPORT9 nvp = { 0, 0, sw, sh, 0.0f, 1.0f };
    d->SetViewport(&nvp);
    d->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetVertexShader(0);
    d->SetPixelShader(0);
    d->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
    d->SetTexture(0, g_dlgTex);
    d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(DlgVtx));

    d->SetRenderState(D3DRS_ZENABLE, zEn);
    d->SetRenderState(D3DRS_ZWRITEENABLE, zWr);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, ab);
    d->SetRenderState(D3DRS_SRCBLEND, src);
    d->SetRenderState(D3DRS_DESTBLEND, dst);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, at);
    d->SetRenderState(D3DRS_CULLMODE, cull);
    d->SetRenderState(D3DRS_LIGHTING, lit);
    d->SetRenderState(D3DRS_FOGENABLE, fog);
    d->SetRenderState(D3DRS_STENCILENABLE, sten);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, scis);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, cwe);
    d->SetRenderState(D3DRS_SRGBWRITEENABLE, srgb);
    d->SetViewport(&vp);
    d->SetTextureStageState(0, D3DTSS_COLOROP, cop);
    d->SetTextureStageState(0, D3DTSS_COLORARG1, ca1);
    d->SetTextureStageState(0, D3DTSS_COLORARG2, ca2);
    d->SetTextureStageState(0, D3DTSS_ALPHAOP, aop);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG1, aa1);
    d->SetTextureStageState(0, D3DTSS_ALPHAARG2, aa2);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, smin);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, smag);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, smip);
    d->SetVertexShader(vs);
    d->SetPixelShader(ps);
    d->SetTexture(0, tex);
    if (decl) d->SetVertexDeclaration(decl);
    else d->SetFVF(fvf);
    d->SetStreamSource(0, vb, vbOff, vbStride);
    d->SetIndices(ib);
    if (vs) vs->Release();
    if (ps) ps->Release();
    if (tex) tex->Release();
    if (decl) decl->Release();
    if (vb) vb->Release();
    if (ib) ib->Release();
    g_internal = wasInternal;
}

static void Dialog_OnPresent(IDirect3DDevice9* d) {
    if (!d) return;
    int vid = Video_OnPresent(d);
    if (vid || VideoIsActive()) return;
    if (!g_cfg.displayDialogs) return;
    static int wasLoading;
    int loading = GameIsLoading();
    if (loading) {
        OnGameLoad();
        wasLoading = 1;
        return;
    }
    if (wasLoading) {
        OnGameLoad();
        wasLoading = 0;
    }
    DrainDialogSounds();
    if (g_cfg.dialogAlpha <= 0) return;

    DWORD now = GetTickCount();
    int paused = GameIsPaused();
    if (paused) {
        if (!g_dlgWasPaused) g_dlgPauseStart = now;
        g_dlgWasPaused = 1;
        return;
    }
    if (g_dlgWasPaused) {
        DWORD pausedFor = now - g_dlgPauseStart;
        EnterCriticalSection(&g_dlgLock);
        if (g_dlgHideAt) g_dlgHideAt += pausedFor;
        LeaveCriticalSection(&g_dlgLock);
        g_dlgWasPaused = 0;
    }

    wchar_t text[768], speaker[64];
    int spk = 0;
    DWORD hideAt, dirty;
    EnterCriticalSection(&g_dlgLock);
    hideAt = g_dlgHideAt;
    if (!g_dlgText[0] || now >= hideAt) {
        LeaveCriticalSection(&g_dlgLock);
        return;
    }
    wcsncpy(text, g_dlgText, 767); text[767] = 0;
    wcsncpy(speaker, g_dlgSpeaker, 63); speaker[63] = 0;
    spk = g_dlgSpeakerIdx;
    dirty = g_dlgDirty;
    LeaveCriticalSection(&g_dlgLock);

    UINT sw = 0, sh = 0;
    if (!DlgBackSize(d, &sw, &sh)) return;

    static DWORD lastDirty;
    if (!g_dlgTex || g_dlgDev != d || lastDirty != dirty) {
        if (!BuildDialogBitmap(d, text, speaker, spk, sw, sh, 0)) return;
        lastDirty = dirty;
    }
    if (!g_dlgTex) return;
    DrawDialogQuad(d, sw, sh);
}

static void Dialog_OnRelease() { Dialog_Release(); }

static bool ApplyDialogHooks() {
    if (!g_cfg.displayDialogs) return true;
    EnsureDlgLock();
    EnsureLoc();
    BuildCueMap();
    const BYTE gfn[7] = {0x56, 0x8B, 0x74, 0x24, 0x08, 0x85, 0xF6};
    if (!g_getFullName && Check(SITE_GETFULLNAME, gfn, 7))
        g_getFullName = (GetFullNameFn)VA(SITE_GETFULLNAME);
    const BYTE talkp[5] = {0x83, 0xEC, 0x20, 0x53, 0x56};
    const BYTE nv[5] = {0x8B, 0x44, 0x24, 0x04, 0x56};
    if (Check(SITE_EPATTERN_TALK, talkp, 5)) {
        g_talkCont = VA(SITE_EPATTERN_TALK + 5);
        o_Talk = (NativeFn)Talk_Trampoline;
        PatchRel32(SITE_EPATTERN_TALK, 0xE9, (void*)H_Talk);
    }
    NativeFn* gn = (NativeFn*)VA(SITE_GNATIVES);
    if (gn && gn[0x20]) {
        o_ObjectConst = gn[0x20];
        NativeFn hook = H_ObjectConst;
        WriteMem((DWORD)&gn[0x20], &hook, 4);
    }
    if (Check(SITE_EPATTERN_SPEECH, talkp, 5)) {
        g_speechCont = VA(SITE_EPATTERN_SPEECH + 5);
        o_Speech = (NativeFn)Speech_Trampoline;
        PatchRel32(SITE_EPATTERN_SPEECH, 0xE9, (void*)H_Speech);
    }
    const BYTE splay[6] = {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x7C};
    if (Check(SITE_SPEECH_PLAY, splay, 6)) {
        g_speechPlayCont = VA(SITE_SPEECH_PLAY + 6);
        o_SpeechPlay = (NativeFn)SpeechPlay_Trampoline;
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)H_SpeechPlay - (VA(SITE_SPEECH_PLAY) + 5);
        WriteMem(VA(SITE_SPEECH_PLAY), b, 6);
    }
    const BYTE aplay[6] = {0x8B, 0x41, 0x7C, 0x8B, 0x50, 0x38};
    if (Check(SITE_ACTOR_PLAYSOUND, aplay, 6)) {
        g_actorPlayCont = VA(SITE_ACTOR_PLAYSOUND + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)ActorPlaySound_Hook - (VA(SITE_ACTOR_PLAYSOUND) + 5);
        WriteMem(VA(SITE_ACTOR_PLAYSOUND), b, 6);
    }
    const BYTE dsample[6] = {0x81, 0xEC, 0xE4, 0x00, 0x00, 0x00};
    if (Check(SITE_DARE_SAMPLE, dsample, 6)) {
        g_dareSampleCont = VA(SITE_DARE_SAMPLE + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)DareSample_Hook - (VA(SITE_DARE_SAMPLE) + 5);
        WriteMem(VA(SITE_DARE_SAMPLE), b, 6);
    }
    const BYTE dstream[6] = {0x8B, 0x46, 0x0C, 0x8B, 0x48, 0x54};
    if (Check(SITE_DARE_STREAM1, dstream, 6)) {
        g_dareStreamCont1 = VA(SITE_DARE_STREAM1 + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)DareStream_Hook1 - (VA(SITE_DARE_STREAM1) + 5);
        WriteMem(VA(SITE_DARE_STREAM1), b, 6);
    }
    if (Check(SITE_DARE_STREAM2, dstream, 6)) {
        g_dareStreamCont2 = VA(SITE_DARE_STREAM2 + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)DareStream_Hook2 - (VA(SITE_DARE_STREAM2) + 5);
        WriteMem(VA(SITE_DARE_STREAM2), b, 6);
    }
    if (Check(SITE_DARE_STREAM3, dstream, 6)) {
        g_dareStreamCont3 = VA(SITE_DARE_STREAM3 + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)DareStream_Hook3 - (VA(SITE_DARE_STREAM3) + 5);
        WriteMem(VA(SITE_DARE_STREAM3), b, 6);
    }
    const BYTE dvoice[6] = {0x8B, 0x43, 0x0C, 0x8B, 0x48, 0x54};
    if (Check(SITE_DARE_VOICE, dvoice, 6)) {
        g_dareVoiceCont = VA(SITE_DARE_VOICE + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)DareVoice_Hook - (VA(SITE_DARE_VOICE) + 5);
        WriteMem(VA(SITE_DARE_VOICE), b, 6);
    }
    const BYTE dpick[7] = {0x51, 0x56, 0x57, 0x8B, 0x7C, 0x24, 0x10};
    if (Check(SITE_DARE_PICK, dpick, 7)) {
        g_darePickCont = VA(SITE_DARE_PICK + 7);
        BYTE b[7] = {0xE9, 0, 0, 0, 0, 0x90, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)H_DarePick - (VA(SITE_DARE_PICK) + 5);
        WriteMem(VA(SITE_DARE_PICK), b, 7);
    }
    if (Check(SITE_NOTIFY_CONV, nv, 5)) {
        g_notifyCont = VA(SITE_NOTIFY_CONV + 5);
        o_NotifyConv = (NativeFn)NotifyConv_Trampoline;
        PatchRel32(SITE_NOTIFY_CONV, 0xE9, (void*)H_NotifyConv);
    }
    const BYTE locp[6] = {0x81, 0xEC, 0x04, 0x02, 0x00, 0x00};
    if (Check(SITE_LOCALIZE, locp, 6)) {
        g_localizeCont = VA(SITE_LOCALIZE + 6);
        o_Localize = Localize_Trampoline;
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)H_Localize - (VA(SITE_LOCALIZE) + 5);
        WriteMem(VA(SITE_LOCALIZE), b, 6);
    }
    const BYTE mtick[6] = {0x83, 0xEC, 0x0C, 0x53, 0x55, 0x56};
    if (Check(SITE_MAGMA_TICK, mtick, 6)) {
        g_magmaTickCont = VA(SITE_MAGMA_TICK + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)MagmaTick_Hook - (VA(SITE_MAGMA_TICK) + 5);
        WriteMem(VA(SITE_MAGMA_TICK), b, 6);
    }
    const BYTE lui[6] = {0x8A, 0x44, 0x24, 0x04, 0x84, 0xC0};
    if (Check(SITE_LOADING_UI, lui, 6)) {
        g_loadingUiCont = VA(SITE_LOADING_UI + 6);
        BYTE b[6] = {0xE9, 0, 0, 0, 0, 0x90};
        *(DWORD*)(b + 1) = (DWORD)(void*)LoadingUi_Hook - (VA(SITE_LOADING_UI) + 5);
        WriteMem(VA(SITE_LOADING_UI), b, 6);
    }
    const BYTE ldone[5] = {0x53, 0x8A, 0x5C, 0x24, 0x08};
    if (Check(SITE_LOADING_DONE, ldone, 5)) {
        g_loadingDoneCont = VA(SITE_LOADING_DONE + 5);
        PatchRel32(SITE_LOADING_DONE, 0xE9, (void*)LoadingDone_Hook);
    }
    return true;
}
