# r257：RGXKickGfx 签名恢复（离线 recon，零硬件触碰）——harness 重建第一步

- **结论**：用户要求重建 r203 GFX harness 命令（r210 配方无可复现脚本，GDB 脚本全是裸 `run`，shell history 无记录）。本轮从反汇编恢复 `RGXKickGfx`（RVA `0x7e230`）6 寄存器参数用途，为重建立项：rdi=render context（贯穿传给 helper + `GetFeatures`），rsi=kick 对象A（非空检查 + `+0x28` 取 allocator 链存 local），rdx=输出缓冲b24（0x410，`param_3`，复制目标），rcx=kick 对象B（r14，`+0x28` 链），r8=输出缓冲b25（0x410，`param_5`，复制源），r9=栈传参（第 7 参数）。b20/b22 工作缓冲经 kick 对象字段链接（harness poke 指针字段），字段偏移待 GDB 动态确认。调用链：`MTSRVGetClientEventFilter` → `RGXPrepareTA(78800)` → `796b0`（7 参数：render/local/prepare-out/b24/kickB/b25/r9）→ `SubmissionCmdGenerate`。下轮：GDB 动态确认各缓冲字段偏移 + fabricated 重建 + 真桥重放（三轮量）。

## 实测（反汇编核对，objdump 地面实锤）

1. 入口：`rsi→rbx`（非空检查），`rdi→r12`，`rcx→r14`，`rdx/r8/r9` 存栈。
2. `call 78800(r12, rbx, r15-local)` = RGXPrepareTA（r197 `FUN_00178800`）。
3. `call 796b0(rdi=r12, rsi=local-ea0, rdx=r15, rcx=rdx_saved, r8=r14, r9=r8_saved, stack=r9_saved)`。
4. `SubmissionCmdGenerate` 在 `0x7ebe0`（r199 已定位 allocator 链 `kick+0x28 → +0x200`）；`SubmissionDestroy` 在 `0x7ebef`。
5. r210 缓冲分配（b24/b25=0x410，b22+0x48=handle/b22+0x50=0x1234，b20+0x6f0=1，tuple b20+0x2f0~0x308）与本轮参数映射一致（b24=rdx 目标，b25=r8 源，0x408 复制）。

## 边界

- 参数语义是反汇编解读（§9：伪 C/反汇编是假设），须 GDB 动态确认；本轮零硬件触碰（未运行 UMD，只读 objdump），未改码、未跑门禁。
- `/tmp` 用途已查清（见下节），与本轮无关。

## 附：为什么中间产物放 `/tmp`（用户要求查清）

- shim 的 `UMD_TRACE` 默认 `fopen(p && *p ? p : "/tmp/opencode/umda/trace.jsonl", "a")`（`umd_bridge_shim.c:861`，append 模式）；`UMD_CCB_DUMP_DIR` opt-in 落盘。默认放 /tmp 是因为 trace 量极大（单轮 8201 行，shim 注释称全量可达数千万行），放仓库会污染 git 且有灌满风险（r67 教训：trace 洪泛灌满伪装成测试回归；AGENTS.md §5：`/tmp` 写大文件前先 `df -h`）。
- 易失中间产物（原始 trace、blit 二进制、GDB 工作区）放 `/tmp`，精选证据拷贝入库（`reports/*.jsonl` 0600、`*.bin`、`*.md`）。r212 起持久化入 reports，之前 r148/r149 的 `/tmp` trace 已随重启丢失（前车之鉴，r212 报告明记）。
- `build/`（5.9G）、`decompiled/`（2.5G）等 gitignore 大目录同理不在版本控制内。

## 下一步（候选，需批准）

1. GDB 动态确认 kick 字段偏移 + fabricated harness 重建（离线）。
2. 真桥 observer 下重放（批准执行）：UMD 实时生成 → 真桥观察活体闭环。
