# Bridge Stage A (bA1)：离线 UMD 桥接 triage——tracer、节点选择与 Connect 序列

日期：2026-09-30。全部离线：无 PCI/BAR 写、无模块加载、无 GPU 提交。
方法：`LD_PRELOAD` 拦截 + 反汇编交叉验证（报告内每条结论标注来源 T=动态 trace / S=静态反汇编 / H=头文件）。

## 1. 工具（新增入库）

- `probe/umd_bridge_shim.c`：拦截 `/dev/dri/*` open（给 `/dev/null` dup fd）、
  两条 PVR ioctl（`0xc0206440` 桥包、`0x40046445` INIT），输出清零伪造、
  按表(`canned[]`)回填关键输出；`mmap/read/pread/lseek` 只记录。
  JSONL 落盘 `$UMD_TRACE`。
- `probe/umd_connect_harness.c`：`dlopen` + `dlsym` 调用 UMD 导出函数。
- `probe/Makefile`：新增两个目标；`mt-status` 不变。

## 2. 材料恢复（重启后 /tmp 被清空）

- `build/legacy-umd-pvr-connect-candidate/rootfs/`（gitignored，但在盘上）
  的 `libsrv_um_MUSA.so.1.0.0` sha256 与审计期望完全一致
  (`b3058c02…`)——身份无损，已复制回 `/tmp/mtt-linux-umd-5.2.0/root/` 供脚本使用。
- `scripts/audit-legacy-umd-pvr-bridges.py` 重跑：205/205，26 缺失，零 diff，可复现。
- 教训：`/tmp` 下的反汇编（58 MB）、DKMS 解包、trace 均为易失；
  关键结论必须落盘到 `reports/`（本文件 + trace JSON），源文件在库内。

## 3. 节点选择（T+S）：version 名必须是 `pvr`

- UMD 扫描 `/dev/dri/renderD128…`（T：先 render 节点，不先 card），每节点发
  `DRM_IOCTL_VERSION`（`0xc0406400`），随后 `strcmp(name, "pvr")`（S：`0xa4989`）。
  只有名字全等 `pvr` 才停（T：改 shim 返回 `pvr` 后首节点即中，之前 `mtgpu` 连扫 256 节点）。
- 另一函数 `PVRDRMGetRenderFromFD` 比的是 `mtgpu`（S：`0xa4e57`）——两条路径并存；
  Connect 走的是 `pvr` 这条。**未来自研 DRM 节点给 UMD 用的 version 名取 `pvr`**
  （现有 `mt-3d-check` 按路径打开，不受影响）。
- 副发现：shim 初版 `date/desc` 置空导致 UMD 内 `strdup(NULL)` 崩溃（T+gdb），
  按真实语义回 `""` 后解决——真正的 KMD 也必须返回合法三字符串。

## 4. Connect 初始化序列（T，伪造输出下走到第 7 步后主动断开）

```text
INIT(init_module=1)
0x1:0x0 Connect            in16  80000850 00000000 00010000 00000020
0x1:0x2 ACQUIREGLOBALEVENTOBJECT  in0/out12
0x1:0xf ACQUIREINFOPAGE    in0/out12 -> hPMR=0x1000（伪造）
0x6:0x6 MM:PMRLOCALIMPORTPMR in8(hPMR=0x1000) -> align/size=0x1000,hPMR=0x1001（伪造）
0x6:0x7 MM:PMRUNREFPMR      in8(hPMR=0x1001)
0x1:0x10 RELEASEINFOPAGE   in8(hPMR=0x1000)
0x1:0x1 DISCONNECT
PVRSRVConnect -> 78
```

- 句柄链 verified（T）：伪造的 `0x1000/0x1001` 原样出现在后续输入里——
  UMD 确实消费这些输出，伪造必须自洽。
- Connect 输入解码（H，对 `common_srvcore_bridge.h`）：
  `build_options=0x80000850`、`DDKbuild=0`、`DDKversion=0x10000`、`flags=0x20`，
  与 `verify-legacy-umd-pvr-connect.py` 的旧断言一致。
- 中止点：release 后立刻 disconnect。info 页在此路径下**未被读取**
  （T：无 mmap/pread/lseek/mmap 命中；read 仅 1 次读 fd 3 配置文件）。
  最可能原因：Connect OUT（17B：`ui64PackedBvnc/u32 eError/u32 caps/u8 arch`，H）
  全零——Bvnc/caps 未过 UMD 检查。**下一步：伪造 Connect OUT 后看序列是否越过 disconnect。**

## 5. Sync `ui64MemType` 实值（S，单调用链，待动态复核）

- 全库仅一处 `(0x02:0x00)` wrapper（`0x390d0`，in8/out32；输出布局与 5.2 头
  `8/32` 逐字节吻合：handle@0、PMR@8、eError@16、BlockSize@20、VAddr@24）。
- 8 字节输入来自调用方 `rsi`；直接调用者 `0x9f970` 经 `rdx` 透传；
  `0x9f970` 地址被取用后存入回调表，由 `0xa00a1` 处以 **`edx=0x2`** 发起。
  证据指向 **memType恒为 2**；待 Connect 走通后由动态 trace 一锤定音
  （届时 allen 值、是否分场景变化一次看清）。
- 5.2 Host KMD 源码中 `ui64MemType` 仅出现在声明里，
  实现（预编译 `objs/`）不可见——忽略 memType 的 2.3 handler 行为
  只能由 UMD 实测反推，这正是 tracer 存在的意义。

## 6. 26 个缺失 ID 的分组（H，对 JSON；渲染主路径相关度初判）

缺失：`0x02:0x0a-0x0e`（SYNC 事件/checkpoint 组）、`0x06:0x2a/0x30/0x31`
（MM 扩展）、`0x81:0x0d-0x10`（RGXCMP）、`0x82:0x11-0x14`（RGXTA3D 高编号）、
`0x86:0x0f-0x11`（HWPerf/PFM）、`0x88:0x04-0x07`（RGXKICKSYNC?/TQ）、
`0x89:0x08-0x0a`（RGXTIMERQUERY/TDM）。
Connect 序列至今未命中其中任何一个；`RGXKICKTA3D3(0x82:0x0e)` 在覆盖侧。
结论待动态序列走到 kick 后再定；当前没有证据表明主路径被缺失 ID 挡住。

## 7. 下一步（bA2）

1. 伪造 Connect OUT（Bvnc + caps + arch），看是否越过 disconnect；
   Bvnc 真值候选：UMD 内 `expected` 常量（`0x3be60` 附近 push 的
   `0x660/0x17/0x23` 待解码）或从硬件只读寄存器取（另起只读 helper，不碰 BAR 写）。
2. 走通 Connect 后依次点亮：heap 查询 → Sync alloc（复核 memType=2）→
   RGX 上下文创建 → `RGXKICKTA3D3`（顺带看清尾部 8 字节内容）。
3. 输出「渲染主路径最小命令集 + 打桩表」，作为 Stage B（内核侧实现）的输入。
