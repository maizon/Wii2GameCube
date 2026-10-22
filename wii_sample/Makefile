ifeq ($(strip $(DEVKITPRO)),)
$(error Please set DEVKITPRO)
endif
ifeq ($(strip $(DEVKITPPC)),)
$(error Please set DEVKITPPC)
endif
include $(DEVKITPPC)/wii_rules
TARGET := wii_guest_sample
OBJ := source/main.o
CFLAGS := -O2 -g -Wall -Wextra -std=gnu11 $(MACHDEP) -I$(LIBOGC_INC)
LDFLAGS := -g $(MACHDEP)
LIBS := -L$(LIBOGC_LIB) -logc -lm
.PHONY: all clean
all: $(TARGET).dol
$(TARGET).elf: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
source/%.o: source/%.c
	$(CC) $(CFLAGS) -c -o $@ $<
$(TARGET).dol: $(TARGET).elf
	elf2dol $< $@
clean:
	rm -f $(OBJ) $(TARGET).elf $(TARGET).dol
