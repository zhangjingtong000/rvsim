# rvsim：RISC-V 模拟器 + 迷你内核
CC       ?= gcc
CROSS    ?= riscv64-unknown-elf-
CFLAGS   ?= -O2 -g -Wall -Wextra -std=c11
# 默认目标必须是 all。第一个目标是 capstone 头文件，直接敲 make 只会检查它，
# 不会构建模拟器和内核镜像。
.DEFAULT_GOAL := all

KCFLAGS  := -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
            -ffreestanding -fno-builtin -O2 -Wall -Wextra
SRCDIR    = src
OBJDIR    = build
TARGET    = build/rvsim
# 干净克隆里 third_party/ 不存在（被 .gitignore），必须显式拉取。
CAPSTONE_HDR := third_party/capstone/include/capstone/capstone.h
$(CAPSTONE_HDR):
	@echo ""
	@echo "缺少 capstone 源码（third_party/capstone）。"
	@echo "先执行：  make deps     （等价于 ./scripts/fetch_deps.sh）"
	@echo ""
	@exit 1

deps:
	./scripts/fetch_deps.sh all

.PHONY: deps

SRCS = $(SRCDIR)/main.c $(SRCDIR)/exec.c $(SRCDIR)/csr.c $(SRCDIR)/bus.c \
       $(SRCDIR)/loader.c $(SRCDIR)/mmu.c $(SRCDIR)/disasm.c $(SRCDIR)/fdt.c
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

KSRCS = kernel/main.c kernel/vm.c kernel/uart.c kernel/proc.c kernel/shell.c kernel/fs.c kernel/elf.c kernel/fb.c
KASMS = kernel/start.S kernel/s_entry.S kernel/user_prog.S

# 桌面外壳启动时无条件拉 images/kernel.elf。以前这一步靠手动 cp，
# 而 *.elf 在 .gitignore 里——干净克隆后页面显示「就绪」但终端永远没提示符。
# 现在内核镜像由 make 自动产出并同步过去。
WEB_KERNEL = web/images/kernel.elf

all: $(TARGET) $(OBJDIR)/kernel.elf $(WEB_KERNEL)

$(WEB_KERNEL): $(OBJDIR)/kernel.elf
	@mkdir -p $(dir $@)
	@cp $< $@
	@echo "  同步 $< -> $@"

$(TARGET): $(OBJS) $(CAPSTONE_HDR) $(CAPSTONE_OBJS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# 干净克隆里 third_party/ 不存在。让每个模拟器目标文件都先过 capstone 头检查，
# 否则 make 会先跑到 disasm.c 的 #include 才报 "capstone.h not found"，
# 而不是给出 "先执行 make deps" 的提示。
$(OBJS): | $(CAPSTONE_HDR)

$(OBJDIR)/capstone/%.o: $(CAPSTONE_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CS_CFLAGS) -c -o $@ $<

# 头文件依赖：改 cpu.h/bus.h 的结构体必须触发重编，
# 否则会拿到 ABI 不一致的 .o（表现为随机崩溃，极难查）。
# 头文件依赖只加在模拟器自己的源文件上。加到全局 CFLAGS 会波及 capstone，
# 而 capstone 的 arch 头文件（tricore.h 等）不能单独编译（clean 后必挂）。
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) -MMD -MP $(CFLAGS) -I$(SRCDIR)/include $(CS_CFLAGS) -c -o $@ $<

# 用户程序：链接到 0x80400000，转成裸二进制
$(OBJDIR)/fork_prog.bin: kernel/user/fork.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $(OBJDIR)/fork_prog.elf kernel/user/fork.S
	$(CROSS)objcopy -O binary $(OBJDIR)/fork_prog.elf $@

$(OBJDIR)/badptr_prog.bin: kernel/user/badptr.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $(OBJDIR)/badptr_prog.elf kernel/user/badptr.S
	$(CROSS)objcopy -O binary $(OBJDIR)/badptr_prog.elf $@

$(OBJDIR)/fdtest_prog.elf: kernel/user/fdtest.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $(OBJDIR)/fdtest_prog.elf kernel/user/fdtest.S

