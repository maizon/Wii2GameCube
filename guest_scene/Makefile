# Build self-authored Wii PPC scene program: freestanding, one-section DOL.
ifeq ($(strip $(DEVKITPPC)),)
$(error Set DEVKITPPC before building)
endif
include $(DEVKITPPC)/wii_rules
CC := $(DEVKITPPC)/bin/powerpc-eabi-gcc
OBJCOPY := $(DEVKITPPC)/bin/powerpc-eabi-objcopy
CFLAGS := -O2 -Wall -Wextra -Werror -std=gnu11 $(MACHDEP) \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pic \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -mno-sdata

.PHONY: all clean
all: scene.dol

scene.o: source/scene.c include/scene_abi.h
	$(CC) $(CFLAGS) -c -o $@ $<

scene.bin: scene.o
	$(OBJCOPY) -O binary --only-section=.text.guest $< $@
	test -s $@

scene.dol: scene.bin ../tools/pack_guest_dol.py
	python3 ../tools/pack_guest_dol.py $< $@

clean:
	rm -f scene.o scene.bin scene.dol
