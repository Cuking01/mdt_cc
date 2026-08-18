# Mindustry 逻辑处理器指令集调研报告

## 1. 调研范围与结论摘要

本文以当前仓库中的以下源码为准：

- `mindustry/core/src/mindustry/logic/LStatements.java`：文本语句、参数和权限声明；
- `mindustry/core/src/mindustry/logic/LExecutor.java`：指令的实际运行语义；
- `mindustry/core/src/mindustry/logic/LVar.java`：运行时值、隐式转换和写值规则；
- `LAccess.java`、`LogicOp.java`、`ConditionOp.java`、`LUnitControl.java`、`LLocate.java`、`FetchType.java` 等枚举；
- `mindustry/core/src/mindustry/world/blocks/logic/LogicBlock.java`：每 tick 的执行调度；
- `ContentType.java`：`lookup` 使用的内容类型。

最重要的结论如下：

1. 一条逻辑指令的参数没有统一的运行时静态类型，执行器按需调用 `num()`、`bool()`、`obj()`、`team()` 等转换。高级语言不应原样继承这种宽松性，应在内置签名中表达更严格的输入和输出类型。
2. 指令失败行为并不统一：有的明确写 `null`，有的写 `0/false`，有的什么也不写并保留输出变量旧值。后一类不能直接包装成普通返回值，必须初始化输出或返回带成功标志的结果。
3. `sensor` 的结果类型不能只由 `LAccess` 决定，还取决于接收者的实际 Java 类型；同一属性可能对某类对象返回数值，对另一类对象不受支持并返回 `null`。需要建立“接收者类型 × 属性”的签名矩阵。
4. `radar`、`ulocate`、单位转移操作、`sync` 等存在缓存或限频。它们不是每执行一次都重新观察/产生副作用。
5. 所有语句在调度器眼中通常只占一个指令槽，即使内部执行了昂贵查询；`wait`、`stop` 和兼容模式下的 `message` 会 `yield`，中止当前 tick 的剩余执行额度。
6. 世界处理器指令由 `LStatement.privileged()` 显式限制。普通单位控制虽不是 world-only，仍受地图规则、队伍、可控性、距离和冷却等运行时条件约束。
7. 第一阶段内置 API 统一采用短小的自由函数，例如 `print(...)`、`lookup_item(...)`、`valid(unit)`、`health(unit)`、`control(building, ...)`；成员函数留待引用或借用模型明确后再评估。

## 2. 运行时值与强制转换

`LVar` 物理上是二态存储：数值态保存 `double`，对象态保存 `Object`（包括对象态的 `null`）。指令读取操作数时常发生以下隐式转换：

| 读取方式 | 数值态 | 非空对象 | `null` | 无效数值 |
| --- | --- | --- | --- | --- |
| `num()` | 原数值 | `1` | `0` | `0` |
| `numOrNan()` | 原数值 | `1` | `NaN` | `0` |
| `bool()` | `abs(x) >= 0.00001` | `true` | `false` | 依数值比较 |
| `obj()` | `null` | 原对象 | `null` | `null` |
| `team()` | 数值按队伍 ID 查找 | 仅接受 `Team` | `null` | 越界为 `null` |

`setnum(NaN/Infinity)` 不会保存无效浮点数，而会把变量改成对象态 `null`。这意味着除零、开方和对数等运算可能把数值结果变成 `null`。

对高级语言的直接影响：

- `number`、`int`、`bool`、`color` 可以共享底层数值表示，但源语言转换应显式、可诊断；
- 对象不能因为底层 `num()` 会转成 `1` 就自动参与算术；
- 可能产生非法浮点值的运算，应在类型或文档中标记 `may_null`，或提供 checked 版本；
- `set` 和 `select` 可复制任意运行时值，因此其高级类型应由赋值兼容性或分支公共类型决定。

## 3. 调度、循环与指令成本

### 3.1 基本调度

`LExecutor.runOnce()` 每次先检查 `@counter`：若它越过指令数组末尾或小于 0，就重置为 0；随后读取当前指令、先递增计数器，再运行指令。由此得到：

- 程序自然落到末尾后，会在下一次 `runOnce()` 从第 0 条重新开始；
- `end` 只是把计数器设到末尾，下一次额度仍可立即从头执行，不等价于 yield；
- `jump` 修改已提前递增的 `@counter`；
- 对 `main_loop` 的语义设计不能假定“一轮函数调用恰好对应一个游戏 tick”。短程序可在同一 tick 内执行多轮，长程序可跨多个 tick。

