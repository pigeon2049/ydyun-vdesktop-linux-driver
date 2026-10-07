# r243：短预算 UMD 全链匹配（批准执行）——`0x2:0xa` 预置 + 翻译 0.046s 即过

- **结论**：`translate_kick=1` + `translate_wait_ms=1000`（默认 major）下 UMD 全链非零匹配 0.046s 即过：`SetSyncPrim → 0`（seq 129 `0x2:0xa ret 0`），`RGXKickSync → 0`（seq 131 `0x88:0x4 ret 0`）；dmesg `syncprimset: sync=0x102d off=0 val=1` → `translated kick: check=1 update=0 tag=1 fence=40`（fence 序列延续）。短预算不影响 UMD 侧预置+翻译命中路径（与默认预算 r222 Leg1 的 0.045s 同构）。拆桥 `unloaded cleanly`（probe 25→1）；桥恢复默认 + L3 全绿，dmesg 干净。**Freeze 已恢复。**

## 实测（执行过）

1. `rmmod`（ref 0）→ `insmod translate_kick=1 translate_wait_ms=1000`（probe 未碰；dmesg 打 `[r243] shortbudget-umdmatch-start` 标记），节点仍 `renderD128`。
2. 活体：r222 Leg1 同链（`SetSyncPrim u1` + kick 值1），全 0、exit 0、0.046s。证据：`reports/r243-umdmatch.jsonl`（131 行）。
3. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node 0 failing/0 mismatch + smoke PASS；终态 1/0；dmesg 计数 0。

## 边界

- 本轮 fresh translator（新桥实例，tag 从 1 起）；未用 `timeout` 包裹；无代码改动、无需门禁重跑。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
