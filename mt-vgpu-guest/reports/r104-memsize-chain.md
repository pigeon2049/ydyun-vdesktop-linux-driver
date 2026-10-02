# r104：memsize 计算链定位（imul×2，输入待溯源）

r103 的下步执行：sample count 4 消掉"Invalid sample count"门后，
新门 `CreateTestSurfaceLayout: memsize zero error`（line 1089），
其计算链已定位（`R2DCreateSurfaceLayout` 内，VMA `0xbd15/0xbd2a`）：

```text
cmove (ebx 为 0 则置 1) → imul eax,ebx → call validator → shr eax,3
  → imul ebx,eax → [r15+0xd0] (memsize 槽)
```

任一乘数为 0 即 size-0。乘数来源混合：宽/高（已给 64/64 非零，
排除）+ 两 validator 返回 + 对象字段（`[r15+…]`，零缓冲可疑）。
下步：断两 validator（`0x15b20` 已过是格式门，`0x15ce0` 返回待查）
+ 读 eax/ebx 在 `bd0e–bd15` 的装配（`mov ebp,edi` 前的来源）。
离线，零硬件触碰。

## 附带（推进刻度）

- sc=4 是 sample 门的合法值之一（0/1 拒，2/8/16 拒——至少 4 通行；
  更全的采样枚举未扫，够用即停）。
- 宽/高/format/采样四维已排除三维（format 经 512 门消失确认、
  宽高 8–1024 全同、采样 4 通行）——剩余变量只剩 validator 返回
  与对象字段，搜索空间收敛了一个量级。
