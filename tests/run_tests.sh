#!/bin/bash
# riscv-tests 批量跑分器
# 用法: ./run_tests.sh [rv32ui|rv32um|rv32ua|rv32mi|rv32si|all]

# 路径按脚本自身位置推导（scripts 位于 <repo>/tests/），
# 这样在任意目录、任意克隆位置都能测到"当前这份代码"，而不是别的仓库。
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ISA_DIR="${ISA_DIR:-$REPO_ROOT/tests/riscv-tests/isa}"
SIM="${SIM:-$REPO_ROOT/build/rvsim}"
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

# 官方用例数为 0 视为环境错误（不能因为定向测试凑够数就宣布"全部通过"）
if [ "$total" -eq 0 ]; then
  echo "------------------------------"
  echo "总计 0/0 通过  ← 未找到任何 riscv-tests 官方用例！"
  echo "检查 ISA_DIR=$ISA_DIR 是否有 rv32*-p-* 文件（先跑 ./scripts/fetch_deps.sh 并编译 riscv-tests）"
  exit 1
fi

# 定向测试（tests/directed/，针对 FR 条目的断言，不是 riscv-tests 官方用例）
DIRECTED_DIR="$REPO_ROOT/tests/directed"
if [ "$SUITE" = "all" ] && ls "$DIRECTED_DIR"/*.elf >/dev/null 2>&1; then
  sub_total=0; sub_pass=0
  for t in "$DIRECTED_DIR"/*.elf; do
    name=$(basename "$t" .elf)
    out=$(timeout 30 "$SIM" --elf "$t" --max-cycles 5000 2>&1)
    sub_total=$((sub_total+1)); total=$((total+1))
    if echo "$out" | grep -q "结果：PASS"; then
      sub_pass=$((sub_pass+1)); passed=$((passed+1))
    else
      failed=$((failed+1)); FAILED_LIST="$FAILED_LIST $name"
    fi
  done
  printf "%-8s %2d/%-2d\n" "directed" "$sub_pass" "$sub_total"
fi

echo "------------------------------"
printf "总计 %d/%d 通过" "$passed" "$total"
# 关键：一个用例都没找到时必须报错。否则路径写错/依赖没拉，
# 会打印"总计 0/0 通过（全部通过）"——这是假通过，比没测试更糟。
if [ "$total" -eq 0 ]; then
  echo "  ← 未找到任何用例！"
  echo "检查 ISA_DIR=$ISA_DIR 是否有 rv32*-p-* 文件（先跑 ./scripts/fetch_deps.sh 并编译 riscv-tests）"
  exit 1
fi
if [ $failed -gt 0 ]; then
  echo "，失败 $failed 个："
  for f in $FAILED_LIST; do echo "  - $f"; done
else
  echo "（全部通过）"
fi