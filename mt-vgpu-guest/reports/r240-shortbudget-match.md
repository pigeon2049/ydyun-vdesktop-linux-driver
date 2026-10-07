# r240：短预算下匹配腿（批准执行）——预算只限等待，不限命中

- **结论**：`translate_wait_ms=1000` 下预置匹配仍即时通过：`pvr_update_writeback`（双 update 版）混合 fire（check=2 + update=2，值全预置）**36.6ms** 即过，写回 probe 0.10ms 即过；dmesg `check=2 update=2 tag=1 fence=18` → `check=1 update=0 tag=2 fence=19`。预算只收紧等待上限，不影响命中路径（与默认预算的 44.8ms 同构）。probe 持有中（25，soak 同窗口续跑）；拆桥见 r241。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0；dmesg 打 `[r240] shortbudget-match-start` 标记。
2. `rmmod`（ref 0）→ `insmod translate_kick=1 translate_wait_ms=1000`（probe 未碰，默认 major），节点仍 `renderD128`。
3. 活体：9 项全 ok，exit 0（新增 poll 项亦过）。
4. 未恢复（同窗口续跑 soak，见 r241）。

## 边界

- 失配腿由 r239 覆盖（1.008s 超时）；本轮只证明命中腿不受预算影响。
- 未用 `timeout` 包裹；无代码改动。
