/*
 * Wii2GameCube Phase 0: GameCube hardware + DOL-header reading diagnostic.
 * WARNING: this app NEVER executes a Wii binary; no IOS emulation exists.
 * This source is original, no Nintendo binaries or SDK headers included.
 */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define DOL_HEADER_LEN 256
static unsigned char dol_header[DOL_HEADER_LEN] ATTRIBUTE_ALIGN(32);

static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void scan_guest_header(void) {
    if (!fatInitDefault()) {
        printf("SD: no supported adapter/card detected\n");
        return;
    }
    printf("SD: mounted; looking for sd:/guest.dol\n");
    FILE *f = fopen("sd:/guest.dol", "rb");
    if (!f) {
        printf("SD: guest.dol not present (optional)\n");
        return;
    }
    const size_t count = fread(dol_header, 1, sizeof dol_header, f);
    fclose(f);
    if (count != sizeof dol_header) {
        printf("DOL: invalid/truncated header\n");
        return;
    }
    const uint32_t entry = be32(dol_header + 0xE0);
    const uint32_t bss_addr = be32(dol_header + 0xD8);
    const uint32_t bss_size = be32(dol_header + 0xDC);
    printf("DOL entry:  0x%08lx\n", (unsigned long)entry);
    printf("DOL BSS:    0x%08lx, %lu bytes\n", (unsigned long)bss_addr,
           (unsigned long)bss_size);
    unsigned int sections = 0, mem2 = 0;
    for (unsigned int i = 0; i < 18; i++) {
        const uint32_t addr = be32(dol_header + 0x48 + 4 * i);
        const uint32_t size = be32(dol_header + 0x90 + 4 * i);
        if (!size) continue;
        sections++;
        const uint64_t end = (uint64_t)addr + size;
        if (addr >= 0x90000000UL && end <= 0x94000000ULL) mem2++;
    }
    if (bss_size && bss_addr >= 0x90000000UL &&
        (uint64_t)bss_addr + bss_size <= 0x94000000ULL) mem2++;
    printf("DOL: %u sections; %u MEM2 consumers\n", sections, mem2);
    printf("NOT LOADED/EXECUTED. Header scan only.\n");
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    VIDEO_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_Configure(mode);
    console_init(xfb, 20, 20, mode->fbWidth - 40, mode->xfbHeight - 40,
                 mode->fbWidth * 2);
    VIDEO_ClearFrameBuffer(mode, xfb, COLOR_BLACK);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    printf("\n  Wii2GameCube -- Phase 0 Diagnostic\n");
    printf("  GameCube native executable (PowerPC)\n");
    printf("  ------------------------------\n");
    printf("  Video: OK if you can read this.\n");
    scan_guest_header();
    printf("\n  Goal: assess Wii homebrew portability.\n");
    printf("  No Wii IOS/GPU/API emulation yet.\n");
    printf("  No control input is required.\n");
    printf("\n  Heartbeat (seconds): ");

    unsigned long frames = 0;
    for (;;) {
        VIDEO_WaitVSync();
        frames++;
        if (frames % 60 == 0) {
            printf(".");
            if ((frames / 60) % 30 == 0) printf("\n  Heartbeat: ");
        }
    }
    return 0;
}
