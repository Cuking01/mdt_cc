# Mindustry `draw` 指令调研报告

## 1. 结论

Mindustry 的 `draw` 不是一条立即在屏幕上执行的绘图命令，而是“向逻辑处理器的图形缓冲区追加命令”。`drawflush display1` 再将当前图形缓冲区转移到目标显示器的命令队列，并无条件清空处理器缓冲区。显示器在渲染阶段异步消费队列。

`draw` 的汇编外形始终是：

```text
draw <type> <x> <y> <p1> <p2> <p3> <p4>
```

`GraphicsType` 按枚举顺序映射到 0–15 的显示命令类型，其中 `col` 和 `print` 是执行器中预处理的虚拟命令，不会原样进入显示器队列。源码依据见：

- `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:23`：底层命令编号。
- `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:296`：`GraphicsType` 的完整集合和顺序。
- `mindustry/core/src/mindustry/logic/LStatements.java:127`：汇编语句的字段布局与编译。
- `mindustry/core/src/mindustry/logic/LExecutor.java:915`：`DrawI` 的执行器预处理。
- `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:156`：显示器命令消费端。

## 2. 通用数值转换与打包

除 `col`、`print`、`image` 和 `scale` 的特殊路径外，参数首先通过 `LVar.numi()` 转成 Java `int`，即数值向 0 截断；对象参数会按“非 `null` 为 1，`null` 为 0”转换。依据为 `LVar.java:56-80`。

每条显示命令被打包进一个 64 位整数：类型占 4 位，`x/y/p1/p2/p3/p4` 各占 10 位，见 `LogicDisplay.java:320-327`。普通数值采用 10 位符号—绝对值表示：

```text
packed = (abs(value) & 0x1ff) | (value < 0 ? 0x200 : 0)
unpacked = (packed & 0x1ff) * (sign ? -1 : 1)
```

因此：

- 可精确表示的整数范围是 `-511..511`；
- 超出范围不是饱和，而是绝对值对 512 回绕；
- `512`、`-512`、`1024` 等值解包后都为 0；
- 坐标、尺寸、颜色分量、边数和旋转角度并没有各自独立的安全打包规则，都受这个限制。

打包和解包的直接依据分别为 `LExecutor.java:1022-1027` 和 `LogicDisplay.java:292-293`。

## 3. 各子命令语义

下表中的“未用”位置在汇编中仍然需要占位，通常填 `0`。参数顺序由 `LStatements.java:172-257` 确认，消费行为由 `LogicDisplay.java:178-218` 确认。

| 子命令 | 汇编参数（`type` 之后） | 执行与副作用 |
| --- | --- | --- |
| `clear` | `r g b 0 0 0` | 将 RGB 整数分别除以 255 后清屏，alpha 永远为 1。先 `Draw.discard()` 丢弃未提交批次，避免旧图元覆盖清屏。不重置颜色、线宽或变换。 |
| `color` | `r g b a 0 0` | 设置显示器持久颜色状态，并影响后续图元。预期分量范围为 0–255。游戏语句读取时会把文本恰好为 `0` 的 alpha 改成 `255`，变量值 0 不触发该改写，见 `LStatements.java:261-266`。 |
| `col` | `packedColor 0 0 0 0 0` | 虚拟命令。对 `packedColor` 的 `double` 原始位低 32 位按 `RRGGBBAA` 拆分，转成一条底层 `color`。汇编器支持 `%rrggbb`、`%rrggbbaa` 和 `%[name]`，见 `LAssembler.java:87-118`。 |
| `stroke` | `width 0 0 0 0 0` | 设置显示器持久线宽状态，数值已在执行器中截断为整数。 |
| `line` | `x y x2 y2 0 0` | 从 `(x,y)` 到 `(x2,y2)` 画线段，使用当前颜色和线宽。 |
| `rect` | `x y width height 0 0` | 从 `(x,y)` 开始绘制实心矩形，底层调用 `Fill.crect`。 |
| `lineRect` | `x y width height 0 0` | 绘制矩形轮廓，使用当前线宽。 |
| `poly` | `x y sides radius rotation 0` | 绘制实心正多边形。显示端仅对上限做 `min(sides, maxSides)`，默认 `maxSides=25`；没有在这一层强制正下限。 |
| `linePoly` | `x y sides radius rotation 0` | 绘制正多边形轮廓，边数上限同为 25，使用当前线宽。 |
| `triangle` | `x1 y1 x2 y2 x3 y3` | 绘制三个顶点指定的实心三角形。 |
| `image` | `x y image size rotation 0` | `image` 必须在运行时是 `UnlockableContent` 或 `LogicDisplayBuild`。内容对象按“内容 ID + 内容类型”打包；显示器按根显示器索引打包。其他对象和纯数值得到无效编码，显示端忽略。`size` 是绘制宽度，高度按图标宽高比自动计算，`rotation` 是角度。不允许显示器直接绘制自己。 |
| `print` | `x y align 0 0 0` | 虚拟命令。读取共享文本缓冲区，计算整段文本对齐偏移，再将每个存在字形的 UTF-16 `char` 展开为一条底层单字符命令。换行不生成图形命令。处理后清空文本缓冲区。 |
| `translate` | `x y 0 0 0 0` | 在显示器持久变换矩阵上追加平移。 |
| `scale` | `x y 0 0 0 0` | 唯一保留小数输入的子命令。执行器先计算 `(int)(value / 0.05)`，再用 10 位有符号格式打包；显示端乘回 0.05。所以精度为 0.05，向 0 量化，不回绕时的可表示缩放约为 `-25.55..25.55`。 |
| `rotate` | `0 0 degrees 0 0 0` | 在持久变换矩阵上追加旋转。角度会先向 0 截断为整数，再受 10 位回绕限制；源码不会主动将它归一化到 0–360 度。 |
| `reset` | `0 0 0 0 0 0` | 将持久变换矩阵重置为单位矩阵。不重置颜色或线宽。 |

