/*
 * Wii2GameCube Phase 3 — Wii-target scene-command DOL loaded by GC.
 * Guest is a self-authored freestanding function, NOT a generic Wii DOL.
 * GameCube remains responsible for hardware access and video output.
 */
#include <gccore.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include "../../guest_scene/include/scene_abi.h"

#define SCENE_DOL_HEADER 0x100u
#define SCENE_MAX_CODE 4096u
#define SCENE_GUARD1 0xF0ABCAFEu
#define SCENE_GUARD2 0xC01DBAADu

extern const unsigned char gcwcl_scene_start[];
extern const unsigned char gcwcl_scene_end[];

static Wii2GcRenderGuest scene_guest = NULL;
static void *scene_exec_buffer = NULL;

typedef struct {
    uint32_t before;
    Wii2GcScene scene;
    uint32_t after;
} GuardedScene;

static uint32_t big_endian_word(const unsigned char *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/* Distinct verifier: never run external DOLs or untrusted input. */
static Wii2GcRenderGuest load_scene_dol(void) {
    const unsigned char *dol = gcwcl_scene_start;
    const size_t len = (size_t)(gcwcl_scene_end - gcwcl_scene_start);
    if (len < SCENE_DOL_HEADER || len > SCENE_DOL_HEADER + SCENE_MAX_CODE)
        return NULL;

    const uint32_t offset = big_endian_word(dol);
    const uint32_t address = big_endian_word(dol + 0x48);
    const uint32_t size = big_endian_word(dol + 0x90);
    const uint32_t entry = big_endian_word(dol + 0xE0);
    if (offset != SCENE_DOL_HEADER || address != 0x80018000u ||
        entry != address || !size || size > SCENE_MAX_CODE ||
        (size & 31u) || (size_t)size + SCENE_DOL_HEADER != len ||
        big_endian_word(dol + 0xD8) || big_endian_word(dol + 0xDC))
        return NULL;
    for (unsigned int section = 1u; section < 18u; section++) {
        if (big_endian_word(dol + section * 4u) ||
            big_endian_word(dol + 0x48u + section * 4u) ||
            big_endian_word(dol + 0x90u + section * 4u))
            return NULL;
    }
    for (size_t i = 0xE4u; i < SCENE_DOL_HEADER; i++) {
        if (dol[i]) return NULL;
    }

    scene_exec_buffer = memalign(32u, size);
    if (!scene_exec_buffer) return NULL;
    memcpy(scene_exec_buffer, dol + SCENE_DOL_HEADER, size);
    DCFlushRange(scene_exec_buffer, size);
    ICInvalidateRange(scene_exec_buffer, size);
    printf("Scene Wii DOL: %lu bytes, %lu PPC code bytes\n",
           (unsigned long)len, (unsigned long)size);
    return (Wii2GcRenderGuest)scene_exec_buffer;
}

/*
 * Host validates all guest-produced drawing commands. The sentinels detect
 * some accidental overwrites, not malicious code. No PPC sandbox is claimed.
 */
static int scene_call(uint32_t frame, Wii2GcScene *result) {
    if (!scene_guest || !result) return 0;
    GuardedScene buffer;
    memset(&buffer, 0, sizeof buffer);
    buffer.before = SCENE_GUARD1;
    buffer.after = SCENE_GUARD2;
    const uint32_t signature = scene_guest(frame, &buffer.scene);
    if (buffer.before != SCENE_GUARD1 || buffer.after != SCENE_GUARD2 ||
        signature != (WII2GC_SCENE_MAGIC ^ frame ^ 3u) ||
        buffer.scene.magic != WII2GC_SCENE_MAGIC ||
        buffer.scene.version != WII2GC_SCENE_VERSION ||
        buffer.scene.frame != frame || buffer.scene.count != 3u ||
        buffer.scene.count > WII2GC_SCENE_MAX_RECTS)
        return 0;
    for (uint32_t i = 0u; i < buffer.scene.count; i++) {
        const Wii2GcRect *r = &buffer.scene.rects[i];
        if (!r->width || !r->height ||
            r->x > WII2GC_SCENE_SCALE || r->y > WII2GC_SCENE_SCALE ||
            r->width > WII2GC_SCENE_SCALE - r->x ||
            r->height > WII2GC_SCENE_SCALE - r->y)
            return 0;
    }
    *result = buffer.scene;
    return 1;
}

/* Simple YUY2 raster — 2 pixels per big-endian 32-bit framebuffer word.
 * Each scene rectangle is completely bounds-checked before conversion.
 */
static void scene_rect(volatile uint32_t *fb, const GXRModeObj *mode,
                       int x, int y, int width, int height, uint32_t color) {
    const int fw = (int)mode->fbWidth;
    const int fh = (int)mode->xfbHeight;
    if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
        (x & 1) || (width & 1) || (fw & 1) ||
        x > fw || width > fw - x || y > fh || height > fh - y)
        return;
    for (int row = 0; row < height; row++) {
        volatile uint32_t *dst = fb + (y + row) * (fw / 2) + x / 2;
        for (int col = 0; col < width / 2; col++) dst[col] = color;
    }
}

int scene_runtime_init(void) {
    scene_guest = load_scene_dol();
    if (!scene_guest) {
        printf("Scene Wii DOL: FAILED safe load; legacy demo kept.\n");
        return 0;
    }
    const uint32_t vectors[] = {0u, 1u, 17u, 123u, 65535u};
    Wii2GcScene s;
    unsigned int passed = 0u;
    for (unsigned int i = 0; i < 5u; i++) {
        if (scene_call(vectors[i], &s)) passed++;
    }
    printf("Wii->GC scene ABI: %u/5 %s\n",
           passed, passed == 5u ? "PASS" : "FAIL");
    if (passed != 5u) {
        scene_guest = NULL;
        printf("Guest scene disabled; legacy sprite remains.\n");
        return 0;
    }
    printf("Phase 3: Wii guest generates 3 rects/frame\n");
    printf("GameCube translates scene commands to YUY2 XFB.\n");
    return 1;
}

int scene_runtime_tick(void *xfb, const GXRModeObj *mode, uint32_t frame) {
    if (!xfb || !mode || !scene_guest) return 0;
    Wii2GcScene s;
    if (!scene_call(frame, &s)) {
        scene_guest = NULL;  /* automatic fallback, no further guest calls */
        return 0;
    }

    const int width = ((int)mode->fbWidth - 80) & ~1;
    const int y0 = mode->xfbHeight > 270 ? (int)mode->xfbHeight - 112 :
                   (int)mode->xfbHeight / 2;
    const int height = (int)mode->xfbHeight - y0 - 10;
    if (width < 80 || height < 32 || y0 < 0) return 0;

    const int x0 = 40;
    volatile uint32_t *fb = (volatile uint32_t *)xfb;
    scene_rect(fb, mode, x0, y0, width, height, 0x30803080u);
    for (uint32_t i = 0; i < s.count; i++) {
        const Wii2GcRect *r = &s.rects[i];
        const int left = (x0 + ((int)r->x * width) / (int)WII2GC_SCENE_SCALE) & ~1;
        const int right = (x0 + ((int)(r->x + r->width) * width) /
                           (int)WII2GC_SCENE_SCALE) & ~1;
        const int top = y0 + (int)(r->y * (uint32_t)height /
                                   WII2GC_SCENE_SCALE);
        const int bottom = y0 + (int)((r->y + r->height) * (uint32_t)height /
                                      WII2GC_SCENE_SCALE);
        scene_rect(fb, mode, left, top, right - left, bottom - top, r->yuy2);
    }
    return 1;
}
