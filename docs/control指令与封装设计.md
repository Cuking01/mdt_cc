# `control` 指令与封装设计

本文根据仓库内 Mindustry 源码（`LAccess`、`LStatements.ControlStatement`、`LExecutor.ControlI` 及各类建筑的 `control` 实现）整理 `control` 指令。本文只讨论建筑控制，不包含隐式控制当前单位的 `ucontrol`。

## 1. 汇编语法与执行前提

Mindustry 逻辑汇编语法为：

```text
control <子命令> <目标建筑> <参数...>
```

例如：

```text
control enabled switch1 false
control shoot turret1 100 80 true
control shootp turret1 unit1 true
control config router1 2
control color light1 %ff0000ff
```

参数顺序以 `LAccess.params` 为准，目标位于子命令之后。`ControlI` 的运行时限制如下：

* 目标必须是 `Building`；普通逻辑处理器还必须能访问该建筑（通常是本处理器的有效链接），特权处理器可绕过链接限制。
* 目标无效、不是建筑、未链接或建筑不接受该控制时，指令通常静默无操作，没有成功返回值。
* `enabled` 会额外维护逻辑禁用来源；设为 `true` 会唤醒建筑，设为 `false` 会记录当前处理器为禁用者。
* 对象参数只有在运行时确实是对象时才调用对象重载；否则走数值重载。`config` 是否生效还取决于建筑的 `logicConfigurable`，并且不会把逻辑建筑自身的配置复制给另一个逻辑建筑。

## 2. 全部子命令

`LAccess.controls` 目前只有五项。

| 子命令 | 汇编参数 | 参数类型 | 源码语义与适用对象 |
|---|---|---|---|
| `enabled` | `control enabled building bool` | `bool`/数值 | 通用建筑控制。`false` 禁用建筑，`true` 启用建筑并调用 `noSleep()`。门（Door/AutoDoor）还会将其解释为开/关，并受门的冷却、阻挡和客户端条件限制。 |
| `shoot` | `control shoot building x y shoot` | `number, number, bool` | 主要由炮塔实现。设置炮塔目标坐标并设置逻辑开火状态；坐标是逻辑世界坐标（源码执行 `World.unconv`）。玩家控制的炮塔不会被该控制覆盖。 |
| `shootp` | `control shootp building unit shoot` | `unit/building 等对象, bool` | 主要由炮塔实现。目标对象需实现 `Posc` 才能提供目标位置；设置炮塔逻辑开火状态。玩家控制的炮塔不会被覆盖。 |
| `config` | `control config building value` | 对象或数值，但两条执行路径不同 | 对象路径由建筑基础实现处理，但目标必须声明 `logicConfigurable`；数值路径默认不处理 `config`，只有少数建筑自行覆盖（当前是 PayloadRouter）。建筑注册过 GUI 配置类型不代表能由该指令设置。完整清单见下文。 |
| `color` | `control color building packed_color`（底层为数值） | `packed_color` | 当前实际处理该控制的主要建筑是 `LightBlock`，把打包颜色写入灯的颜色。其他建筑通常忽略该控制；颜色值使用 Mindustry 的 packed color double 表示。 |

`shoot` 与 `shootp` 的最后一个参数都叫 `shoot`，是布尔开火开关，不是目标对象。`shootp` 的对象参数在炮塔实现中必须是 `Posc`；不满足时不会产生目标位置更新。

## 3. 建筑支持范围

`control` 指令本身不按建筑类型在编译期分派，最终由建筑的 `control` 方法决定是否响应：

* 所有建筑继承的基础实现可靠支持 `enabled`；具体建筑可以覆盖该行为（门就是例子）。
* 炮塔（`TurretBuild` 及其派生类）实现 `shoot`、`shootp`，并保留基础 `enabled/config` 行为。
* 任何声明 `logicConfigurable` 且注册了配置类型的建筑都可能支持 `config`。配置类型不是固定的 `control` 子命令枚举，必须以目标建筑的配置注册为准。
* `LightBlockBuild` 实现 `color`。其他建筑即使能感知 `color`，也不代表接受 `control color`。
* 某些建筑会复用 `config` 实现特殊行为，例如 PayloadRouter 把数值配置解释为方向；这属于建筑自己的配置语义。

因此高级语言不应承诺“任意 building 都能成功执行任意控制”。除 `enabled` 外，最好在文档和 debug 诊断中把控制视为可能静默失败的副作用操作。

