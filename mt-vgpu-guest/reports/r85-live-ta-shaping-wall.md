# r85：活体 KickTA——链入 PrepareTA 深部，手工整形到墙（转真 GLES 绘制）

用户选定的 B 方案执行结果：passthrough `RGXKickTA` 6/6 走到
PrepareTA 深部（远超 fabricated 到达点，活体同步对象真实存在），
新崩溃点是 `RGXKickTA` 透传的第 5 参数残留（r8=0）——那是真实绘制
状态指针，手工编不出来。结论：**手工整形到此为止**，下一阶段是
用 musa mesa GLES 栈跑真实绘制，让 UMD 自己走完整路径。
会后零残留。证据：`r85-live-ta-shaping.jsonl`。

## 活体执行（批准的单次实验，无 rmmod/timeout/GPU 工作）

- 前置发现：桥引用数 1≠0——持有者是 Chrome（PID 24317，fd 27  passive
  open 做 GPU 枚举），非我方残留；不杀（非本轮 scope），per-file 隔离下
  不影响 ioctl 实验，如实记录。
- 6 次 passthrough（ta6 整形 + `UMD_DUMP_BRIDGE=0x82:0xc,0x88:0x4`）：
  桥序列走到 `0x2:0x7`（SyncAllocEvent stub，ret=0），89 条记录，
  真 sync PMR/句柄全程真实；无 `0x82:0xc`（崩在之前）。
- 新崩溃（dmesg `segfault at 20`，读空）：PrepareTA VMA `0x78e30`
  `mov 0x20(%r8),%esi`，r8=0。r8 是 `RGXKickTA` 第 5 参数（p5）的透传残留——
  harness 传 `u0`，真绘制传 TA 绘制状态指针。fabricated 同位置同 crash，
  与活体一致（非活体特有问题）。

## 整形墙在哪里（诚实结算 r83–r85）

已拿下（门卫→`+0x30` 指针→`+0x120` 空写→uint 换算→`+0xb6` 状态→新 RIP）；
墙：p5 及后续同步对象（TA3D sync 查询要真 sync 上下文数组，
`puVar5 + uVar11*0x34` 的构造只存在于真实绘制会话里）。
继续手工编 = 穷举 mesa 的绘制状态机，方向错误。

## 转向：真 GLES 绘制（新项目提案，不在本轮动手）

树内有全套 musa mesa 栈（`libGLESv2_MUSA_MESA`、`libmusa_dri_support`、
`musa_mesa_wsi`、`rogue2d`、`usc`、`sutu_display`，见
`build/legacy-umd-pvr-connect-candidate/…`）：最小 GLES 应用
（clear + swap）走 `LD_PRELOAD` shim + `UMD_DUMP_BRIDGE`，
让 UMD 自己产生**走完整绘制路径的 kick**——这正是 r56 起要的
"非零 CCB 内容"的唯一正解来源。需立项（EGL/display 接线、
surface、loader 配置均未知），另批。

## 会后状态

`pending=0 completed=23`、`objects=34`、引用 38/1（1=Chrome）、
D 态 0、无新增 WARN。崩溃进程按文件释放，无残留。
