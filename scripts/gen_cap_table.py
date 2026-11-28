#!/usr/bin/env python3
"""从 kernel/main.c 的 g_caps 表生成 Markdown 能力表。

docs/ai-guide.md 里的能力表必须和内核真正发布的 manifest 一致。
手抄过两次都抄漏了（26 项 vs 37 项），所以改成生成：

    python3 scripts/gen_cap_table.py          # 打印表格
    python3 scripts/gen_cap_table.py --check  # 只检查 ai-guide.md 是否过期

加了新能力后重跑，把输出贴回 docs/ai-guide.md 的「3. 能力清单」一节。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MAIN = os.path.join(ROOT, "kernel", "main.c")
GUIDE = os.path.join(ROOT, "docs", "ai-guide.md")

BEGIN = "<!-- CAP-TABLE:BEGIN（由 scripts/gen_cap_table.py 生成，勿手改） -->"
END = "<!-- CAP-TABLE:END -->"

FIELD = re.compile(r'"((?:[^"\\]|\\.)*)"')


def parse_caps():
    lines = io.open(MAIN, encoding="utf-8").read().splitlines()
    start = None
    for i, l in enumerate(lines):
        if "g_caps[]" in l and "=" in l:
            start = i
            break
    if start is None:
        raise SystemExit("找不到 g_caps 表")
    caps = []
    for l in lines[start + 1:]:
        s = l.strip()
        if s.startswith("};"):
            break
        if not s.startswith("{"):
            continue
        # 每条是：{name, usage, args, irreversible, desc}
        # 字符串字段有 4 个（irreversible 是数字）：name / usage / args / desc
        fields = FIELD.findall(s)
        if len(fields) < 4:
            continue
        name, usage, _args, desc = fields[:4]
        caps.append((name, usage, desc))
    return caps


def table(caps):
    out = ["| 能力 | 用法 | 说明 |", "|---|---|---|"]
    for name, usage, desc in caps:
        u = usage.replace("|", "\\|")
        d = desc.replace("|", "\\|")
        out.append("| `%s` | `%s` | %s |" % (name, u, d))
    return "\n".join(out)


def main():
    caps = parse_caps()
    body = table(caps)

    if "--write" in sys.argv:
        guide = io.open(GUIDE, encoding="utf-8").read()
        i, j = guide.find(BEGIN), guide.find(END)
        if i < 0 or j < 0:
            print("ai-guide.md 里没有生成标记，先手工加 %s / %s" % (BEGIN, END))
            return 1
        new = guide[:i + len(BEGIN)] + "\n" + body + "\n" + guide[j:]
        io.open(GUIDE, "w", encoding="utf-8", newline="\n").write(new)
        print("已写入 ai-guide.md（%d 条能力）" % len(caps))
        return 0

    if "--check" in sys.argv:
        guide = io.open(GUIDE, encoding="utf-8").read()
        i, j = guide.find(BEGIN), guide.find(END)
        if i < 0 or j < 0:
            print("ai-guide.md 里没有生成标记")
            return 1
        cur = guide[i + len(BEGIN):j].strip("\n")
        if cur != body:
            print("ai-guide.md 的能力表已过期（g_caps 有 %d 条），重跑 gen_cap_table.py" % len(caps))
            return 1
        print("能力表与 g_caps 一致（%d 条）" % len(caps))
        return 0

    print("%s\n%s\n%s" % (BEGIN, body, END))
    return 0


if __name__ == "__main__":
    sys.exit(main())