### 3.1 `config` 的两条运行时路径

`LAccess.config` 被标记为对象型控制，但 `ControlI` 仍根据第一个实参当前是对象还是数值选择 Java 重载：

```text
对象值（包括内容对象、building、null）
    -> Building.control(config, Object, ...)
    -> 要求 target.block.logicConfigurable
    -> target.configured(null, value)

数值值（包括 int/bool/number）
    -> Building.control(config, double, ...)
    -> 建筑基础实现忽略 config
    -> 仅建筑自己的数值 control 覆盖可能响应
```

这带来三个容易误判的结论：

1. `config(Boolean.class, ...)` 或 `config(Integer.class, ...)` 只是注册建筑配置处理器，并不会使 `control config target true/1` 自动生效。
2. `Block.init()` 只在配置键是 `UnlockableContent` 子类时自动设置 `logicConfigurable=true`。原版中 `Item`、`Liquid`、`Block`、`UnitType` 满足；`Boolean`、`Integer`、`Point2`、数组、字符串和 `UnitCommand` 都不满足。
3. 某建筑只要因一个内容类型变得 `logicConfigurable`，对象路径就可以调用它注册的其他对象配置。例如 UnitFactory 因注册 `UnitType` 而开放逻辑配置，随后理论上也能接受 `UnitCommand`；但数值 `Integer` 仍不会走对象路径。

### 3.2 原版建筑配置总表

下表覆盖当前源码中全部 `Block.config(...)`/`configClear(...)` 注册。`可由 control config` 指原生逻辑指令直接构造该值并使目标处理它，不等同于玩家 UI 或蓝图配置。

