#!/usr/bin/env bash
# Gate 6 的「串口行集合一致」对照：同一份 Linux Image，在 rvsim 与 QEMU 上各跑一遍，
# 执行同一组命令，比对**命令输出的行集合**。
#
# 为什么不是逐字节比对整段启动日志：
#   1. 启动日志带时间戳（[0.114374]），两边时序不可能一样；
#   2. 内核探测顺序在两套设备树上略有差异；
#   3. `free` 这类命令的输出本身就是「跑起来之后」的运行时状态，
#      内核自己占了多少内存两边本来就会不同（实测差 10 KB 左右）。
# 所以分两级：
#   - **严格比对**：`uname -a` 这类完全确定的输出，必须逐字节一致；
#   - **归一化比对**：`ls /`、`free` 这类，去掉 ANSI 颜色、把数字换成占位符后比对行集合。
#
# 用法：
#   ./tests/qemu_linux_crosscheck.sh [Image 路径]
#
# 前置条件（都不随仓库提供，需自备）：
#   - `third_party/linux-image/Image`（或通过参数指定）
#   - `dts/minimal.dtb`（`make dts` 生成）
#   - `qemu-system-riscv32`
# 缺任何一样就跳过（退出码 0），不要让缺环境变成 CI 红灯。
#
# 为什么这份脚本能进仓库、而 Image/QEMU 不能：
#   脚本只是流程；Image 是 3.4 MB 的二进制、QEMU 是系统包，两者都不该进 git。
#   验证时临时放进去，跑完删掉即可 —— 别人拿到仓库后按 README 自备就能复现。

set -u

cd "$(dirname "$0")/.." || exit 1

IMAGE="${1:-third_party/linux-image/Image}"
DTB="dts/minimal.dtb"
SIM="./build/rvsim"
MAX_CYCLES="${MAX_CYCLES:-6000000000}"

skip() { echo "跳过：$*"; exit 0; }

command -v qemu-system-riscv32 >/dev/null 2>&1 || skip "没有 qemu-system-riscv32"
[ -x "$SIM" ] || skip "没有 $SIM（先 make）"
[ -f "$IMAGE" ] || skip "没有 $IMAGE（Linux 镜像需自备）"
[ -f "$DTB" ] || skip "没有 $DTB（先 make dts）"

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

# 同一组命令，两边都执行
CMDS='uname -a
ls /
free'

echo "=============================================================="
echo " 1/3：rvsim 启动 Linux 并执行命令"
echo "=============================================================="
# rvsim 的 buildroot 镜像会直接把 console 交给 shell（不经过 login 密码）
{ sleep 16; printf '%s\n' "$CMDS"; sleep 4; } \
  | timeout 240 "$SIM" --image "$IMAGE" --dtb "$DTB" --max-cycles "$MAX_CYCLES" \
  > "$TMP/rv.out" 2>&1
echo "  输出 $(wc -l < "$TMP/rv.out") 行"

echo
echo "=============================================================="
echo " 2/3：QEMU 启动同一份 Image 并执行同样的命令"
echo "=============================================================="
# QEMU 侧会经过 login，需要先给用户名；buildroot 默认 root 空密码
{ sleep 8; printf 'root\n'; sleep 1; printf '\n'; sleep 2; printf '%s\n' "$CMDS"; sleep 4; } \
  | timeout 240 qemu-system-riscv32 -M virt -cpu sifive-e34 -m 64M -bios none \
      -kernel "$IMAGE" -nographic -serial mon:stdio -append 'console=ttyS0' \
  > "$TMP/qm.out" 2>&1
echo "  输出 $(wc -l < "$TMP/qm.out") 行"

echo
echo "=============================================================="
echo " 3/3：比对"
echo "=============================================================="

# 去 ANSI 转义、去内核时间戳前缀、去行尾空白
clean() {
    sed -e 's/\x1b\[[0-9;]*m//g' \
        -e 's/^\[[[:space:]]*[0-9.]*\]//' \
        -e 's/[[:space:]]*$//' "$1"
}

# 从 shell 提示符之后开始截取：rvsim 是 "~ # "，QEMU 也是 "~ # "
after_prompt() {
    awk '/^~ #/{f=1} f' "$1"
}

clean "$TMP/rv.out" | after_prompt /dev/stdin > "$TMP/rv.cmd"
clean "$TMP/qm.out" | after_prompt /dev/stdin > "$TMP/qm.cmd"

fail=0

# ── 严格比对：uname -a ──────────────────────────────────────────
RV_UNAME=$(grep -m1 '^Linux buildroot' "$TMP/rv.cmd" | sed 's/^[[:space:]]*//')
QM_UNAME=$(grep -m1 '^Linux buildroot' "$TMP/qm.cmd" | sed 's/^[[:space:]]*//')
echo "uname -a（严格比对）"
echo "  rvsim: ${RV_UNAME:-<无>}"
echo "  qemu : ${QM_UNAME:-<无>}"
if [ -n "$RV_UNAME" ] && [ "$RV_UNAME" = "$QM_UNAME" ]; then
    echo "  ✅ 一致"
else
    echo "  ❌ 不一致"
    fail=1
fi

# ── 归一化比对：命令输出行集合 ──────────────────────────────────
# 数字、十六进制地址换成占位符；空行与提示符行去掉；排序后比对
normalize() {
    sed -e 's/0x[0-9a-fA-F]\+/<ADDR>/g' \
        -e 's/[0-9][0-9]*/<N>/g' \
        -e '/^[[:space:]]*$/d' \
        -e '/^~ #/d' \
        -e '/^\[rvsim\]/d' \
        -e '/^[a-z]* login:/d' \
        -e '/^Password:/d' \
        -e '/login\[/d' \
        -e '/qemu-system-riscv32: terminating/d'
}

clean "$TMP/rv.cmd" | normalize | sort -u > "$TMP/rv.set"
clean "$TMP/qm.cmd" | normalize | sort -u > "$TMP/qm.set"

echo
echo "命令输出行集合（归一化后）"
only_rv=$(comm -23 "$TMP/rv.set" "$TMP/qm.set")
only_qm=$(comm -13 "$TMP/rv.set" "$TMP/qm.set")

if [ -n "$only_rv" ]; then
    echo "  只在 rvsim 出现："
    echo "$only_rv" | sed 's/^/    /'
    fail=1
fi
if [ -n "$only_qm" ]; then
    echo "  只在 QEMU 出现："
    echo "$only_qm" | sed 's/^/    /'
    fail=1
fi
if [ -z "$only_rv" ] && [ -z "$only_qm" ]; then
    echo "  ✅ 行集合一致（$(wc -l < "$TMP/rv.set") 行）"
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "结论：Gate 6「串口行集合一致」通过。"
else
    echo "结论：存在差异，见上。"
fi
exit "$fail"
