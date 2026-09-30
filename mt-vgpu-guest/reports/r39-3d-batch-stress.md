# r39 阶段报告：3D 渲染执行多帧批量提交、性能延迟基准与环形回绕验证

## 1. 概述与核心突破

在 r38 成功打通 3D 渲染执行上下文（11 BOs 显存切片、CSW 调度字与 Linux 原厂 18,160 字节 Universal Queue 包）单次硬件执行的基础上，本阶段（r39）实现了 **3D 渲染执行多帧连续批量提交、零延迟极限压测与硬件 Ring 队列完整回绕验证**。

核心成果包括：
1. **多帧连续渲染调度架构升级**：
   - 升级 [kernel/recovery/mt_live_3d.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_live_3d.c)，支持配置 `count`（帧数，默认 1..1000）、`delay_ms`（帧间延时），在同一个 GPU VM 与 3D 上下文生命周期内连续发射任务；
   - 实现了 Envelope 帧标记动态修改（在 `+0x08` 注入递增帧号 `frame_tag`），证明固件接受动态参数并按序流水线执行；
   - 引入高精度内核纳秒计时器（`ktime_get()`），对每一次任务从提交至 `dma_fence` signaled 进行微秒级延迟采样，统计 `min_latency_us`、`max_latency_us` 和 `avg_latency_us`。
2. **安全退出与完整生命周期自愈**：
   - 移除了旧版的 `__module_get(THIS_MODULE)` 锁止逻辑；
   - 实现了优雅卸载清理函数 `mt_live_3d_cleanup()`：自动销毁执行上下文与执行进程、释放 11 个专用 BO 与 Command BO、销毁 GPU VM 地址空间并释放设备引用；
   - 模块可随时自由装载、测试、卸载，未残留任何未配对资源或野指针。
3. **硬件环形缓冲区（Ring Buffer）回绕与高频压力验证**：
   - 累计向硬件连续提交超过 105 次真实 3D 渲染工作包（1 + 10 + 50 + 20 + 25 帧）；
   - **硬件 Ring 队列完美跨越 64-slot 边界发生自动回绕**（游标从 61 -> 64/0 -> 17 -> 42），S3000 硬件固件与 Guest 驱动严格保持游标同步（Head=Tail）；
   - 50 帧零延迟极限压测中，总耗时仅 4.568 毫秒，平均每帧延迟仅 **58 ~ 60 微秒**（最低 48 微秒），等效吞吐量达到每秒万帧级！

---

## 2. 真实硬件性能基准测试数据

基于 [scripts/verify-3d-execution.py](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/scripts/verify-3d-execution.py) 在 MTT S3000 物理设备（PCI `0000:00:0e.0`）上的实测结果：

| 压测轮次 | 提交帧数 | 帧间延迟 | 成功率 | 硬件槽位推进 | 最低延迟 | 平均延迟 | 最高延迟 | 验证特征 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **第一轮 (初探)** | 1 帧 | - | 100% (1/1) | 0 -> 1 | 106 us | 106 us | 106 us | 单次最小工作负载打通 |
| **第二轮 (模拟帧率)** | 10 帧 | 5 ms | 100% (10/10) | 1 -> 11 | 91 us | 660 us | 5,290 us | 模拟 100~200 FPS 间隔调度 |
| **第三轮 (全速极限)** | 50 帧 | 0 ms | 100% (50/50) | 11 -> 61 | 48 us | 60 us | 167 us | 极限吞吐，总耗时 4.56 ms |
| **第四轮 (回绕验证)** | 20 帧 | 1 ms | 100% (20/20) | 61 -> 17 (模 64) | 63 us | 217 us | 2,668 us | **成功发生 Ring 回绕** (61+20=81%64=17) |
| **第五轮 (回绕后连续)**| 25 帧 | 0 ms | 100% (25/25) | 17 -> 42 | 48 us | 58 us | 126 us | 回绕后全速消费，均延 58 us |

**总体统计**：
- 累计提交帧数：**106 帧**；
- 累计失败/超时数：**0 帧 (100% 成功率)**；
- 极值执行延迟：**48 微秒**；
- 典型平均延迟：**58 ~ 60 微秒**。

---

## 3. 产物清单

- [kernel/recovery/mt_live_3d.c](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/kernel/recovery/mt_live_3d.c) (多帧连续提交与性能采样压测驱动)
- [scripts/verify-3d-execution.py](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/scripts/verify-3d-execution.py) (自动化多帧压测与回绕基准套件)
- [reports/r39-3d-batch-validation.json](file:///opt/ydyun-vdesktop-linux-driver/mt-vgpu-guest/reports/r39-3d-batch-validation.json) (最新 25 帧基准机器报告)
