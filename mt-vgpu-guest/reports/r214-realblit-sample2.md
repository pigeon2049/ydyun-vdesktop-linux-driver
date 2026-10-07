# r214：真实绘制第二样本落定（批准执行）——新会话真实 blit，39B 跨会话比对 37 一致

- **结论**：r211 新会话上第二真实绘制样本落定：桥以 `drm_major=2` 重载（observe handler 在载，`translate_*` 全关），真实 `musa_blit_test -device 0 -f -o`（shim 仅 passthrough 记录 `0x89:0xa`，零 fabrication）发出单次 `0x89:0xa`（108/4，trace seq 8201/8201 行）；桥 observe 行 `va=0x8000f44000 bytes=4608 res=0x1035 pmr=0x1034 nonzero=39 first=0x10`——VA/尺寸/res/PMR/非零数/首偏移与 r174 两轮**全同**；39 非零字节中 37 与 r174 两轮逐字节一致，仅 head[8:9]（窗内 `+0x40`）取第三个相异值 `33 57`（r174 见 `5c 80`/`cd 7c`）。UMD 随后用户态 SIGABRT（exit 134，r172 同例：无真实 GPU 工作后的自身后提交路径），内核侧干净。拆桥 `unloaded cleanly`（probe ref 1），桥恢复默认 + L3 全绿，dmesg 零 WARNING/BUG/Oops。**Freeze 已恢复。**

## 实测（执行过）

1. 开工预检：`/tmp` 2%；UMD SHA `b3058c02…`；`musa_blit_test` 用 rootfs 库路径补齐（`ldd` 零缺失，`-h` 见 `-f`/`-n`/`-o`/`-device`）；blit 自枚举 `/dev/dri/renderD128`（r172 trace 同形）；dmesg 打 `[r214] realblit-sample2-start` 标记。
2. `rmmod`（ref 0）→ `insmod drm_major=2` rc=0，节点仍 `renderD128`；probe 未碰。
3. 真实 blit：8201 行 trace（与 r174 次轮**同行数**，单 submit 落 seq 8201——跨会话流程确定性），observe 行如上；FNV `0x7013f7f8f041dbdb`（轮变，预期内）。证据：`reports/r214-realblit-sample2.jsonl`（已入库）。
4. 字节比对（执行，非推断）：39 head 字节对 r174A/B 逐项比较，仅位置 8/9 相异（`python3` 实测，见上）。`+0x40` 第三值终结任何单调计数器残余解释；r161 命名收回（r174）在新会话复核成立。
5. 源码核对（执行）：`pvr_cmd_tdm_submit3_observe`（`mt_pvr_bridge.c:3111`）在双开关关闭时走 `return pvr_out(cmd, &out, sizeof(out))`——回 0、零填充 OUT、不读嵌套指针、不执行、无 fence；UMD 的 SIGABRT 与桥无关。
6. 恢复：`rmmod` → `unloaded cleanly`；`insmod` 默认桥 → node probe 0 failing/0 mismatch + dma smoke PASS；终态 ref 1/0；dmesg `WARNING|BUG|Oops` 计数 0。

## 边界

- accept-and-log 不是执行：CCB 有效性、update 数组、fence/完成语义一概未碰；T3 翻译仍缺 DM 格式。本轮只证明真实 UMD 在新会话生成**结构相同**的绘制输入。
- 本轮一次只载一个 live 配置，做完即卸并恢复默认；未用 `timeout` 包裹 blit（UMD 134 系其自身退出码，进程已终结，无需外部杀）。
- 无代码改动、无需门禁重跑；`make kernel` 状态沿用 r211。

## 下一步（候选，需批准）

- 真实绘制执行仍待 DDK2 render backend 接线（r207/r208 边界）；不得用 accept-and-log 代替执行。
- 同步 update 语义活体验证（STATUS #2）：r159 离线结论 + r203 fabricated `flag=2`，待真实 `0x82:0x14` handler。
