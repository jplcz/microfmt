CROSS_COMPILE ?= aarch64-linux-gnu-

AS  = $(CROSS_COMPILE)as
CXX = $(CROSS_COMPILE)g++
LD  = $(CROSS_COMPILE)ld
OBJDUMP = $(CROSS_COMPILE)objdump

CFLAGS   = -Os -Wall -Wextra -nostdlib -fno-exceptions -fno-rtti -DMICROFMT_DISABLE_ASSERT_STDIO=1 -fno-pic
CXXFLAGS = $(CFLAGS) -std=c++20 -I. -I../../../include

all: firmware.elf

start.o: start.s
	$(AS) start.s -o start.o

main.o: main.cpp
	$(CXX) $(CXXFLAGS) -c main.cpp -o main.o

firmware.elf: start.o main.o linker.ld
	$(LD) -T linker.ld -static -Map firmware.map start.o main.o -o firmware.elf
	$(OBJDUMP) -d firmware.elf | c++filt > firmware-elf.S

run: firmware.elf
	qemu-system-aarch64 -M virt -cpu cortex-a53 -nographic -kernel firmware.elf -semihosting-config enable=on,target=native

clean:
	rm -f *.o firmware.elf
