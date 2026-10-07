# r304：CCB 目的扫描离线实现（零硬件触碰）——官方源定位 + 确定性归属

- **用户问**：要不要自己写？另一款驱动不是类似的吗？**结论**：官方 Linux guest 源码树只有 OS 胶水（bridge 分发骨架/PMR OS/sync OS，`src/pvr` 无 RGX/TDM/TA/fill 执行逻辑——官方 KMD 是往真实固件的转发器）；Windows 包是闭二进制（已通过 RE 持续采矿：堆表 r76、DDK 门 r134、0x2:0xa r221、update 语义 r159、fill 格式 r181）。**转译/执行适配层没有官方源可以抄**——它只存在于“UMD 要画、固件不在环”的 vGPU guest 语境里，正是我们手写的部分。官方源回答不了“哪个池是目的”（UMD 运行时行为），只能活体看。
- **实现**：CCB 窗口扫 4B-destination-block（magic `{0x40000005,0x2da100}+0xb8000000`——`+40` 取值以执行为准纠正过一次 `a8→b8`），VA 高位与 format 天然不交叠（VA 40 位）；`locate` 加 `force_pmr`（0=启发沿用 dry-run，定向 must-resolve 否则 `-ENODATA`）；observe 记 `ccbdst:` 行。扫描函数放共享头，C 自测同源（构建镜像→回环 VA 高位→拒绝零窗，反向另计）。
- **门禁**：C 自测 +2 checks（299 总）；Python `test_pvr_tdm_submit3.py` +2（清单/CCB 定向，15/15）；`0xa8→0xb8` 由 C 失败一次抓获（门禁有效性实证）；反向验证通过；`check-offline` 380+299 全绿；`make kernel` W=1 零警告。未加载。
- **边界**：`+40` 误记一次（执行纠正）；nz early-exit 改全量计数（等像素池的 tie-break 语义更精确，行为变化已评估：像素数不同时无影响）；`locate` 行为变化仅定向路径。
