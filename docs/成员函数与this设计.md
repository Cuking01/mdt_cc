# 成员函数与 this 设计

## 1. 支持范围

成员函数必须直接定义在结构体内部：

```cpp
struct accumulator {
    int value;

    void add(int amount) {
        value += amount;
    }

    int get() {
        return this->value;
    }
};
```

调用语法与 C++ 相同：

```cpp
accumulator value{1};
value.add(2);
int result = value.get();
```

成员函数内部可以直接访问字段和调用同结构体的其他成员函数，也可以显式写
`this->field`、`this->method()`。当前没有函数重载，同一个结构体内字段和方法也不能
使用相同名称。

## 2. ABI lowering

编译器把：

```cpp
struct counter {
    int value;
    void add(int amount) { value += amount; }
};
```

概念上降低为：

```cpp
void __member_counter_add(restrict counter& __this, int amount) {
    __this.value += amount;
}
```

调用 `object.add(3)` 等价于把 `object` 作为第一个引用实参传入。底层仍使用项目现有的
值—结果引用 ABI：调用前复制对象槽，返回后写回原接收者。成员函数无需指针、地址或栈。

隐式 `this` 参数默认带 `restrict`。只有它一个引用参数时，该限定不会改变行为；存在
其他无法证明互不重叠的内存引用参数时，仍要求所有相关参数都为 `restrict`。已知别名
永远报错，例如：

```cpp
struct holder {
    int value;
    void take(restrict int& input) { value = input; }
};

holder object{};
object.take(object.value); // this 与 input 已知重叠，编译错误
```

这与普通引用函数的别名规则完全一致。

## 3. 接收者

成员调用要求可赋值左值，因为函数返回后必须把隐式引用写回：

```cpp
counter local{};
local.add(1);              // 支持
wrapper.inner.add(1);      // 支持
values[index].add(1);      // arr<counter> 内存元素，支持
counter{}.add(1);          // 右值，拒绝
```

内存元素接收者会在调用前冻结句柄和地址，保证参数求值或函数执行不会改变最终写回位置。

## 4. this 不是指针

语言没有通用指针模型，因此 `this->` 被解析为专用整体语法：

```cpp
this->value
this->add(1)
```

以下写法不受支持：

```cpp
this                 // 不能作为值、参数或运算数
object->field        // 没有通用 -> 运算符
*this                // 没有解引用
&object              // 没有可保存的地址
```

未来即使增加函数指针，也不意味着加入对象指针或改变 `this->` 的上述语义。

## 5. 暂不支持

- 成员函数重载；
- `const`、`&`、`&&` 成员函数限定符；
- 静态成员、成员变量初始化器；
- 构造函数、析构函数和其他特殊成员函数；
- 类外成员定义、继承、虚函数；
- 直接或间接递归。

普通值参数、受限引用参数、结构体返回值、控制流以及调用其他普通函数均可在成员函数中
正常使用。
