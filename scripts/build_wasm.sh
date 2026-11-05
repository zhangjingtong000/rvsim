#!/usr/bin/env bash
# 重新生成 web/rvsim.wasm。源文件列表与 Makefile 的 CAPSTONE_SRCS 保持一致。
# 关键：disasm.c 依赖 capstone，wasm 目标必须把 capstone 一起编进去，
# 否则 rv_disasm_at 未定义（这正是之前 README 里的 emcc 命令编不过的原因）。
set -euo pipefail
cd "$(dirname "$0")/.."

command -v emcc >/dev/null || { echo "缺少 emcc（Emscripten）"; exit 1; }

CAPSTONE="third_party/capstone/cs.c \
third_party/capstone/MCInst.c \
third_party/capstone/MCInstrDesc.c \
third_party/capstone/MCRegisterInfo.c \
third_party/capstone/Mapping.c \
third_party/capstone/SStream.c \
third_party/capstone/utils.c \
third_party/capstone/arch/RISCV/RISCVDisassembler.c \
third_party/capstone/arch/RISCV/RISCVInstPrinter.c \
third_party/capstone/arch/RISCV/RISCVMapping.c \
third_party/capstone/arch/RISCV/RISCVModule.c"

emcc -O2 -s WASM=1 -s MODULARIZE=1 -s EXPORT_NAME=createRvsim \
  -s EXPORTED_FUNCTIONS='["_rv_reset","_rv_load","_rv_step","_rv_pc","_rv_reg","_rv_priv","_rv_cycles","_rv_result","_rv_insn","_rv_csr","_rv_mem","_rv_read32","_rv_tohost","_rv_set_reg","_rv_set_pc","_rv_mem_write","_rv_uart_read","_rv_uart_write","_rv_uart_clear","_rv_disasm_at","_rv_class","_rv_hot_r","_rv_hot_w","_rv_hot_clr","_rv_bp_add","_rv_bp_del","_rv_bp_clear","_rv_bp_count","_rv_bp_at","_rv_bp_hit","_malloc","_free"]' \
  -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap","HEAPU8"]' \
  -s ALLOW_MEMORY_GROWTH=1 -s INITIAL_MEMORY=134217728 \
  -I src/include -I third_party/capstone/include -I third_party/capstone -I third_party/capstone/arch/RISCV \
  -DCAPSTONE_HAS_RISCV -DCAPSTONE_USE_SYS_DYN_MEM=1 \
  -o web/rvsim.js \
  web/wasm_entry.c src/exec.c src/csr.c src/bus.c src/mmu.c src/loader_mem.c src/fdt.c src/disasm.c \
  $CAPSTONE

echo "生成完成: $(ls -l web/rvsim.wasm | awk '{print $5}') 字节"
