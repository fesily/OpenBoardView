# XZZPCBFile 完整解析

日期：2026-08-25  
状态：draft，待用户审阅

## 问题

上游 `XZZPCBFile` 只把 PCB 当网表+封装外形：走线、过孔、铜皮弧、焊盘几何、元件层都丢掉。本 fork 的 `BRDFileBase` / `BRDBoard` / `BoardView` 已经能吃 TRACK/VIA/ARC、pin size/shape/angle、多层 side。打开 `.pcb` 时这些字段是空的。

不再做 PCB→BVR3 转换器。补全 `XZZPCBFile`，直接填 `BRDFileBase`。

## 非目标

- 不写 BVR3 writer，不新增 CLI。
- 不改 `BRDBoard` / `BoardView` 渲染（`texts` 解析进 `BRDFileBase::texts`，本轮不接线）。
- 不抽公共 decoder，不把 XZZ 先填进 iguana JSON 结构。
- 不改 DES/XOR/密钥校验路径。
- 不把单块几何解析失败升级成整板 `valid=false`。

## 架构

```
.pcb ──► XZZPCBFile ──► BRDFileBase ──► BRDBoard ──► BoardView
. json ──► XJsonFile ─┘（同一套字段；json 只作布局真相和验收基准）
```

- 原地改 `XZZPCBFile.cpp` / `.h`。
- `LayerMapper` + `PCB_LAYER_ID` 从 `XJsonFile.cpp` 抽到 `src/openboardview/FileFormats/XzzLayers.h`，两处共用。每个解析器实例自己构造 `LayerMapper`，禁止文件级 `static unique_ptr`（避免两次加载互相覆盖）。XJson 映射规则不变。
- `XZZPCBFile` 构造结束时：`scale = 10000`，`boardSymmetry = true`。
- 坐标保持文件里的整数，**不再** `/ XZZ_GLOBAL_SCALE`，**不再**调用 `find_xy_translation` / `translate_pins` / `translate_segments`。这三个函数删除。
- `BRDBoard` 已经对所有几何做 `/ scale`。与 `XJsonFile`（`toPt` = json 浮点 × 10000，`scale = 10000`）对齐。

## 层

与 `XJsonFile.cpp` 现有枚举一致：

| id | 名 | side |
|----|----|------|
| 16 | Bottom | Bottom |
| 17 | SILKSCREEN | Top |
| 18 | Board3 | Top |
| 28 | Board | Both；几何进 `outline_segments`，不进 tracks/arcs |
| 29 | PART_OUTLINES | Top |
| 34 | TestPad | Top |
| 其它 | 铜层 | `(BRDPartMountingSide)(id + Top)` |

`BRDBoard` 仍会把出现过的最大层号折成 Bottom。解析器不要自己做这个折叠。

## 语义映射（目标字段）

XJson 是劫持绘制路径得到的可见数据，作为语义真相。`XZZPCBFile` 填同一套 `BRD*` 字段：

| 来源 | BRD 目标 | 规则 |
|------|----------|------|
| net 块 | `nets[id]={id,name}`，保留 `net_dict` | pin/track/via/arc 同时写 `netId` 和 `net` 字符串。`NC` → `UNCONNECTED` |
| 0x05 线，layer≠28 | `tracks` | `points`、`width`、`side`、`net`/`netId` |
| 0x05 线，layer=28 | `outline_segments` | 两端点，不写 width |
| 0x01 弧，layer≠28 | `arcs` | `pos`、`radius`、`width`、`startAngle`/`endAngle`（弧度）、`side`、`net`/`netId`。角度按 XJson：度，若 start>end 则 start-=360，再 ×π/180 |
| 0x01 弧，layer=28 | `outline_segments` | 继续用现有 `xzz_arc_to_segments` 细分；输入角度为度 |
| 0x02 via | `vias` | `pos`、`size`、`side`、`target_side`、`net`/`netId` |
| 0x06 text | `texts` | `pos`、`text`、`side`、`net`/`netId`。BoardView 本轮不画 |
| 0x07 part | `parts` + `pins` | 名、layer→`mounting_side`、内部 0x05→`part.format`（恰好 4 点时 `BRDBoard` 走 `special_outline`）、内部 0x09→pin |
| 顶层 0x09 | dummy part `"..."+name` + pin | 与现在 test pad 行为一致，补 size/side/netId |
| pin | `BRDPin` | `pos`、`name`/`snum`、`side`、`size`/`shape`/`angle`、`radius`（无 length 时 `min(size)/2`）、`top_*`/`bottom_*`+`complex_draw`（与 XJson pad 相同条件）、`diode_vale`、`net`/`netId` |

