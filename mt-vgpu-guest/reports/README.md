# reports 证据索引（2026-10-03）

本目录是逐轮实测证据链，文件名即编号，**不要改名**（正文与门禁引用文件名）。
找证据时先查本索引，不要逐个打开。非 r 编号的是 09-28 前后的专题记录，
只在追溯对应子系统时读。

当前主线结论见仓库根 `STATUS.md` 与 `PROGRESS-SNAPSHOT.md`。

## 主线（r23 → r71：TQX 复制 → 3D 执行 → UMD 桥 → S4-3 交接 → RGX 执行）

| 报告 | 主题 |
|---|---|
| r23–r29 | 固件连接、fence、TQX 真机复制打通与连续复制 |
| r30–r32 | 用户态复制链路、DRM/GEM/syncobj、原生矩形填充 |
| r33–r35 | Linux UMD 提交格式、1080p 大表面、Windows 图形包 C 重建 |
| r36–r38 | 渲染上下文/CSW 闭合、DM2 连通、最小 3D 工作负载 |
| r39–r41 | 106 帧批量+环回绕、用户态 DRM-3D 接口、render-target 读回 |
| r42、r44 | VA 映射上限改页表几何推导（15872）、重载后真机验证 |
| r45–r49 | PVR DMA 真机：smoke、首绑 mask 核验、handoff 验证、TQX 回读、GPU-PA 窗口转换 |
| r50–r52 | VM plan 软件侧 → 真机验证 → 新桥八级阶梯全绿 |
| r53–r54 | `0x88:0x4` 包解剖（只有同步记账）、一次 kick 的 12 PMR 全清单 |
| r55 | byte-tight 答案：round-down 会 double-map，arena + per-page 绑定 |
| r56–r57 | CCB 归属（server 侧持有；非零 CCB 待绘制路径 kick）、flags 解码 |
| r58–r59 | 全轮复核（抓到 `dma_source_release` 真 bug）、重启后验证链重放 |
| r60–r61 | file-arena 落桥、cover-page 绑定 + 独占策略实测 |
| r62–r63 | 签发包装解码（T1/T2 算法输入）、kick 只读观察落桥 |
| r64–r66 | 通道健康重验、单帧实验设计、**首次 RGX 真实执行** |
| r67–r69 | device-mutex 泄漏事件（只能重启恢复）+ 两次重启恢复 |
| r70–r71 | RGX 像素验证（64 KiB 读回）、20 帧批量像素闭环 |

## 关键单篇（本轮最常用）

- 翻译器输入规约：r53（包）→ r54（PMR/VA）→ r55（对齐策略）→ r56（CCB 归属）→ r62（签发路径）。
- 运维红线：r67（泄漏）→ r68（恢复流程）。
- 门禁与复核：r58。构建 canvases：`runtime-integration-build.json`。

## 非 r 编号专题（按子系统追溯时读）

`boot-bo-lifetime`、`buffer-object-layer`、`ce-*`、`context-pool*`、
`event-fence-path`、`execution-context-path`、`firmware-*`、`gem-*`、
`gpu-vm-mapping`、`tqx-*`、`work-*` 等。完整清单见目录；
各 `*-validation.json` 为机器可读验证结果，与同名 `.md` 配对。
