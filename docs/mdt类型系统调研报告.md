# Mindustry 逻辑处理器类型系统调研报告

## 结论摘要

当前逻辑处理器的变量并不是带有 Java/C++ 静态类型的槽位，而是一个动态值：

- 数值模式：`double`；
- 对象模式：一个 Java 对象引用，也可以是 `null`；
- 通过 `isobj` 标志区分两种模式。

因此，编译器不应把目标类型直接建模成“寄存器类型”或完整 C++ 对象类型。比较合适的语言模型是：

```text
number       数值
bool         布尔值，底层仍为 0/1
string       字符串对象
content      内容对象的抽象基类
item/block/... 具体内容类型
building     建筑实体
unit         单位实体
team         队伍
enum         指令参数枚举
null         空对象
```

其中对象类型应当由编译器静态检查，最终仍落到 Mindustry 的对象变量；不能把任意对象指针、地址和内存生命周期暴露给用户。

本文基于当前仓库中的 Mindustry 源码，重点证据包括：

- `mindustry/core/src/mindustry/logic/LVar.java`
- `mindustry/core/src/mindustry/logic/LAssembler.java`
- `mindustry/core/src/mindustry/logic/GlobalVars.java`
- `mindustry/core/src/mindustry/logic/LExecutor.java`
- `mindustry/core/src/mindustry/logic/LAccess.java`
- `mindustry/core/src/mindustry/logic/LogicOp.java`
- `mindustry/core/src/mindustry/logic/ConditionOp.java`
- `mindustry/core/src/mindustry/logic/ContentType.java`
- `mindustry/core/src/mindustry/logic/Senseable.java`

## 1. 变量的真实运行时表示

### 1.1 `LVar` 是二选一动态值

`LVar` 的关键字段为：

```text
boolean isobj
Object objval
double numval
boolean constant
```

当 `isobj == false` 时读取 `numval`；当 `isobj == true` 时读取 `objval`。这两个字段不是同时有效的两个联合字段，而是由 `isobj` 选择其中一种表示。

普通变量由 `LAssembler.putVar` 创建，默认是对象模式且对象值为 `null`。因此，未初始化变量在目标机上天然表现为空对象，而不是 C/C++ 意义上的未初始化数值。

### 1.2 数值实际是 `double`

逻辑运算的实现使用 Java `double`。整数操作只是通过转换或特定运算符模拟：

- `numi()` 使用 Java `int` 转换；
- 位移和位运算把数值转换为 `long`；
- `idiv` 使用 `floor(a / b)`；
- 普通除法使用浮点除法；
- 颜色也通过一个 `double` 的位模式传输。

因此源语言可以提供 `int` 和 `float` 语法，但第一版最好把它们都降低为 `number`，只在编译期做整数语义检查。若强行模拟 C++ 的固定宽度整数溢出，结果可能与处理器真实行为不一致。

### 1.3 非有限数会变成 `null`

`LVar.setnum` 遇到 `NaN` 或无穷时，会清空对象值并切换到对象模式。因此，以下结果不能按普通 IEEE 浮点值在程序中继续传播：

- 0 除以 0；
- 某些非法数学函数结果；
- 越界或无效感知产生的 `NaN`；
- 无效颜色/数值转换产生的非有限值。

语言层面应把它规定为“运行时空值”或“无效值转空值”，而不是承诺完整 C++ 浮点异常语义。

## 2. 转换和比较规则

### 2.1 数值转换

`LVar.num()`、`numf()` 和 `numi()` 对对象执行隐式转换：

- 非空对象转换为 `1`；
- 空对象转换为 `0`；
- 数值模式直接转换；
- `NaN`/无穷数值在普通读取中按无效数值处理，通常返回 `0`；
- 部分指令使用 `numOrNan()`，有意保留空值对应的 `NaN`。

这意味着 `if (object)` 在底层有意义，但对象和数字之间的隐式转换可能掩盖错误。建议语言规则为：

- 条件表达式允许 `bool` 和 `number`；
- 对象到布尔值可以允许，非空为真；
- 对象到 `number` 默认禁止，必须使用显式转换或专用 API；
- `null` 只能隐式转换为对应的可空对象类型。

### 2.2 比较规则

逻辑处理器至少存在三种不同的比较语义：