| 建筑/建筑族 | 注册的 Java 配置类型 | 配置语义 | `logicConfigurable` | `control config` 结论 | 高级语言建议 |
|---|---|---|---|---|---|
| Door（普通门、大门；AutoDoor 另有自动逻辑） | `Boolean` | 开门/关门，连锁门同步；关闭会检查单位阻挡 | 否 | `config true` 数值路径无效；应使用 `control enabled`，门的覆盖实现还受 80 tick 冷却等限制 | 已由 `building.enable(bool)` 覆盖，不再封装 config |
| SwitchBlock（开关、world switch） | `Boolean` | 设置开关状态 | 否 | `config` 无效；`enabled` 直接设置/读取状态 | 已由 `enable/get_enabled` 覆盖 |
| UnitFactory（海陆空工厂、Erekir fabricator） | `Integer`、`UnitType`、`UnitCommand`、clear | 整数选生产计划索引；单位类型查找对应计划；命令设置出厂单位命令；clear 只清命令 | 是（由 `UnitType` 触发） | `UnitType` 对象有效；`UnitCommand` 对象若能取得也有效；整数无效；`null` 只清命令，不清生产计划 | `set_production(unit_kind)`、`clear_unit_command()`；计划索引和命令设置暂不暴露 |
| Reconstructor（所有重构厂） | `UnitCommand`、clear | 设置/清除单位命令 | 否 | 全部无法通过 `control config` 设置 | 暂缓；除非游戏以后显式开放 `logicConfigurable` |
| UnitCargoUnloadPoint | `Item`、clear | 选择要卸载的物品，null 表示任意/无筛选 | 是 | `Item` 和 `null` 有效 | `set_unload_item(item)`、`clear_unload_item()` |
| ItemSource | `Item`、clear | 沙盒物品源选择输出物品 | 是 | `Item` 和 `null` 有效 | `set_output_item(item)`、`clear_output_item()` |
| LiquidSource | `Liquid`、clear | 沙盒液体源选择输出液体 | 是 | `Liquid` 和 `null` 有效 | `set_output_liquid(liquid)`、`clear_output_liquid()` |
| Sorter（正向/反向分拣器） | `Item`、clear | 选择分拣物品并刷新显示缓存 | 是 | `Item` 和 `null` 有效 | `set_sort_item(item)`、`clear_sort_item()` |
| DuctRouter / StackRouter | `Item`、clear | 选择分拣物品；空值恢复普通路由行为 | 是 | `Item` 和 `null` 有效 | `set_sort_item(item)`、`clear_sort_item()` |
| DirectionalUnloader（duct unloader） | `Item`、clear | 限定卸载物品；空值轮询所有物品 | 是 | `Item` 和 `null` 有效 | `set_unload_item(item)`、`clear_unload_item()` |
| Unloader | `Item`、clear | 限定从相邻库存/核心卸载的物品 | 是 | `Item` 和 `null` 有效 | `set_unload_item(item)`、`clear_unload_item()` |
| LandingPad | `Item`、clear | 设置接收物品 | 是 | 对象路径有效，但只接受建筑可访问、当前星球存在且已解锁的物品；clear 也要求建筑可访问 | `set_delivery_item(item)`、`clear_delivery_item()`；运行时可能静默拒绝 |
| PayloadSource | `Block`、`UnitType`、clear | 选择无限生成的方块或单位；切换时清当前 payload/进度 | 是 | `Block`、`UnitType`、`null` 有效；内容还必须通过尺寸、可见性、禁用和环境检查 | `set_payload_kind(block/unit_kind)`、`clear_payload_kind()` |
| PayloadRouter | `Block`、`UnitType`、clear；另覆盖数值 `control config` | 选择要直行的 payload 类型；数值 control 直接设置旋转方向 `mod 4` 并锁定自动旋转 6 秒 | 是 | 内容对象和 null 有效；这是当前唯一明确响应数值 `control config` 的建筑 | `set_straight_payload(block/unit_kind)`、`clear_straight_payload()`、`set_rotation(int)` |
| Constructor（含 large constructor） | `Block`、clear | 选择制造配方；配方必须满足尺寸、环境、禁用与过滤条件 | 是 | `Block` 和 `null` 有效 | `set_recipe(block)`、`clear_recipe()` |
| ItemBridge 族（phase conveyor、BufferedItemBridge、LiquidBridge） | `Point2`、`Integer` | `Point2` 是相对 tile 偏移；整数是打包后的绝对 tile 位置 | 否 | 对象/数值均无效 | 暂缓；`point` 结构体不是 Java `Point2` 对象，不能直接传递 |
| MassDriver | `Point2`、`Integer` | 设置接收端链接；相对点会转为绝对打包位置 | 否 | 无效 | 暂缓 |
| PayloadMassDriver | `Point2`、`Integer` | 设置 payload 接收端链接 | 否 | 无效 | 暂缓 |
| PowerNode 族（普通/大节点、surge tower、LongPowerNode、PowerSource） | `Integer`、`Point2[]` | 整数切换一个绝对位置链接；点数组整体替换链接集合 | 否 | 无效 | 暂缓；复合点数组也无法作为单个 mlogic 值构造 |
| LightBlock（illuminator） | `Integer` | GUI 配置使用 RGBA 整数 | 否 | `control config` 无效；该建筑另行实现 `control color` packed double | 使用 `set_color(packed_color)`，不暴露 config integer |
| LogicBlock（所有处理器/world processor） | `byte[]`、`String`、`Character`、`Integer` | 写入程序、文本或链接位置等内部配置 | 否 | 目标不开放逻辑配置；此外对象路径特意禁止以 `LogicBuild` 作为配置复制源 | 不封装，避免自修改/配置复制卡顿 |
| CanvasBlock | `byte[]` | 写入画布压缩像素数据 | 否 | 无效 | 暂缓；字节数组不是 mlogic 可构造对象 |
| MessageBlock（普通/强化/world message） | `String` | 设置消息文本 | 否 | 无效 | 不映射到 `control config`；消息显示应继续使用 print buffer/printflush |

#### UnitFactory 为什么注册三种配置类型

`UnitFactory` 同时保存两类互相独立的状态：

* `currentPlan`：当前生产计划的索引，决定生产哪一种单位；
* `command`：新生产单位离开工厂后采用的单位命令。

因此它注册的三种配置值并不是同一字段的三个等价重载：

| 配置值 | 修改的状态 | 具体语义 | 普通逻辑能否直接传入 |
|---|---|---|---|
| `Integer` | `currentPlan` | 把整数当作 `plans` 的索引；越界变成 `-1`，即不选择生产计划；切换后进度归零 | 不能。`control config factory 2` 走数值 `control` 重载，而 UnitFactory 没有处理这个数值分支 |
| `UnitType` | `currentPlan` | 在 `plans` 中查找生产该单位类型的计划，再设置对应索引；找不到时得到 `-1`；切换后进度归零 | 可以，只要逻辑程序持有一个 `UnitType` 对象 |
| `UnitCommand` | `command` | 设置新生产单位的默认命令，不改变生产计划 | 分派机制允许，但普通 mlogic 当前没有方便取得 `UnitCommand` 对象的公开 lookup/全局常量，因此实际上难以使用 |
| `null` / clear | `command` | 只把命令清空 | 可以，但不会清除 `currentPlan`，也不会停止当前生产计划 |

