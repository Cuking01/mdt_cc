# mdt_cc

`mdt_cc` 是一个面向 Mindustry 逻辑处理器的 C 风格编译器。当前初版把结构化控制流、静态类型和普通函数调用编译为 mlogic 汇编。

## 构建

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

编译器只依赖 C++20 工具链和 CMake，不需要 JDK 或完整 Mindustry 运行环境。

## 使用

```sh
./build/mdtc examples/basic.mdtc -o build/basic.mlog
./build/mdtc -I include examples/basic.mdtc -o build/basic.mlog
```

省略 `-o` 时，汇编输出到标准输出。

## 预处理

支持不带参数的对象宏，以及使用相对路径或 `-I` 搜索路径的包含文件：

```cpp
#define TARGET_STOCK 500
#include "plans/common.mdtc"
#include <game/items.mdtc>
```

`#include "..."` 优先相对于当前文件所在目录查找，再依次搜索命令行中的 `-I`
目录；`#include <...>` 只搜索 `-I` 目录。`-Ipath` 与 `-I path` 两种写法均可使用。
宏按标识符替换并递归展开，不会在字符串和注释中展开。当前暂不支持带参数宏、条件编译、
跨行宏和 `#pragma once`；循环包含会直接报告编译错误。

## 入口与链接

程序必须定义：

```cpp
void main_loop() {
}
```

程序还可以定义一个可选的一次性初始化入口：

```cpp
void main_init() {
    // 全局变量初始化完成后执行一次。
}
```

启动顺序是全局变量初始化、`main_init`、`main_loop`。处理器每次运行到 `main_loop`
末尾后会重新进入 `main_loop`；不会再次执行全局初始化或 `main_init`。两个特殊入口都必须返回
`void` 且不接受参数，也不能由源码主动调用。`main_init` 中的 `return;` 会立即进入 `main_loop`。
全局变量保持原值，`main_loop` 局部变量每轮重新初始化。

Mindustry 自动生成的原版建筑链接名由编译器内置识别，不需要 `extern`：

```cpp
void main_loop() {
    print("hello");
    printflush(message1);
}
```

## 当前语法

支持的类型：

```text
void
bool
int
float
double (number 的别名)
number
string
message
building
posc
display
item
liquid
block
unit
unit_kind
team
sensor
sensor_value
radar_filter（仅内置常量）
radar_sort（仅内置常量）
color
packed_color
point
vec
rect
```

支持的语言功能：

- UTF-8 源码、非 ASCII 标识符、UTF-8 字符串以及 `//`、`/* ... */` 注释；
- 全局变量、局部变量、内置原版建筑链接和模组用 `extern` 链接；
- `if/else`、`switch/case/default`、`while`、`for`；
- C 风格空语句，包括 `for (...);`；
- `break`、`continue`、`return`；
- 算术、比较、逻辑短路和复合赋值；
- 基于原生 `select` 的受限三目运算符；
- 前置/后置 `++`、`--`；
- 按值参数、受限引用参数和返回值的普通函数；
- C++ 风格结构体、字段访问、嵌套聚合初始化和聚合赋值；
- 结构体成员函数和专用 `this->成员` 语法；
- 类型或表达式形式的 `sizeof`，且不求值表达式；
- 内置 `print`、`printchar`/`putchar`、`format`/`printf`、`printflush`、`drawflush`、`getlink` 和类型化 `lookup`。

源码必须是合法 UTF-8，可带 UTF-8 BOM。标识符支持 ASCII 字母、数字、下划线和非 ASCII
Unicode 字符；数字不能作为首字符，Unicode 空白和常见标点不会成为标识符的一部分。字符串和
注释可以直接包含中文等 UTF-8 文本：

```cpp
/* 计算本轮需要搬运的数量。 */
int 计算缺口(int 目标库存, int 当前库存) {
    // 字符串在输出汇编中保持 UTF-8 编码。
    string 状态 = "正在计算搬运计划";
    print(状态);
    return 目标库存 - 当前库存;
}
```

块注释目前不嵌套；未闭合的字符串、块注释和非法 UTF-8 字节都会产生带行列位置的编译错误。

`switch` 当前接受 `int` 条件和带可选正负号的整数字面量 case，并采用 C/C++ 风格的贯穿
语义；只有显式 `break` 才会离开 switch。没有匹配项且没有 `default` 时，直接继续执行
switch 后的语句：

```cpp
switch (remaining) {
    case 3:
        process(index++);
    case 2:
        process(index++);
    case 1:
        process(index++);
    case 0:
        break;
}
```

