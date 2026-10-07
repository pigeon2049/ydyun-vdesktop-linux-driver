# r290：UMD 驱动 fire 首绿（批准执行）——真实 blit 矩形 21 块全像素验过

- **结论**：停桌面窗口（`systemctl stop`，renderD128 释放）+ 三开重载（`=2` + `tqx_ctx` + `fire`，r290 串行移植构建）+ 真实 DDK2 blit：桥 observe 真实 `0x89:0xa`（VA/尺寸/res/PMR 与历史全同）→ translator bring-up（`tqx-ctx: ready`）→ fire 以 **UMD 矩形**（1280×1024/`0xff0000ff`，与 r226 干跑预言跨会话一致）调度 → `fired=1 chunks=21 verified=1 bad=0/1310720`。**STATUS #1 的真实绘制执行链首次打通。**恢复：拆三开干净（probe 31→1 对称）+ 默认桥 + L3 全绿 + 桌面拉回；窗口零新增 WARN。**Freeze 已恢复。**

## 实测（执行过）

1. 批准：用户原话“写个脚本实测 render128 退出 opencode 一分钟收集日志再打开”。离线先修桥 fire 串行化（handler 只定位+调度，work 逐块 prepare→submit→等→验；single-flight 改 `fire_running`；teardown 先 `fire_abort` 再 cancel，卸载延时以单块为界）：门禁改判（13 项）+ 反向验证 + `check-offline` 366+292 全绿 + `make kernel` W=1 零警告。新 `.ko`（vermagic 对版、含 `scheduled chunks`）上机。
2. 窗口脚本 `scripts/fire-window.sh`（trap 全程兜底 + systemd scope 逃生）：停桌面 → ref 0（fuser 空）→ `rmmod` → 三开 `insmod`（节点仍 renderD128）→ 真实 blit（`timeout -s KILL 90`，137 即 r279 式 hanging，不代表失败）→ 等 fired 行（150s 期限）→ 拆桥 → 默认桥 → L3 → 拉桌面。
3. 活体结果（dmesg 直读）：observe 行 `va=0x8000f44000 bytes=4608 res=0x1035 pmr=0x1034` 全同历史，`nonzero=40`（历史 6 样本皆 39，+1 字节未定位，见边界）；`tqx-ctx: ready`（无 `-22` 回归）；`fire seq=1: scheduled chunks=21 1280x1024`；1 秒后 `fired=1 chunks=21 verified=1 bad=0/1310720`。trace 8201 行（blit 部分，tid 过滤见下）+ submit 仍落 seq 8201（跨 4 样本确定性延续）。
4. 恢复event：fire 等待循环因脚本 grep 无 `-a`（dmesg 含二进制）误判 TIMEOUT；随后桌面在窗口内自重启（01:33:48，stop 后约 2 分钟）重占 renderD128 → 三开 `rmmod` 被拒 → 默认回装失败（trap 按设计停手保现场）。手动补恢复：二次停桌面 → 10 秒内 `rmmod`（过自重启窗口）→ 默认桥 → L3（node 0 failing/0 mismatch + smoke PASS）→ 拉桌面（10 进程，renderD128 重持为正常态）。终态 refs 1/1。
5. 证据：`r290-window-blit.jsonl`（0600，blit=tid 75835 的 8201 行；另含 741 行脚本自污染见边界）+ `r290-window.dmesg`（0600）；窗口操作日志留盘暂存区（`build/traces/r290/window.log`，gitignore）。暂存区其余已清空。

## 边界与教训（三个，全如实记）

- 脚本两 bug 已修（未重跑窗口，`bash -n` 过）：① grep dmesg 缺 `-a`/`LC_ALL=C` 致 fired 行漏检；② blit 后未 unset shim 环境变量，150 轮等待循环的子进程（sudo/dmesg/grep/tail）继承 `LD_PRELOAD` 向同一 trace 追加 741 条自记录（`/proc/self/maps`、`refcnt` 等）。blit 证据本身干净（tid 过滤可分）。
- 桌面 stop 后约 2 分钟必自重启（两次实锤：kill 后 65770、stop 后 76124）：**停桌面窗口的可靠操作上限约 90 秒**，长等待（blit 90s + fired 150s）必然撞上。以后窗口设计必须“停→快干→（若需长等）先拉回桌面再说”，或找到并关闭自重启源（未查，留待）。
- CCB `nonzero=40`（r279 同会话 75 分钟前为 39）：head 流对位显示轮值字节取第 8 个相异值（`3b 28`）且疑似多 1 非零字节；全窗转储缺失，位置未命名。fire 用 pool 矩形不受影响（矩形/颜色/尺寸全同）；记为待命名，不展开。

## 下一步（候选）

1. 下一 holder 窗口（90 秒设计）：真实绘制已通，余量是 TA/3D CCB（`0x82:0xC`/`0x81:0x5`，STATUS #3）与 update 写回的 UMD 驱动验证。
