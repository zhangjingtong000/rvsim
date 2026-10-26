#!/bin/bash
# riscv-tests 批量跑分器
# 用法: ./run_tests.sh [rv32ui|rv32um|rv32ua|rv32mi|rv32si|all]

ISA_DIR="${ISA_DIR:-/home/rvsim/tests/riscv-tests/isa}"
SIM="${SIM:-/home/rvsim/build/rvsim}"
MAXC="${MAXC:-5000000}"
SUITE="${1:-all}"

if [ ! -x "$SIM" ]; then echo "模拟器不存在: $SIM"; exit 1; fi

if [ "$SUITE" = "all" ]; then
  PREFIXES="rv32ui rv32um rv32ua rv32mi rv32si"
else
  PREFIXES="$SUITE"
fi

total=0; passed=0; failed=0
FAILED_LIST=""

for p in $PREFIXES; do
  files=$(ls "$ISA_DIR"/${p}-p-* 2>/dev/null | grep -v '\.')
  [ -z "$files" ] && continue
  sub_total=0; sub_pass=0
  for t in $files; do
    name=$(basename "$t")
    out=$(timeout 120 "$SIM" --elf "$t" --max-cycles "$MAXC" 2>&1)
    sub_total=$((sub_total+1)); total=$((total+1))
    if echo "$out" | grep -q "结果：PASS"; then
      sub_pass=$((sub_pass+1)); passed=$((passed+1))
    else
      failed=$((failed+1))
      FAILED_LIST="$FAILED_LIST $name"
    fi
  done
  printf "%-8s %2d/%-2d\n" "$p" "$sub_pass" "$sub_total"
done

echo "------------------------------"
printf "总计 %d/%d 通过" "$passed" "$total"
if [ $failed -gt 0 ]; then
  echo "，失败 $failed 个："
  for f in $FAILED_LIST; do echo "  - $f"; done
else
  echo "（全部通过）"
fi