编译器会自动识别稠密整数 case：至少四个 case、取值跨度不超过 case 数量的两倍且不超过
64 时生成基于 `@counter` 的跳转表；其他 switch 生成普通比较跳转。每个 case 当前具有独立
的局部作用域；需要跨 case 使用的变量应声明在 switch 之前。

`getlink(index)` 返回对应处理器链接的 `building`；索引越界时，Mindustry 运行时会返回 `null`：

```cpp
building target = getlink(0);
```

三目运算符使用标准 C 语法，并降低为 Mindustry `select`：

```cpp
int magnitude = value >= 0 ? value : -value;
point position = use_first ? point{10, 20} : point{30, 40};
building target = found ? turret1 : null;
```

比较条件会直接融合进 `select`；结构体等多槽值按字段生成多条 `select`。当两支都是可
安全提前求值的纯表达式时，编译器采用这条紧凑路径，支持字面量、变量、字段、内存读取、
算术和 `abs/max/dot` 等纯内置计算。

如果任一分支包含赋值、`++/--`、普通函数、成员调用、`rand`、打印、绘图或其他副作用，
编译器会自动回退为条件跳转，只执行被选中的一支，再把结果写入统一临时槽。因此以下
写法保持 C 的惰性求值语义：

```cpp
int result = condition ? update_and_get() : fallback();
condition ? print("yes") : print("no"); // 两支同为 void 也受支持
```

一支为 `void`、另一支产生值仍然是类型错误。

`item`、`liquid`、`block`、`unit_kind` 和 `team` 是互不隐式转换的单槽内容句柄类型。编译器内置当前 Mindustry 子模块对应的原版内容常量，并在汇编中保留其 `@` 名称：

```cpp
item resource = @copper;
liquid fluid = @water;
block kind = @router;
unit_kind unit = @dagger;
team owner = @sharded;
```

这些类型不能参与算术或顺序比较，也不能存入内存元；同类型值可以使用 `==` 和 `!=`。不同内容类型之间没有隐式转换。模组新增的 `@` 内容常量当前不在静态类型表中，会产生“不支持的常量”诊断。

原生 `lookup` 通过五个类型化函数提供，参数必须是 `int` Logic ID；越界时游戏写入 `null`：

```cpp
block found_block = lookup_block(block_id);
unit_kind found_unit = lookup_unit(unit_id);
item found_item = lookup_item(item_id);
liquid found_liquid = lookup_liquid(liquid_id);
team found_team = lookup_team(team_id);
```

Logic ID 不是普通内容 ID，应该只把游戏提供的 Logic ID 用于这些函数。

Mindustry `sensor` 指令提供通用成员接口和按属性命名的便捷接口：

```cpp
number health = turret1.get(@health);
number copper = vault1.get(@copper);
bool enabled = switch1.get_enabled();
item first = vault1.get_first_item();
string kind_name = @router.get_name();
```

`@health`、`@enabled`、`@firstItem` 等 `LAccess` 名称是内置的 `sensor` 常量；
item、liquid、block 和 unit_kind 内容常量也能作为 `get` 参数，结果为对应内容的数量。
`get_xxx()` 别名覆盖全部可感知属性并生成相同的单条 `sensor` 指令。对象类别可能随目标
动态变化的 `@config`、`@payloadType` 和 `@currentAmmoType` 返回不透明的
`sensor_value`。目标不支持某属性时，Mindustry 通常在运行时产生 `null`，静态返回类型
不代表该属性对每个具体建筑都有效。完整属性和类型表见
[`docs/sensor指令与封装设计.md`](docs/sensor指令与封装设计.md)。

`null` 是内置空值字面量，可用于检查包括数值静态类型在内的运行时结果：

```cpp
number health = target.get_health();
if (health == null) {
    print("target does not expose health");
}
```

与 `null` 的 `==` 和 `!=` 总是使用 Mindustry `strictEqual` 语义，因此不会把对象态
`null` 与数值 `0` 混淆。`null` 可以赋给 `building`、`item`、`sensor_value` 等对象
句柄类型，但不能赋给 `number`、`int` 或 `bool`。

可由普通逻辑到达的建筑配置使用专用动作名，而不是通用 `.config(...)`：

```cpp
factory1.set_production(@dagger);
factory1.clear_unit_command();
source1.set_output_item(@copper);
sorter1.set_sort_item(@lead);
unloader1.set_unload_item(@titanium);
source2.set_payload_kind(@router);               // 也接受 unit_kind
router1.set_straight_payload(@dagger);           // 也接受 block
router1.set_rotation(2);
constructor1.set_recipe(@beryllium-wall-large);
constructor1.clear_recipe();
factory1.copy_configuration_from(factory2);
```