- `equal`/`notEqual`：数值近似比较；两个对象则使用对象相等比较；
- `strictEqual`：要求两侧都为同一模式，并分别比较数值或对象；
- `lessThan` 等：主要走数值转换。

因此源语言中的 `==` 不应直接假定等价于 C++ 的严格类型比较。建议：

- `==` 采用语言定义的安全比较；
- `===` 暴露处理器的严格模式比较；
- 对不同具体对象类型的比较在编译期报错，除非显式转换为 `object` 或 `nullable`。

## 3. 对象值的实际类别

### 3.1 内容对象

`ContentType` 中当前定义的主要内容类别包括：

| 逻辑类别 | 典型对象 | 备注 |
| --- | --- | --- |
| `item` | `Item` | 物品内容，支持查找、感知和库存相关 API |
| `block` | `Block` | 方块类型；与具体建筑实体不同 |
| `liquid` | `Liquid` | 液体内容 |
| `unit` | `UnitType` | 单位类型，不是场上的单位实例 |
| `status` | `StatusEffect` | 状态效果 |
| `weather` | `Weather` | 天气 |
| `sector` | `SectorPreset` | 扇区内容 |
| `planet` | `Planet` | 星球内容 |
| `team` | `Team` | 队伍是可 lookup 的特殊类别，不是普通 `Content` |
| `unitCommand` | `UnitCommand` | 单位命令内容 |
| `unitStance` | `UnitStance` | 单位姿态内容 |

`ContentType` 还保留了一些当前未使用或兼容性占位项，例如 `bullet`、`mech_UNUSED` 等。编译器不应把枚举中的所有项都当作可用语言类型，应根据具体指令支持情况生成 API。

### 3.2 实体对象

指令会产生或接收以下运行时实体：

- `Building`：场上建筑实例；
- `Unit`：场上单位实例；
- `Team`：队伍对象；
- `Bullet`：部分高级/特权指令涉及；
- `Tile` 的相关层对象通常最终输出 `Floor`、`Block`、`Building` 或 `null`；
- 查询指令使用 `Seq` 保存实体集合。

必须区分内容类型和实体类型。例如：

```text
@duo       -> UnitType（单位类型）
ubind @duo -> @unit 为 Unit（单位实例）
@copper    -> Item（物品内容）
getblock   -> Block 或 Building，取决于 layer
```

### 3.3 字符串

字符串是对象模式。汇编器支持带引号字符串常量，`print`、`format`、`read` 等指令也会处理字符串。字符串可以被读取字符或读取长度，但这些操作不是普通 C++ `char*` 内存访问。

语言层面应提供不可变 `string`，并用专用函数完成：

```text
length(string)
char_at(string, index)
format(...)
```

不应提供字符串指针、指针算术或可变字符缓冲区。

### 3.4 枚举和指令参数对象

以下枚举会作为常量对象进入指令：

- `LAccess`：sensor/control 的属性；
- `ConditionOp`：jump 条件；
- `LogicOp`：op 运算符；
- `RadarTarget`、`RadarSort`；
- `FetchType`、`QueryType`；
- `TileLayer`、`BlockFlag`、`LUnitControl` 等。

它们在汇编文本中通常表现为关键字，但运行时可能是枚举对象，不能一律按数字处理。建议在源语言中把它们设计成编译期关键字或强类型枚举参数，而不是普通整数。

## 4. 全局常量和内置值

`GlobalVars.init` 注册了三类重要值。

### 4.1 数值常量

包括：

- `false`、`true`；
- `@pi`、`@e`、`@degToRad`、`@radToDeg`；
- `@time`、`@tick`、`@second`、`@minute`；
- `@waveNumber`、`@waveTime`；
- `@mapw`、`@maph`；
- `@server`、`@client`；
- 各类 `@...Count`；
- 颜色位打包后的数值。

### 4.2 空值和特殊变量

- `null` 是对象模式、对象值为 `null` 的常量；
- `@unit` 表示当前绑定单位，可能为空；
- `@this` 表示当前处理器关联对象，可能为空；
- `@counter` 是特殊的数值程序计数器；
- `@links`、`@ipt` 等是执行器相关特殊变量。

`@counter` 不应暴露为普通可写变量，除非语言专门支持底层跳转。普通控制流应由编译器生成标签和 jump。

