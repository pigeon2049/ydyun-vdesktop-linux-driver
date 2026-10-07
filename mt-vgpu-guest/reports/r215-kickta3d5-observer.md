# r215：`0x82:0x14` accept-and-log observer 落桥（离线实现，零硬件触碰）

- **结论**：STATUS #2 的 concrete 缺口（r190）已 offline 闭合一半：桥新增 `pvr_cmd_kickta3d5_observe`（r174 的 `0x89:0xa` 模式）：108B 定界 + render-context 鉴权（`pvr_object_find` + `MT_PVR_KIND_CONTEXT`，r189 helper）+ VA→reservation→PMR 三重定界 + 1MiB 上限 + 非零/FNV/64B head 统计 + flags/VA/size/ID/check/update/PMRsync 标量上报 + dmesg 一行；不读嵌套指针、不执行、无 fence。分发接 `case MT_PVR_FN_RGXKICKTA3D5`（wire.h 新宏 `0x14U`，r188 惯例），`0x82` 组其余拒绝语义不变。门禁：新文件 7 项 + `fn_ids` 55→56（含反向掐断验证）；`check-offline` 302 Python + 292 C 全绿；`make kernel` W=1 零警告。**未加载模块、未碰会话**（在载桥仍是旧构建；下次批准窗口重载后方可活体验证）。

## 实测（执行过，零硬件触碰声明）

1. 开工即声明零硬件触碰：全程只改源码 + 跑离线门禁；`lsmod` 开工收工一致（probe ref 1 / bridge 默认 ref 0），无 insmod/rmmod、无 UMD、无 `/dev/dri` 操作。
2. 代码：`mt_pvr_wire.h`（FN 宏 + struct 注释“尚未分发”改判为 observer 口径）与 `mt_pvr_bridge.c`（+82 行：handler + dispatch case）。`git status` 仅 3 改 + 1 新测试文件；`.ko` 产物 gitignore，不入库。
3. 门禁镜像 r174：路由/定界/鉴权/零嵌套读（8 个 GFX 指针字段名禁入）/标量上报/零执行 token（含 `copy_from_user` 与 translator prepare 双禁入）/零填充 OUT 回 0。
4. 反向验证：把 dispatch case 改成复用 `RGXDESTROYRENDERCONTEXT2` 标签 → `test_kickta3d5_routed` FAIL；还原后全绿；`git diff` 确认仅预期行变更。
5. `make check-offline`：302 Python OK（295+7 新）+ C `pvr_bridge_core_test OK (292 checks)`；`make kernel`（含 recovery）grep warning|error 空。

## 边界

- observer 不是执行：check/update 数组内容、同步语义、fence/完成、GPU CCB 一概未碰；真实 TA/3D 执行仍待 DDK2 render backend（r207/r208）。
- 真实 3D producer 仍是 open 问题（r190：GLES 不通、Rogue2D 死代码、blit 走 TDM 不进 TA）；本轮未尝试活体 GFX kick（无现成生产者脚本，翻炒 GDB 手塑链风险收益不成正比，方向已止损记录）。
- requirements.json 生成器产物未动（r190 口径：实现轮同步改）；`fn_ids` 契约变更（55→56 + 派生名注释）即本轮门禁资产。

## 下一步（候选，需批准）

- 活体：批准窗口内 `=2` 重载新构建 → 真实 GFX/TA 流量观察（若 producer 落定）→ 按 r159 语义验证 update 写回。
- 仍缺：DDK2 render backend 接线（per-file VM、execution context、PMR VA 绑定、资源闭包、fence）。
