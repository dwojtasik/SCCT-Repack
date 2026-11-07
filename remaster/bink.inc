// ------------------------------------------------------------------------------------------------
// bink.inc - Bink video target 3840x2160 (stock is 640x480).
//
// Three CreateTexture sites on the HW path, plus blit UV scales (1/3840, 1/2160). Sites already
// changed by another mod are left untouched.
// ------------------------------------------------------------------------------------------------
static const DWORD SITE_BINK_TEX1 = 0x10C86200;
static const DWORD SITE_BINK_TEX2 = 0x10C8C015;
static const DWORD SITE_BINK_TEX3 = 0x10CA4134;
static const DWORD SITE_BINK_UV_H = 0x10C86490;   // fmul 1/480
static const DWORD SITE_BINK_UV_W = 0x10C864AB;   // fmul 1/640

static void PatchBinkUv(DWORD va, const BYTE* stock, DWORD scaleBits) {
    if (!Check(va, stock, 14)) return;
    BYTE p[14] = {0xEB, 0x06, 0, 0, 0, 0, 0x90, 0x90, 0xD8, 0x0D, 0, 0, 0, 0};
    memcpy(p + 2, &scaleBits, 4);
    DWORD abs = VA(va + 2);
    memcpy(p + 10, &abs, 4);
    WriteMem(VA(va), p, 14);
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
}