这里还有两个名字相近但用途不同的开关：构造函数中的 `configurable=true` 表示建筑可以通过玩家 UI、蓝图或常规配置系统修改；`logicConfigurable` 才决定对象形式的 `control config` 能否进入 `configured(...)`。`Block.init()` 发现 UnitFactory 注册了继承自 `UnlockableContent` 的 `UnitType`，于是自动把 `logicConfigurable` 打开。这个结果也使同一张配置分派表中的 `UnitCommand` 对象路径可达，但不会使数值 `Integer` 路径自动可达。

直接逻辑调用的实际路径可概括为：

```text
control config factory unitTypeObject
    -> 对象 control 重载
    -> configured(UnitType)
    -> 查找对应计划并写 currentPlan

control config factory 2
    -> 数值 control 重载
    -> UnitFactory 没有处理 config
    -> 静默无操作

control config factory null
    -> 对象 control 重载
    -> configured(void/null)
    -> 只清 command
```

还有一条间接路径：`control config targetFactory sourceFactory` 会先读取源建筑的 `config()`。UnitFactory 的 `config()` 返回 `currentPlan` 整数，因此把一个工厂的配置复制给另一个工厂时，整数处理器会被内部递归分派命中，从而复制生产计划索引。这不等价于直接传数值，而且不会复制 `command`。

编译器接口为：

```cpp
factory.set_production(unit_kind_value);          // 直接、安全地选择能生产该单位的计划
factory.copy_configuration_from(other_factory);  // 运行时复制计划索引，目标/来源类型不匹配时可能无操作
factory.clear_unit_command();                     // 只清单位命令
```

不应提供 `factory.configure(int planIndex)`，因为它会生成在 UnitFactory 上静默无效的数值 `control config`。`UnitCommand` 接口也应等到语言能够可靠取得相应游戏对象后再开放。

### 3.3 清除配置与建筑复制

`configClear(...)` 实际注册的是 `void.class`。对象模式的 `null` 传给 `configured` 时会选择这个处理器，所以对于已经开放逻辑配置的目标，可以生成：

```text
control config target null
```

但清除语义由建筑自行决定。例如 UnitFactory 的 clear 只清单位命令，并不取消生产计划。高级语言为每种语义提供显式成员函数：

```cpp
sorter.clear_sort_item();
constructor.clear_recipe();
factory.clear_unit_command();
```

这些函数都生成 `control config target null`，但名称明确说明目标建筑会清除什么，也不要求用户拥有通用 nullable 内容类型。

对象参数还可以是另一个 `building`。`Building.configured` 会读取源建筑的 `config()`，再按真实配置类型应用到目标，因而下面这种“复制配置”在运行时存在：

```text
control config target sourceBuilding
```

限制如下：

* 目标仍必须 `logicConfigurable`。
* 源建筑当前配置必须非 null；null 不会转化为 clear。
* `LogicBuild` 源被明确拒绝，避免复制逻辑程序导致极端卡顿。
* 配置类型必须恰好被目标注册；不匹配时静默无操作。

编译器将这条弱类型运行时路径命名为 `target.copy_configuration_from(source)`，避免与各个专用选择接口混为一谈。

### 3.4 原生值与内部复合值

从 mlogic 的角度可把配置值分为三类：

| 类别 | 值 | 可行性 |
|---|---|---|
| 可直接存在于单个逻辑变量中的对象 | `Item`、`Liquid`、`Block`、`UnitType`、`Building`、`null` | 指令层可直接传递；本编译器需要新增前四种内容类型及 null/clear 接口 |
| 可直接存在于单个逻辑变量中的数值 | `Boolean`、`Integer`、packed number | 能传入指令，但建筑基础数值重载不做通用 config；不能仅凭 Java 注册类型就封装 |
| Java 内部复合/不可构造对象 | `Point2`、`Point2[]`、`byte[]`、`UnitCommand`（当前无全局常量和 lookup）、配置字符串/字符 | 普通 mlogic 程序无法从 `point` 结构体等值构造出相应 Java 对象；应暂缓或改用专用游戏指令/API |

