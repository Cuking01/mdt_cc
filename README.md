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

用 `extern message` 声明由 Mindustry 处理器提供的链接名：

```cpp
extern message message1;

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
number
string
message
building
```

支持的语言功能：

- 全局变量、局部变量和 `extern` 链接；
- `if/else`、`while`、`for`；
- C 风格空语句，包括 `for (...);`；
- `break`、`continue`、`return`；
- 算术、比较、逻辑短路和复合赋值；
- 前置/后置 `++`、`--`；
- 按值参数和返回值的普通函数；
- 内置 `print(value)`、`printflush(message)` 和 `getlink(int)`。

`getlink(index)` 返回对应处理器链接的 `building`；索引越界时，Mindustry 运行时会返回 `null`：

```cpp
building target = getlink(0);
```

函数使用静态调用帧和编译器生成的返回地址，不依赖栈。当前禁止直接递归和间接递归。

## 示例

```cpp
extern message message1;

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

`printflush` 也可以接收信息板名称的字符串字面量，例如 `printflush("message1")`。编译器会将其转换成裸链接名；不支持运行时 `string` 变量作为目标。

## 测试方式

测试包含一个轻量 mlogic 模拟器。它执行生成的 `set`、`op`、`jump`、`getlink` 和 `@counter` 控制流，并记录 `print`/`printflush` 结果，因此不需要启动游戏。后续仍可增加基于 Mindustry `LAssembler` 的低频兼容性测试。

## 当前限制

- 不支持指针、引用、成员函数和递归；
- 不支持结构体、数组、模板和标准库；
- 目前只封装 `print`、`printflush` 和 `getlink` 三个特殊指令；
- `--debug` 参数已经预留，但运行时诊断插桩尚未实现；
- 尚未调用游戏自身的 assembler 验证生成结果。
