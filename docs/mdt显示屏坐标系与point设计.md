# Mindustry 显示屏坐标系与 `point` 设计

## 结论

原版两种普通逻辑显示屏使用左下角为原点、向右为正 X、向上为正 Y 的像素坐标系：

| 元件 | 逻辑尺寸 | 中心点 |
| --- | --- | --- |
| 逻辑显示屏 `logic-display` | `80 × 80` | `(40, 40)` |
| 大型逻辑显示屏 `large-logic-display` | `176 × 176` | `(88, 88)` |

尺寸来自 `Blocks.java` 中的 `displaySize`。显示器创建同尺寸的 `FrameBuffer`，处理绘图命令时使用 `Draw.proj(0, 0, width, height)`，因此逻辑绘图直接使用帧缓冲像素坐标。最终把纹理贴到世界中时会翻转纹理 Y 方向，从而保持逻辑坐标的 Y 轴向上。

不应把 `80` 和 `176` 固化进通用绘图代码。`displayWidth`、`displayHeight` 感知属性会返回目标显示器的实际逻辑尺寸；拼接显示单元的尺寸还会随布局变化。后续封装 `sensor` 时，建议提供：

```cpp
int display_width(display target);
int display_height(display target);
point display_size(display target);
point display_center(display target);
```

## 图元对坐标的解释

| 子命令 | 坐标含义 |
| --- | --- |
| `line` | 两个端点 `(x1,y1)`、`(x2,y2)` |
| `rect` / `lineRect` | 左下角 `(x,y)` 加宽高 |
| `poly` / `linePoly` | 中心 `(x,y)`、半径和旋转角 |
| `triangle` | 三个顶点 |
| `image` | 图片中心 `(x,y)`；高度按图片比例计算 |
| `print` | 文本锚点 `(x,y)`；具体延伸方向由对齐值决定 |
| `translate` | 给后续命令追加坐标偏移 |

超出显示区域的图形由帧缓冲裁剪，不会自动缩放。普通几何参数在执行器中向 0 截断成整数，再以 10 位符号格式打包；安全范围为 `-511..511`，超出后会回绕而不是饱和。对 80 和 176 像素显示屏来说，正常屏内坐标不会触发打包回绕。

颜色、线宽和变换矩阵属于显示器持久状态，会跨 `drawflush` 保留。`draw clear` 只覆盖整个帧缓冲，不会重置这些状态；坐标封装不能假定每轮绘制都从单位变换开始。

## `col` 的含义

`draw col packedColor` 与 `draw color r g b a` 最终都设置同一个持久颜色状态。区别是 `col` 把 RGBA 四个分量打包在一个 Mindustry 数值中：

```text
%ff0000     红色，隐含 alpha=ff
%ff000080   半透明红色
%[accent]   游戏命名颜色
```

它不是普通整数 `0xRRGGBBAA`。汇编器把颜色解析成 `Color.toDoubleBits` 的特殊 `double` 位模式，执行器再读取该位模式的低 32 位并拆成 RGBA。因此直接暴露 `draw_col(number)` 会让用户误传常规整数。当前实现区分四分量 `color` 和不透明单槽 `packed_color`：

```cpp
color rgb(int red, int green, int blue);
color rgba(int red, int green, int blue, int alpha);
packed_color pack_color(color value);
packed_color pack_color(int red, int green, int blue);
packed_color pack_color(int red, int green, int blue, int alpha);
color unpack_color(packed_color value);
void draw_col(packed_color value);
void set_packed_color(packed_color value);
void set_color(color value);
```

`color` 含 `int r/g/b/a`，可直接修改字段；`rgb`/`rgba` 只是便捷构造器。`pack_color` 把 `0..255` 分量归一化后降低为 `packcolor`，`unpack_color` 执行反向转换并恢复整数分量。`draw_color`/`set_color` 使用四分量命令，`draw_col`/`set_packed_color` 使用打包命令。

## `point` 类型的价值

绘图、雷达、单位控制和世界坐标接口都会反复传递 X/Y。增加值类型 `point` 能显著改善可读性：

```cpp
point start = {10, 20};
point end = {70, 60};

draw_line(start, end);
vec size = {20, 10};
draw_rect(start, size);

draw_line(point{10, 20}, point{70, 60});
draw_rect(rect{{10, 20}, {30, 30}});

start.x += 1;
start.y += 2;
```

`.x`、`.y` 是字段访问，不需要成员函数、引用或指针语义。当前编译器已经实现通用结构体，并内置 `point`、`vec`、`rect`：`point`/`vec` 含 `number x/y`，`rect` 含两个绝对坐标 `point min/max`。绘图内置函数已经同时支持自然聚合形式和原始标量形式。

## 实现难度与改造范围

通用结构体实现已经完成以下改造：

1. 词法器增加 `.`；解析器增加字段访问表达式。
2. 类型系统增加内置 `point`、`vec` 和 `rect`，并保留用户结构体能力。
3. 把表达式结果从单个 operand 扩展为可包含多个分量的值。
4. 局部变量、全局变量和赋值分别为 X/Y 分配底层名称。
5. `p.x`、`p.y` 返回可赋值的标量左值。
6. `point` 参数按值展开成两个静态参数槽。
7. `point` 返回值展开成两个结果槽。
8. 绘图内置函数按参数类型或参数数量接受标量版与 `point` 版。

这些聚合值会递归展开成独立 mlogic 变量，不依赖通用内存、结构体布局或引用，因此仍明显比完整 C 结构体简单。`color` 同样按四个字段展开；`packed_color` 才是与游戏特殊 `double` 位模式对应的单槽不透明标量。

当前编译器不支持普通函数重载。绘图内置函数可以像 `printf` 一样由编译器按参数数量和类型特殊分派，例如同时接受：

```cpp
draw_line(int x1, int y1, int x2, int y2);
draw_line(point start, point end);
```

普通用户函数若也要重载，仍需另行设计签名表和重载决议；实现 `point` 本身不要求先完成通用重载。

## 当前后续工作

绘图子命令与 `color`、`point`、`vec`、`rect` 封装已经完成。后续仍需封装 `sensor` 的 `displayWidth`/`displayHeight`，并为 `draw_image` 增加游戏内容常量类型，使 `@copper`、`@router` 等也能作为图像源。

## 源码依据

- 小屏和大屏尺寸：`mindustry/core/src/mindustry/content/Blocks.java:6886-6900`
- 显示器尺寸感知：`mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:116-120`
- 帧缓冲创建：`mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:141-147`
- 左下原点投影：`mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:162-169`
- 图元坐标消费：`mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:174-218`
- 拼接屏动态尺寸：`mindustry/core/src/mindustry/world/blocks/logic/TileableLogicDisplay.java:138-143`
- 10 位坐标打包：`mindustry/core/src/mindustry/logic/LExecutor.java:1018-1027`
