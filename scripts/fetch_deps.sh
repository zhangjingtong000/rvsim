#!/usr/bin/env bash
# 拉取 rvsim 的第三方依赖。被 .gitignore 忽略的目录都由本脚本生成。
# 用法：./scripts/fetch_deps.sh [capstone|riscv-tests|linux|all]
set -euo pipefail
cd "$(dirname "$0")/.."

PROXY="${HTTPS_PROXY:-}"
what="${1:-all}"

say() { printf '\n=== %s ===\n' "$*"; }

fetch_capstone() {
    say "capstone 5.0.7 源码"
    if [ -d third_party/capstone/include ]; then
        echo "已存在，跳过"
        return
    fi
    mkdir -p third_party
    local url="https://github.com/capstone-engine/capstone/archive/refs/tags/5.0.7.tar.gz"
    local tmp; tmp="$(mktemp -d)"
    if command -v curl >/dev/null; then
        curl -fL ${PROXY:+-x "$PROXY"} "$url" -o "$tmp/cs.tgz"
    else
        wget -O "$tmp/cs.tgz" "$url"
    fi
    tar xzf "$tmp/cs.tgz" -C "$tmp"
    mkdir -p third_party/capstone
    # 只需要源码 + 头文件，不需要 cmake（本项目手工编译 11 个 .c）
    cp -r "$tmp"/capstone-5.0.7/. third_party/capstone/
    rm -rf "$tmp"
    echo "完成：third_party/capstone"
}

fetch_riscv_tests() {
    say "riscv-tests（82 个 p 测试的 ELF）"
    if [ -d tests/riscv-tests/isa ]; then
        echo "已存在，跳过"
        return
    fi
    mkdir -p tests
    local tmp; tmp="$(mktemp -d)"
    if command -v curl >/dev/null; then
        curl -fL ${PROXY:+-x "$PROXY"} \
            https://github.com/riscv-software-src/riscv-tests/archive/refs/heads/master.tar.gz \
            -o "$tmp/t.tgz"
    else
        wget -O "$tmp/t.tgz" https://github.com/riscv-software-src/riscv-tests/archive/refs/heads/master.tar.gz
    fi
    tar xzf "$tmp/t.tgz" -C "$tmp"
    mv "$tmp/riscv-tests-master" tests/riscv-tests
    echo "源码已就位；需要自行用 riscv64-unknown-elf-gcc 编译出 isa/rv32*-p-* 的 ELF"
    echo "参考：make -C tests/riscv-tests/isa XLEN=32"
    rm -rf "$tmp"
}

fetch_linux() {
    say "Linux 6.1.14 Image（可选，约 5 MB）"
    if [ -f third_party/linux-image/Image ]; then
        echo "已存在，跳过"
        return
    fi
    mkdir -p third_party/linux-image
    echo "本仓库不内置内核镜像。请自行从 https://www.kernel.org/ 下载 6.1.x"
    echo "并用 riscv32 工具链编译（defconfig + CONFIG_RISCV_SBI_V01 + 4 KB 页）。"
    echo "放到 third_party/linux-image/Image 即可。"
}

case "$what" in
    capstone)     fetch_capstone ;;
    riscv-tests)  fetch_riscv_tests ;;
    linux)        fetch_linux ;;
    all)          fetch_capstone; fetch_riscv_tests; fetch_linux ;;
    *) echo "用法: $0 [capstone|riscv-tests|linux|all]"; exit 1 ;;
esac

say "依赖检查"
for c in riscv64-unknown-elf-gcc dtc python3; do
    command -v "$c" >/dev/null && echo "  $c: $(command -v "$c")" || echo "  $c: 缺失"
done
echo "  node: $(command -v node || echo 缺失，WASM 需要)"
echo "  emcc: $(command -v emcc || echo 缺失，WASM 需要)"
echo "  cmake: $(command -v cmake || echo '缺失（本项目手工编译 capstone，不需要）')"
