# r100：sutu 设备选择 standalone 可用；+0x54 指向 surface 创建

自主推进本轮结论三则（全离线 fabricated）：

1. **`sutu_dev_select(128)` 返回 0**——真实枚举 `/dev/dri` 并选中我方桥
  （renderD128）。`sutu_DevInit` 不需要（CreateContext 路径与其无关；
  DevInit 要非零 OUT 结构 + 设备状态，另议）。
2. select 后直接 `R2DCreateContext` = 原 142 条路径（DekInit 省掉无影响）。
3. `+0x54` 写入者锁定方向：**surface 创建链**。`R2DCreateSurfaceLayout`
  头部即查 OUT 结构 + 栈上第 7 参数（宽/高/格式类），正是填充传输参数
  （含计数）的位置；create-struct 的 P 槽内容当与 surface 无关时恒空。
  下一步：Layout/Surface 签名整形（NULL 门 + 栈参 offsets 已开头），
  然后看 P 槽。

## 附带（整形方法论，再记一笔）

- 先读头部 NULL 门 + 参数使用再调，比盲调省一个量级
  （DevInit 三次 abort/segfault 才定位到 connect-core；
  dev_select 一次读头即中）。
- fabricated 枚举走真实文件系统（`/dev/dri` scandir 未被 shim 劫持）——
  这是 select 能在 fabricated 下真工作的原因；凡需"真实存在"的资源
  （设备节点、文件），fabricated 天然可用，只有"桥语义"需要编造。
