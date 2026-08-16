# ESP32-C6 Kernel Makefile
CROSS   = riscv64-unknown-elf-
AS      = $(CROSS)as
CXX     = $(CROSS)g++
LD      = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy
OBJDUMP = $(CROSS)objdump
SIZE    = $(CROSS)size

ASFLAGS  = -march=rv32imac_zicsr -mabi=ilp32
CXXFLAGS = -march=rv32imac_zicsr -mabi=ilp32 -ffreestanding -fno-exceptions \
           -fno-rtti -nostdlib -O2 -Wall -I include -I .
LDFLAGS  = -T linker.ld -nostdlib -m elf32lriscv

ASM_SRCS = asm/entry.S asm/ctxsw.S
CXX_SRCS = src/main.cpp src/console.cpp src/espsys.cpp src/mm.cpp src/vfs.cpp src/pki.cpp src/software.cpp src/coreutils.cpp src/sched.cpp src/partition.cpp src/net.cpp \
           src/wifi.cpp src/bt.cpp \
           src/gui/gui_core.cpp src/gui/gui_draw.cpp src/gui/gui_font.cpp src/gui/gui_window.cpp \
           src/gui/gui_fb.cpp src/gui/gui_lcd_spi.cpp src/gui/gui_touch_i2c.cpp src/gui/gui_widget.cpp src/gui/gui_cmd.cpp

ASM_OBJS = $(ASM_SRCS:.S=.o)
CXX_OBJS = $(CXX_SRCS:.cpp=.o)
OBJS     = $(ASM_OBJS) $(CXX_OBJS)
TARGET   = kernel
BIN      = $(TARGET).bin
IMG      = $(TARGET).img

all: $(IMG)

%.o: %.S
	$(AS) $(ASFLAGS) -o $@ $<

%.o: %.cpp include/kernel.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

$(TARGET).elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	$(SIZE) $@

$(BIN): $(TARGET).elf
	$(OBJCOPY) -O binary $< $@
	@echo "Binary size: $$(stat -c%s $@) bytes"
	$(OBJDUMP) -d -M no-aliases $< > $(TARGET).dis

$(IMG): $(BIN)
	python3 tools/pack_kernel.py $(BIN) $(IMG)

flash: $(IMG)
	esptool --chip esp32c6 --port /dev/ttyACM0 --baud 460800 \
		write_flash --flash_mode dio --flash_freq 80m \
		0x0 ../boot/bootloader.bin 0x1000 $(IMG)

monitor:
	python3 -m serial.tools.miniterm /dev/ttyACM0 115200

clean:
	rm -f $(OBJS) $(TARGET).elf $(BIN) $(IMG) $(TARGET).dis

.PHONY: all clean flash monitor
