# 运行上下文接入主模块

2026-09-28。按用户要求先继续适配，未尝试恢复当前停滞会话。

## 已接入的行为

此前 `mt_trial_run` 即使连接成功也马上断开、恢复显存。现在抽出 `mt_trial_start`，
允许保留已成功连接并持有模块引用的会话；原试验仍使用连接、断开、恢复的完整流程。
重复调用试验不会意外断开已有会话。

主模块增加 `runtime_context` 参数，默认 0：

- 配合 `query_info=1`、`reserve_memory=1`、`prepare_resources=1`、`load_firmware=1`
  时，准备持久上下文和内存窗口；没有 `trial_connect` 则不上传或发布它们。
- 另加 `trial_connect=1` 时，成功连接后检查 Guest2/FW2、started、健康值及资源持有，
  才通过 BAR1+f0 发布描述符 GPA，并按 info flags bit0 写 BAR1+70=1。
- 发布前失败继续走试验的断开/恢复流程；未确认的固件请求仍持有旧资源。
- 发布后保留描述符、窗口、信息页及全部固件资源。尚未确认独立的上下文撤销协议，
  此时 `trial_control` 返回 EOPNOTSUPP，不能以固件断开代替上下文撤销。

描述符和窗口各占独立清零的 4 KiB 页；80 字节描述符和已还原的 280 字节窗口由已有
构造函数生成。root 是 GPU PA，固件 GPA 由 BAR2 加实际分配偏移得到，系统页使用
`virt_to_phys` 的 Guest GPA，固件 VA 来自同一 heap plan。发布前后执行内存屏障。
当前 S3000 Guest 使用初始 board-cap 空指针对应的 device_config=0，platform+18=0、
platform+9d8=0x8000000000、platform+9e0=0；系统内存量取本机 `totalram_pages()`。
这是当前固定配置实现，不是通用多型号平台识别。

新增只读 `runtime`、管理员只读 `runtime_context_raw` 和 `runtime_windows_raw`。
`published/notified` 仅表示已发通知，`render_ready` 仍为 0，不作为宿主接受证明。
持续 RPC worker 在运行模式下同时收集启动事件；沿用无任务阶段的诊断策略，最多
保留 128 条。容量满或游标非法后停止消费，记录 event_result，不静默丢弃事件。
引入任务提交前必须换为按上下文分发与 fence 完成处理。

## 验证

```sh
cd /opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest
python3 scripts/verify-runtime-integration.py
python3 scripts/verify-fw-context.py
python3 scripts/verify-window-initialization.py
```

运行集成检查通过：

- 31 种不满足发布前提的组合不产生通知；成功顺序为 +f0、条件成立再 +70；重复拒绝。
- 构造失败不修改两份输出或所有权；发布标记在首次对外暴露地址前设置。
- 实际试验 C 代码在 RAM 模型中覆盖持久连接、普通试验退出、连接超时资源持有、
  后续断开及六段数据逐字节恢复；覆盖事件容量不足和损坏游标不继续消费。
- UBSan、Werror 测试及 W=1 内核构建通过；`struct mt_guest` 与在用模块备份布局一致。
- 既有上下文 33 个新建、33 个缓存及 18 个拒绝案例通过；窗口初始化验证通过。

构建散列和运行输出见 [runtime-integration-build.json](runtime-integration-build.json)。
`fresh-trial.py` 仍明确指定 runtime_context=0，验证使用本轮构建记录，不会自动选择
持续运行模式。本轮没有加载新模块，也没有修改开机启动项。

## 尚未完成的运行功能

目前只接通了持久会话和上下文发布基础，没有 DRM/GEM ioctl、任务调度、fence、
用户态渲染栈。当前 Host 对窗口的实际读取长度、通知接受情况和运行时撤销尚未实测；
不能用 4 KiB 后备页推断 Host ABI 已完整确认。这些限制不阻止继续实现内存对象、
GPU 页表与任务数据路径，但它们各自仍需验证，不能把编译或模拟通过称为硬件可用。
