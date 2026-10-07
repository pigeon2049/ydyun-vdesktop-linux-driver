# r259：r210 配方可复现性审计（fabricated，零硬件触碰）——缺 GDB 手动步骤，不可直接复现

- **结论**：用户要求重建 r203/r210 GFX harness，本轮审计结论是**不可直接复现**（根因已定位，非推诿）：r210 的 GDB 脚本全是裸 `run`（无 harness 参数），shell history 无记录，报告只描述缓冲布局（b20/b22/b24/b25 + poke 位置）但没写完整 harness 命令；关键缺失件是 UMD 对象指针来源（render context/allocator 的 UMD 侧地址 harness 拿不到——`RGXCreateRenderContext` 的 b9 输出是 bridge 句柄；r210 的"设置 allocator、render context"依赖 GDB 手动读址填址，无归档）。r194 同族命令（`RGXKickTA conn b20 b24 u0 u0 u0`）可复用骨架，本轮实测最小 GFX 命令在 GDB 下 `RGXKickGfx → 3` 干净退出（r194 同形复现成功；standalone SIGSEGV 是堆 flake）。完整重建 = fixture + kick 手塑 + GDB 读址填址 + helper 观察（多轮量），立项不硬凑。

## 实测（fabricated 零硬件触碰，UMD_TRACE 走硬盘暂存区）

1. 开工预检：默认桥在载（未碰，ref 1/0）；`mkdir build/traces/r259`（新约束流程）；工作目录 `build/r210-replay`（musa.ini 在）。
2. 最小命令（r194 骨架 + r203 缓冲尺寸）：fixture 省略 + `buf{20:512,22:8192,23:768,24:1040,25:1040}` + `u64 20:{0x28,0x30}=b22,0x2d8=b25,0x2e0=b23` + `u32 20:0x4=1` + `call RGXKickGfx conn b20 b24 u0 u0 u0` → standalone SIGSEGV（预期内，堆 flake）。
3. GDB 下：`RGXKickGfx(...) -> 3`，`exited normally`（返回 3 = PrepareTA 拒，无 sync/render；与 r194 的 `RGXKickTA → 3` 同构）。
4. 暂存区已清空（`rm -rf build/traces/r259`）；`df` 无压力。

## 边界

- 返回 3 ≠ 提交；update 非零仍未见（r194 口径延续）。
- 本轮未改码、未跑门禁（纯 recon）、未碰会话；GDB 只读用户态。
- r257 的签名恢复（rdi=render ctx 等）与本轮实测一致（conn 作 rdi 可进 PrepareTA）。

## 下一步（候选，需批准）

1. fixture + kick 手塑 + GDB 读址填址完整重建（离线，多轮）。
2. 真桥 observer 下重放（批准执行）。