`LogicBlock` 使用累加器按 `ipt`（instructions per tick）调用 `runOnce()`。普通情况下，每次调用后累加器减 1。因此所有源码级指令名义成本都是一个调度额度，即使其 Java 实现进行世界查询、遍历或网络调用。编译器应另设“名义指令数”和“潜在运行开销”两个指标。

当前解析器/UI 使用 `LExecutor.maxInstructions = 1000` 作为单个程序的指令条数上限。`LogicBlock` 基类默认 `instructionsPerTick = 1`、可调整上限 `maxInstructionsPerTick = 40`；具体方块可覆盖这些值，仓库中的特权 world processor 配置可把上限设到 1000。因此 manifest/目标配置不能把某一个方块的 IPT 写死为全局常量，编译器应分别报告“程序总指令数”和目标处理器的执行速率。

### 3.2 yield 与停止

| 指令 | 行为 |
| --- | --- |
| `wait t` | `t <= 0` 时前进到下一条但 yield；`t > 0` 时反复把计数器退回自身并 yield，直到累计时间达到参数。时间按 `Time.delta / 60` 累积。 |
| `stop` | 计数器退回自身，设置 `yield` 和 `stop`；在方块处理器中表现为每 tick 停在此处。`stop` 字段还供一次性 `LogicScript` 执行器停止循环。 |
| `message ... @wait` | world 指令的向后兼容模式；当 UI 正忙时退回自身并 yield。普通成功输出变量则写 `false`，由用户重试。 |

调度循环发现 `yield` 后立即退出本 tick，且在累加器减 1 之前退出。实现 `main_loop` 时，若需要“一轮结束后让出本 tick”，应显式生成可 yield 的语义，而不能只生成 `end` 或回跳。

### 3.3 缓存、限频和缓冲区

- `radar`：建筑雷达约每 30 tick 更新；单位雷达使用 `LogicAI` 的目标定时器和执行缓存。重复执行可能返回旧目标。
- `ulocate`：结果按绑定单位和该指令实例缓存在 `LogicAI.execCache` 中；非更新时间返回缓存坐标、found 和建筑。
- 单位物品/载荷操作：使用全局按单位 ID 记录的转移冷却；冷却未到时整条操作无副作用。
- `sync`：单变量最多约 20 次/秒（`1000 / 20 ms`），且常量或没有 processor building 时不发送。
- 图形暂存缓冲最多 256 条命令；满后新的 `draw` 静默忽略。显示器自身命令队列最多 1024 条。
- 文本缓冲最多 400 字符；`print`/`format` 截断，`printchar` 满后忽略。部分 `draw print`、flush、message、marker 操作会清空缓冲。
- world marker 数量上限在实现中为 20000。

## 4. 普通处理器指令

下表中的“稳定写出”表示执行后输出变量一定被本次指令覆盖；“保留旧值”意味着高级封装必须特别处理。

### 4.1 赋值、运算和控制流

| 指令 | 输入 | 输出 | 失败/特殊语义 |
| --- | --- | --- | --- |
| `set dst src` | 任意值 | 与 `src` 同运行时种类 | `dst` 为常量时不写。普通变量稳定写出。 |
| `op kind dst a b` | 大多数运算读取数值；`equal/notEqual` 在双方都是对象时比较对象；`strictEqual` 检查数值/对象种类 | 数值，比较为 0/1 | 非法浮点结果经 `setnum` 变成 `null`。位运算先转 `long`。角度/三角函数使用角度制。`rand` 有随机副作用。 |
| `select dst cond a b` | 条件比较任意值；选择支可为任意值 | 复制 `a` 或 `b` | `dst` 常量时不写；普通变量稳定写出。适合 IR 的 phi/select，但两支需计算公共静态类型。 |
| `jump addr cond a b` | 地址为汇编期整数；条件见 `ConditionOp` | 无 | `addr == -1` 不跳。对象等值仅在 `equal/notEqual/strictEqual` 的对应规则下生效。 |
| `end` | 无 | 无 | 把计数器置于末尾；下一次执行从头开始，不 yield。 |
| `wait` | 数值秒 | 无 | 见调度章节；状态保存在该指令实例的 `curTime`。 |
| `stop` | 无 | 无 | 永久停留并逐 tick yield，除非代码/状态重载。 |
| `setrate` | 整数 | 修改 processor `ipt` 和内置 `@ipt` | 按普通/特权处理器允许范围 clamp；没有 building 时无效。 |
| `noop` | 无 | 无 | 无操作。无效语句也构造为 noop。 |

