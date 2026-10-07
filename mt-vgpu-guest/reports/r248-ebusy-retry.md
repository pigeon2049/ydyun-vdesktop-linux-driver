# r248：EBUSY 后重试验证（批准执行）——拒绝无副作用，可恢复

- **结论**：translator 并发拒绝的可恢复性在活体证实：ck 桥下循环并发，第 2 轮复现一胜一败（败者混合 fire `FAIL errno=16` + probe `FAIL errno=110`，与 r247 同形）；胜者退出后，败者链**单独重跑全过**（混合 0.18ms 即过，translator 常驻；probe 0.10ms 即过；fence=57/58 延续）。EBUSY 拒绝不污染 translator 状态、不留持有残留。拆桥见 r249（同窗口续跑后统一恢复）。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0；dmesg 打 `[r248] ebusy-retry-start` 标记。
2. `rmmod`（ref 0）→ `insmod translate_kick=1`（probe 未碰，默认 major）。
3. 首轮并发双双通过（竞争概率性，未撞上）；循环至第 2 轮撞上（C2 PASS / D2 FAIL）。
4. 重试：D 链单独重跑，9 项全 ok，exit 0。
5. 未恢复（同窗口续跑 R_U 前保持；见 r249 恢复节）。

## 边界

- 只证明拒绝后可恢复；并发冲突本体由 r247 覆盖。
- 未用 `timeout` 包裹；无代码改动。
