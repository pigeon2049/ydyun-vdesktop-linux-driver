# r236：fence fd poll 验证（批准执行）——translator 交出的 fence 已 signaled

- **结论**：translator fence 语义在活体证实：`pvr_update_writeback` 在混合 fire 回 0 后立即 `poll(fence_fd, 5000)`——**即时就绪**（`POLLIN|POLLHUP`），证明桥在 `wait_fence` 成功后才交 fd（源码顺序与活体一致）。dmesg `check=2 update=2 tag=1 fence=16` → `check=1 update=0 tag=2 fence=17`（fence 序列延续）。refs 不变，dmesg 干净。**会话未动（`=2`+translate_kick 在载未卸，R_H 并发在默认桥），freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0；`/tmp` 2%；dmesg 打 `[r236] fence-poll-start` 标记。注意：本轮活体跑在 `=2`+translate_kick 桥上（上一窗口遗留配置，translator 路径与 major 正交已由 r232 证实）。
2. 工具（离线部分）：`#include <sys/poll.h>` + fire 后 poll 断言；门禁 +1；`-Werror` 零警告构建。
3. 反向验证走弯路一次（改弱条件门禁抓不住——门禁只查标签存在；如实记录），随后删 poll 调用 → FAIL，还原 → OK，重编。
4. 活体：9 项全 ok，exit 0（新增 poll 项）。
5. `check-offline`：326 Python（325+1 新）OK；C/内核沿用 r222（本轮零内核改动，未重跑）。

## 边界

- 只证明 fence 交出时已 signaled；fence 等待路径本身（wait_fence 超时分支）未触发也未验证。
- 本轮未重载（复用在载桥）；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- TQX 真发射立项（离线先行）；CCB 解读（离线）；真实执行 backend（离线大工程）；DDK2 param_1 recon（离线）。
