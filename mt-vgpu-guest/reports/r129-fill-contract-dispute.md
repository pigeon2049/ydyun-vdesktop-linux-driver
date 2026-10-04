# r129：fill smoke 红——out_syncobj=0 的契约分歧（旧测试 vs 现驱动）

本轮目标（新会话首个绘制像素）被前置门卡住：
`mt-fill-check smoke` 倒在非法用例第 4 项，未进入像素断言。
结论：这是**旧测试与现驱动的契约分歧**，不是回归，也不是硬件拒绝；
绘制像素仍待下一轮（先离线裁决契约，再跑像素）。

## 实测

- `insmod mt_live_3d_drm.ko` → `card3`/`renderD130`（minors 递增）。
- `sudo ./build/userspace/mt-fill-check /dev/dri/renderD130 smoke`
  （argv[1] 接节点，无 symlink hack）：
  `check failed line 49 ... errno=0`——某非法用例被**接受执行**，
  而非按预期 errno 拒绝。
- 定位（读码 + 计数器旁证，无需额外提交）：
  `bad[3] = {out_syncobj, 0, EINVAL}` 是唯一能成功的项——
  驱动 `fill_ioctl` 以 `if (r->out_syncobj)` 跳过查找（0 = 不用 syncobj），
  其余 13 项的拒绝路径与代码逐项吻合（flags/尺寸/越界/坏句柄均有对应分支）。
  QUERY 旁证：`submitted/completed = 1/1, faulted=0, last_seq=1`——
  该 16×16 fill 真执行了（颜色 0，即 base 零值），会话无 fault。
- 二进制与源码同为 09-30 构建，不存在"旧二进制测新驱动"的过期问题；
  smoke 是否曾全绿：未知（不 claim 回归）。

## 契约分歧（需离线裁决，未动代码）

- 驱动侧：`fill_ioctl` 把 0-syncobj 当合法"不用"（与 submit 路径一致；
  uapi 头 `out_syncobj` 注释即 `/* Optional syncobj handle (0 if unused) */`，
  且是同一头文件对 fill 结构体的注释）。
- 测试侧：`invalid_cases` 要求 0-syncobj 返回 EINVAL。
- 倾向（推断）：测试过期；但改测试=放宽门禁，需评审后单独立项，
  本轮不动（一次只做一件事）。

## 卸载与健康（实测）

- `rmmod`："unloaded cleanly"，新增 2 条同签名 WARN（674/679，
  累计 6 条，无新模式）。
- probe 引用 61 → **87**（又是 +26；r127 的 +34 仍是孤例）。
- 桥探针 0 failing / 0 mismatch；bridge 引用 0；无 D/Z 任务。freeze 继续。

## 未做

- 绘制像素断言（被本轮卡点阻塞，契约裁决后重跑 smoke 即得）。
- push：66 提交未 push（用户此前明确暂不 push）。
