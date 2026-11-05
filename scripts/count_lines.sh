#!/usr/bin/env bash
# 统计自研代码行数（排除 third_party 与 CoreMark 官方源），供 README 引用。
# 用法：./scripts/count_lines.sh            # 人类可读
#       ./scripts/count_lines.sh --md       # 直接输出 README 表格行
set -euo pipefail
cd "$(dirname "$0")/.."

src=$(find src -name '*.c' -o -name '*.h' | xargs wc -l | tail -1 | awk '{print $1}')
kernel=$(find kernel -name '*.c' -o -name '*.h' -o -name '*.S' | xargs wc -l | tail -1 | awk '{print $1}')
web=$(cat web/index.html web/wasm_entry.c 2>/dev/null | wc -l)
bench=$(cat bench/core_portme.c bench/core_portme.h bench/cm_stubs.c bench/start_cm.S 2>/dev/null | wc -l)
fdt=$(wc -l < src/fdt.c)
disasm=$(wc -l < src/disasm.c)

if [ "${1:-}" = "--md" ]; then
    printf '| 模拟器源码 | %s 行（`src/`，含 FDT 解析 %s 行、反汇编适配 %s 行） |\n' "$src" "$fdt" "$disasm"
    printf '| 迷你内核 | %s 行（`kernel/`） |\n' "$kernel"
    printf '| 前端自研 | %s 行（`web/`，不含生成物） |\n' "$web"
    printf '| CoreMark 移植层 | %s 行（`bench/`，不含 EEMBC 官方源） |\n' "$bench"
else
    echo "src      : $src  (fdt.c=$fdt, disasm.c=$disasm)"
    echo "kernel   : $kernel"
    echo "web      : $web"
    echo "bench    : $bench"
fi
