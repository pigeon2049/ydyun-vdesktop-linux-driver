# r381: 3D opcode 为 0x68 (RGXCompute)；0x66 在 DM2 仅对真实命令有效

## 结论

**3D (DM2) 的 firmware opcode 是 0x68 (RGXCompute)**，对应 work type 5。0x66 在 DM2 上仅对真实命令包有效（mt_live_3d.c 已证明，r37–r41），对空 marker 被 firmware 忽略（r380）。0x82:0x14 应使用 0x68。

## 1. 证据链

### 1.1 mt_work_opcode() (mt_work_command.h:14-24) [MEASURED]

type->opcode 映射：
- type 1 → 0x67 (Transfer Op), DM 1
- type 3, 11 → 0x66 (RGXVertex / UniversalQueue), DM 4
- type 5 → 0x68 (RGXCompute), DM 2
- type 6 → 0x65 (Preempt)
- type 9 → 0x69 (CopyEngine)
- default → 0x64

### 1.2 DM 路由 (mt_work_command.h:89-106) [MEASURED]

- type 5 → DM 2, flags=1, capabilities=0xf, scheduling_class=1
- type 8 → DM 2, flags=1, capabilities=0xf, group=1

### 1.3 mt_live_3d.c 实证 (r37–r41) [MEASURED]

- node_type=5 → DM=2
- req.type=3 → opcode 0x66
- 结论：0x66 + 真实命令包在 DM2 上工作正常

### 1.4 r380 失败分析 [MEASURED]

- DM2 + 0x66 + 空 marker → firmware 忽略
- DM2 要求完整命令包，不接受空 marker

### 1.5 Windows KMD (mtkm64.sys) [MEASURED]

- strings 提取：RGXVertex, RGXPixel, RGXCompute
- 二进制包，无源码

## 2. 0x82:0x14 opcode 选择

推荐：0x68 (RGXCompute)
- type 5 → DM 2，路由明确
- 'MUSAKICKGFX5' 名称暗示 type 5
- Windows KMD 确认 RGXCompute 存在

备选：0x66 (RGXVertex)
- mt_live_3d.c 证明在 DM2 上有效（真实命令）

## 3. 完成码预测

- 3D：标准 COMPLETE (0)，无特殊码定义
- TA 对照：0x66 → 0x100，0x64 → 0

## 4. DM2 vs DM3 包结构

相同：opcode@0x0c, fence@0x48, root_pa@0x18, command_va@0x28, bytes@0x30
差异：opcode (0x68 vs 0x66)，完成码 (0 vs 0x100)，空 marker 行为 (忽略 vs 接受)

## 5. 缺口

- [TO-VALIDATE] 0x68 在 DM2 的 firmware 响应（需真实 UMD 调用验证，不得单发试探）
- [GAP] Windows KMD 无源码