### 3.1 `col` 是虚拟命令

`GraphicsType.col.ordinal()` 恰好等于底层 `commandColorPack=2`，但显示器消费端没有 `commandColorPack` 分支。`DrawI` 会先在处理器端将打包颜色拆成 RGBA，然后追加一条真正的 `commandColor`，见 `LExecutor.java:937-947`。因此 `col` 仍只占一个图形缓冲槽位。

### 3.2 `draw print` 与文本缓冲区

`print`、`printchar`和 `format` 共享 `LExecutor.textBuffer`，其上限为 400 个 Java `char`，见 `LExecutor.java:43-58`。`draw print` 具有如下精确行为：

1. 如果图形缓冲区已满 256 条，`DrawI` 在读取文本之前就返回，文本缓冲区不会被清空。
2. 空文本不产生命令。
3. 对齐值是 Arc `Align` 位标志，逻辑全局常量包括 `@topLeft`、`@top`、`@topRight`、`@left`、`@center`、`@right`、`@bottomLeft`、`@bottom`、`@bottomRight`，清单见 `LStatement.java:27-39`。
4. 每个可用字形生成一条图形命令；不支持的字符不绘制，但仍增加水平光标位置。游戏界面文案明确声明只支持 ASCII，而实现还受字体是否有字形以及单字符字段仅 10 位的双重限制。本项目应把 ASCII 作为保证范围。
5. 文本过长导致图形缓冲区达到 256 条时，剩余字符被丢弃，但本次文本缓冲区仍会全部清空。

展开和清空路径见 `LExecutor.java:948-999`，显示端单字符消费见 `LogicDisplay.java:206-213`。

### 3.3 `image` 对象和限制

`image` 不是字符串图片名。它要求 mlogic 变量中实际保存下列对象之一：

- `UnlockableContent`：如 `@copper`、`@router`、`@dagger`，使用对象的 `content type` 和 ID 取图标；
- `LogicDisplayBuild`：如 `display1`，实际使用可拼接显示器的根显示器画面。

只有这两种运行时对象会生成有效编码，见 `LExecutor.java:1003-1011`。显示端保持原图宽高比，并拒绝显示器画自己，见 `LogicDisplay.java:192-204`。

## 4. 缓冲区、刷新和持久状态

- 逻辑处理器图形缓冲区最多 256 条；达到上限后的 `draw` 直接无效。无头服务器中所有 `draw` 都直接无效。
- `drawflush` 只有在目标是可绘制、有效且权限允许的 `LDrawable` 时才传递命令；不论目标是否有效，它最后都会清空处理器图形缓冲区，见 `LExecutor.java:1031-1047`。
- 普通逻辑显示器的待消费队列最多 1024 条。刷新时只接收剩余容量能容纳的前缀，多余命令静默丢弃；`operations` 计数仍加 1，见 `LogicDisplay.java:130-139`。
- 显示器在渲染时消费全部已接收命令。颜色、线宽和变换矩阵都是 `LogicDisplayBuild` 字段，因此跨 `drawflush`、跨逻辑处理器运行周期持久。`clear` 不重置它们；只有 `reset` 重置变换。
- 变换操作按命令顺序累积。变换矩阵还会被保存进存档，见 `LogicDisplay.java:232-255`。

## 5. 本项目的 C 风格 API 建议

### 5.1 使用多个 `draw_*` 自由函数

不建议将所有功能暴露为一个变参 `draw(type, ...)`，原因是：

- 各子命令的参数数量和含义完全不同；
- `image` 需要对象类型，其余大部分需要数值；
- `scale` 保留 0.05 精度，其余几何参数被截断为整数；
- 独立函数能在编译期给出准确的参数数量和类型错误，也不需要为通用变参调用建立 ABI。

