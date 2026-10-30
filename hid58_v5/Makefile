ifeq ($(strip $(DEVKITPRO)),)
$(error Define DEVKITPRO)
endif
ifeq ($(strip $(DEVKITPPC)),)
$(error Define DEVKITPPC)
endif
include $(DEVKITPPC)/wii_rules
TARGET := boot
OBJECTS := main.o
CFLAGS := -O2 -g -Wall -Wextra -std=gnu11 $(MACHDEP) -I$(LIBOGC_INC)
LDFLAGS := -g $(MACHDEP)
LIBS := -L$(LIBOGC_LIB) -lfat -lwiiuse -lbte -logc -lm
.PHONY: all clean
all: $(TARGET).dol
$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@
$(TARGET).dol: $(TARGET).elf
	elf2dol $< $@
clean:
	rm -f main.o $(TARGET).elf $(TARGET).dol
