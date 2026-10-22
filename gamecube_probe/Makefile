# GameCube host executes an isolated, embedded freestanding Wii-target DOL.
ifeq ($(strip $(DEVKITPRO)),)
$(error Please set DEVKITPRO, for example /opt/devkitpro)
endif
ifeq ($(strip $(DEVKITPPC)),)
$(error Please set DEVKITPPC, usually $(DEVKITPRO)/devkitPPC)
endif
include $(DEVKITPPC)/gamecube_rules

TARGET := gcwcl_probe
OBJ := source/main.o source/guest_blob.o source/scene_runtime.o source/scene_blob.o
CFLAGS := -O2 -g -Wall -Wextra -Werror -std=gnu11 $(MACHDEP) -I$(LIBOGC_INC)
LDFLAGS := -g $(MACHDEP)
LIBS := -L$(LIBOGC_LIB) -lfat -logc -lm

.PHONY: all clean
all: $(TARGET).dol

../guest_probe/guest.dol: ../guest_probe/source/guest.c ../guest_probe/Makefile ../tools/pack_guest_dol.py
	$(MAKE) -C ../guest_probe all

source/guest_blob.o: source/guest_blob.S ../guest_probe/guest.dol
	$(CC) $(MACHDEP) -c -o $@ $<

../guest_scene/scene.dol: ../guest_scene/source/scene.c ../guest_scene/include/scene_abi.h ../guest_scene/Makefile ../tools/pack_guest_dol.py
	$(MAKE) -C ../guest_scene all

source/scene_blob.o: source/scene_blob.S ../guest_scene/scene.dol
	$(CC) $(MACHDEP) -c -o $@ $<

source/%.o: source/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

$(TARGET).dol: $(TARGET).elf
	elf2dol $< $@

clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).dol
	$(MAKE) -C ../guest_probe clean
	$(MAKE) -C ../guest_scene clean
