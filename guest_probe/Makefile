# Freestanding Wii PowerPC code. This is not an IOS-capable homebrew.
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
all: guest.dol

guest.o: source/guest.c
	$(CC) $(CFLAGS) -c -o $@ $<

guest.bin: guest.o
	$(OBJCOPY) -O binary --only-section=.text.guest $< $@
	test -s $@

guest.dol: guest.bin
	python3 ../tools/pack_guest_dol.py $< $@

clean:
	rm -f guest.o guest.bin guest.dol
