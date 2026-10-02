# r94：0x89:0x0 线上成功，CCB 自检 unwind（rogue2d 内建状态问题）

r93 的下一步执行完毕：`0x89:0x0` 在 fabricated 下已成功
（IN 40B / OUT 非零 context / ret 0），但 UMD 紧接着拆除
（unmap×3 → unref → release×2 → destroy），未再发任何新桥调用。
结论：卡点从"桥缺件"转为"CCB 自检"——`RGXTDMCreateTransferContextCCB`
（512 行）桥后有约 10 道门，查的是 rogue2d 内建状态
（如 `*(create_struct+8)==0` 即拒），与桥 OUT 无关。全程离线。

## 本轮证据

- `0x89:0x0 IN` 解码（40B）：`{0x10000, 0x80, 0x3001(handle),
  stack_ptr, 0…}`——UMD 侧构造正常，桥侧照单全收。
- CCB 门卫群（L62974 起约 100 行内）：`param_1/param_2/param_5` 非空、
  `*(param_2+2)` 非零、`puVar8` 非空、`local_1b0/1b4/1b8` 标志、
  `uVar14` 状态机（`&~2` 后必须为 0/1）——任一即 `LAB_0018b772`（返 3）。
- 这些槽位由 rogue2d 的 `R2DCreateContext` 内部流程填写，
  不是 harness 参数能直接给的（r83 TA 整形的同构处境）。

## 路径判断（二选一）

A. 继续自底向上：跟 `R2DCreateContext`（rogue2d 侧 оповещ，
   非 libsrv）内部构造顺序，把 create-struct 槽位逐个喂真值——
   又是整形长征（TA 链已证明此路以周计）。
B. 换源头：`sutu_DevInit + sutu_dev_select`（rogue2d 自带设备初始化，
   r86 已定位）可能正是装配这些状态的正规入口——先试正路，
   不通再回 A。

下轮先 B（一次 fabricated 调用即见分晓），B 不通则 A。