还提供与上述选择操作对应的 `clear_output_item`、`clear_output_liquid`、`clear_sort_item`、`clear_unload_item`、`clear_delivery_item`、`clear_payload_kind` 和 `clear_straight_payload`。这些接口进行静态参数类型检查，但目前所有接收者仍是 `building`；目标建筑是否支持该动作由 Mindustry 运行时决定，错误目标通常静默无操作。

`posc` 是代表 Mindustry `Posc` 位置对象能力的不透明单槽类型。`building` 以及
`unit`、`message`、`display`、`memory` 等专用句柄可以隐式转换为 `posc`，也可以用
`extern posc` 接入外部对象，但数值不能构造它。它目前主要用于炮塔的对象瞄准接口；
本轮没有改变 `getlink` 的返回类型：

```cpp
point target{100, 80};
turret1.shoot(target, true);
turret1.shootp(conveyor1, true);
illuminator1.set_color(pack_color(255, 64, 0));
```

`unit` 表示运行时单位实例，与表示单位种类的 `unit_kind` 不同。它不能写入内存元；
`unit_bind` 接受种类或已有单位，执行 `ubind` 并返回新的 `@unit`：

```cpp
unit worker = unit_bind(@poly);
worker.move(point{80, 40});
worker.approach(80, 40, 5);
bool arrived = worker.within(point{80, 40}, 5);
```

单位控制使用成员接口。编译器会在每次 `ucontrol` 前自动绑定接收者，但会删除刚执行
`unit_bind` 后以及连续控制同一单位时的冗余 `ubind`；`unbind()` 后的重新绑定不会被删除。
坐标动作 `move`、`approach`、`pathfind`、`target`、`mine`、`deconstruct` 和查询接口均接受
双标量或 `point` 形式。其他接口包括：

```cpp
worker.idle();
worker.stop();
worker.auto_pathfind();
worker.boost(true);
worker.targetp(enemy, true);
worker.item_drop(vault1, 20);
worker.discard_items(20);                 // itemDrop @air
worker.item_take(container1, @copper, 20);
worker.payload_drop();
worker.payload_take(true);
worker.payload_enter();
worker.set_flag(7);
worker.build(point{10, 20}, @router, build_up);
worker.unbind();
```

建造方向常量为 `build_right`、`build_up`、`build_left`、`build_down`，分别对应 `0..3`；
也可直接传数值。`get_block` 接受三个输出左值，忠实对应原生三输出指令；常用拆分接口为
`get_block_type`、`get_block_building` 和 `get_block_floor`。完整语义见
[`docs/unit指令与封装设计.md`](docs/unit指令与封装设计.md)。

函数使用静态调用帧和编译器生成的返回地址，不依赖栈。当前禁止直接递归和间接递归。

函数参数支持受限的值—结果引用。调用时先把左值复制到形参槽，函数返回后再按参数顺序写回原左值：

```cpp
void increment(int& value) {
    value += 1;
}

void swap(restrict int& first, restrict int& second) {
    int temporary = first;
    first = second;
    second = temporary;
}
```

引用实参必须是类型精确匹配的可赋值左值，右值、隐式数值转换和引用数组描述符均不支持。普通变量和结构体字段会精确检查存储槽重叠；单个内存元素引用可以直接使用。多个内存引用若能通过相同数组描述符和常量索引证明区间不重叠，也无需 `restrict`，例如 `swap(values[0], values[1])`。已知重叠始终报错；动态地址无法证明时，相关参数必须全部声明为 `restrict`。

内存引用的句柄和地址在调用前固定，因此后续参数求值即使修改索引，也会写回原位置。`restrict` 是调用者关于本次调用不发生存储重叠的承诺；违反承诺属于未定义行为。该机制不是可保存或返回的运行时引用，函数通过其他全局访问路径读取同一位置时，也不会提前看到尚未写回的形参修改。

结构体按字段展开为多个 Mindustry 变量，支持全局/局部变量、嵌套、按值参数和返回值。`point`、`vec` 和 `rect` 是不可重新定义的内置结构体：

```cpp
point position{10, 20};
vec velocity{2, 3};
position += velocity;

rect area = {{0, 0}, {80, 80}};
print(area.min.x);
print(area.max.y);

struct transform {
    point origin;
    vec offset;
};
```

用户结构体可以在定义内部声明成员函数：

