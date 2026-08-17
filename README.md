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
```

省略 `-o` 时，汇编输出到标准输出。

## 入口与链接

程序必须定义：

```cpp
void main_loop() {
}
```

处理器每次运行到函数末尾后会重新进入 `main_loop`。全局变量保持原值，局部变量每轮重新初始化。

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
display
color
packed_color
point
vec
rect
```

支持的语言功能：

- 全局变量、局部变量、内置原版建筑链接和模组用 `extern` 链接；
- `if/else`、`while`、`for`；
- C 风格空语句，包括 `for (...);`；
- `break`、`continue`、`return`；
- 算术、比较、逻辑短路和复合赋值；
- 前置/后置 `++`、`--`；
- 按值参数和返回值的普通函数；
- C++ 风格结构体、字段访问、嵌套聚合初始化和聚合赋值；
- 类型或表达式形式的 `sizeof`，且不求值表达式；
- 内置 `print`、`printchar`/`putchar`、`format`/`printf`、`printflush`、`drawflush` 和 `getlink`。

`getlink(index)` 返回对应处理器链接的 `building`；索引越界时，Mindustry 运行时会返回 `null`：

```cpp
building target = getlink(0);
```

函数使用静态调用帧和编译器生成的返回地址，不依赖栈。当前禁止直接递归和间接递归。

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

完整的彩色旋转线与五帧拖影示例见 [`examples/rotate_line.mdtc`](examples/rotate_line.mdtc)。

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
