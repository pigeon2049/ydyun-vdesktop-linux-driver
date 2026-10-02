# r86：真绘制栈 recon——GLES 无 EGL，Rogue2D 是首选 spike

为 GLES 绘制立项提供离线依据，结论明确：**别从 GLES/EGL 起步，
从 Rogue2D 起步**。树内根本没有 EGL loader（`libGLESv2` 零 `egl*`
导出，上下文创建得另寻 DRI/wsi 接线）；而 `librogue2d_api` 只依赖
`libsrv_um`（走同一座桥，shim 直接可用），API 是"建 ctx→建面→
填充→等 fence"四步，且自带测试脚手架。全程 readelf/nm/objdump，
零硬件触碰。

## 排除：GLES/EGL（起步成本高）

- `libGLESv2_MUSA_MESA.so` 有 359 个 gl 导出、**0 个 egl 导出**；
  树内无 `libEGL`。EGL 上下文/DRI 绑定（`musa_dri.so` 在，但接线未知）
  是另一个项目。本轮不展开。

## 入选：Rogue2D（96 导出，同一座桥）

```text
NEEDED: libsrv_um_MUSA.so + libc   ← 与 8 rung 同桥，shim/fabricated 直接复用
R2DCreateContext → R2DCreateSurface[Layout] → R2DSurfaceFill/Clear/Ramp/Blit
  → R2DWaitForDevVar / GpuWaitFence → Destroy
自带：R2DGenerateTestImage / R2DSurfaceToFile / R2DWriteTestSurface（像素闭环现成）
     sutu_DevInit / sutu_dev_select（设备初始化脚手架）
```

- `sutu_DevInit` 头部：首参判零分流、第三参比 `-1`——类 device-index 初始化，
  harness `call` 可直接喂（16 参数上限内）。
- 填充类（`Rogue2DFillBlt/Gpu2DFillBlt/R2DSurfaceFill`）即真实绘制路径 kick
  的最简形状：同步（WaitForDevVar）+ 有意义的 CCB 候选，一次到位。

## 下一步（spike 设计，离线可先行）

fabricated 下试调序列（失败即信息，无硬件影响）：
`sutu_DevInit → R2DCreateContext → R2DCreateSurfaceLayout →
R2DCreateSurface → R2DSurfaceFill → R2DWaitForDevVar`，
看是否出现非零 counts/CCB 的桥流量（`UMD_DUMP_BRIDGE=0x88:0x0,0x88:0x4,0x82:0xc`）。
一旦出现 → 转活体 passthrough 抓包（需批准）。