`LogicOp` 当前包括：四则、整除、两种模、幂、宽松/严格比较、逻辑与、移位和位运算、`min/max`、角度、向量长度、噪声、绝对值、符号、对数、取整、平方根、随机数及三角函数。`ConditionOp` 包括 `equal`、`notEqual`、大小比较、`strictEqual` 和 `always`。

当前语言将四则、整数余数、比较和逻辑与映射为常规运算符。游戏支持小数的普通余数另有 `mod`。其余 `LogicOp` 使用无 `op_` 前缀的内置函数：`idiv`、`mod`、`emod`、`pow`、`strict_equal`、`shl`、`shr`、`ushr`、`bit_or`、`bit_and`、`bit_xor`、`bit_not`、`max`、`min`、`angle`、`angle_diff`、`len`、`noise`、`abs`、`sign`、`log`、`logn`、`log10`、`floor`、`ceil`、`round`、`sqrt`、`rand` 以及全部三角函数。`angle`/`len` 额外接受 `vec`，`noise` 额外接受 `point` 或 `vec`。

### 4.2 链接、内存和通用感知

| 指令 | 输入 | 输出 | 失败/权限语义 |
| --- | --- | --- | --- |
| `getlink dst index` | 整数索引 | `building?` | 越界稳定写 `null`。 |
| `read dst target pos` | `LReadable`、字符串或序列；位置可为整数，LogicBlock 还接受变量名字符串 | 动态 | 不可读、越界或不支持时通常稳定写 `null`；字符串越界通过 NaN 最终写 `null`。具体 `LReadable.read` 决定类型。 |
| `write value target pos` | `LWritable`、位置和值 | 无 | 目标不是可写对象或权限检查失败时静默无操作。 |
| `sensor dst from property` | 接收者对象；property 为内容或 `LAccess` | 数值或对象 | `from == null && property == dead` 写 1；支持的属性稳定写出；未知属性/接收者稳定写 `null`。远程单位/建筑也可感知。 |

普通 processor 对 LogicBlock 的读写还受 `readable(exec)` 约束：同队、非特权目标或当前执行器本身的权限规则。`control` 对建筑则要求目标是有效链接（world processor 例外）。

`LAccess` 只列出属性名，没有声明返回类型和适用接收者。例如 `health` 通常是数值，`dead` 是布尔语义，`team/type/controller/currentAmmoType/payloadType` 是对象，`firstItem` 的结果也依接收者而定。不能直接从该枚举自动生成可靠类型；必须扫描 `Senseable.sense/senseObject` 的实现，形成接收者专属表。

### 4.3 查找、雷达和内容

| 指令 | 输入 | 输出 | 失败/缓存语义 |
| --- | --- | --- | --- |
| `lookup kind dst id` | 内容类型和整数 ID | 对应内容对象 `T?` | 无效 ID 稳定写 `null`。公开种类应限于有实际内容类且 logic lookup 支持的类型。 |
| `radar ... source order dst` | `source` 必须是己方可用的 `Ranged` 建筑或可控单位 | 实际为 `unit?`（存为 `Healthc`） | 无目标或来源非法稳定写 `null`；结果按固定周期缓存。 |
| `uradar ... order dst` | 隐式使用当前 `@unit` | `unit?` | 与 `radar` 同一执行器；依赖已绑定、有效且可控制的单位。 |
| `ulocate kind ... outX outY found outBuild` | 隐式当前单位；ore 需要 `item`，building 还使用 flag/enemy | 坐标、`bool`、`building?` | 结果缓存。无有效绑定单位时只写 `found=false`，坐标和 building **保留旧值**；未找到时写 `found=false` 和 `outBuild=null`，坐标仍可能保留旧值。 |

`ContentType` 包含 item、block、bullet、liquid、status、unit、weather、sector、planet、team、unitCommand、unitStance 及若干 `*_UNUSED/error` 占位。高级语言 manifest 应以 `GlobalVars.lookupContent` 的实际支持集合为准，不能无条件暴露整个枚举。

