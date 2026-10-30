ifeq ($(strip $(DEVKITPRO)),)
$(error DEVKITPRO required)
endif
ifeq ($(strip $(DEVKITPPC)),)
$(error DEVKITPPC required)
endif
include $(DEVKITPPC)/wii_rules
TARGET := boot
OBJECTS := main.o deep_probe.o recovery_journal.o
CFLAGS := -O2 -Wall -Wextra -std=gnu11 $(MACHDEP) -I$(LIBOGC_INC) $(EXTRA)
LDFLAGS := $(MACHDEP)
LIBS := -L$(LIBOGC_LIB) -lfat -logc -lm
.PHONY: all clean
all: $(TARGET).dol
$(TARGET).elf: $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)
deep_probe.o: deep_probe.c deep_probe.h
	$(CC) $(CFLAGS) -c $< -o $@
recovery_journal.o: recovery_journal.c recovery_journal.h
	$(CC) $(CFLAGS) -c $< -o $@
main.o: main.c
	$(CC) $(CFLAGS) -c $< -o $@
$(TARGET).dol: $(TARGET).elf
	elf2dol $< $@
clean:
	rm -f main.o deep_probe.o recovery_journal.o $(TARGET).elf $(TARGET).dol