当前 `lookup` 的公开可查集合只有 block、unit、item、liquid、team，`GlobalVars` 的全局内容常量也只直接注册 item、liquid、block、unit type 等常用内容。虽然 `ContentType` 枚举包含 `unitCommand`，当前 `GlobalVars.lookupContent` 的公开白名单并不包含它，不能据此声称命令对象可由普通逻辑获得。

### 3.5 对当前编译器的落地顺序

当前编译器已经实现 `item`、`liquid`、`block`、`unit_kind`、`team`、对应原版 `@` 内容常量、五类类型化 `lookup`，以及上表所有普通逻辑可达的专用配置接口。另提供 `copy_configuration_from(building)` 表达游戏已有的建筑配置复制路径。

仍暂缓 `Point2`/数组/字节数组/字符串程序配置和 `UnitCommand`：前几种无法从普通 mlogic 值构造，`UnitCommand` 则没有可靠的公开常量或 lookup。`point` 只是展开为两个数值槽的语言结构体，并不是 Mindustry Java `Point2` 对象。

## 4. 类 C 封装建议

初期不引入命名空间，常用操作直接使用短名称。`building` 是已有内置句柄类型。

### 4.1 适合成员函数的接口

已有接口保持不变：

```cpp
building.enable(bool value);       // control enabled
bool building.get_enabled();       // sensor @enabled，不是 control 子命令
```

本轮已实现：

```cpp
void building.shoot(point target, bool enabled); // control shoot
void building.shootp(posc target, bool enabled); // control shootp
void building.set_color(packed_color value);     // control color
```

配置接口按实际动作命名，不提供通用 `configure`：

```cpp
void building.set_production(unit_kind value);
void building.clear_unit_command();
void building.set_output_item(item value);
void building.clear_output_item();
void building.set_output_liquid(liquid value);
void building.clear_output_liquid();
void building.set_sort_item(item value);
void building.clear_sort_item();
void building.set_unload_item(item value);
void building.clear_unload_item();
void building.set_delivery_item(item value);
void building.clear_delivery_item();
void building.set_payload_kind(block value);
void building.set_payload_kind(unit_kind value);
void building.clear_payload_kind();
void building.set_straight_payload(block value);
void building.set_straight_payload(unit_kind value);
void building.clear_straight_payload();
void building.set_rotation(int value);
void building.set_recipe(block value);
void building.clear_recipe();
void building.copy_configuration_from(building source);
```

这些名称表达预期建筑语义，但当前接收者静态类型仍是 `building`，所以目标是否真是对应建筑、是否链接以及配置是否被运行时接受，仍由游戏决定。`set_rotation(int)` 只用于 PayloadRouter；不提供对其他建筑静默无效的通用数值配置。

### 4.2 不提供通用配置入口

编译器不提供 `building.config(...)`、`configure(building, ...)` 或任意字符串形式的底层 `control(...)`。这些入口会掩盖不同建筑完全不同的配置语义，也容易让无效的数值配置看起来普遍可用。专用成员名至少能表达调用者预期的运行时行为，并保留参数数量和静态类型检查。

### 4.3 暂缓或不建议承诺的接口

* 不提供通用 `can_control` 结果：源码没有对应的 `control` 成功返回值，链接和建筑能力检查也不是一个稳定的单条指令语义。
* 不提供通用数值 `configure(building, number)`；它看起来普适，实际仅 PayloadRouter 的特殊覆盖会响应。
* 不按每种建筑静态细分类型；先对四种可直接获得的内容句柄做重载，运行时仍可能因目标建筑或内容限制而静默失败。
* `color` 只针对 `LightBlock` 作为专用接口；不要与绘图指令的 `set_packed_color` 混淆。
* `shootp` 的静态类型只保证参数能表示 `Posc`；对象是否仍然有效、目标炮塔是否接受控制，继续由游戏运行时决定。

## 5. 编译器映射示例

```cpp
switch1.enable(false);
turret1.shoot(point{100, 80}, true);
turret1.shootp(conveyor1, true);
router1.set_rotation(2); // PayloadRouter 专用数值控制
illuminator1.set_color(pack_color(255, 0, 0, 255));
```

对应的核心汇编顺序分别是：

```text
control enabled switch1 false
control shoot turret1 100 80 true
control shootp turret1 conveyor1 true
control config router1 2
control color illuminator1 %ff0000ff
```

编译器应在生成阶段检查参数数量和静态类型；建筑是否链接、是否支持某控制以及目标对象是否有效，留给 Mindustry 运行时处理。debug 模式可在无法静态确认时输出提示，但不应伪造成功结果。
