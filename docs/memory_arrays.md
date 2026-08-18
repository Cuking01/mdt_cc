# 内存与类型数组

编译器提供三种内存访问类型：

```cpp
memory mem;
arr<int> values = {cell1, 0};
arr2d<point> points = {bank1, 100, 16};
```

`memory[index]` 读取一个 `number`，地址是 memory 建筑中的绝对槽位。`arr<T>` 保存 memory 句柄和起始偏移，索引结果是 `T` 左值；`arr2d<T>` 额外保存行 stride，第一次索引返回一维 `arr<T>` 右值，因此可以写成 `points[y][x]`。

数组不记录长度，也不执行越界检查。元素类型必须能展开为连续的数值槽位，允许基本数值类型以及 `point`、`vec`、`rect`、`color` 和只包含这些类型的结构体；`string`、建筑句柄、`memory` 和数组类型不能作为元素类型。

```cpp
values[2] = 10;
values[2] += 5;
points[1][3] = point{40, 20};
print(points[1][3].x);
```

`sizeof(arr<T>)` 为 `2`，`sizeof(arr2d<T>)` 为 `3`；元素大小由 `sizeof(T)` 决定，结构体按字段顺序展开。内存访问生成 Mindustry 的 `read` 与 `write` 指令，暂不进行别名分析或越界诊断。
