# r61：cover-page 绑定落桥与独占策略实测

r55/r60 的后续：plan 绑定改成 cover 集（arena 已在位）。arena-backed
range（对齐与否）按页绑定 file-arena 外观；fallback 保持对齐单体绑定；
未对齐 fallback 仍降级。线格式零变化。

## 独占策略的确定性实测（`probe/pvr_cover_probe`）

两个 byte-tight PMR（`[BASE,BASE+0x253)` + `[BASE+0x253,+0x408f)`，与 r54
ladder 同形）共享 VA 页 BASE：13 项检查全过——两次 MapPMR 都返回 mapping，
`dmesg` 恰好只有一条 plan 行（先映射的 A，`pages=1`），B 无声降级；
unmap/unreserve/unref 全 orderally，无 WARN。共享页永不双主人：
先占有效，后到在 `bind_many` 内以 `-EEXIST` 整批拒绝。

## 全链路（构建 `b5b20381`，loaded == 在盘）

217 项 Python、runtime integration、`W=1` 零警告；热换后 smoke、探针、
UMD 八级阶梯全部通过；ladder 的 plan 行显示未对齐 range 已入 plan
（如 `va=0xe000010000 bytes=174079 pages=43`）；各文件
`fallbacks=0`，最高占用 99/512 页；零 WARN/BUG/Oops。
终态 `Guest/FW 2/2 pinned`、`pending=0`；主模块引用 1，bridge 引用 0。

## 边界（诚实记录）

cover 页取“首个 PMR 有效字节所在 arena 页”——对齐 range 精确 1:1；
未对齐 prefix/tail 是近似（整页映射，邻居字节同页可见，这正是独占策略
要拦的）。plan 仍不上载不执行；translator 用到时需重审该近似。
回滚点：`/tmp/opencode/bridge-rollback-r60/`（`e812d938`）。