### 4.4 建筑控制

`control access building p1...` 接受 `LAccess.controls`：

- `enabled(bool)`；
- `shoot(x, y, bool)`；
- `shootp(object, bool)`；
- `config(object-or-number)`；
- `color(number)`。

目标必须是建筑，普通处理器还必须把它作为有效链接。参数不匹配或目标非法时静默无操作，没有成功输出。`enabled(false)` 还会记录禁用来源；对象型控制仅在 `p1` 当前确为对象态时走对象重载。

高级 API 可先使用自由函数：`set_enabled(building, false)`、`shoot(building, x, y, true)`、`configure(building, value)`。这类 API 仍应在文档中标记“可能静默失败”，或提供可选的前置 `valid(building)`/link 检查。

### 4.5 单位绑定、查询和控制

| 指令 | 输入/输出 | 失败与副作用 |
| --- | --- | --- |
| `ubind unitType-or-unit` | 写内置 `@unit: unit?` | 类型绑定会轮询队伍缓存；无单位、不可控、异队或非法类型时稳定写 `null`。受 `logicUnitControl` 规则限制。 |
| `ucontrol ...` | 隐式读取 `@unit`；大多无输出 | 单位必须有效、同队/特权、可被 logic AI 控制。失败通常静默无操作。多数动作会接管/刷新 LogicAI 控制计时器。 |
| `ucontrol within ... result` | 输出 bool | 仅在当前对象确为 Unit 时写；无效绑定或权限失败时 **保留旧值**。 |
| `ucontrol getBlock ... type building floor` | 输出 `block?`、`building?`、`block?` | 有效单位但越界/无 tile 时三个输出稳定写 null；无效绑定时三个输出 **保留旧值**。 |
| `ulocate` | 见上一节 | 缓存且存在部分输出旧值问题。 |

`ucontrol` 子操作包括 idle/stop/move/approach/pathfind/autoPathfind、boost、target/targetp、itemDrop/itemTake、payDrop/payTake/payEnter、mine、flag、build/deconstruct、getBlock、within、unbind。

额外约束包括：

- build/deconstruct 还受 `logicUnitBuild`/`logicUnitDeconstruct` 规则、单位能力、方块可建造性和解锁状态限制；
- item/payload 转移受距离、队伍、容量、有效性和全局 transfer cooldown 限制；
- `targetp` 只接受实现 `Teamc` 的对象，否则清空目标但仍可设置 shoot；
- `build` 的 config 只保留 `Content` 或 `Building`，其他对象退化为 `null`；
- `unbind` 与 `within` 不要求创建 LogicAI，其余控制通常会接管单位控制器。

语言层不宜把任意 `unit` 对象直接假定为“当前受控单位”。自由函数可以显式接收 `bound_unit`/控制句柄；若允许 `move(unit, ...)` 自动生成 `ubind unit`，必须明确这会改变全局 `@unit` 状态。

### 4.6 文本、绘图和颜色

| 指令 | 输入/输出 | 状态副作用 |
| --- | --- | --- |
| `print value` | 任意值，无直接输出 | 追加到共享文本缓冲；对象按内容名、建筑块名、单位类型名等格式化；最多 400 字符。 |
| `printchar value` | 数值字符码或 UnlockableContent emoji | 追加字符；不支持的对象无操作。 |
| `format value` | 任意值 | 替换文本缓冲中编号最小的 `{0}`…`{9}` 占位符；无占位符则无操作。 |
| `printflush target` | 可打印建筑 | 尝试输出后无条件清空文本缓冲，即使目标错误。 |
| `draw kind ...` | 数值、颜色、内容图像等 | 向共享 graphics buffer 追加；headless 或缓冲满时忽略。`draw print` 消费并清空文本缓冲。 |
| `drawflush target` | 可绘制显示建筑 | 尝试 flush 后无条件清空 graphics buffer。 |
| `packcolor dst r g b a` | 0..1 数值（会 clamp） | 稳定写 packed color 数值。 |
| `unpackcolor r g b a color` | packed color | 稳定写四个 0..1 数值。 |

这些指令不是纯函数：文本和图形缓冲是执行器级隐式状态。高级语言提供 `draw(...)`、`print(...)`、`drawflush(display)` 等自由函数时，仍应在 IR 中保留缓冲区 effect，防止优化器错误重排或删除。

