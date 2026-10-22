/*
 * Minimal freestanding PowerPC guest compiled with the Wii (-mrvl) target.
 *
 * A returnable function, NOT Wii libogc homebrew and NOT a full Wii DOL.
 * All arithmetic happens here; host code must not inline this routine.
 * No globals, heap, stack-external symbols, syscalls, pointers, or hardware.
 */
#include <stdint.h>

__attribute__((used, noinline, section(".text.guest")))
uint32_t wii_guest_step(uint32_t frame)
{
    /* Return packed 2D sprite coordinates for an endless small animation. */
    uint32_t x = ((frame * 3u) + 7u) % 40u;
    uint32_t y = ((frame * 5u) + 11u) % 14u;
    return (x << 16) | y;
}
