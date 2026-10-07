# r311：poolshape 活体读取（批准执行）——形态落定，判决丢失备忘

- **结论**：五开窗口读形态行：`0x1019: distinct=1 first=0x0`（全零实锤）、`0x101b: distinct=2 first=0x4c`（单字节标志池）、`0x1032: distinct=2 first=0x0`（零 + 单值）。`pristine override pool=0x1019` 生效 → fire `todst=1` 全验 → bump 成功。**UMD 判决丢失**：blit stdout 与 trace 在入库前随暂存区清理丢失（r295 同类事故重演——入库纪律仍未长记性），仅 dmesg 侧（44 行）幸存。形态结论不受影响（dmesg 执行值完整）。
- **形态解读**：源池非 solid（2 distinct words，首字零）——solid fill 先天不可复现；目的池全零。图案几何（首/末非零偏移）仍未知——r312 poolbox 即为此而加，r313 窗口读数。

## 实测（执行过）

1. 批准：standing 授权。停桌面 → ref 0 → 五开（含 r310 构建）→ 真实 blit → 读形态/override/fire/bump 行 → 拆桥 → 默认 → L3 → 拉桌面（终态 refs 1/1，窗口零新增 WARN，freeze 已恢复；恢复轮次未逐项记录——纪律赤字一并备忘）。
2. 证据：`r311-window.dmesg`（0600，44 行， marker 起全切片）；blit trace/stdout 丢失。
