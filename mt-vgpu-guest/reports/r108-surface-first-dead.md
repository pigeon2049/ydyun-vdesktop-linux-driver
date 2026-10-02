# r108：surface 先行证伪——无 context 则零桥调用

r107 出路二选一之 surface 先行，执行即证伪：
`sutu_dev_select → R2DCreateSurface`（跳过 CreateContext）返回 3，
且**零条桥调用**——surface 创建无条件依赖 context 状态，
不存在独立推进的捷径。依赖链最终形态（单向，无环可钻）：

```text
CreateContext（+0x54 计数）→ Surface（堆）→ Fill（包）→ Wait
      ↑ 唯一真卡点仍在此
```

CCB 由 `R2DCreateContext` 直接调用（bt 实锤，无 wrapper 中转；
rogue2d 内无静态调用点，走函数指针表）。
`+0x54` 写入者仍是唯一未知数——但搜索域已收窄到
"R2DCreateContext 内、CCB 调用点之前、写 P 所指堆块者"。
离线，零硬件触碰。