当前实现提供以下核心标量签名，并对几何参数增加 `point`、`vec`、`rect` 形式：

```cpp
void draw_clear(int red, int green, int blue);
void draw_clear(color value);
void draw_color(int red, int green, int blue, int alpha);
void draw_color(color value);
void draw_col(packed_color value);
void set_packed_color(packed_color value);
void draw_stroke(int width);
void draw_line(int x1, int y1, int x2, int y2);
void draw_rect(int x, int y, int width, int height);
void draw_line_rect(int x, int y, int width, int height);
void draw_poly(int x, int y, int sides, int radius, int rotation);
void draw_line_poly(int x, int y, int sides, int radius, int rotation);
void draw_triangle(int x1, int y1, int x2, int y2, int x3, int y3);
void draw_image(int x, int y, display image, int size, int rotation);
void draw_print(int x, int y, int align);
void draw_translate(int x, int y);
void draw_scale(number x, number y);
void draw_rotate(int degrees);
void draw_reset();
void drawflush(display target);
```

`lineRect` 和 `linePoly` 转为 C 风格的 `draw_line_rect` 和 `draw_line_poly`，避免在语言 API 中混用 camelCase。`draw_print` 保留与 mlogic 子命令的对应关系，也避免与已有 `print(value)` 冲突。`draw_image` 当前先支持显示器图像源，内容常量将在增加相应类型后开放。

### 5.2 类型设计

- 几何坐标、尺寸、边数、颜色分量和旋转在游戏底层都是整数，第一阶段应声明为 `int`。
- `draw_scale` 确实接受小数，建议使用 `number`。
- `color` 保存四个可访问的 `int r/g/b/a` 分量；`packed_color` 是特殊位模式的单槽值。`pack_color`/`unpack_color` 通过游戏的 `packcolor`/`unpackcolor` 转换，`draw_col` 只接受 `packed_color`。
- `pack_color` 接受一个 `color`，也接受 RGB 三个 `int` 或 RGBA 四个 `int`；三参数形式的 alpha 默认为 255。
- `pack_color` 的分量全部可在编译期确定时，编译器直接生成 Mindustry 原生 `%RRGGBBAA` 字面量；含运行期值时仍生成 `packcolor` 指令。
- `draw_image` 的参数是“内容对象或逻辑显示器”的联合约束，不应误标为普通 `building`。如果暂时不引入通用联合类型，可把 `image_source` 做成仅供内置函数类型检查使用的“能力类型”，允许各种 `UnlockableContent` 常量和 `display` 隐式转入。
- `drawflush` 应要求专用 `display` 类型，不应接收任意 `building`。与 `printflush` 一样，可允许整型字面量 `1` 作为 `display1` 的编译期简写，但不应接收字符串。
- 对齐参数初期可接受 `int` 和预定义的 `@bottomLeft` 等常量；长期建议提供一个封闭 `draw_align` 枚举，避免任意整数误用。

### 5.3 Debug 模式可选检查

这些检查不应影响 release 模式的直接降低，但可在 Debug 模式中阻止最常见的静默回绕：

- 普通打包整数是否在 `-511..511`；
- RGBA 分量是否在 `0..255`；
- `sides` 是否在有意义的范围，且不超过显示器默认上限 25；
- `scale` 是否在不回绕的约 `-25.55..25.55` 范围；
- `draw_image` 的运行时对象是否是允许的内容或显示器；
- 一次 `drawflush` 前的图形命令数是否可能超过 256，尤其是 `draw_print` 的逐字符展开。

## 6. 主要源码证据索引

| 主题 | 源码位置 |
| --- | --- |
| 命令 ID、缩放步长、多边形边数上限 | `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:23-53` |
| `GraphicsType` 完整列表 | `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:296-317` |
| 64 位显示命令字段布局 | `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:320-327` |
| 汇编参数显示、默认值和读取修正 | `mindustry/core/src/mindustry/logic/LStatements.java:127-275` |
| `DrawI` 完整执行路径 | `mindustry/core/src/mindustry/logic/LExecutor.java:915-1028` |
| `drawflush` 的目标检查和无条件清缓冲 | `mindustry/core/src/mindustry/logic/LExecutor.java:1031-1047` |
| 显示器队列接收和 1024 条上限 | `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:125-139` |
| 显示器对所有命令的最终消费 | `mindustry/core/src/mindustry/world/blocks/logic/LogicDisplay.java:156-224` |
| 数值、对象到 `int` 的转换 | `mindustry/core/src/mindustry/logic/LVar.java:35-80` |
| `%rrggbb`、`%rrggbbaa`、`%[name]` 解析 | `mindustry/core/src/mindustry/logic/LAssembler.java:87-118` |
| 显示命令的官方英文/简中说明 | `mindustry/core/assets/bundles/bundle.properties:2898-2910`、`mindustry/core/assets/bundles/bundle_zh_CN.properties:2893-2904` |
