/*
 * Wii2GameCube Phase 1: GC-native host + Wii-target freestanding PPC guest.
 *
 * Trust boundary: ONLY built-in, project-authored guest code runs.
 * This program does NOT execute arbitrary .dol files, emulate IOS, or run
 * commercial Wii games or the original Wii userland.
 */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <malloc.h>

#define DOL_HEADER_LEN 256u
#define MAX_GUEST_BYTES 4096u
static unsigned char dol_header[DOL_HEADER_LEN] ATTRIBUTE_ALIGN(32);
extern const unsigned char gcwcl_guest_start[];
extern const unsigned char gcwcl_guest_end[];

typedef uint32_t (*guest_step_fn)(uint32_t);

/* Independent, host-compiled oracle for deterministic guest output. */
static uint32_t expected_step(uint32_t frame) {
    const uint32_t x = ((frame * 3u) + 7u) % 40u;
    const uint32_t y = ((frame * 5u) + 11u) % 14u;
    return (x << 16) | y;
}

static uint32_t be32(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void scan_guest_header(void) {
    if (!fatInitDefault()) {
        printf("SD: adapter not found (not needed for embedded test)\n");
        return;
    }
    printf("SD: mounted; optional header-only guest.dol\n");
    FILE *f = fopen("sd:/guest.dol", "rb");
    if (!f) {
        printf("SD: guest.dol absent (normal)\n");
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
    printf("DOL entry: 0x%08lx BSS: 0x%08lx (%lu bytes)\n",
           (unsigned long)entry, (unsigned long)bss_addr,
           (unsigned long)bss_size);
    unsigned int sections = 0u, mem2 = 0u;
    for (unsigned int i = 0; i < 18u; i++) {
        const uint32_t addr = be32(dol_header + 0x48 + 4u * i);
        const uint32_t size = be32(dol_header + 0x90 + 4u * i);
        if (!size) continue;
        sections++;
        const uint64_t end = (uint64_t)addr + size;
        if (addr >= 0x90000000UL && end <= 0x94000000ULL) mem2++;
    }
    if (bss_size && bss_addr >= 0x90000000UL &&
        (uint64_t)bss_addr + bss_size <= 0x94000000ULL) mem2++;
    printf("DOL header: %u sections, %u MEM2 sections\n", sections, mem2);
    printf("Only metadata inspected, no .dol execution.\n");
}

/*
 * Load a deliberately restricted one-section Wii-freestanding DOL fixture.
 * Its original virtual address is metadata. The code has no relocations,
 * global data, IOS calls or absolute pointer references, so we can rebase
 * it into an allocated MEM1 region after flushing/invalidation of caches.
 *
 * This is a closed-world loader: only bundled project-authored DOL bytes,
 * never any user-provided Wii executable.
 */
static guest_step_fn prepare_embedded_guest(void **allocated) {
    const size_t file_size = (size_t)(gcwcl_guest_end - gcwcl_guest_start);
    const unsigned char *dol = gcwcl_guest_start;
    if (file_size < DOL_HEADER_LEN || file_size > DOL_HEADER_LEN + MAX_GUEST_BYTES) {
        printf("Guest DOL: invalid file size (%lu)\n", (unsigned long)file_size);
        return NULL;
    }
    const uint32_t off = be32(dol);
    const uint32_t address = be32(dol + 0x48);
    const uint32_t size = be32(dol + 0x90);
    const uint32_t entry = be32(dol + 0xE0);
    if (off != DOL_HEADER_LEN || address != 0x80018000u ||
        entry != address || !size || size > MAX_GUEST_BYTES ||
        (size & 3u) || file_size != DOL_HEADER_LEN + (size_t)size ||
        be32(dol + 0xD8) || be32(dol + 0xDC)) {
        printf("Guest DOL: rejected unsafe header/entry/layout\n");
        return NULL;
    }
    for (unsigned int section = 1; section < 18; section++) {
        if (be32(dol + 4u * section) ||
            be32(dol + 0x48u + 4u * section) ||
            be32(dol + 0x90u + 4u * section)) {
            printf("Guest DOL: extra sections forbidden\n");
            return NULL;
        }
    }
    for (unsigned int byte = 0xE4u; byte < DOL_HEADER_LEN; byte++) {
        if (dol[byte]) {
            printf("Guest DOL: nonzero reserved header\n");
            return NULL;
        }
    }

    const size_t rounded = ((size_t)size + 31u) & ~(size_t)31u;
    unsigned char *code = (unsigned char *)memalign(32u, rounded);
    if (!code) {
        printf("Guest DOL: MEM1 allocation failed\n");
        return NULL;
    }
    memcpy(code, dol + DOL_HEADER_LEN, size);
    memset(code + size, 0, rounded - size);
    DCFlushRange(code, rounded);
    ICInvalidateRange(code, rounded);
    *allocated = code;
    printf("Guest Wii DOL: %lu bytes | TEXT 0x%08lx rebased to MEM1\n",
           (unsigned long)file_size, (unsigned long)address);
    printf("Wii PPC: loaded %lu code bytes into MEM1\n", (unsigned long)size);
    return (guest_step_fn)code;
}

/* VI XFB uses YUY2 (2 pixels per 32-bit word), NOT RGB565.
 * We write into uncached XFB below the console's text area.
 * Sprite positions originate in the Wii-target guest function.
 */
static void rect_yuy2(volatile uint32_t *fb, const GXRModeObj *mode,
                      int x, int y, int w, int h, uint32_t yuy2) {
    const int fbw = (int)mode->fbWidth, fbh = (int)mode->xfbHeight;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > fbw || y + h > fbh ||
        (x & 1) || (w & 1)) return;
    const int pairs_per_line = fbw / 2;
    for (int row = 0; row < h; row++) {
        volatile uint32_t *line = fb + (y + row) * pairs_per_line + x / 2;
        for (int pair = 0; pair < w / 2; pair++) line[pair] = yuy2;
    }
}

static void draw_guest_2d(void *xfb, const GXRModeObj *mode, uint32_t coordinates) {
    const int x0 = 40;
    const int width = ((int)mode->fbWidth - 80) & ~1;
    const int y0 = mode->xfbHeight > 270 ? (int)mode->xfbHeight - 112 :
                   (int)mode->xfbHeight / 2;
    const int height = (int)mode->xfbHeight - y0 - 10;
    if (width < 80 || height < 32 || y0 < 0) return;
    volatile uint32_t *fb = (volatile uint32_t *)xfb;
    rect_yuy2(fb, mode, x0, y0, width, height, 0x30803080u);
    const int step_x = ((width - 20) * (int)((coordinates >> 16) % 40u)) / 39;
    const int step_y = ((height - 18) * (int)((coordinates & 0xFFFFu) % 14u)) / 13;
    const int sprite_x = (x0 + step_x) & ~1;
    rect_yuy2(fb, mode, sprite_x, y0 + step_y, 16, 16, 0xD080D080u);
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    VIDEO_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_Configure(mode);
    console_init(xfb, 20, 20, mode->fbWidth - 40,
                 mode->xfbHeight - 40, mode->fbWidth * 2);
    VIDEO_ClearFrameBuffer(mode, xfb, COLOR_BLACK);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();

    printf("\nWii2GameCube - Phase 1 PowerPC guest\n");
    printf("Host: GameCube native | Guest: Wii-built PPC\n");
    printf("Controls and SD not required for this test.\n");
    scan_guest_header();

    void *guest_memory = NULL;
    guest_step_fn guest = prepare_embedded_guest(&guest_memory);
    if (guest) {
        const uint32_t cases[] = {0u, 1u, 17u, 123u, 65535u};
        unsigned int passed = 0u;
        for (unsigned int i = 0u; i < sizeof cases / sizeof cases[0]; i++) {
            const uint32_t result = guest(cases[i]);
            const uint32_t expected = expected_step(cases[i]);
            if (result == expected) passed++;
            printf("Wii->GC %lu: (%lu,%lu) %s\n",
                   (unsigned long)cases[i],
                   (unsigned long)(result >> 16),
                   (unsigned long)(result & 0xffffu),
                   result == expected ? "PASS" : "FAIL");
        }
        printf("%s Wii-target PPC execution: %u/5\n",
               passed == 5u ? "PASS" : "FAIL", passed);
    } else {
        printf("FAIL: embedded guest unavailable.\n");
    }
    printf("No Wii IOS/GX API emulation; no arbitrary DOL loading.\n");
    printf("2D guest sprite: small moving square in YUY2 XFB.\n");
    printf("Automatic demo; no SD, Wii IOS, or controls.\n");
    unsigned long frame = 0ul;
    for (;;) {
        VIDEO_WaitVSync();
        frame++;
        if (guest && frame % 4ul == 0ul) {
            const uint32_t p = guest((uint32_t)(frame / 4ul));
            draw_guest_2d(xfb, mode, p);
        }
    }
    /* kept alive for entire process, no free(guest_memory) needed */
    return 0;
}
