# 验证口径与数字来源

README 里的数字都能用仓库里的脚本复现。这里记口径、复现前提，以及哪些结论不能声称。
方法论层面的论证在 `docs/design-ai-os.md` 与 `docs/CHANGELOG-review.md`。

## 数字从哪来

| 数字 | 来源 |
|---|---|
| 模拟器 2,560 行 / 内核 6,638 行 / 外壳 3,946 行 | `python3 scripts/count_lines.py` |
| 91 个测试 | 82 个 riscv-tests 官方用例 + 9 个 `tests/directed/*.S` |
| 19 项能力契约 | `tests/contract.js` 里 `record()` 的条数 |
| 153 个界面用例 | `scripts/ui/e2e.js` 里 `test(` 的条数 |
| WASM 96,935 字节 | `scripts/build_wasm.sh` 产出的 `web/rvsim.<hash>.wasm` |

## 复现前提

干净克隆里没有 `build/`、`third_party/`、`tests/riscv-tests/`、`web/images/kernel.elf`，
它们都在 `.gitignore` 里。先 `make deps && make -j4 && make directed`，再跑测试。
Linux 镜像、QEMU、playwright 同样要自备。

离线构建时从已构建好的机器拷这两个目录：

```bash
tar czf rvsim-deps.tgz third_party/capstone tests/riscv-tests   # 源机器
tar xzf rvsim-deps.tgz && make -j4                              # 目标机器
```

GitHub 直连失败时走代理（脚本读大写 `HTTPS_PROXY`，会传给 `curl -x` 与 `git -c http.proxy`）：

```bash
HTTPS_PROXY=http://127.0.0.1:7897 make deps
```

## QEMU 对照的口径

- CoreMark：只比四个 CRC 与 Size，不比时间、不比 cycle。QEMU 侧跑不满 10 秒会打印
  `ERROR! Must execute for at least 10 secs for a valid result!`，脚本靠 `timeout` 结束，
  不影响 CRC 一致。
- Linux：`uname -a` 逐字节一致；`ls /` 与 `free` 去 ANSI、去时间戳、数字归一化后按行集合比。
  启动日志带时间戳，两套设备树的探测顺序不同，内核自身内存占用也差约 10 KB，这些属于噪声；
  整段逐字节比只会得到假失败。

## 不能声称的部分

- **PMP 没验到**：`rv32mi-p-*` 里涉及 PMP 的用例在 `G=0` 下不校验权限，属于「通过但没验到」。
  要补这块得让用例在 `G=1` 下跑，并确认越权访问真的触发异常。
- **CoreMark 的时间基准是 guest 时间**：`MTIME_DIV=10` 让每 10 条指令 `mtime+1`，
  333 iters/sec 按墙钟折算约 80；由它算出的 3.33 CoreMark/MHz 假设了 100MHz，没有硬件依据。
  可信的是四个 CRC。
- **事件流有界**：16384 条环形缓冲会覆盖旧事件，`replay` 的 `complete` 字段标记有没有丢。
- **WASM 体积随工具链变**：CI 比对的是仓库内产物是否与源码同步，不是固定字节数。