## 5. 特权/world processor 指令

以下语句在 `LStatements` 中 `privileged() == true`，只允许 world processor：

`query`、`getblock`、`setblock`、`spawn`、`bullet`、`status`、`weathersense`、`weatherset`、`spawnwave`、`setrule`、`message`、`cutscene`、`effect`、`explosion`、`fetch`、`sync`、`clientdata`、`getflag`、`setflag`、`setprop`、`playsound`、`playmusic`、`setmarker`、`makemarker`、`localeprint`。

### 5.1 查询、获取和修改世界

| 指令 | 输入 | 输出 | 失败/特殊语义 |
| --- | --- | --- | --- |
| `query shape type team x y w h` | rect/circle；unit/building（bullet 当前禁用） | 隐式写 `@queries` 序列 | 每次先清空结果；team 为 null 表示所有队伍。bullet 因池化旧引用问题直接 return，UI 可查询集合也已排除它。 |
| `getblock layer dst x y` | tile 坐标、floor/ore/block/building | floor/block 内容或 `building?` | 越界稳定写 `null`；building 层可空。 |
| `setblock layer block x y team rotation` | 可设置层仅 floor/ore/block | 无 | 客户端无操作；tile/类型非法无操作；层和 block 子类有额外检查，rotation clamp 到 0..3。 |
| `fetch kind dst team index extra` | team、索引；unit/build 可选内容类型过滤 | `unit?`、`building?` 或整数 count | team 非法时 **不写 dst，保留旧值**；合法时索引越界稳定写 null，count 稳定写数值。player 返回 Unit，block unit 时可能返回其 Building tile。 |
| `setprop property of value` | `Settable` 接收者；键为 `LAccess` 或内容 | 无 | 接收者/键不支持时静默无操作；值保留对象或数值种类。 |

`query` 结果通过特殊常量 `@queries` 暴露，后续 `read` 可按索引读取。高级 API 可把它抽象成只读、瞬时的 query result view，不应承诺普通可持久化容器语义。

### 5.2 生成、状态、天气与规则

| 指令 | 输出稳定性 | 主要副作用和限制 |
| --- | --- | --- |
| `spawn unitType ... result` | 成功写 `unit`；team/type/单位上限非法或客户端执行时 **保留旧 result** | 服务端创建单位，可选出生效果。安全封装应先清空结果。 |
| `bullet ... result` | 成功写 `bullet`；来源/武器/弹药非法时 **保留旧 result** | 从单位武器或各类 turret 推导 BulletType 并创建。owner 可空；team 可由 owner 推导，最终可退为 derelict。 |
| `status apply/clear ...` | 无 | 客户端无操作；仅 Unit + StatusEffect 生效；duration 转 tick。 |
| `weathersense weather dst` | 始终写 bool | 非 Weather 也写 false。 |
| `weatherset weather bool` | 无 | 激活、延长或收束天气状态。 |
| `spawnwave natural x y` | 无 | 客户端无操作；natural 直接触发正常下一波，否则在指定位置按波次组生成。 |
| `setrule rule value ...` | 无 | 修改全局或队伍规则，大量参数会 clamp；ban/unban 仅接受 Block/UnitType；mapArea 触发网络调用和世界边界更新。 |
| `explosion ...` | 无 | 客户端无操作；半径上限 100 logic units，负伤害被远程实现忽略。 |
| `effect ...` | 无 | 图形/世界效果；部分 rotation/size 被上限 1000 限制。 |

`spawn` 和 `bullet` 是最明确的“失败保留旧输出”实例，manifest 必须标记为 `write_on_success`，高级 API 应生成 `set result null` 后再执行，或返回 `{success, value}`。

### 5.3 UI、网络、声音和标记

