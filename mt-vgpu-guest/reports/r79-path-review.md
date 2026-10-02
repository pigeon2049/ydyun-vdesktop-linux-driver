# r79：vGPU 实现路径全梳理与方向评估（用户指令，只读复核）

结论先行：**方向正确，进展扎实，但胜利不在延长线上**——输入侧
（UMD  spokes）已近乎吃透，输出侧（DM 格式）还是白纸，活体跑道
（对象存储/sealed 会话）基本耗尽。下一步最优解不是继续啃输入，
而是开输出侧（T3 recon）+ 规划会话更新。

## 实现路径（as-built，五段）

```text
[1 Guest/固件] mt_guest_probe 绑 00:0e.0 → trial pinned/connected (2/2)
      │  connect-only，无 workload 提交（设计如此，非缺失）
[2 桥 ABI] mt_pvr_bridge @renderD128：connect/device/heap/PMR/map/sync/
      render/compute/kicksync 全绿；arena backing；CPU-only plan + cover；
      kick accept-and-inspect + 即时 fence；caps 归零 + features+0x54<2
      故意钉 legacy（bring-up 刻意选择）
[3 UMD 链路] 8 rung 合成流量全 0；check 非零已活体（r73 ufo 1/1）；
      update 需 DDK2（需特性开关，r78 立项待批）；CCB pack 公式已得
[4 RGX 执行] live_3d_drm 手工 DM2 负载：单帧 + 64KiB 像素 + 20 帧批量，
      21 次零 fault（注意：手工负载，非翻译 UMD 负载）
[5 翻译器] T1/T2 输入规约完（check 侧）+ 只读观察落桥；T3（DM 格式）
      + 真实 CCB 内容缺失；翻译器从未翻译过任何东西
```

handoff（§10）：PMR→DMA（r49）、VA→plan（r51/r60/r61）已验证；
kick→DM（T3）待定；顺序 1→2→3 每步独立可验证——前两步已走完。

## 方向三问

**1. 翻译器路线对吗？对。** 替代路线（重写 KMD、host 侧代执行）都比它
更难；accept-and-inspect 把"UMD 跑通"与"GPU 执行"解耦是本仓库最关键
的设计决策（r63），25 个真实缺陷的修复史证明这条路走得通。S4-2（固件执行）
与 S4-3（UMD 经桥执行）的切分干净，证据分级诚实（fence≠像素，r66/r70）。

**2. 最大风险在哪？输出侧空白。** r64–r78 共 15 轮，全部在输入侧
（counts、pack、DDK2 形状）——T3 的 DM 队列格式**零进展**，
"RGX 环待从 mtkm64.sys 反推"（STATUS 原话）至今没动过一锹。
输入再精确也只是更清楚的"不知道输出怎么写"。这是方向上唯一的
结构性偏科，必须纠正：T3 recon（`decompiled/mtkm64.sys/` 语料现成，
§9 流程）应成为下一阶段主线。

**3. 活体跑道还剩多少？基本见底。** 对象存储满（34 对象/2 空间/2 上下文，
新 live_3d 被 `-EBUSY` 拒）、sealed VM 不可卸载、freeze 持续——
特性开关 ladder、首个翻译 kick 执行这两场硬仗，极大概率需要新会话
（重启 + 重建 + 重验证 r68/r69 流程）。这不是"顺手做"，要当一次
**窗口规划**：攒够离线输入（T3 格式 + check-only 翻译设计），一次窗口
内打完，不要为单次实验反复启停。

## 建议顺序（供决策）

1. **T3 recon**（离线，§9）：`mtkm64.sys` 语料中找 RGX kick/DM 环编码，
   先出"DM 包长什么样"的规约，不写代码。
2. **check-only 首帧设计**（离线）：r73 已证明 check=1 kick 可上真机；
   若首个真实绘制的同步需求能落进 check 侧，首帧翻译**不需要 DDK2/
   特性开关**——这是风险最低的首胜路径，先验证这个假设再谈开关。
3. **特性开关单独立项**（待批）：新 DDK 路径（SyncPrim/SubmissionBuf/
   DDK2/CCB）要自己的 ladder；默认在新会话里做，不碰当前 freeze 会话。
4. **会话更新窗口**（规划）：对象存储与 sealed 状态决定了任何
   "再执行一次"类实验都要新会话；窗口前必须输入齐备（1+2），
   窗口内按 §11 清单一次过。

## 本轮边界

纯只读复核：三条线并行勘察（subagent）+ 快照 §3/§8/§9/§10 对照；
零硬件触碰，会话未碰；旧结论未改，只增本评估。
