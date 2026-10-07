# r225：UMD 生成 CCB 进真桥观察（批准执行）——r210 字节重放，nonzero/FNV/head 全命中

- **结论**：fabricated UMD 链（r210）与真桥 observer（r215）本轮闭环：`pvr_observe_ping` 新增 CCB 相——退役 4 页窗口后建 8 页新 PMR（同 VA），从 `r210-gfx-ccb-capture.bin` 逐槽（仅非零，61 槽，`0x2:0xa` 写链复用）载入 0x4700 字节，再以 UMD 自身 VA/size/ID/counts（flags=0/VA=`0x8000023000`→本窗基址/size=`0x4700`/ID=1/check=1/update=1/pmrsync=0，阵列 NULL 已声明）fire。桥报 `nonzero=107 first=0x10 fnv=0x8effdedcbd0d9d57`——与开工前离线预言逐项一致；head 64 非零字节另行逐字节比对全等。22 项全 ok，exit 0；refs 1/0 不变（默认桥，无需重载），dmesg 零 WARNING/BUG/Oops。**Freeze 继续。**

## 实测（执行过）

1. 开工预检：refs 1/0（r222 构建：observer + `0x2:0xa` 双在载）；r210 请求包解码（packed 偏移，先错一次 unpack 越界后按 r207 偏移重算）+ 预言（nonzero=107/fnv）记录在案；dmesg 打 `[r225] ccb-observe-start` 标记。
2. 工具（离线部分）：CCB 相（退役/重建/载入/fire）+ `ccb_path` 可选参数（默认 `reports/r210-gfx-ccb-capture.bin`，相对工作目录——必须在 `mt-vgpu-guest/` 下运行）；门禁 +1（载入 + fire 回 0 期望 + `CCB_WINDOW` 尺寸）；`-Werror` 零警告构建。
3. 反向验证：改错 check 标签大小写 → FAIL，还原 → OK，重编（有效；r224 的注释残留教训已吸收）。
4. 活体：22/22 ok。`ccb slots planted 61 slots` 与离线 `nonzero u32 slots: 61` 一致；事后 refs 不变；dmesg 仅三 observe 行（零/手塑/CCB 窗口）。
5. `check-offline`：325 Python（324+1 新）OK；C/内核沿用 r222（本轮零改动，未重跑）。

## 边界

- 字节是 UMD 生成的，但 envelope（PMR/reservation/context）是手建的；sync 数组仍 NULL；CCB 内容语义（包头/payload 边界）未解读；执行未碰。
- 本轮 fresh file 自包含 + 全 teardown；默认桥配置；未用 `timeout` 包裹。

## 下一步（候选，需批准）

- CCB 内容解读（包头/payload 边界，离线 recon；r175–r181 算术为 TDM 系，GFX 系待立项）。
- TQX 真发射立项；真实绘制执行（backend 接线）。