数值单位：json 浮点 × 10000 = BRD 整数。XZZ 整数应直接等于这个值（匹配容差 1）。

## 二进制偏移：已知假设 + 配对发现

实现者**没有**一份权威 XZZ 布局文档。用户提供**同名** `.pcb` + `.json`（json 由绘制劫持得到）。偏移用几何匹配钉死，然后写进解析器，不在运行时猜。

### 已解析、只需改用途的块

**0x05 线**（`parse_line_segment_block` 现有顺序）：

```
u32[0] layer
u32[1] x1
u32[2] y1
u32[3] x2
u32[4] y2
u32[5] 现被当成 scale 后丢弃 → 按 width 用（json.width×10000）
u32[6] 现注释掉的 net index → netId
```

**0x01 弧**（`parse_arc_block` 现有顺序）：

```
u32[0] layer
u32[1] x
u32[2] y
u32[3] r          → radius（json.rectWidth×10000）
u32[4] angle_start → 度 = raw/10000（与当前 /XZZ_GLOBAL_SCALE 再当度 一致）
u32[5] angle_end
u32[6] 现被当成 scale 后丢弃 → width
u32[7] 现未读 → netId
```

这两块用配对文件**确认或改槽位**。坐标匹配：`|xzz - json×10000| ≤ 1`。netId 必须能在 `nets`/`net_dict` 对上 json.netId。对不上就改假设，禁止 silently 填错网。

### 必须靠配对发现的块

对每个主数据块打：`type`、`size`、按 LE u32 列出 payload（尾部不足 4 字节单独标）。

匹配规则：

1. **Via（0x02）**  
   按 `pos` 对 json.via。剩余整数分别对应 `size`（json.size×10000）、`layer`、`to`、`netId`。`panelPos`/`aperture` 若对不上任何 u32 则忽略（BoardView 不用）。

2. **Text（0x06）**  
   按 `pos` 对 json.text。字符串按 length-prefixed 扫；layer/netId 同 via。

3. **Pin（0x07 解密后的 0x09 子块）**  
   现有：`size` 头、跳 4、x、y、跳 8、name、跳 32、net_index。  
   按 `pos` 对 json.module.items pad / json.pad。把「跳过的 4+8+32 字节」按 u32 对齐到：`layer`、`size`、`shape`、`topSize`/`topShape`、`bottomSize`/`bottomShape`、`length`、`angle`、`diode`。对不上的槽保持跳过，**不得**丢掉已经能填的 pos/name/net。

4. **Part 头 / 内部 0x05**  
   内部 0x05 与顶层线同一布局，端点收集进 `part.format`（去重；`BRDBoard` 只在恰好 4 点时画 special outline）。  
   当前 `part_size` 后跳过的 18 字节 + group name：从中找 layer，对 json.module.layer。找不到则该 part 仍默认 Top（与现在行为相同），不要让整板失败。

5. **顶层 0x09 test pad**  
   现有 x/y/name/net 保留。inner diameter 那 8 字节按 json.pad.size/length 对齐。

发现结果写成解析器里的具名偏移（常量或局部 `constexpr`），不要留「magic skip N」。

同一块 json 对象匹配到多个 XZZ 块或零个：记 `SDL_LogWarn`，该对象跳过，继续下一块。

## 数据流（构造函数）

