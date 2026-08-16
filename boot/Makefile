# ESP32-C6 Bootloader Makefile
CROSS   = riscv64-unknown-elf-
AS      = $(CROSS)as
CC      = $(CROSS)gcc
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
SIZE    = $(CROSS)size

ASFLAGS = -march=rv32imac_zicsr_zifencei -mabi=ilp32 -I include
LDFLAGS = -T linker.ld -nostdlib -m elf32lriscv

SRCS    = asm/start.S
OBJS    = $(SRCS:.S=.o)
TARGET  = bootloader
BIN     = $(TARGET).bin

all: $(BIN)

HEADERS = include/reg_c6.inc include/kernel_hdr.inc include/boot_api.inc

%.o: %.S $(HEADERS)
	$(AS) $(ASFLAGS) -o $@ $<

$(TARGET).elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	$(SIZE) $@

$(BIN): $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "Binary size: $$(stat -c%s $@) bytes"
	$(OBJDUMP) -d -M no-aliases $< > $(TARGET).dis

flash: $(BIN)
	python3 tools/flash.py $(BIN)

monitor:
	python3 -m serial.tools.miniterm /dev/ttyACM0 115200

clean:
	rm -f $(OBJS) $(TARGET).elf $(BIN) $(TARGET).dis

.PHONY: all clean flash monitor
