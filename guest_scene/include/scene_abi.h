#ifndef WII2GC_SCENE_ABI_H
#define WII2GC_SCENE_ABI_H

#include <stdint.h>

/*
 * Explicit, platform-neutral guest/host ABI.
 * All members are 32-bit unsigned integers; both PPC targets are big-endian.
 * Coordinates/dimensions are in normalized 0..1000 scene units.
 * No raw hardware addresses or pointers are carried inside the scene.
 */
#define WII2GC_SCENE_MAGIC 0x57324333u /* "W2C3" */
#define WII2GC_SCENE_VERSION 1u
#define WII2GC_SCENE_SCALE 1000u
#define WII2GC_SCENE_MAX_RECTS 4u

typedef struct {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
    uint32_t yuy2;
} Wii2GcRect;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t frame;
    uint32_t count;
    Wii2GcRect rects[WII2GC_SCENE_MAX_RECTS];
} Wii2GcScene;

typedef uint32_t (*Wii2GcRenderGuest)(uint32_t frame, Wii2GcScene *output);

#endif
