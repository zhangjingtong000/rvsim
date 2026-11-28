#!/usr/bin/env bash
# 反复 reset，看页池占用是否上涨（泄漏）。
set -u
cd "$(dirname "$0")/.." || exit 1
N="${1:-10}"

OUT=$(
  {
    echo mem
    for _ in $(seq 1 "$N"); do echo reset; done
    echo mem
  } | timeout 120 ./build/rvsim --elf build/kernel.elf --max-cycles 100000000 2>&1
)

echo "reset 执行次数: $(printf '%s' "$OUT" | grep -c '已重置')"
echo "--- 前后页池 ---"
printf '%s' "$OUT" | grep '页池'
echo "--- 崩溃迹象 ---"
printf '%s' "$OUT" | grep -cE '耗尽|fatal|卡' || true
