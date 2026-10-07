# r218：`0x82:0x14` observer 全路径活体验证（批准执行）——合法 envelope 进观察，阵列指针全 NULL

- **结论**：observer 全路径（鉴权之后）首次在活体走通：`pvr_observe_ping` 扩展为 11 步——ping（`-ENOENT`）→ control（`-ENOTTY`）→ render create（`0x82:0x8` 真句柄）→ PMR alloc/reserve/map（dma_smoke 同款）→ `0x82:0x14` 全 fire（VA=`0x5000000000`/64B/ID=1/check=1/update=1/pmrsync=0，阵列指针**全 NULL**，门禁级保证永不解引用）→ 逐项 teardown；11 项全 ok，exit 0。桥 dmesg 行 `kickta3d5 observe: flags=0x0 va=0x5000000000 bytes=64 id=1 check=1 update=1 pmrsync=0 res=0x1002 pmr=0x1001 nonzero=0 first=0x0`——标量全上报、新零页窗口零统计。refs 1/0 不变，dmesg 零 WARNING/BUG/Oops。**无重载，freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0（在载即 r216 构建）；`/tmp` 2%；dmesg 打 `[r218] observe-fullfire-start` 标记。
2. 工具（离线部分）：envelope 构造照抄 `pvr_dma_smoke`（alloc/reserve/map/cleanup 逐项）+ `pvr_cmd_handle_only` 语义（`0x82:0x8` IN 忽略，`0x82:0x9` 以 `devmem_heap` 销号）；`binding->va = res->arg0` 源码核对（提交 VA 取 reservation 基址）；`-Werror` 零警告构建。
3. 门禁：`test_pvr_observe_ping` 5→8 项（envelope 四调用/回 0 期望/teardown 四调用）；反向验证先用错类名+弱正则走空一轮（诚实记录：该次无效），随后精确删除 `!gfx_out.error` → FAIL，还原 → OK，工具重编。
4. 活体：11/11 ok。事后 `lsmod` 1/0，`kickta3d5|WARNING|BUG|Oops` 仅 observe 行命中。
5. `check-offline` Python 310（307+3 新）OK；C/内核沿用 r215（本轮零改动，未重跑）。

## 边界

- 阵列内容仍是 NULL 不是真实同步块：证明的是分发→鉴权→定界→统计→上报→回 0 全链，不证明 check/update 数组语义；`flag&2`/写回仍待真实 producer。
- 本轮是 fresh file 自包含 envelope + 全 teardown；风险类同 L3；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- 真实 check/update 数组的 observer 流量（需 GFX producer 或最小合法同步块构造，离线先行）。
- 真实 3D producer recon（离线）；真实绘制执行（待 backend 接线）。
