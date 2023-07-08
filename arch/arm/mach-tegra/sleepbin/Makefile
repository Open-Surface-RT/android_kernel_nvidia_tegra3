CROSS_COMPILE = arm-none-eabi-

# Use our cross-compile prefix to set up our basic cross compile environment.
CC      = $(CROSS_COMPILE)gcc
LD      = $(CROSS_COMPILE)ld
OBJCOPY = $(CROSS_COMPILE)objcopy

CFLAGS = \
	-mtune=generic-armv7-a \
	-mcpu=cortex-a9 \
	-mlittle-endian \
	-fno-stack-protector \
	-fno-common \
	-fno-builtin \
	-ffreestanding \
	-fomit-frame-pointer \
	-std=gnu99 \
	-Werror \
	-Wall \
	-Wno-error=unused-function \
	-g \
	-O0 \
	-I/home/usr/src/surface-2/android_kernel_nvidia_tegra3/include \
        -I/home/usr/src/surface-2/android_kernel_nvidia_tegra3/arch/arm/mach-tegra/include \
        -I/home/usr/src/surface-2/android_kernel_nvidia_tegra3/arch/arm/mach-tegra/ \
        -I/home/usr/src/surface-2/android_kernel_nvidia_tegra3/arch/arm/include \
	-I/home/usr/src/surface-2/android_kernel_nvidia_tegra3/arch/arm/mach-tegra/sleepbin \
        -I/home/usr/src/surface-2/android_kernel_nvidia_tegra3/arch/arm/include/generated

LDFLAGS = -nostdlib -L /usr/lib/gcc/arm-none-eabi/4.9.3 -lgcc

all: sleep.bin 

sleep.elf: sleep.o
	$(LD) $^ -T sleep.lds  $(LDFLAGS) -Map=sleep.map -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) $< -c -o $@

%.o: %.S
	$(CC) $(CFLAGS) $(DEFINES) $< -c -o $@

%.bin: %.elf
	$(OBJCOPY) -v -O binary $< $@

clean:
	rm *.o *.bin *.elf *.map
