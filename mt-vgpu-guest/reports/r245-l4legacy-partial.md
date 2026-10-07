# r245：L4 legacy 部分新会话复验（批准执行）——compute 过，render 路径复现 r167 flake

- **结论**：默认桥上 L4 legacy rung5–8 新会话复验：rung7（compute 建销）**全过 exit 0**；rung5/6/8 在 `RGXCreateRenderContext` 处 standalone SIGSEGV（6 连崩 + 首轮 3 连崩，非运气），GDB 下**全过 exit normally**——r167 翻版（UMD 堆时序 flake，`RGXCreateRenderContextCCB+1525` 取空），桥无罪。rung7 的通过证明建链/PMR/堆表在新会话正常。refs 1/0 不变，L3 全绿，dmesg 干净。**会话未动（默认桥，未重载），freeze 继续。**

## 实测（执行过）

1. dmesg 打 `[r245] l4-legacy-start` 标记；Makefile rung 配方手跑（不用 `make umd` 整套）。
2. rung5/6/8：standalone 139（render create 后）；rung5 追加 6 连试全崩。rung7：全符号 0、exit 0。
3. GDB（用户态只读调试）：同 rung5 链全过、`CreateSyncPrim → 0`、`exited normally`。与 r167（standalone 0/20，GDB 5/5）同构。
4. 事后：refs 不变；node probe 0 failing；dmesg 计数 0。

## 边界

- 未验证 rung5/6/8 的桥语义（UMD 没走到桥）；r212/r213 的 kick 链已覆盖关键语义。
- 未用 `timeout` 包裹；GDB 只读用户态；无代码改动。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
