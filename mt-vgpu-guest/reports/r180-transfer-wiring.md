# r180：T3-transfer 活体接线规约（scratch 中转架构；只读 recon）

- **结论**：transfer 活体发射走 scratch 中转，不直绑池页面——决定性依据：`mt_gpu_vm_bind_many` 要求 `bo->store/ops` 与表一致（`mt_gpu_vm.h:201`），外页 facade 无法绑进 translator 会话空间，而 `mt_bo_create` 无封装现成页 API；且池（5–10MB）直绑虽容量够（表 32 页→15872 项，`mt_mmu_bootstrap.h:32`），新 API + VA 管理成本高于一次精确拷贝。scratch 架构只用已验证原语：会话 BO 分配/绑定、TQX fill/copy 构造器、submit+fence、CPU 回填。零硬件触碰，本轮只读、无代码改动。

## 接线（fill，copy 类推双面）

1. `pvr_cmd_tdm_submit3_observe` 内（`translate_transfer` 参数门，默认 off 关断原样）：CCB 定界（现 observe 已做）→ `mt_transfer_pool_parse`（r179）得 `{pixels, color}` → dims（调用方给，`w*h` 校验）→ rect。
2. 会话 scratch：`mt_bo_create`（`d->buffers.ops`，`pixels*4`，`PAGE_SIZE`）→ `bind(translator.space, scratch, SCRATCH_VA, …)` → `upload`（沿用 prepare 锁序：`translator_lock` → `trial_lock`）。
   SCRATCH VA 建议 `0x49000000`（cmd `0x48000000`/32K、rt `0x48100000`/64K、ctx `0x50000000+` 之外；64MB 上限内）。
3. 发射：`mt_tqx_fill_build`（dst=scratch VA + rect + color）→ 现有 submit+fence 路径（`live_3d_drm.c:455` 链 / translator 等待语义）→ 5s 超时即 cancel + 回错（UMD 见非零，不挂起）。
4. 落位：fence 成功后 `memcpy(pmr->host + surface_off, scratch_cpu, pixels*4)`（桥拥有 host 内存；`pmr->host` 在 DMA 注册后仍是有效 CPU 地址，r60 file-arena 语义）→ 回 0。
5. 生命周期：每提交创建/解绑/释放（简单可审计；常驻对的 VA 碎片化以后再说）。

## 不做的事

- 不扩 translator 表（32 页已够 15872 项，池直绑是备选未选）。
- 不新增 BO 封装 API；不碰 UMD VA 语义（translator 空间独立，scratch VA 自选）。
- 不读嵌套指针（沿用 observe 边界）；不碰 DM/TA/CDM。

## 门禁计划（实现轮）

- 离线：rect/parse 已有（r179）；新增接线顺序门禁（param off 时行为与 observe 逐字节一致：同一 fabricated 提交跑两遍 diff 空）。
- 反向：掐任一 gate（parse 失败注入）→ 回错非零且无发射（dmesg 无 submit 行）。
- 活体（`=2` 窗口）：像素回读比对（`live_tqx_readback` 模型）+ ref 平衡 + dmesg 零 WARN。

## 边界

- copy 需双 scratch（src 上传 + dst 落位），本规约只写 fill；copy 的 chunk 描述符映射（`mt_tqx_copy.h`）待实现轮细化。
- dims factorization 仍是调用方输入（r179）；stride/朝向证据一到即换。