### 4.3 内容常量

游戏内容被注册成带 `@` 前缀的对象常量：

- `@copper` 等 `Item`；
- `@water` 等 `Liquid`；
- `@duo` 等 `UnitType`；
- 方块、天气、状态、星球等内容；
- 队伍常量，如 `@sharded`。

这些常量名称依赖游戏内容和版本，编译器最好从运行中的 Mindustry 内容表或导出的元数据生成，而不要硬编码完整清单。

## 5. 指令产生的值

### 5.1 数值输出

常见数值输出包括：

- 算术、比较和数学函数；
- `sensor` 的大多数属性；
- `fetch` 的 count 变体；
- `ulocate` 的坐标和 found 标志；
- `getflag` 的布尔结果；
- `packcolor` 的颜色编码；
- 各类计数、容量、生命值、坐标和时间。

### 5.2 对象输出

常见对象输出包括：

- `lookup`：内容对象或 `null`；
- `getlink`：`Building` 或 `null`；
- `ubind`：把 `@unit` 设置为 `Unit` 或 `null`；
- `ulocate`：`Building` 或 `null`；
- `uradar`：目标 `Unit` 或 `null`；
- `fetch unit/player/core/build`：对应实体或 `null`；
- `getblock`：`Floor`、`Block`、`Building` 或 `null`；
- `sensor` 的对象属性：例如 `type`、`firstItem`、`building`、`payloadType`、`config`；
- `spawn`/`bullet`：创建出的实体或失败时保留/设置为空的结果；
- `query`：执行器内部保存的 `Seq` 集合。

### 5.3 `sensor` 是动态输出接口

`Senseable.senseObject` 对某个属性可能返回对象，也可能返回特殊的 `noSensed` 标记；执行器看到这个标记时才会改走数值感知路径。未识别或无效属性通常最终会写入 `null`。

因此 `sensor` 不适合设计成返回一个完全静态固定类型的普通函数。建议提供按属性区分的 API，例如：

```text
number health(building b)
item? first_item(building b)
block? type(building b)
unit? controller(unit u)
```

如果保留通用形式，则返回值应是受控的 `sensor_value` 联合类型，而不是任意 C++ 对象。

## 6. 读写接口和集合

`read`/`write` 的目标可以是：

- 实现 `LReadable`/`LWritable` 的建筑或设备；
- 字符串（只读字符）；
- 内部 `Seq`（查询结果）；
- 其他不支持时返回 `null` 或忽略写入。

这不是通用内存模型。字符串和查询集合的索引访问都应通过专用语言 API 表示。查询结果当前由执行器持有并复用，不能当作普通拥有所有权的 C++ 容器。

## 7. 对编译器类型设计的建议

### 7.1 第一阶段类型集合

建议先实现：

```text
number
bool
string
object?
content
item
liquid
block
unit_kind
building?
unit?
team
```

其中 `?` 表示可空。`content` 可以作为少数 API 的共同参数类型，但不建议让所有具体对象自动互转。

### 7.2 不建议实现的类型

第一阶段不实现：

- 指针和指针算术；
- 引用语义；
- 任意堆对象；
- `void*`；
- 任意 Java/游戏对象；
- 依赖对象地址的哈希和排序；
- 可变字符串缓冲区；
- 普通 C++ 联合体。

### 7.3 隐式转换规则

建议只保留少数明确转换：

```text
bool -> number       0 或 1
number -> bool       0 为假，非 0 为真
object? -> bool      null 为假，非 null 为真
具体内容 -> content  向上转型
null -> T?           允许
```

以下转换默认禁止：

```text
object? -> number
number -> item/block/unit
building -> block
unit_kind -> unit
string -> number
```

需要时通过内置函数显式完成，例如 `id(item)`、`content_by_id(...)`、`as_block(...)`。

### 7.4 第一阶段不支持成员函数和引用

第一阶段采用自由函数，不实现普通成员函数、引用参数和接收者限定：

```cpp
unit? target = radar(...);

if (target != null && valid(target)) {
    number hp = health(target);
}
```

`health(target)` 可直接降低为对应的 `sensor` 指令。自由函数只需处理按值参数和静态函数槽位，不需要为隐含接收者引入类似引用的别名语义。

