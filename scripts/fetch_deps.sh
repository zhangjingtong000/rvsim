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
    # 用 git clone --recursive：riscv-tests 的 isa 测试依赖 env 子模块（../env/p/link.ld）。
    # 不写死 commit（我没有核实过上游 hash，编一个只会误导）；改为 clone 后把实际
    # commit 记进 tests/riscv-tests/.pinned-commit，便于复现时对齐。
    if command -v git >/dev/null; then
        if ! git clone --recursive --depth 1 \
             ${PROXY:+-c http.proxy="$PROXY"} \
             https://github.com/riscv-software-src/riscv-tests.git "$tmp/rt" 2>/dev/null; then
            echo "git clone 失败，回退到 tarball"
        fi
    fi
    if [ -d "$tmp/rt" ]; then
        ( cd "$tmp/rt" && git rev-parse HEAD > /tmp/rt_commit 2>/dev/null ) || true
        mv "$tmp/rt" tests/riscv-tests
        if [ -s /tmp/rt_commit ]; then
            cp /tmp/rt_commit tests/riscv-tests/.pinned-commit
            echo "已记录 riscv-tests commit: $(cat /tmp/rt_commit)"
        fi
        # riscv-tests 的 isa 测试依赖 env 子模块（../env/p/link.ld），必须一起拉
        if [ -d tests/riscv-tests/env ] && [ -f tests/riscv-tests/env/p/link.ld ]; then
            echo "env 子模块已就位"
        else
            echo "警告：tests/riscv-tests/env/p/link.ld 缺失，官方用例无法编译"
        fi
    else
        if command -v curl >/dev/null; then
            curl -fL ${PROXY:+-x "$PROXY"} \
                "https://github.com/riscv-software-src/riscv-tests/archive/refs/heads/master.tar.gz" \
                -o "$tmp/t.tgz"
        else
            wget -O "$tmp/t.tgz" "https://github.com/riscv-software-src/riscv-tests/archive/refs/heads/master.tar.gz"
        fi
        tar xzf "$tmp/t.tgz" -C "$tmp"
        mv "$tmp"/riscv-tests-* tests/riscv-tests
        echo "已用 tarball 获取（未含 env 子模块，可能编不出官方用例）"
    fi
    rm -rf "$tmp"
    echo "源码已就位，开始编译 rv32 的 p 测试..."
    # 只编 -p 变体（bare-metal）。-v 变体链接 newlib 的 string.h，
    # 没装 newlib 的工具链会直接编不过——而 -v 从来没被本项目的测试用到。
    if ( cd tests/riscv-tests/isa && \
         pt=""; for d in rv32ui rv32um rv32ua rv32mi rv32si; do
             for f in "$d"/*.S; do [ -e "$f" ] && pt="$pt $d-p-$(basename "$f" .S)"; done
         done; \
         make -j"$(nproc 2>/dev/null || echo 4)" XLEN=32 $pt ) >/dev/null 2>&1; then
        echo "编译完成：$(ls tests/riscv-tests/isa/rv32*-p-* 2>/dev/null | grep -v '\.' | wc -l) 个 ELF"
    else
        echo "编译失败。请确认 riscv64-unknown-elf-gcc 在 PATH 里，然后手动执行："
        echo "  make -C tests/riscv-tests/isa XLEN=32 rv32ui rv32um rv32ua rv32mi rv32si"
    fi
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
