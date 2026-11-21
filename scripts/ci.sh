#!/usr/bin/env bash
# 一条命令跑完全部回归。CI 与本地共用。
#
#   ./scripts/ci.sh          构建 + 内核回归 + 界面 e2e
#   ./scripts/ci.sh kernel   只跑内核
#   ./scripts/ci.sh ui       只跑界面
#
#   --allow-skip   环境缺东西（没有 playwright）时，跳过并当作通过
#
# 退出码：任何一步失败、或该跑的被跳过却没给 --allow-skip，都是非零。
# 早先的版本「跳过 e2e 仍然打印全部通过并 exit 0」——CI 绿了不等于界面绿了，
# 这是最危险的一种假通过，现在默认不允许。
set -uo pipefail

cd "$(dirname "$0")/.." || exit 1
MODE="all"
ALLOW_SKIP=0
for a in "$@"; do
  case "$a" in
    all|kernel|ui) MODE="$a" ;;
    --allow-skip) ALLOW_SKIP=1 ;;
    *) echo "未知参数：$a"; exit 2 ;;
  esac
done
FAIL=0
SKIPPED="" 

hr() { printf '\n\033[1m== %s ==\033[0m\n' "$1"; }

# ── 构建 ──
if [ "$MODE" = "all" ] || [ "$MODE" = "kernel" ]; then
  hr "构建"
  if ! make -j"$(nproc 2>/dev/null || echo 2)" >/tmp/rvsim-ci-build.log 2>&1; then
    echo "构建失败，日志：/tmp/rvsim-ci-build.log"
    tail -20 /tmp/rvsim-ci-build.log
    exit 1
  fi
  echo "ok"
fi

# ── 内核回归（riscv-tests + 能力契约）──
if [ "$MODE" = "all" ] || [ "$MODE" = "kernel" ]; then
  hr "内核回归"
  if ! ./tests/run_tests.sh all; then
    echo "内核回归失败"
    FAIL=1
  fi
fi

# ── 界面 e2e ──
if [ "$MODE" = "all" ] || [ "$MODE" = "ui" ]; then
  hr "界面 e2e"
  if ! command -v node >/dev/null 2>&1; then
    echo "跳过：没有 node"
    SKIPPED="$SKIPPED node"
  elif ! node -e "require('playwright')" >/dev/null 2>&1; then
    echo "跳过：没有 playwright（Windows 侧用 scripts/ui/ci-e2e.ps1 跑）"
    SKIPPED="$SKIPPED playwright"
  else
    PORT="${E2E_PORT:-8799}"
    if ! curl -s --noproxy '*' -o /dev/null "http://127.0.0.1:$PORT/index.html"; then
      echo "起前端服务 :$PORT"
      python3 -m http.server "$PORT" --directory web >/dev/null 2>&1 &
      SRV=$!
      trap 'kill $SRV 2>/dev/null' EXIT
      sleep 1.5
    fi
    if ! node scripts/ui/e2e.js; then
      echo "界面 e2e 失败"
      FAIL=1
    fi
  fi
fi

hr "结果"
if [ "$FAIL" -ne 0 ]; then
  echo "有测试失败"
  exit 1
fi
if [ -n "$SKIPPED" ]; then
  if [ "$ALLOW_SKIP" = "1" ]; then
    echo "通过（跳过了：$SKIPPED）"
    exit 0
  fi
  echo "有测试被跳过：$SKIPPED"
  echo "这些没跑，不能算通过。确认要跳过就加 --allow-skip。"
  exit 1
fi
echo "全部通过"
