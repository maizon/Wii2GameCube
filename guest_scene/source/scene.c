/*
 * Standalone 2D scene generator compiled with the Wii PowerPC target.
 *
 * Intentionally: no Wii system library, no IOS, no MEM2 or global data.
 * The guest writes scene primitives to caller-provided bounded memory.
 * GameCube host supplies the display backend.
 */
#include "../include/scene_abi.h"

__attribute__((used, noinline, section(".text.guest")))
uint32_t wii_guest_scene(uint32_t frame, Wii2GcScene *out)
{
    if (!out) return 0u;

    out->magic = WII2GC_SCENE_MAGIC;
    out->version = WII2GC_SCENE_VERSION;
    out->frame = frame;
    out->count = 3u;

    out->rects[0].x = (frame * 7u) % 800u;
    out->rects[0].y = (frame * 5u) % 700u;
    out->rects[0].width = 150u;
    out->rects[0].height = 180u;
    out->rects[0].yuy2 = 0xE080E080u;

    out->rects[1].x = 720u - (frame * 11u) % 640u;
    out->rects[1].y = 120u + (frame * 3u) % 500u;
    out->rects[1].width = 110u;
    out->rects[1].height = 130u;
    out->rects[1].yuy2 = 0xA040A0C0u;

    out->rects[2].x = 430u;
    out->rects[2].y = 400u + (frame * 13u) % 320u;
    out->rects[2].width = 90u;
    out->rects[2].height = 100u;
    out->rects[2].yuy2 = 0xD090D040u;

    return WII2GC_SCENE_MAGIC ^ frame ^ 3u;
}
