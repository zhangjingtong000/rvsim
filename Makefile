# rvsim：RISC-V 模拟器 + 迷你内核
CC       ?= gcc
CROSS    ?= riscv64-unknown-elf-
CFLAGS   ?= -O2 -g -Wall -Wextra -std=c11
KCFLAGS  := -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
            -ffreestanding -fno-builtin -O2 -Wall -Wextra
SRCDIR    = src
OBJDIR    = build
TARGET    = $(OBJDIR)/rvsim

SRCS = $(SRCDIR)/main.c $(SRCDIR)/exec.c $(SRCDIR)/csr.c $(SRCDIR)/bus.c \
       $(SRCDIR)/loader.c $(SRCDIR)/mmu.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

KSRCS = kernel/main.c kernel/vm.c kernel/uart.c kernel/proc.c
KASMS = kernel/start.S kernel/s_entry.S kernel/user_prog.S

all: $(TARGET) $(OBJDIR)/kernel.elf

$(TARGET): $(OBJS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR)/include -c -o $@ $<

# 用户程序：链接到 0x80400000，转成裸二进制
$(OBJDIR)/user_prog.bin: kernel/user/hello.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $(OBJDIR)/user_prog.elf kernel/user/hello.S
	$(CROSS)objcopy -O binary $(OBJDIR)/user_prog.elf $@

# 内核
$(OBJDIR)/kernel.elf: $(KSRCS) $(KASMS) $(OBJDIR)/user_prog.bin kernel/linker.ld
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc $(KCFLAGS) -T kernel/linker.ld -o $@ \
		$(KASMS) $(KSRCS) -lgcc

clean:
	rm -rf $(OBJDIR)

.PHONY: all clean