1. 密钥 / XOR 到 `v6v6555v6v6`：不变。
2. `parse_net_block`：除 `net_dict` 外写 `nets[net_index]`。
3. `process_blocks`：按 type 调新/改过的 parse_*。0x02、0x06 不再空 return。
4. 解析过程中用 `LayerMapper::castSide` / `castPinSide` 设 side（`toSide` 不依赖收集到的 layer 列表；最大层折 Bottom 仍由 `BRDBoard` 做）。
5. `this->scale = 10000`；`boardSymmetry = true`。
6. 不平移。`valid = true` 条件保持：结构完整且 `parse_*` 没有把 `error_msg` 设成结构错误。
7. `num_parts` / `num_pins` / `num_format` / `num_nails` 照旧。

`process_block` 里未识别 type：现有 `SDL_LogWarn`，跳过。

## 错误处理

| 情况 | 行为 |
|------|------|
| 文件截断、`read_uint32_t` 越界、net_size 非法、DES 后 part 头对不上 0x06 | `ENSURE_OR_FAIL` → `error_msg`，`valid` 保持 false |
| 密钥非法 | 现有 Invalid Key 路径 |
| 单条 track/via/arc/text/pin 字段不够或匹配不到 net | `SDL_LogWarn`，丢这一条，继续 |
| json 对里有、pcb 解析后没有的对象 | 验收失败（实现阶段），不是运行时错误 |
| 未知主块 type | warn + skip |

## 文件

| 路径 | 动作 |
|------|------|
| `src/openboardview/FileFormats/XzzLayers.h` | 新建。`PCB_LAYER_ID`、`LayerMapper`（从 XJson 原样搬）。无文件级静态 mapper。 |
| `src/openboardview/FileFormats/XJsonFile.cpp` | 删本地 LayerMapper，include 共享头 |
| `src/openboardview/FileFormats/XZZPCBFile.h` | 声明 `parse_via_block` / `parse_text_block`；删 translation API；`scale` 不再靠默认 1 |
| `src/openboardview/FileFormats/XZZPCBFile.cpp` | 按上表改 parse_*、构造函数、process_block |

CMake 不用改（头文件被 cpp include 即可）。

## 验收

前置：用户给出至少一对同名 `.pcb` + `.json`。没有这对文件则不能钉 VIA/pin 偏移，也不能做数量对比。

1. **布局钉死**：配对匹配后，0x05/0x01/0x02/pin 尾字段槽位写进代码，与 json 抽检 20 条 track、全部 via、10 个 pad 的 pos/layer/netId/width-or-size 一致（整数容差 1，角度容差 0.5°）。
2. **打开 `.pcb`**：`tracks`/`vias`/`arcs` 非空（json 里对应数组非空的前提下）；pin 有非零 `size`；多层出现在 `AllSide()`。
3. **对比 json**：同板 `XJsonFile` vs `XZZPCBFile`：`tracks.size()`、`vias.size()`、`arcs.size()`、`pins.size()` 差为 0，或文档化每一处差的原因（例如 json 劫持没画 silk text）。
4. **回归**：layer 28 轮廓仍在；非法 key 仍报 Invalid Key；损坏 net_size 仍走 `error_msg`，不崩。
5. **肉眼**：BoardView 打开该 pcb，走线/过孔/弧按层可显隐，焊盘矩形/旋转可见。

仓库无单元测试框架，不为此新建。对比用一次性本地程序或调试日志，不提交测试夹具（除非用户把配对文件放进仓库并要求保留）。

## 风险

- json 只含绘制路径上的对象，PCB 里未画的块会对不完。多出来的 XZZ 块：能填 BRD 的照填，对不上 json 的不因此丢弃。
- `part.format` 非 4 点时 `BRDBoard` 不画 special outline，只影响元件框，不影响 pin。
- 取消原点平移后，旧 pcb 在屏幕上的绝对位置会变，相对几何与 json/BVR3 一致。
- `ARC` 角度单位若配对证明不是 ×10000，以配对为准并改本 spec 的 0x01 假设。
