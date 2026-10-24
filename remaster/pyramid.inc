// ------------------------------------------------------------------------------------------------
// pyramid.inc - [Pyramid] Size: scales the game's coarse glow pyramid (the 256x256 / 128x128 levels
// used by bloom and the vision modes) by Size / 256. Patched in memory at start-up, only when every
// site still holds its stock value.
// ------------------------------------------------------------------------------------------------
#include "pyramid_sites.h"

static void ApplyPyramidSize(int size) {
    if (size == 256 || size < 16 || size > 8192) return;
    for (const PyrSite& s : g_pyrSites)
        if (*(DWORD*)VA(s.va) != s.stock) return;
    const float k = size / 256.0f;
    for (const PyrSite& s : g_pyrSites) {
        DWORD v = s.stock;
        float f;
        memcpy(&f, &v, 4);
        switch (s.cls) {
            case 0: v = (DWORD)size; break;
            case 1: v = (DWORD)(size / 2); break;
            case 2: f = f * k; memcpy(&v, &f, 4); break;
            case 3: f = f / k; memcpy(&v, &f, 4); break;
            case 4: f = (f - 1.0f) / k + 1.0f; memcpy(&v, &f, 4); break;
        }
        WriteMem(VA(s.va), &v, 4);
    }
}
