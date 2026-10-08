# r346：TA bring-up 阶梯定义——入口链已定，缺三件（离线，零硬件触碰）

- **入口链（本轮静态实锤）**：`RGXKickTA @ 0x7afd0`：`rdi=NULL→0x7b1d0` 拒、`rsi=NULL→0x7b198` 拒、`psKickTA+0x30==0→0x7b198` 拒（返回 3，r194 活体复现过）→ `MTSRVGetClientEventFilter` 取 `al&2` 定分支（`0x7b0f8` 另路）→ `PrepareTA @ 0x78800(rdi=ctx, rsi=psKickTA, rdx=&local)` → `SubmitTA @ 0x796b0` → `0x82:0x14`。与 r192 的 `FUN_00178800/001796b0` 逐项对上（`call 78800/796b0` 各一次）。
- **缺件清单**：
  1. **producer**：`psKickTA+0x30` 须是 PrepareTA 产物；r194 手塑卡在返回 3。模板是 GFX 链（r196–r210：render ctx `+0x200` allocator、`+0x318` SubmissionHead、`0x4700` CCB 捕获），TA 侧尚未复刻。
  2. **桥 submit 口**：`0x82:0x14` observer 只看不执行（r215–r218）；`0x82:0xC` 在门禁钉死的 57 功能号之外（`test_pvr_fn_ids.py` 无此项），身份与语义均未知，仍是 S4 边界。
  3. **执行**：translator/fire 的 TA 版（TQ 版 r267–r316 不可直接套用，CCB 语义不同）。
- **阶梯（按序，每步独立可验）**：
  - T1：`SubmitTADataEnQueue`（`0x54380`）发出哪个桥命令——静态找 `handle,0x82,0xN` 调用点（r190 找 `0x14` 的同手法），小而 bounded，下轮即做；
  - T2：fabricated TA producer（r210 配方移植到 `RGXKickTA`：真实 render ctx + 手塑 psKickTA，目标 `0x82:0x14` 非零发出 + 返回 0）；
  - T3：TA translator/fire（待 T2 的真实 CCB 字节）。

## 实测与边界

1. 本轮零硬件触碰：未加载、未重载、未跑 UMD；`bridge ref 0` / `probe ref 1` 不变，会话 freeze 继续。
2. 证据：`objdump -d 0x7afd0–0x7b0a0`（三处 `call` 各一）+ `nm -D`（`RGXKickTA/PrepareTA/SubmitTA/EnQueue` 地址）+ 门禁表缺 `0xC` 项。
3. 未断言：`0x82:0xC` 的身份（无证据不命名）；`al&2` 分支（`0x7b0f8`）的内容（未展开）。

## 下一步

1. T1：`SubmitTADataEnQueue` 的桥命令归属（离线）。
