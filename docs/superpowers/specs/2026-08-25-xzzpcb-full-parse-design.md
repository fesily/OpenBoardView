# XZZPCBFile 完整解析

日期：2026-08-25  
状态：draft，待用户审阅

配对文件（`compare/`）：`Switch OLED-HEG-CPU-01 PCB layer.pcb` + 同名 `.json`。下列偏移已对该板钉死。

## 问题

上游 `XZZPCBFile` 只把 PCB 当网表+封装外形：走线、过孔、铜皮弧、焊盘几何、元件层都丢掉。本 fork 的 `BRDFileBase` / `BRDBoard` / `BoardView` 已经能吃 TRACK/VIA/ARC、pin size/shape、多层 side。打开 `.pcb` 时这些字段是空的。

不再做 PCB→BVR3 转换器。补全 `XZZPCBFile`，直接填 `BRDFileBase`。

## 非目标

- 不写 BVR3 writer，不新增 CLI。
- 不改 `BRDBoard` / `BoardView` 渲染（`texts` 解析进 `BRDFileBase::texts`，本轮不接线）。
- 不抽公共 decoder，不把 XZZ 先填进 iguana JSON 结构。
- 不改 DES/XOR/密钥校验路径。
- 不把单块几何解析失败升级成整板 `valid=false`。
- 不从 PCB 填 `BRDPin.angle` / `diode_vale`：json 里有，该对 4424 个 pin 的二进制里没有对应槽（json 来自绘制劫持，可含运行时测量）。

## 架构

```
.pcb ──► XZZPCBFile ──► BRDFileBase ──► BRDBoard ──► BoardView
. json ──► XJsonFile ─┘（同一套字段；json 作布局真相和验收基准）
```

- 原地改 `XZZPCBFile.cpp` / `.h`。
- `LayerMapper` + `PCB_LAYER_ID` 从 `XJsonFile.cpp` 抽到 `src/openboardview/FileFormats/XzzLayers.h`。每个解析器自己用静态 `castSide` / `castPinSide`。禁止文件级 `static unique_ptr`。XJson 映射规则不变。
- 构造结束：`scale = 10000`，`boardSymmetry = true`。
- 坐标保持文件整数，**不再** `/ XZZ_GLOBAL_SCALE`，删除 `find_xy_translation` / `translate_pins` / `translate_segments`。
- `BRDBoard` 对几何 `/ scale`。与 `XJsonFile`（`toPt` = json×10000）对齐。

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

`BRDBoard` 把出现过的最大层号折成 Bottom。解析器不做这个折叠。

## 语义映射

| 来源 | BRD 目标 | 规则 |
|------|----------|------|
| net 块 | `nets[id]={id,name}`，保留 `net_dict` | 同时写 `netId` 和 `net`。`NC` → `UNCONNECTED` |
| 顶层 0x05，layer≠28 | `tracks` | points、width、side、net/netId |
| 顶层 0x05，layer=28 | `outline_segments` | 两端点 |
| 顶层 0x01，layer≠28 | `arcs` | pos、radius、width、角度（度=raw/10000；start>end 则 start-=360；再 ×π/180）、side、net/netId |
| 顶层 0x01，layer=28 | `outline_segments` | `xzz_arc_to_segments`，输入为度 |
| 顶层 0x02 | `vias` | pos、size、side、target_side（`to` 层）、net/netId。aperture、panelPos 不进 BRD |
| 0x06（顶层或 part 内） | `texts` | pos、text、side。本轮不画 |
| 0x07 part | `parts` + `pins` | 名=第一个非空 0x06 文本；layer→`mounting_side`；内部 0x05 端点去重进 `part.format`（恰好 4 点时 special_outline）；内部 0x09→pin |
| 顶层 0x09 | dummy `"..."+name` + pin | 保持 test pad 行为，补 size/side/netId |
| pin | `BRDPin` | pos、name/snum、side、size/shape、top_*/bottom_*、`complex_draw`（topShape≠shape 或 shape≠bottomShape）、radius=`min(size)/2`、net/netId。angle=0 |

单位：json 浮点 × 10000 = XZZ/BRD 整数。容差 1。

## 二进制布局（已钉死）

主数据块：`u8 type` + `u32 size` + `size` 字节 payload。part 内部子块相同。整数均为 LE。

### 0x05 线（payload 28）

```
u32 layer
u32 x1, y1, x2, y2
u32 width          // json.width × 10000；本对 width=2 → 20000
u32 netId
```

该对：json 26438 条全部命中；XZZ 多 1 条，照填。layer 28：104 条进 outline。

### 0x01 弧（payload 32）

```
u32 layer
u32 x, y
u32 r              // json.rectWidth × 10000
u32 angle_start    // 度 × 10000（180° → 1800000）
u32 angle_end
u32 width          // json.width × 10000
u32 netId
```

该对：74/74 命中。json 多 1 条无 position，忽略。

### 0x02 via（payload 32）

```
u32 x, y
u32 size           // json.size × 10000；本对 6 → 60000
u32 aperture       // json.aperture × 10000；不进 BRD
u32 layer
u32 to             // 目标层 → target_side
u32 netId
u32 panelPos_inv   // 0=json panelPos true，1=false；不进 BRD
```

