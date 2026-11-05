#!/usr/bin/env bash
# rvsim 与 QEMU 的语义对照：同一份 CoreMark ELF，比对四个 CRC 值
#
# 用法: ./tests/qemu_crosscheck.sh
#
# 为什么用 CoreMark 做对照：
#   CRC 值是对全部计算结果（链表操作、矩阵乘法、状态机转换）的压缩摘要。
#   两边 CRC 完全相同，说明指令语义、异常处理、内存行为一致。
#   两边 Total ticks 不同，说明只是解释速度差异，不影响正确性。

set -u
cd "$(dirname "$0")/.." || exit 1

ELF=build/coremark.elf
if [ ! -f "$ELF" ]; then
    echo "缺少 $ELF，正在自动构建..."
    make build/coremark.elf >/dev/null 2>&1 || { echo "构建失败，请手动 make build/coremark.elf"; exit 1; }
fi

RV_CYCLES=${RV_CYCLES:-2000000000}
CM_ITER=${CM_ITER:-4000}

extract() {
    grep -oE '^\[0\](crclist|crcmatrix|crcstate|crcfinal)[[:space:]]*:[[:space:]]*0x[0-9a-f]+' \
    | sed 's/[[:space:]]//g'
}

echo "=============================================================="
echo " 对照 1/2：rvsim"
echo "=============================================================="
if command -v timeout >/dev/null; then T="timeout 280"; else T=""; fi
RV_OUT=$($T ./build/rvsim --elf "$ELF" --max-cycles "$RV_CYCLES" < /dev/null 2>&1)
echo "$RV_OUT" | grep -E "CoreMark Size|Iterations |seedcrc|crclist|crcmatrix|crcstate|crcfinal|Correct operation|Errors detected" || true
RV_CRC=$(echo "$RV_OUT" | extract)
RV_TICKS=$(echo "$RV_OUT" | grep -oE 'Total ticks[[:space:]]*:[[:space:]]*[0-9]+' | grep -oE '[0-9]+')

echo
echo "=============================================================="
echo " 对照 2/2：QEMU (virt, sifive-e34, 64M)"
echo "=============================================================="
if command -v qemu-system-riscv32 >/dev/null; then
    QM_OUT=$($T qemu-system-riscv32 -M virt -cpu sifive-e34 -m 64M -bios none \
             -kernel "$ELF" -nographic -serial mon:stdio < /dev/null 2>&1)
    echo "$QM_OUT" | grep -E "CoreMark Size|Iterations |seedcrc|crclist|crcmatrix|crcstate|crcfinal|Correct operation|Errors detected" || true
    QM_CRC=$(echo "$QM_OUT" | extract)
    QM_TICKS=$(echo "$QM_OUT" | grep -oE 'Total ticks[[:space:]]*:[[:space:]]*[0-9]+' | grep -oE '[0-9]+')
else
    echo "未安装 qemu-system-riscv32，跳过"
    QM_CRC=""
    QM_TICKS=""
fi

echo
echo "=============================================================="
echo " 结论"
echo "=============================================================="
echo "rvsim CRC:"
echo "$RV_CRC" | sed 's/^/  /'
echo "QEMU  CRC:"
echo "$QM_CRC" | sed 's/^/  /'
echo

if [ -z "$QM_CRC" ]; then
    echo "结果：未做对照（缺少 QEMU）"
    exit 0
fi

if [ "$RV_CRC" = "$QM_CRC" ]; then
    echo "结果：PASS —— 两边 CRC 完全一致，语义一致性成立"
    echo "      rvsim ticks = ${RV_TICKS:-?}，QEMU ticks = ${QM_TICKS:-?}（性能差异，不影响正确性）"
    exit 0
else
    echo "结果：FAIL —— 两边 CRC 不一致"
    diff <(echo "$RV_CRC") <(echo "$QM_CRC") | sed 's/^/  /'
    exit 1
fi