用户结构体可以先支持字段访问、聚合初始化和按值传递，但不支持成员函数。构造函数、析构函数、复制/移动构造、复制/移动赋值和转换函数等特殊成员函数同样不实现。

后续若建立了明确的借用、`inout` 或函数特化方案，可以重新评估成员函数；届时成员调用可以作为自由函数的语法糖，而不是第一阶段的基础能力。

## 8. 与 `main_loop` 和生命周期的关系

逻辑变量的物理存储会跨处理器周期存在，但编译器可以在语言层模拟 C++ 风格作用域：

- 全局变量映射为持久化槽位；
- `main_loop` 局部变量在每轮入口初始化；
- 普通函数局部变量使用编译期分配的临时槽位；
- `static` 局部变量映射为持久化槽位；
- 离开作用域只影响可见性和槽位复用，不要求底层对象真的析构。

这与目标机的宽松模型兼容，也不需要引入指针或真正的栈。

## 9. Debug 模式与运行时错误

第一阶段提供轻量 Debug 模式，只插入目标机能够可靠完成的检查，不维护影子类型标签，也不尝试实现通用运行时类型反射。

适合自动检查的内容包括：

- 用户显式编写的 `assert`；
- 除数为零、平方根和对数定义域等数学前置条件；
- 数值运算后意外产生 `null`；
- `int` 转换的整数性和安全范围；
- 能够取得容量时的索引越界；
- 从低层/raw 接口进入非空参数时的空值；
- 编译器生成代码自身的不变量。

不自动插入单位或建筑的 `valid` 检查。实体死亡、目标消失和链接状态变化属于游戏逻辑，程序应根据需求显式调用 `valid(value)` 或处理可空返回值。Debug 模式不替玩家定义这些逻辑是否错误。

调试信息板通过一个只在编译期生效的特殊函数指定：

```cpp
debug_output(message1);
```

第一阶段约束如下：

- 每个程序最多指定一个调试输出目标；
- 参数必须是可静态识别的链接符号；
- Debug 构建使用该目标输出错误码、源码行号、函数名和必要的值；
- Release 构建删除该声明，不生成运行时指令；
- 未配置或目标无效时仍进入停止状态，只是无法显示信息板诊断。

发生错误时，调试处理器先 flush 一次以清空可能残留的共享文本缓冲，再打印诊断、flush 到调试信息板，最后执行 `stop`。`stop` 会停留在自身并按 tick yield，效果等价于低开销死循环，等待玩家修改代码或重新加载处理器；不使用普通 `jump` 忙循环消耗全部指令额度。

为便于信息板不可用时排查，处理器同时保留：

```text
__debug_error
__debug_line
__debug_value
```

玩家可以直接在处理器变量界面查看这些值。

安全 API 为消除“失败时保留旧输出”而生成的初始化属于正式语言语义，在 Debug 和 Release 中都必须保留。Debug 模式只增加诊断性检查，因此会增加指令数并改变执行时序；编译器应分别报告两种构建的指令总数。

## 10. 仍需单独决定的问题

以下问题需要在正式实现前定案：

1. `number` 是否对用户暴露整数/浮点两个静态类型，还是只提供一个数值类型。
2. `null` 是单独类型，还是只允许出现在 `T?` 中。
3. 是否允许对象到 `bool` 的隐式转换。
4. `sensor` 是通用动态返回值，还是拆成按属性命名的强类型函数。
5. `query` 集合是否在第一版支持，还是推迟到内存/集合 API 完成后。
6. 是否把特权/world 指令纳入同一语言，还是先只支持普通逻辑处理器指令。
7. 游戏内容常量由编译器内置、外部元数据文件提供，还是从 Mindustry 运行时导出。
8. 指令生成失败时，结果变量应保持旧值、变成 `null`，还是由编译器要求显式错误处理。

## 最终建议

先按“强类型对象句柄 + 单一数值类型 + 可空对象 + 显式 Mindustry API”的方向设计。语言表面可以继续使用 C++ 风格的变量、表达式、控制流和生命周期，但不要承诺 C++ 指针、对象 ABI 或标准库兼容性。

第一版的类型系统应服务于生成正确的 Mindustry 指令，而不是模拟一台不存在的传统计算机。
