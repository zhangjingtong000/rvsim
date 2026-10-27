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
       $(SRCDIR)/loader.c $(SRCDIR)/mmu.c $(SRCDIR)/disasm.c
OBJS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SRCS))

# capstone 5.0.7（仅 RISC-V，源码内置）
CAPSTONE_DIR  = third_party/capstone
CAPSTONE_SRCS = $(CAPSTONE_DIR)/cs.c $(CAPSTONE_DIR)/MCInst.c \
                $(CAPSTONE_DIR)/MCInstrDesc.c $(CAPSTONE_DIR)/MCRegisterInfo.c \
                $(CAPSTONE_DIR)/Mapping.c $(CAPSTONE_DIR)/SStream.c \
                $(CAPSTONE_DIR)/utils.c \
                $(CAPSTONE_DIR)/arch/RISCV/RISCVDisassembler.c \
                $(CAPSTONE_DIR)/arch/RISCV/RISCVInstPrinter.c \
                $(CAPSTONE_DIR)/arch/RISCV/RISCVMapping.c \
                $(CAPSTONE_DIR)/arch/RISCV/RISCVModule.c
CAPSTONE_OBJS = $(patsubst $(CAPSTONE_DIR)/%.c,$(OBJDIR)/capstone/%.o,$(CAPSTONE_SRCS))
CS_CFLAGS     = -DCAPSTONE_HAS_RISCV -DCAPSTONE_USE_SYS_DYN_MEM=1 \
                -I$(CAPSTONE_DIR)/include -I$(CAPSTONE_DIR) -I$(CAPSTONE_DIR)/arch/RISCV

KSRCS = kernel/main.c kernel/vm.c kernel/uart.c kernel/proc.c kernel/shell.c
KASMS = kernel/start.S kernel/s_entry.S kernel/user_prog.S

all: $(TARGET) $(OBJDIR)/kernel.elf

$(TARGET): $(OBJS) $(CAPSTONE_OBJS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/capstone/%.o: $(CAPSTONE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CS_CFLAGS) -c -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -I$(SRCDIR)/include $(CS_CFLAGS) -c -o $@ $<

# 用户程序：链接到 0x80400000，转成裸二进制
$(OBJDIR)/fork_prog.bin: kernel/user/fork.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $(OBJDIR)/fork_prog.elf kernel/user/fork.S
	$(CROSS)objcopy -O binary $(OBJDIR)/fork_prog.elf $@

$(OBJDIR)/user_prog.bin: kernel/user/hello.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $(OBJDIR)/user_prog.elf kernel/user/hello.S
	$(CROSS)objcopy -O binary $(OBJDIR)/user_prog.elf $@

# 内核
$(OBJDIR)/kernel.elf: $(KSRCS) $(KASMS) $(OBJDIR)/user_prog.bin $(OBJDIR)/fork_prog.bin kernel/linker.ld
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc $(KCFLAGS) -T kernel/linker.ld -o $@ \
		$(KASMS) $(KSRCS) -lgcc

# CoreMark：与迷你内核共用 uart/vm，链接到 0x80000000
CM_SRCS = bench/core_list_join.c bench/core_matrix.c bench/core_state.c \
          bench/core_util.c bench/core_main.c bench/core_portme.c bench/ee_printf_official.c
CM_OBJS = $(OBJDIR)/coremark.o

$(OBJDIR)/coremark.elf: $(CM_SRCS) bench/core_portme.h kernel/uart.c kernel/vm.c kernel/start.S kernel/linker.ld
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc $(KCFLAGS) -DSTANDALONE=1 -DPERFORMANCE_RUN=1 -Ibench -Ikernel -T kernel/linker.ld -o $@ \
		bench/start_cm.S bench/cm_stubs.c kernel/uart.c $(CM_SRCS) -lgcc

clean:
	rm -rf $(OBJDIR)

.PHONY: all clean