| 指令 | 输出/缓冲 | 特殊语义 |
| --- | --- | --- |
| `message type duration success` | 先写 success=true；UI 忙时写 false，或对特殊 `@wait` 变量 yield；通常消费文本缓冲 | headless 下除 mission 外直接清空文本并返回。 |
| `cutscene action ...` | active/getHud 写 bool，其余无输出 | headless 时所有动作都不执行，因此 getter 输出保留旧值。 |
| `sync variable` | 无 | 每变量最多 20 Hz；通过网络同步 processor 变量，常量不发送。 |
| `clientdata channel value reliable` | 无 | channel 必须是 String；发送可靠/不可靠客户端逻辑数据。 |
| `getflag dst string` | bool 或 null | 字符串稳定写 bool；非字符串稳定写 null。 |
| `setflag string bool` | 无 | 仅状态发生变化时发网络调用。 |
| `playsound ...` | 无 | sound ID 非法退为 none；音量上限 2；支持位置/非位置模式和 limit。 |
| `playmusic name interrupt` | 无 | headless 无操作；找不到音乐可得到 null，null 语义为停止。 |
| `setmarker` | 无 | marker 不存在时无操作；部分操作消费文本缓冲；普通数值参数使用 `numOrNan()`，null 可作为 NaN 传入 marker 控制。 |
| `makemarker` | 无 | 类型存在、数量未满且 replace 条件满足时创建；否则静默无操作。 |
| `localeprint name` | 无直接输出 | 只对对象态名字执行，从地图 locale 取文本并追加；此路径未再次执行 400 字符截断，值得上层主动限制。 |

## 6. 输出可靠性总表

为便于编译器实现，可先按以下策略给指令标记输出模式：

| 模式 | 指令示例 | 高级封装策略 |
| --- | --- | --- |
| `always_write` | `getlink`、`lookup`、`sensor`、`radar`、`getblock`、`weathersense`、`getflag`、`packcolor` | 可直接作为表达式；按实际结果声明 `T` 或 `T?`。 |
| `write_on_valid_receiver` | `ucontrol within/getBlock`、部分 cutscene getter | 先初始化输出，或要求可证明接收者/运行环境有效。 |
| `write_on_success` | `spawn`、`bullet` | 先写 null，再发指令；高级返回 `T?`。 |
| `write_if_context_valid` | `fetch`（team 非法不写）、`ulocate`（无绑定时只写 found） | 包装器必须初始化所有输出；多输出用结构体表达。 |
| `side_effect_only` | `control`、多数 `ucontrol`、`write`、`setblock`、`setrule` | 返回 `void`，或另做带显式前置检查的 checked API；不能凭“指令已执行”推断成功。 |

不建议统一把底层所有失败都解释为 null，因为这会隐藏“旧值残留”这一真实行为。机器清单应忠实描述原始语义，高级 API 再选择是否付出额外指令获得稳定结果。

## 7. 对高级语言内置 API 的建议

### 7.1 内置接口形式

普通感知、内存和控制接口仍优先使用自由函数；对于高频且语义明确的建筑开关操作，编译器额外支持成员函数语法：

```cpp
bool active = switch1.get_enabled();
switch1.enable(false);
```

它们分别降低为 `sensor result switch1 @enabled` 和 `control enabled switch1 false`。这不是通用类/对象方法机制，只对编译器内置的 `building` 接收者和已登记的方法名生效。

### 7.2 第一阶段统一使用自由函数

常用全局动作保持短名称更适合游戏脚本，例如：

```cpp
print(value);
printflush(message1);
draw_clear(127, 127, 127);
item? value = lookup_item(id);
```

接收者作为普通的第一个参数传入，不需要放进命名空间：

```cpp
if (valid(unit)) {
    number hp = health(unit);
}

set_enabled(building, false);
control(building, ...);
number value = read(memory, index);
write(memory, index, value);
drawflush(display);
```

建议的映射范围：

- `valid(unit)` / `valid(building)`：可由 `sensor receiver @dead` 后取反实现；底层对 `null + @dead` 特判为 dead=true，因此该封装有稳定语义；
- `health(unit)`、`max_health(unit)`、`team(unit)`、`type(unit)`、`health(building)`、`item(building, ...)` 等：由第一个参数类型决定 `sensor` 签名；
- `set_enabled(building, ...)`、`shoot(building, ...)`、`configure(building, ...)`、`set_color(building, ...)`：映射 `control`；
- `read(memory, ...)`、`write(memory, ...)`：映射 `read/write`，但不同第一个参数类型可能对应不同结果；
- `drawflush(display)`、`printflush(message)`：标记为清空共享缓冲；
- 单位移动、采矿和建造等操作仅对当前绑定控制句柄直接映射 `ucontrol`。若允许自由函数对任意 `unit` 自动绑定，必须承认它会修改全局绑定状态。

