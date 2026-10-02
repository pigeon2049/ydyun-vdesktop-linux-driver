# r73：非零 check kick 上真机（passthrough 实测，T2 活体验证）

r72 的 fabricated 非零 kick 已在真机复现：passthrough 全 ladder +
512B 手工结构体，一次即成，`0x88:0x4 ioctl_real ret=0 check=1 update=0`。
活桥 dmesg 落下 `ufo_known=1/1`——T2（UFO 句柄 → bridge PMR）在真机走通，
这是翻译器输入链第一次活体验证。无 GPU 执行（accept-and-inspect），
零 fault，会话状态前后一致。

## 实验（用户本轮明确批准真机测试；`make umd` 因首步 rmmod 仍禁用，只手跑 harness）

命令 = rung8 链 + `buf 26 512` + `u32@216=1 / u64@224=*b10 / u32@232=1`，
`UMD_SHIM_PASSTHROUGH=1` + `UMD_DUMP_BRIDGE="0x88:0x4"`，无 `timeout` 包裹
（r67 红线），无 rmmod/insmod。证据：`reports/r73-live-nonzero-kick.jsonl`。

## 实测结果

- 活桥接受：`ioctl_real 0x88:0x4 ret=0`，`check=1 update=0`，含 `out_hex`
 （fence 路径正常）。
- 活桥只读观察行（t≈3191s）：
  ```text
  kick sync inventory: check=1 update=0 ufo_known=1/1 check_fd=0 timeline_fd=-1 extref=0
  ```
  `1/1` 的含金量：passthrough 下 `CreateSyncPrim` 返回的是**真实 bridge
  sync PMR 句柄**，T2 在该 file 的 PMR/对象表中命中——r63 的 `2/3` 用的是
  手工真假混合句柄，本轮是 UMD 全链路自然产生的真句柄。
  `check_fd=0` 与 r72 fabricated 一致（r53 合成路径的垃圾值/−1 是另一条路）。
- 附带两行确认 arena 行为正常：`VM plan … pages=5`（kick ranges 进 plan），
  进程退出时 `arena close: high_water=99/512, 17/512 pages fallbacks=0`
  （按文件释放，无泄漏足迹）。
- 事后状态与事前完全一致：`pending=0 completed=23`，引用数 38/0，
  `buffers objects=34`（一字不差），D 态 0，dmesg 无新增 WARN/BUG/Oops
  （6 条命中全是 t≤561s 启动期旧行）。

## 结论与边界

- STATUS 下一步 1 的"是否上真机"已有答案：**非零 check kick 可上真机**，
  活桥 inspect 路径（T1 拷贝 + T2 解析）对真实句柄全绿。
  剩下的缺口仍是 update 侧数组偏移（r72 遗留）与真实 CCB 内容（需绘制路径），
  不在本轮。
- 本轮是用户批准的单次 live 实验；freeze 红线其余部分继续有效
  （不 rmmod、不 unbind、不跑 `make probe/umd`、不提交 GPU 工作）。