该对：XZZ 3547 条按坐标全部命中。json 多 1 条无 position。同坐标碰撞 2 条（layer/to/net 以 XZZ 为准）。

### 0x07 part（DES 解密后）

```
u32 part_size      // 子块走到 offset part_size+4
u32 layer
u32 x, y           // json.module.postion × 10000
u32 angle          // 度 × 10000；本轮不写入 BRDPart（结构无此字段）
u16 unk            // 本对为 1
u32 fpid_len
char fpid[fpid_len]
然后子块：
  0x06 文本（第一个非空 → part.name）
  0x05 线（与顶层同布局；端点进 part.format，不进全局 tracks）
  0x09 焊盘
```

该对：1039 part = 1039 module。不要再用「跳 18 字节再 skip 31 取名」。

### 0x06 文本（part 内 payload 36，本对无顶层 0x06）

```
u32 layer
u32 x, y
u32 orient_or_unused
u32 scale_or_unused    // 常见 10000
u32 unk
然后与 FPID 相同的名字：u16 unk, u32 len, char[len]
```

解析器仍应能处理顶层 0x06（同布局或按 size 截断）。

### 0x09 pin（part 内；type 之后 `u32 size` + size 字节）

```
u32 layer              // 现代码当 unknown 跳过的 4 字节
u32 x, y
u32 drill_x, drill_y   // json.drillSize × 10000；本对为 0。不进 BRDPin
u32 name_len
char name[name_len]
然后 32 字节几何：
  u32 top_w, top_h;  u8 topShape
  u32 size_w, size_h; u8 shape
  u32 bot_w, bot_h;  u8 bottomShape
  剩余 5 字节本对全 0（不要当 angle）
u32 netId
u8 extra[8]            // 本对全 0；不是 diode
```

该对：4424 pin 的 pos/size/shape/net 与 json pad 全中。json `pad.angle` 与这 32 字节无关（1606 个非 0 角在二进制里找不到）。

顶层 0x09（test pad）：本对没有。保持现有解析，几何字段能填则填。

## 数据流（构造函数）

1. 密钥 / XOR 到 `v6v6555v6v6`：不变。
2. `parse_net_block`：写 `net_dict` 和 `nets[net_index]`。
3. `process_blocks`：0x01/0x05 按层分流；0x02 调 `parse_via_block`；0x06 调 `parse_text_block`；0x07 解密后走子块。
4. side 用 `LayerMapper::castSide` / `castPinSide`。
5. `scale = 10000`；`boardSymmetry = true`。
6. 不平移。结构完整且 `error_msg` 空则 `valid = true`。
7. `num_parts` / `num_pins` / `num_format` / `num_nails` 照旧。

未识别 type：`SDL_LogWarn` + skip。

## 错误处理

| 情况 | 行为 |
|------|------|
| 截断、`read_uint32_t` 越界、非法 net_size、DES 后 `part_size` 越界 | `ENSURE_OR_FAIL` → `error_msg`，`valid` false |
| 密钥非法 | 现有 Invalid Key |
| 单条 track/via/arc/text/pin 字段不够或 net 缺失 | `SDL_LogWarn`，丢这一条，继续 |
| json 有、pcb 没有（无 position 的 via/arc；json 独有的 pad.angle/diode） | 验收时记录，不是运行时错误 |
| 未知主块 type | warn + skip |

## 文件

| 路径 | 动作 |
|------|------|
| `src/openboardview/FileFormats/XzzLayers.h` | 新建。`PCB_LAYER_ID`、`LayerMapper`。无静态 mapper |
| `src/openboardview/FileFormats/XJsonFile.cpp` | 删本地 LayerMapper，include 共享头 |
| `src/openboardview/FileFormats/XZZPCBFile.h` | `parse_via_block` / `parse_text_block`；删除 translation API |
| `src/openboardview/FileFormats/XZZPCBFile.cpp` | 按上表改 parse_* 和构造函数 |

CMake 不用改。

## 验收

基准：`compare/Switch OLED-HEG-CPU-01 PCB layer.{pcb,json}`。

1. 打开 pcb：`tracks` ≈ 26334（26438−104 条 layer28）或含 silk 的非 28 全进 tracks；`vias.size()==3547`；`arcs` 为非 28 的弧（该对 0 条铜弧，74 条 layer28 进 outline）；`pins.size()==4424`；pin.size 非 0。
2. 与 json：顶层非 28 track 条数一致；via 3547；pin 4424；layer28 轮廓非空。允许 json 多 1 条无坐标 via/arc。
3. 回归：非法 key；损坏 net_size 不崩。
4. 肉眼：BoardView 打开该 pcb，走线/过孔按层显隐，焊盘矩形可见。pad 旋转与 json 不一致可接受（二进制无角）。

不新建测试框架。不提交 `compare/` 夹具，除非用户要求。

## 风险

- json 劫持可含 PCB 没有的字段（pad.angle、diode、无坐标 via/arc）。PCB 多出来的块照填。
- `part.format` 非 4 点时不画 special outline。
- 取消平移后，旧 pcb 屏幕原点会变。
- 本布局来自一块 Switch OLED 板。其它板 type/size 不同则按 `size` 截断解析，缺字段 skip，不要假设永远 28/32/69。
