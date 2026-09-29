# Guest 重启后的固件连接试验（2026-09-29）

用户重启 Guest 后，原版 `mtgpu` 自动绑定 S3000，但仍在 `PhysHeapsInit` 的
`[644]` 堆数检查失败，未创建 MTT DRM 节点。解绑前只读采样到 BAR0 Guest 状态
`0`、固件状态 `1`。原驱动通过 PCI driver core 正常解绑成功；随后
`scripts/fresh-trial.py` 的设备、内核、模块、固件及 Guest0/FW1 预检全部通过。

对候选 `kernel/mt_guest_probe.ko`（SHA-256
`04bf739dc21a0cd13a1f0620929cda5a6ddc5436a929a4d39f9e42de9ec5c3df`）执行了一次有界
固件试验。共享通道注册 15 项，RPC 返回成功；本次信息页是 version 2、OSID 6，BAR2
显存保留和 CPU 侧启动资源构造均成功。固件连接函数返回 0，按其已核验的条件，这表示
轮询期间 FW 状态达到 2 且 `started` 非零；Guest 状态也到达 2。这是首次在这次
Guest 启动里观测到连接确认。

脚本接着按 `runtime_context=0` 流程请求断开。断开未获确认，返回 `-108`
（`ESHUTDOWN`），之后快照为 Guest=2、FW=1、started=0；连接模块已自持引用
（`pinned=1`），并保留发布过的 BAR2 固件/启动资源。当前 RPC 仍运行，IRQ 确认及 RPC
回复计数继续增加、拒绝数为 0。内核没有 Oops；当前只有 QXL `card0`，没有 MTT DRM
节点，`render_ready=0`。因此连接成功是固件协议层进展，不等同于 GPU 渲染或加速可用。

快照及完整内核日志位于
`build/fresh-trials/20260929T144220Z-eb28ef82/`。由于试验模块保留了可能仍被设备引用
的页，不能在当前状态解绑或卸载；仓库已将 `fresh-trial.py` 扩展为支持
`--runtime-context`，以便下一次干净启动直接测试“连接确认后发布运行上下文”的路径。
当前活动的 pinned 会话使预检拒绝任何再次加载；下一轮需先重新启动 Guest，然后执行：

```sh
python3 scripts/fresh-trial.py --runtime-context --run
```

此模式即使成功发布上下文也不会创建 DRM 节点或宣称硬件加速，后续仍需接通并验证
DRM/GEM 用户接口与实际 GPU 工作。
