# Mindustry Logic `@` 表达式实质与名称清单

本文基于仓库内 Mindustry 源码整理，重点说明 `@copper` 一类 token 在编译器和处理器运行时中的实际含义。

## 1. 结论

`@copper` 不是 C/C++ 风格的取地址运算，也不是字符串字面量。它是一个以 `@` 开头的 **Logic 全局变量名**。编译器解析操作数时先按完整字符串查询 `Vars.logicVars`；如果命中，就直接取得一个 `LVar` 常量。`@copper` 对应的 `LVar` 保存的是游戏内容对象 `Items.copper`（类型为 `Item`），并标记为 `constant`。

因此应区分以下概念：

| 写法 | 实质 |
| --- | --- |
| `@copper` | `Item` 内容常量，值为 `Items.copper` 对象 |
| `copper` | 普通变量名（除非程序自行定义） |
| `"copper"` | 字符串 `copper` |
| `0`、`1` | 数字常量 |

`@` 本身没有独立的运行时运算语义；真正的语义由全局变量表中该名字绑定的值决定。

## 2. 从源码到运行时

### 2.1 解析

`LAssembler.var(symbol)` 的第一步是 `Vars.logicVars.get(symbol, privileged)`。命中全局表后立即返回，不再尝试数字或字符串解析。[`mindustry/core/src/mindustry/logic/LAssembler.java:59`](../mindustry/core/src/mindustry/logic/LAssembler.java#L59)

### 2.2 注册

全局表通过 `GlobalVars.init()` 建立：

- 遍历 `Vars.content.items()`，注册 `@` + 物品名；
- 遍历液体、建筑、单位、天气、状态效果并注册对应名字；
- 注册传感器枚举、颜色、对齐方式等非内容常量。[`mindustry/core/src/mindustry/logic/GlobalVars.java:108`](../mindustry/core/src/mindustry/logic/GlobalVars.java#L108)

注册对象时，`LVar` 的 `isobj` 为真、`objval` 为对象、`constant` 为真。[`mindustry/core/src/mindustry/logic/GlobalVars.java:272`](../mindustry/core/src/mindustry/logic/GlobalVars.java#L272)

### 2.3 指令消费

操作数不会统一转换成数字，而是由具体指令决定如何使用：

- `sensor ... @copper`：将对象作为 `Content` 传给 `Senseable.sense()`，得到数量或属性；
- `print @copper`：识别 `MappableContent`，打印其内容名 `copper`；
- `draw image ... @copper`：将内容类型和 ID 编码到显示命令；
- `lookup item <logic-id>`：按逻辑 ID 反查内容对象。

内容表 ID 和 Logic ID 不等价；源码专门维护了二者的映射，不能把 `@copper` 固化为某个数字。[`mindustry/core/src/mindustry/logic/GlobalVars.java:236`](../mindustry/core/src/mindustry/logic/GlobalVars.java#L236)

### 2.4 常量性

`set` 指令会拒绝以常量为目标，因此 `set @copper 123` 不会改变该绑定。[`mindustry/core/src/mindustry/logic/LExecutor.java:832`](../mindustry/core/src/mindustry/logic/LExecutor.java#L832)

## 3. 名字的完整集合

不存在脱离游戏版本、地图和模组的永久固定列表。完整集合应理解为下面这些集合的并集：

```text
固定内建名
∪ 当前内容注册表生成的 @<content.name>
∪ 当前资源生成的 @sfx-<sound-name>
∪ 当前 logicids.dat 生成的 @<type>Count
∪ 模组/数据补丁注册的 @<unlockable-content.name>
```

### 3.1 固定内建名

以下名字由 `GlobalVars.init()` 直接注册：

| 类别 | 名字 | 值/说明 |
| --- | --- | --- |
| 数学 | `@pi`, `@e`, `@degToRad`, `@radToDeg` | 数学常量 |
| 时间/地图 | `@time`, `@tick`, `@second`, `@minute`, `@waveNumber`, `@waveTime`, `@mapw`, `@maph` | 处理器运行时动态更新 |
| 等待/网络 | `@wait`, `@server`, `@client` | `@wait` 为特权等待变量；网络状态为数字 |
| 客户端（特权） | `@clientLocale`, `@clientUnit`, `@clientName`, `@clientTeam`, `@clientMobile`, `@clientMusicPlaying`, `@clientCurrentMusic` | 非特权处理器读取时按权限返回 `null` 常量 |
| 控制枚举 | `@ctrlProcessor`, `@ctrlPlayer`, `@ctrlCommand` | 控制目标类型数字 |
| 特殊链接 | `@this`, `@thisx`, `@thisy`, `@links`, `@ipt` | 处理器自身、坐标、链接和输入端相关变量 |
| 颜色 | `@color<颜色名>` | 来自 Arc 颜色表，如 `@colorRed`、`@colorWhite`；颜色名集合随颜色表变化 |
| 音效 | `@sfx-<文件名>` | 从已加载音频资源生成，值为音效 ID |
| 对齐 | `@center`, `@top`, `@bottom`, `@left`, `@right`, `@topLeft`, `@topRight`, `@bottomLeft`, `@bottomRight` | 文本/绘图对齐枚举 |

另外，`false`、`true`、`null` 是全局名字，但它们不带 `@`。

### 3.1.1 处理器实例级保留名

下列名字由汇编器或处理器构造过程注入，生命周期依赖具体处理器实例，不完全等同于 `GlobalVars.init()` 注册的全局常量：

| 名字 | 说明 |
| --- | --- |
| `@counter` | 当前指令地址；由汇编器创建的可写数值变量 |
| `@unit` | 当前受控单位；对象常量槽，由处理器执行时更新 |
| `@this` | 当前处理器/逻辑块引用 |
| `@links` | 当前处理器链接数量，部分逻辑块在加载时注入 |
| `@ipt` | 每 tick 指令预算，处理器实例注入 |
| `@thisx`, `@thisy` | 处理器所在位置的逻辑坐标，逻辑块实例注入 |
| `@queries` | 特权查询结果容器；仅特权处理器创建 |

因此，若要实现完整的符号解析，除了全局表，还必须考虑汇编器和处理器实例向变量表注入的这些名字。

### 3.2 传感器名字（全部）

以下每个 `LAccess` 枚举都会注册为 `@` + 枚举名：

```text
@totalItems @firstItem @totalLiquids @totalPower @itemCapacity
@liquidCapacity @powerCapacity @powerNetStored @powerNetCapacity
@powerNetIn @powerNetOut @ammo @ammoCapacity @currentAmmoType
@memoryCapacity @health @maxHealth @heat @shield @armor @efficiency
@progress @timescale @rotation @x @y @velocityX @velocityY @shootX @shootY
@cameraX @cameraY @cameraWidth @cameraHeight @displayWidth @displayHeight
@bufferSize @operations @size @solid @dead @range @shooting @boosting
@mineX @mineY @mining @buildX @buildY @pingX @pingY @pingText @building
@breaking @speed @team @type @flag @flying @controlled @controller @name
@payloadCount @payloadType @totalPayload @payloadCapacity @maxUnits @id
@selectedBlock @selectedRotation @bulletLifetime @bulletTime
@enabled @shoot @shootp @config @color
```

这些名字的值通常是 `LAccess` 枚举对象；它们不是物品、建筑等内容对象。

### 3.3 内容名字（当前内容注册表决定）

注册规则如下：

| 来源 | 生成规则 | 例子 |
| --- | --- | --- |
| 阵营 | `@<team.name>` | `@sharded`, `@crux` |
| 物品 | `@<item.name>` | `@copper`, `@lead`, `@silicon` |
| 液体 | `@<liquid.name>` | `@water`, `@slag` |
| 建筑 | `@<block.name>`，若同名物品已存在则跳过建筑 | `@router`, `@duo` |
| 单位类型 | 非 internal 单位使用 `@<unit.name>` | `@dagger`, `@flare` |
| 天气 | `@<weather.name>` | `@rain`, `@snowing` |
| 状态效果 | `@status-<effect.name>` | `@status-burning` |

这部分不能用一份跨版本常量表替代：新版本、模组和数据补丁都可以增加内容。数据补丁会把 `UnlockableContent` 注册成 `@` 名字。[`mindustry/core/src/mindustry/mod/DataPatcher.java:194`](../mindustry/core/src/mindustry/mod/DataPatcher.java#L194)

### 3.4 Logic ID 计数名字

如果 `logicids.dat` 存在，以下四类会生成计数常量：

```text
@itemCount
@blockCount
@unitCount
@liquidCount
```

数值是逻辑 ID 表的数量，不是内容表中所有对象的数量。[`mindustry/core/src/mindustry/logic/GlobalVars.java:160`](../mindustry/core/src/mindustry/logic/GlobalVars.java#L160)

## 4. 对 mdt_cc 的实现建议

建议将 `@` token 建模为“全局符号引用”，解析结果携带明确类型：

```text
BuiltinNumber       // @time, @pi
SensorAccess        // @health, @dead
ContentConstant<T>  // @copper -> Item
EnumConstant        // @center, @ctrlProcessor
SoundId             // @sfx-xxx
RuntimeObject       // @this, @clientUnit
```

后端输出时保留原始 `@name` 形式；只有目标指令明确要求数值、内容类型或 Logic ID 时，才进行相应转换。不要在编译期把 `@copper` 降级为字符串或固定整数。

### 4.1 当前实现状态

mdt_cc 当前已经实现第一阶段的内容常量：

```text
item       liquid       block       unit_kind       team
```

编译器根据当前仓库中的 Mindustry 内容注册表为原版 `@` 常量赋予精确类型，常量不可赋值，不同句柄类型之间没有隐式转换。允许同类型 `==`/`!=`，禁止算术和顺序比较。天气、状态效果、传感器、运行时变量、颜色、音效和对齐等其他 `@` 名字仍属于后续阶段，不能误当作内容句柄。

公开 lookup 映射为：

```cpp
block lookup_block(int logic_id);
unit_kind lookup_unit(int logic_id);
item lookup_item(int logic_id);
liquid lookup_liquid(int logic_id);
team lookup_team(int logic_id);
```

它们分别生成 `lookup block/unit/item/liquid/team`。无效 Logic ID 由游戏写入 `null`；Logic ID 与普通内容 ID 不等价。
