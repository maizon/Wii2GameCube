/* Wii-native test victim: compiles for Wii; execution on GameCube is NOT assumed. */
#include <gccore.h>
#include <stdio.h>

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
    printf("Wii guest: video initialization completed.\n");
    printf("Static Wii homebrew proof fixture -- source included.\n");
    for (;;) VIDEO_WaitVSync();
    return 0;
}