```cpp
struct counter {
    int value;

    void add(int amount) {
        this->value += amount;
    }

    int add_and_get(int amount) {
        add(amount);       // 可省略 this->
        return value;      // 字段也可省略 this->
    }
};

counter count{1};
int result = count.add_and_get(2);
```

成员函数在 ABI 中降低为带隐式 `restrict counter& __this` 参数的普通函数，复用现有
值—结果引用机制。因此接收者必须是可赋值左值；结构体变量、嵌套字段以及 `arr<T>` 的
内存元素均可调用，`counter{}.add(1)` 则会报错。`this` 不能单独作为表达式，只支持
`this->字段` 和 `this->方法()`；语言没有通用指针，也不提供通用 `->` 运算符。

当前不支持成员函数重载、`const`/引用限定符、静态成员、构造函数、析构函数和类外定义。
成员递归与普通函数递归一样禁止。完整设计与限制见
[`docs/成员函数与this设计.md`](docs/成员函数与this设计.md)。

`point` 和 `vec` 的字段均为 `number x/y`；`rect` 的字段为两个绝对坐标 `point min/max`。支持 `vec±vec`、`point±vec`、`vec+point`、`point-point`，以及 `vec` 与任意数值标量的双向乘法；对应的 `+=`、`-=`、`*=` 也受支持。`vec-point` 没有自然的仿射几何含义，因此不支持。

二维向量还提供返回 `number` 的自由函数 `dot(vec, vec)` 和 `cross(vec, vec)`，后者返回叉积的标量值 `a.x*b.y-a.y*b.x`。

Mindustry `op` 中与 C++ 语义一致的部分直接使用运算符：`+ - * / % == != < <= > >= &&`。其余运算提供无 `op_` 前缀的内置函数：

```cpp
idiv(a, b);              // 向下取整的除法
mod(a, b);               // 支持小数；整数通常直接使用 %
emod(a, b);              // 结果与除数同号的模
pow(a, b);
strict_equal(a, b);

shl(a, b); shr(a, b); ushr(a, b);
bit_or(a, b); bit_and(a, b); bit_xor(a, b); bit_not(a);

max(a, b); min(a, b);
angle(x, y); angle(vec_value); angle_diff(a, b);
len(x, y); len(vec_value);
noise(x, y); noise(point_value); noise(vec_value);

abs(x); sign(x); sqrt(x);
log(x); logn(x, base); log10(x);
floor(x); ceil(x); round(x); rand(limit);
sin(degrees); cos(degrees); tan(degrees);
asin(x); acos(x); atan(x);
```

位运算只接受 `int`。`floor`、`ceil`、`round` 和 `idiv` 返回 `int`；`max`/`min` 与 `abs` 尽量保留输入数值类型，其余数学函数返回 `number`。所有三角函数均使用角度制。`rand` 会读取游戏的随机状态，不是纯函数；`&&` 保持语言的短路求值，而不是强制执行两个操作数。

初始化项不足时剩余字段默认初始化，过多时报错；嵌套结构体必须保留对应的花括号层级。所有非 `void` 内置标量类型的 `sizeof` 都是 `1`，结构体大小是其字段大小之和：

类型名加花括号可在任意表达式位置构造值，包括内置类型和用户结构体：

```cpp
draw_line(point{0, 0}, point{79, 79});
draw_rect(rect{{10, 10}, {30, 25}});
int value = int{3};
```

```cpp
sizeof(int)       // 1
sizeof(point)     // 2
sizeof(area)      // 4，且不会读取或修改 area
```

## 示例

完整的彩色旋转线与五帧拖影示例见 [`examples/rotate_line.mdtc`](examples/rotate_line.mdtc)。经典 2048 示例见 [`examples/2048.mdtc`](examples/2048.mdtc)，使用两个内存元、16 个普通逻辑显示屏、1 个大型逻辑显示屏和5个开关。

```cpp
int add(int left, int right) {
    return left + right;
}

int iteration = 0;

void main_loop() {
    int result = add(iteration, 1);
    iteration = result;
    print(result);
    printflush(message1);
}
```

`printflush` 可以接收内置 `message` 链接或正整数字面量；`printflush(2)` 会生成 `printflush message2`。字符串字面量和运行时 `string` 变量均不支持。

`printchar(int)` 和 C 风格别名 `putchar(int)` 都生成 Mindustry 的 `printchar` 指令，当前不检查 UTF-16 编码范围。`format(value)` 直接封装游戏指令；`printf(string, ...)` 在编译期展开为一次 `print` 和若干次 `format`：

```cpp
printf("x={0}, y={1}", x, y);
printflush(message1);
```

