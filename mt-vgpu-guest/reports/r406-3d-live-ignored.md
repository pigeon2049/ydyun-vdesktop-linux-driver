# r406：3D 包活体提交——固件无响应（忽略签名）

> 轮次：r406（2026-10-09）。**活体单发，中风险。**
> 背景：r405 路线图第一步；r381 确定 3D opcode=0x68；r382 基础设施（门控关闭）；r380 教训：DM2 需完整包。

## 结论

**3D 包可提交至 DM2，但固件 5 秒内无完成事件（忽略签名）。**

- 包构建成功：opcode `0x68` @+0x0c，command_va @+0x28，size @+0x30，wire_id @+0x48，pid @+0x4c（r382 布局）
- 提交路径通：`mt_marker_submit_engine_work()` 返回 0（非 `-EINVAL`/`-EHOSTDOWN`），fence 已分配
- 固件无响应：`dma_fence_wait_timeout(5000ms)` 返回 0（超时），完成码未设置
- 签名：**"submitted-but-ignored"** —— 队列接受，固件不回完成事件

**含义**：3D 基础设施（包格式/提交路径/fence 机制）工作正常；固件需要真实 3D 负载，非 marker 包。与 r380（DM2 忽略空 marker）、r405 G1（真实 payload 缺口）一致。

## 测试方法

- Pre-live 门禁：T1/T2/T3 全过；`(2,0x68)` 白名单确认
- 桥侧测试钩子（未提交）：`mt_bridge_3d_test_submit()` 直接调用 `mt_marker_submit_engine_work()`，绕过 `MT_3D_SUBMIT_GATE`（门控保持 0）
- 测试模块 `mt_3d_test.ko`：`va=0x70001000`（dummy），`size=256`，单发即卸
- 一次 live，无重试；`timeout` 未进临界区（fence 等待在 ioctl 外）

## 活体证据

```
[24588.700255] mt_pvr_bridge: r406: 3D test submit va=0x70001000 size=256
[24588.700307] mt_pvr_bridge: r406: 3D test submitted, waiting for fence
[24593.718682] mt_pvr_bridge: r406: 3D fence wait failed waited=0
[24593.718691] mt_3d_test: r406: result ret=-110 completion_code=3735928559
```

- `ret=-110` = `-ETIMEDOUT`
- `completion_code=0xdeadbeef`（未设置，初始哨兵值）

## 状态与清理

- 测试桥（含钩子）仍在载（ref=1，pending 3D fence 持有引用，无法卸载）
- 源码已恢复至 r404（测试钩子未提交）
- 系统稳定：无 oops/WARN/hang；probe ref=13 基线不变
- 待用户冷重启清除 pending fence（类 r389 泄漏）

## 门禁

- `make -C mt-vgpu-guest check-offline`：待跑（本轮有活体，需确认）
- `make kernel` W=1：零警告（测试构建）

## 诚实边界

- 单次活体，无重试；dummy VA 未映射真实命令缓冲
- 未验证固件 FAULT 行为（无 fault 发生，仅超时）
- 3D 完成码 0 未实测（fence 未 signal）
- 测试钩子为一次性代码，已从源码移除

## 下一步（r407）

r405 路线图：真实 TA payload 捕获（UMD TA kick IN + 命令缓冲内容，r359 observer 配方）。