这种设计只需要自由函数重载或不同名称，不引入隐含接收者、引用参数、特殊成员函数、对象布局或虚派发。

### 7.3 可空与动态有效性

- lookup、radar、getlink、getblock、fetch object、spawn、bullet 等应返回 `T?`；
- `valid()` 检查当前实体状态，不应等价于“静态非空”；游戏世界可在后续指令中变化；
- 对 `ulocate` 等多输出指令，建议返回结构：`{ bool found; number x; number y; building? building; }`，包装前先初始化全部字段；
- 对底层动态 `sensor` 保留 raw 入口，但正常代码使用按第一个参数类型重载的自由函数，避免任意 `raw` 扩散。

### 7.4 副作用分类

IR 至少应区分：

- pure numeric（大多数 `op`，但 `rand` 例外）；
- reads world（sensor、radar、lookup、getblock、fetch）；
- writes world（control、ucontrol、setblock、spawn、setrule 等）；
- reads/writes memory/link；
- reads/writes text buffer；
- reads/writes graphics buffer；
- scheduler barrier/yield；
- network/UI/headless-sensitive。

否则常量折叠、公共子表达式消除和指令重排会改变实际行为，尤其是缓存查询、随机数、flush 和控制指令。

## 8. 机器可读 instruction manifest 建议

建议先建立 YAML/JSON 清单，忠实描述原始指令，而不是直接把高级语法硬编码在类型检查器里。单个 variant 至少包含：

```yaml
name: spawn
variant: unit
privilege: world
operands:
  - {name: type, direction: in, type: unit_type}
  - {name: x, direction: in, type: number, unit: logic_coordinate}
  - {name: y, direction: in, type: number, unit: logic_coordinate}
  - {name: rotation, direction: in, type: number, unit: degree}
  - {name: team, direction: in, type: team}
  - {name: result, direction: out, type: "unit?"}
output_write: on_success
effects: [writes_world, server_only]
nominal_instruction_cost: 1
failure: preserve_output
```

推荐字段：

- `name`、`variant`、`opcode/enum`、`since_version`；
- `privilege`: normal/world/both，以及地图规则门控；
- operands 的 `direction`、静态类型、底层读取模式（num/bool/obj/team/raw）、可空性和单位；
- `operand_constraints`：Building、Ranged、Senseable、LReadable、当前绑定 Unit 等；
- `output_write`: always/on_success/on_valid_receiver/partial/none；
- `failure`: write_null/write_false/preserve_output/noop/yield；
- `effects`：world、unit control、network、UI、text buffer、graphics buffer、random；
- `scheduler`：yield、counter rewrite、cache period、throttle、cooldown；
- `environment`：server-only、client-only、headless behavior；
- clamp、坐标换算、角度制、缓冲上限；
- 名义指令成本和潜在昂贵操作提示；
- 高级 API 名称、第一个参数的类型约束、是否由包装器插入初始化指令。

`sensor` 不应只有一条动态记录，而应拆成按第一个参数类型区分的 overload，例如：

```yaml
- builtin: health
  lower: sensor
  first_operand: unit
  property: health
  result: number

- builtin: type
  lower: sensor
  first_operand: unit
  property: type
  result: unit_type

- builtin: valid
  lower_sequence:
    - sensor: {property: dead, result: tmp}
    - op: {kind: equal, lhs: tmp, rhs: false}
  first_operand: "unit? | building?"
  result: bool
```

最终可从 manifest 生成：编译器内置自由函数签名、参数诊断、文档、编辑器补全、原始语义测试和不同 Mindustry 版本兼容表。

## 9. 建议的后续调研

本报告已经覆盖顶层指令和执行器的关键失败/调度语义。下一步最有价值的源码梳理不是继续扩充指令名称，而是：

1. 扫描所有 `Senseable` 实现，建立 `receiver × LAccess -> result type/nullability` 矩阵；
2. 扫描 `LReadable/LWritable/Settable` 实现，确定 memory、processor、字符串、query 序列等读写签名；
3. 核对 `GlobalVars.lookupContent` 的实际内容种类和所有 `@` 内置对象类型；
4. 用小型游戏侧测试验证旧值残留、radar/ulocate 更新周期、wait 与 main loop 的 tick 边界；
5. 基于 manifest 先生成一版自由函数 API 草案，再确定最终源语言的 number/int/bool 和 nullable 规则。