$(OBJDIR)/spin_prog.elf: kernel/user/spin.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $(OBJDIR)/spin_prog.elf kernel/user/spin.S

# user_prog.elf 必须有自己的规则：user_prog.bin 依赖它，
# 而 user_prog.S 里 .incbin 的是 .elf。原来只在 .bin 的 recipe 里临时编译，
# 干净树上 make 会直接 "No rule to make target 'build/user_prog.elf'"。
$(OBJDIR)/user_prog.elf: kernel/user/hello.S
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany -nostdlib -nostartfiles \
		-Ttext=0x80400000 -o $@ kernel/user/hello.S

$(OBJDIR)/user_prog.bin: $(OBJDIR)/user_prog.elf
	$(CROSS)objcopy -O binary $< $@

# 内核
# fdtest_prog.elf 必须进依赖：kernel/user_prog.S 里 .incbin 了它，
# 漏掉的话干净树和 -j 并行构建都会挂（偶尔能过，极难查）。
$(OBJDIR)/kernel.elf: $(KSRCS) $(KASMS) $(OBJDIR)/user_prog.bin $(OBJDIR)/fork_prog.bin \
    $(OBJDIR)/badptr_prog.bin $(OBJDIR)/fdtest_prog.elf $(OBJDIR)/spin_prog.elf kernel/linker.ld
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc $(KCFLAGS) -T kernel/linker.ld -o $@ \
		$(KASMS) $(KSRCS) -lgcc

# 精简内核（A/B 实验的基线组）：能力集只含基础命令
$(OBJDIR)/kernel-min.elf: $(KSRCS) $(KASMS) $(OBJDIR)/user_prog.bin $(OBJDIR)/fork_prog.bin $(OBJDIR)/badptr_prog.bin kernel/linker.ld
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc $(KCFLAGS) -DAIOS_MINIMAL -T kernel/linker.ld -o $@ \
		$(KASMS) $(KSRCS) -lgcc

# CoreMark：与迷你内核共用 uart/vm，链接到 0x80000000
CM_SRCS = bench/core_list_join.c bench/core_matrix.c bench/core_state.c \
          bench/core_util.c bench/core_main.c bench/core_portme.c bench/ee_printf_official.c
CM_OBJS = $(OBJDIR)/coremark.o

$(OBJDIR)/coremark.elf: $(CM_SRCS) bench/core_portme.h kernel/uart.c kernel/vm.c kernel/start.S kernel/linker.ld
	@mkdir -p $(OBJDIR)
	$(CROSS)gcc -Wno-implicit-fallthrough -Wno-unused-parameter $(KCFLAGS) -DSTANDALONE=1 -DPERFORMANCE_RUN=1 -Ibench -Ikernel -T kernel/linker.ld -o $@ \
		bench/start_cm.S bench/cm_stubs.c kernel/uart.c $(CM_SRCS) -lgcc

clean:
	rm -rf $(OBJDIR)
	rm -f dts/*.dtb tests/directed/*.elf

.PHONY: all clean

# ── 定向测试（tests/directed/*.S -> *.elf） ──
# 这些 ELF 是构建产物（被 .gitignore），所以必须在干净克隆里能重建，
# 否则 run_tests.sh 会静默少跑这几条。
DIRECTED_SRCS := $(wildcard tests/directed/*.S)
DIRECTED_ELFS := $(patsubst %.S,%.elf,$(DIRECTED_SRCS))

$(DIRECTED_ELFS): %.elf: %.S tests/directed/link.ld
	$(CROSS)gcc -march=rv32ima_zicsr_zifencei -mabi=ilp32 -mcmodel=medany \
		-nostdlib -nostartfiles -T tests/directed/link.ld -o $@ $<

directed: $(DIRECTED_ELFS)

# ── 设备树（dts/*.dts -> *.dtb） ──
DTS_SRCS := $(wildcard dts/*.dts)
DTS_DTBS := $(patsubst %.dts,%.dtb,$(DTS_SRCS))

$(DTS_DTBS): %.dtb: %.dts
	dtc -I dts -O dtb -o $@ $<

dts: $(DTS_DTBS)

.PHONY: directed dts

-include $(wildcard $(OBJDIR)/*.d)
