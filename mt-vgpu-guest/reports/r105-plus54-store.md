# r105：+0x54 存储点定位（bb13），跳过条件 bb17（edx 待溯源）

r104 的乘数溯源落地一半：`[r15+0x54]` 的唯一写入点是
`R2DCreateSurfaceLayout` 内 `0xbb13: mov %eax,0x54(%r15)`，
eax 经 bsr 对齐数学从宽/高算出（`1 → cmovne 高 → min(宽) →
bsr → xor 0x1f`)；但紧接 `0xbb17: test %edx; je bce0`
跳过存储直接进 memsize 路径——我方触发的正是此跳
（+0x54 保持 calloc 零）。edx 在 `bb0e–bb17` 之间装配，
是最后一米（读一次即定论）。离线，零硬件触碰。

## 附带澄清（栈读之谜收敛）

- `[rsp+0x90]` 门 = a10（位移 0x68 已实证：全非零即过）。
- `[r15+0x44/0x58/0x64]` 存的是调用者栈读数——内部调用者
  （`R2DCreateSurface`）自建栈实参，故外部直调 Layout 时读到的是
  harness 栈残留：**Layout 不支持裸调**，必须经 Surface 系入口
  （r101"正解是上层"判断的汇编级确认）。
- 故下步不是继续调 Layout 参数，而是转 `R2DCreateSurface`
 （`0xbfb0`，自建栈实参，签名待读）。