格式占位符仍遵循 Mindustry 的 `{0}` 到 `{9}` 规则，而不是 C 标准库的 `%d` 等规则；`printf` 只负责写入共享文本缓冲区，不会自动刷新信息板。

`drawflush` 接受内置 `display` 链接或正整数字面量；`drawflush(2)` 会生成 `drawflush display2`。字符串字面量和普通 `building` 均不接受。

`wait(number seconds)` 直接生成 Mindustry 的 `wait` 指令，可用于限制 `main_loop` 帧率。参数单位是秒；正数等待到累计时间达到该值，零或负数也会 yield 一次。

`stop()` 直接生成 Mindustry `stop` 指令，处理器会永久停在该指令并逐 tick yield，直到
逻辑代码或处理器状态被外部重载。`exit()` 是完全相同的别名：

```cpp
if (fatal_error) {
    print("fatal error");
    printflush(message1);
    exit();
}
```

它不同于从函数 `return`，也不同于结束本轮 `main_loop`；位于它后面的代码在正常运行中
不会继续执行。

Radar 作为 `building` 的内置成员函数提供。常用排序方向直接编码在函数名中，并支持
0～3 个 `radar_filter`：

```cpp
unit nearest = turret1.radar_nearest(radar_enemy);
posc weakest_flying = turret1.radar_min_health(radar_enemy, radar_flying);
posc toughest_attacker = turret1.radar_max_armor(
    radar_enemy, radar_flying, radar_attacker
);
```

缺少的筛选槽自动补 `radar_any`。通用版本接受 0～3 个筛选条件、一个内置
`radar_sort` 常量和动态 `int order`：

```cpp
int order = 1;
unit target = turret1.radar(radar_enemy, radar_distance, order);
```

筛选和排序选择器是编译期指令枚举，不能声明变量或由运行时计算；只有 `order` 可以动态
传入。Radar 的完整语义与接口清单见
[`docs/radar指令与封装设计.md`](docs/radar指令与封装设计.md)。

`color` 是包含四个 `int` 字段 `r/g/b/a` 的内置聚合类型；`packed_color` 是占一个底层变量的不透明打包颜色。两者都可以赋值、按值传参和返回：

```cpp
color red = rgb(255, 0, 0);
color overlay = rgba(0, 0, 255, 128);
overlay.a = 64;

packed_color packed = pack_color(overlay);
packed_color opaque = pack_color(255, 0, 0);
packed_color translucent = pack_color(255, 0, 0, 128);
color restored = unpack_color(packed);

draw_clear(red);
set_color(overlay);
draw_col(packed);
set_packed_color(packed);
draw_stroke(2);
set_stroke(2);
```

`draw color r g b a` 传递四个独立分量，`draw col packedColor` 则把同一颜色装在一个特殊 Mindustry 数值中；两者最终都设置相同的持久颜色状态。`draw_color`/`set_color` 接受 `color` 或四个 `int`；`draw_col`/`set_packed_color` 只接受 `packed_color`。普通 `number` 不能冒充特殊颜色位模式。`sizeof(color)` 是 `4`，`sizeof(packed_color)` 是 `1`。

几何绘图接口既保留标量形式，也提供聚合形式：

```cpp
draw_line(point start, point end);
draw_rect(rect bounds);
draw_rect(point origin, vec size);
draw_line_rect(rect bounds);
draw_poly(point center, int sides, number radius, number rotation);
draw_line_poly(point center, int sides, number radius, number rotation);
draw_triangle(point a, point b, point c);
draw_image(point center, display source, number size, number rotation);
draw_print(point position, int align);
draw_translate(vec offset);
draw_scale(vec factors);
draw_rotate(number degrees);
draw_reset();
```

`draw_clear`、上述接口以及 `draw_stroke` 均另有与原始 mlogic 参数顺序一致的标量形式。`draw_image` 当前支持将另一块 `display` 作为图像源；游戏内容常量要等内容类型和 `@copper` 等常量语法实现后再开放。

## 测试方式

测试包含一个轻量 mlogic 模拟器。它执行生成的控制流、函数调用、文本和基础绘图指令，并记录 flush 结果，因此不需要启动游戏。后续仍可增加基于 Mindustry `LAssembler` 的低频兼容性测试。

## 当前限制

- 不支持指针、引用、成员函数和递归；
- 不支持数组、模板和标准库；
- 结构体暂不支持成员函数、指定初始化、花括号层级省略和比较运算；
- `draw_image` 暂不支持游戏内容常量作为图像源；
- `--debug` 参数已经预留，但运行时诊断插桩尚未实现；
- 尚未调用游戏自身的 assembler 验证生成结果。
