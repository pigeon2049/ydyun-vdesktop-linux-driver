# r149：DDK2 check-only kick 经真实 DM2 空 marker 完成

- **结论**：在 `drm_major=2, translate_kick=1` 下，UMD 同链命中 DDK2 render create（`0x82:0x12`）、kick-sync context create（`0x88:0x5`）及同步 kick（`0x88:0x4`），均返回 0；check-only kick 经真实 DM2 空 marker 完成并返回 fence。桥卸载后 probe 引用回到 1。

## 实测

1. 以 r145/r146 的完整链参数（CCB handle 使用 `b5*`）执行厂商 UMD。轨迹：`0x82:0x12`（12/12B）、`0x88:0x5`（8/12B）、`0x88:0x4`（84/8B），均 `ret=0`；其余六符号亦全 0。
2. dmesg：`DBG ck off=16 val=0 ufo=4141`，随后 `translated kick: check=1 update=0 tag=1 fence=2`。这是 check-only 空 marker，不含真实绘制 CCB。
3. 首个 harness 变体漏掉 `b5*` 间接，UMD 在 CCB create 后段错误，轨迹未到 `0x88:0x4`；修正参数后同链通过。无内核异常或 bridge WARNING。
4. 卸载后 `mt_pvr_bridge` 报 `unloaded cleanly`，probe ref `25 → 1`，render node 移除；`mt_guest_probe` retained 保持加载。

## 边界与下一步

- 证明 DDK2 选择下 check-only 同步 kick仍可走 `0x88:0x4` 翻译路径；没有验证 `0x82:0xC` TA 或 `0x81:0x5` CDM 真提交，也没有真实非零 CCB。
- 当前源码含 update 数组映射/完成后写回逻辑，但本轮没有活体验证；不要把它等同于已验证支持。先从厂商 UMD 输入和同步语义确定这些数组的契约，再做专门实验。
- 易失 trace：`/tmp/opencode/umda/r149t2.jsonl`；本轮 session 为 r148 重建的 retained trial。
