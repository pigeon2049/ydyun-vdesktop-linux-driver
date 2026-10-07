# r300：fire-into-destination 离线设计与实现（零硬件触碰）

- **设计**：UMD 越过 submit3 后比对目的池像素（`-f` = fill with clear colour），而 fire 只写 scratch——缺口是最后一公里。方案：work 每块验过即 `memcpy` 进 UMD 目的池（`host + POOL_HEAD(3841) + chunk_off`，pool 偏移模型沿用 observe 已证的 `va - binding->va`）；排序由 handler 保证（bump 前等 fire 完成，60s 可中断预算，失败大声返错→UMD abort，不 hang）；单线程 UMD 阻塞于 ioctl + teardown 先 cancel，PMR 生存成立（无 kref，见边界）。
- **实现**：新参 `translate_fire_to_dst`（默认关）；translator 增 `fire_dst_host/span/to_dst/result`；work 单遍读（bulk→temp→验→拷）；handler 在 bump 段先等 `fire_running` 清再读 `fire_result`；尾块 bug 现场修（span 忘加 POOL_HEAD → 尾块 `-ERANGE`，改 `span=dst->bytes` + 调度时预检）。
- **门禁**：`test_pvr_tqx_fire.py` +5（to_dst 默认关/验拷/结果上报/observe 等 fire/无 fence 等）17/17；`test_pvr_submit3_bump.py` 沿用；反向验证通过；`check-offline` 378+292 全绿；`make kernel` W=1 零警告。未加载。
- **边界**：work 触 file PMR host（r267 规则破例，安全论证见上；跨线程 close 不存在于单线程 blit）；`fire_result` 初值 `-EBUSY` 防旧值；dst 池选择沿用 locate “最大池”启发（源/目的误判是 r302 首要怀疑项，见 r301）。
