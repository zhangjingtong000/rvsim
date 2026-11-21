#!/usr/bin/env python3
"""统计各目录行数，输出 README「数字」段用的表格。

改代码后重跑这个脚本，把结果贴回 README，不要手改数字。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SKIP_DIRS = {"node_modules", ".git", "build", "third_party", "riscv-tests",
             "riscv-test-env", "target", "dist"}


def count(rel, exts):
    total = 0
    files = 0
    base = os.path.join(ROOT, rel)
    if not os.path.exists(base):
        return 0, 0
    for dirpath, dirnames, names in os.walk(base):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for n in names:
            if not any(n.endswith(e) for e in exts):
                continue
            try:
                with io.open(os.path.join(dirpath, n), encoding="utf-8", errors="ignore") as f:
                    total += sum(1 for _ in f)
                files += 1
            except OSError:
                pass
    return total, files


def one(rel):
    p = os.path.join(ROOT, rel)
    if not os.path.exists(p):
        return 0
    with io.open(p, encoding="utf-8", errors="ignore") as f:
        return sum(1 for _ in f)


def main():
    rows = []
    n, f = count("src", [".c", ".h"])
    rows.append(("模拟器源码", n, "%d 个文件" % f))
    n, f = count("kernel", [".c", ".h", ".S"])
    rows.append(("迷你内核", n, "%d 个文件" % f))
    rows.append(("桌面外壳", one("web/index.html"), "web/index.html"))

    spec = io.open(os.path.join(ROOT, "docs/ui-spec.md"), encoding="utf-8").read()
    n_feat = len(re.findall(r"^\|\s*\d+\.\d+", spec, re.M))
    rows.append(("规格文档", one("docs/ui-spec.md"),
                 "docs/ui-spec.md，%d 项功能" % n_feat))
    rows.append(("对标表", one("docs/ui-benchmark.md"), "docs/ui-benchmark.md"))

    e2e = io.open(os.path.join(ROOT, "scripts/ui/e2e.js"), encoding="utf-8").read()
    n_e2e = len(re.findall(r"^\s*test\(", e2e, re.M))
    rows.append(("界面 e2e", n_e2e, "scripts/ui/e2e.js，%d 个用例" % n_e2e))

    print("| 项 | 值 |")
    print("|---|---|")
    for name, n, note in rows:
        print("| %s | **%s** | %s |" % (name, format(n, ","), note))


if __name__ == "__main__":
    main()
