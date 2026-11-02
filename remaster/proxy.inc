// ------------------------------------------------------------------------------------------------
// proxy.inc - AVIFIL32.dll proxy. Remaster.dll is installed as AVIFIL32.dll next to the game EXE
// (the EXE imports it, no mod uses that name). Every AVIFIL32 export jumps to the Windows DLL,
// which is loaded from the system directory on the first call (never inside DllMain).
// Generated export list: avifil32.dll ordinals 1..79.
// ------------------------------------------------------------------------------------------------
enum { AVI_COUNT = 79 };
static const char* const g_aviNames[AVI_COUNT] = {
    "AVIBuildFilter",
    "AVIBuildFilterA",
    "AVIBuildFilterW",
    "AVIClearClipboard",
    "AVIFileAddRef",
    "AVIFileCreateStream",
    "AVIFileCreateStreamA",
    "AVIFileCreateStreamW",
    "AVIFileEndRecord",
    "AVIFileExit",
    "AVIFileGetStream",
    "AVIFileInfo",
    "AVIFileInfoA",
    "AVIFileInfoW",
    "AVIFileInit",
    "AVIFileOpen",
    "AVIFileOpenA",
    "AVIFileOpenW",
    "AVIFileReadData",
    "AVIFileRelease",
    "AVIFileWriteData",
    "AVIGetFromClipboard",
    "AVIMakeCompressedStream",
    "AVIMakeFileFromStreams",
    "AVIMakeStreamFromClipboard",
    "AVIPutFileOnClipboard",
    "AVISave",
    "AVISaveA",
    "AVISaveOptions",
    "AVISaveOptionsFree",
    "AVISaveV",
    "AVISaveVA",
    "AVISaveVW",
    "AVISaveW",
    "AVIStreamAddRef",
    "AVIStreamBeginStreaming",
    "AVIStreamCreate",
    "AVIStreamEndStreaming",
    "AVIStreamFindSample",
    "AVIStreamGetFrame",
    "AVIStreamGetFrameClose",
    "AVIStreamGetFrameOpen",
    "AVIStreamInfo",
    "AVIStreamInfoA",
    "AVIStreamInfoW",
    "AVIStreamLength",
    "AVIStreamOpenFromFile",
    "AVIStreamOpenFromFileA",
    "AVIStreamOpenFromFileW",
    "AVIStreamRead",
    "AVIStreamReadData",
    "AVIStreamReadFormat",
    "AVIStreamRelease",
    "AVIStreamSampleToTime",
    "AVIStreamSetFormat",
    "AVIStreamStart",
    "AVIStreamTimeToSample",
    "AVIStreamWrite",
    "AVIStreamWriteData",
    "CLSID_AVISimpleUnMarshal",
    "CreateEditableStream",
    "DllCanUnloadNow",
    "DllGetClassObject",
    "DllRegisterServer",
    "DllUnregisterServer",
    "EditStreamClone",
    "EditStreamCopy",
    "EditStreamCut",
    "EditStreamPaste",
    "EditStreamSetInfo",
    "EditStreamSetInfoA",
    "EditStreamSetInfoW",
    "EditStreamSetName",
    "EditStreamSetNameA",
    "EditStreamSetNameW",
    "IID_IAVIEditStream",
    "IID_IAVIFile",
    "IID_IAVIStream",
    "IID_IGetFrame",
};

// data exports (interface / class IDs) are constants, exported directly
extern "C" const GUID g_avi_CLSID_AVISimpleUnMarshal = {0x00020009, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const GUID g_avi_IID_IAVIEditStream = {0x00020024, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const GUID g_avi_IID_IAVIFile = {0x00020020, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const GUID g_avi_IID_IAVIStream = {0x00020021, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};
extern "C" const GUID g_avi_IID_IGetFrame = {0x00020023, 0x0000, 0x0000, {0xC0, 0, 0, 0, 0, 0, 0, 0x46}};

extern "C" {
    extern void* g_aviProcs[AVI_COUNT];   // jump targets (asm below), initially the lazy resolvers
    void __stdcall AviResolve();
    HRESULT __stdcall AviMissing() { return E_NOTIMPL; }
}

static HMODULE g_realAvi;

extern "C" void __stdcall AviResolve() {
    if (!g_realAvi) {
        wchar_t path[MAX_PATH];
        UINT n = GetSystemDirectoryW(path, MAX_PATH - 16);
        if (n && n < MAX_PATH - 16) {
            lstrcpyW(path + n, L"\\avifil32.dll");
            g_realAvi = LoadLibraryW(path);
        }
    }
    for (int i = 0; i < AVI_COUNT; i++) {
        FARPROC f = g_realAvi ? GetProcAddress(g_realAvi, g_aviNames[i]) : 0;
        g_aviProcs[i] = f ? (void*)f : (void*)AviMissing;
    }
}

// stub_N: jmp [g_aviProcs + 4*N]; lazy_N: resolve all, then continue at the real function
asm(
    ".data\n"
    ".globl _g_aviProcs\n"
    "_g_aviProcs:\n"
    ".irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78\n"
    "    .long _avi_lazy_\\i\n"
    ".endr\n"
    ".text\n"
    ".irp i, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78\n"
    ".globl _avi_stub_\\i\n"
    "_avi_stub_\\i:\n"
    "    jmp *_g_aviProcs+4*\\i\n"
    "_avi_lazy_\\i:\n"
    "    pushl $\\i\n"
    "    jmp _avi_lazy_common\n"
    ".endr\n"
    "_avi_lazy_common:\n"
    "    pushal\n"
    "    pushfl\n"
    "    call _AviResolve@0\n"
    "    popfl\n"
    "    popal\n"
    "    pushl %eax\n"                       // [esp] = eax, [esp+4] = index
    "    movl 4(%esp), %eax\n"
    "    movl _g_aviProcs(,%eax,4), %eax\n"
    "    movl %eax, 4(%esp)\n"               // replace index by the real function
    "    popl %eax\n"
    "    ret\n"                              // jump with the caller's stack untouched
);
