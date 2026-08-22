# Radar 指令与封装设计

## 1. 指令形式

建筑雷达的原生格式是：

```text
radar target1 target2 target3 sort source order result
```

例如：

```text
radar enemy any any distance turret1 1 target
```

它从 `turret1` 的射程内寻找敌方单位，按距离选择最近目标，并把单位对象或 `null`
写入 `target`。

`uradar` 使用同一执行器和参数规则，但来源固定为当前绑定的 `@unit`：

```text
uradar enemy flying any distance 0 1 target
```

其中固定的 `0` 占据普通 `radar` 的来源字段；执行器实际忽略它并改用 `@unit`。

本文先梳理普通 `radar`；`uradar` 应等单位绑定与控制接口一起封装。

## 2. 三个过滤条件

三个 `target` 条件是逻辑 AND，不是优先级列表，也不是 OR：

```text
radar enemy flying attacker distance turret1 1 target
```

只会匹配同时满足“敌方、飞行、能够射击”的单位。

| 条件 | 含义 |
| --- | --- |
| `any` | 不增加限制 |
| `enemy` | 与来源不同队且不是 `derelict` |
| `ally` | 与来源同队 |
| `player` | 由玩家控制 |
| `attacker` | 单位能够射击 |
| `flying` | 飞行单位 |
| `boss` | Boss 单位 |
| `ground` | 地面单位 |

条件可以重复，但没有收益。`enemy` 与 `ally`、`flying` 与 `ground` 等互斥组合不会找到
目标。少于三个实际条件时，剩余位置应填 `any`。

## 3. 排序与 order

可用排序字段如下：

| 排序字段 | 被比较的值 |
| --- | --- |
| `distance` | 到来源的距离；内部使用负的距离平方 |
| `health` | 当前生命值 |
| `shield` | 当前护盾值 |
| `armor` | 护甲值 |
| `maxHealth` | 最大生命值 |

`order` 由 Mindustry 按布尔值读取：非零为正序方向，零为反向。其实际选择规则是：

| sort | `order = 1` | `order = 0` |
| --- | --- | --- |
| `distance` | 最近 | 最远 |
| `health` | 最高生命值 | 最低生命值 |
| `shield` | 最高护盾 | 最低护盾 |
| `armor` | 最高护甲 | 最低护甲 |
| `maxHealth` | 最高最大生命值 | 最低最大生命值 |

因此把参数简单命名为 `ascending` 或 `descending` 容易误导，特别是 `distance` 的内部键值
带负号。高级接口更适合使用 `bool greatest`，并为常见距离查询提供 `nearest/farthest`
便捷函数。

## 4. 来源、范围与结果

来源必须实现游戏的 `Ranged` 能力，并满足控制权限：

- 普通建筑必须属于处理器队伍，特权建筑通常不能被普通处理器用作来源；
- 单位来源必须具备当前处理器可用的 `LogicAI` 控制状态；
- 搜索半径使用来源自身的 `range()`；
- 候选单位还必须在范围内、可被相应队伍选中，并且不能就是来源本身。

符合条件时结果是单位对象；没有目标或来源无效时稳定写入 `null`。当前语言尚无独立
`unit` 类型，可以先用 `posc` 表示结果，因为单位具备位置能力并可用于 `shootp`：

```cpp
unit target = /* radar result */;
if (target != null) {
    turret1.shootp(target, true);
}
```

若后续加入独立 `unit` 句柄，Radar 应改为返回 `unit`，并允许 `unit` 隐式转换为
`posc`，而不是永久把返回类型定义为泛化的 `posc`。

## 5. 缓存行为

Radar 并非每执行一次都重新扫描：

- 建筑来源大约每 30 tick 更新一次，或来源对象改变时立即更新；
- 单位来源使用 `LogicAI` 的目标更新定时器和指令实例缓存；
- 两次更新时间之间重复执行同一条 Radar 指令会返回缓存目标；
- 缓存属于具体指令实例，不能把它当成即时世界查询。

这意味着在紧密循环中重复调用 Radar 不会获得更高刷新率。移动、死亡或离开射程的目标
也可能在下次更新前短暂保留在结果中，使用前可结合 `target != null` 和 Sensor 状态检查。

## 6. 已实现的语言接口

封装使用两个独立的内置选择器类型，避免把任意字符串或整数传进指令：

```cpp
radar_filter   // any, enemy, ally, player, attacker, flying, boss, ground
radar_sort     // distance, health, shield, armor, max_health
```

项目暂不支持命名空间，裸名称 `enemy`、`health` 容易与用户标识符或 Sensor 名称冲突。
对应保留常量使用明确前缀：

```cpp
radar_any
radar_enemy
radar_ally
radar_player
radar_attacker
radar_flying
radar_boss
radar_ground

radar_distance
radar_health
radar_shield
radar_armor
radar_max_health
```

这些类型仅描述原生指令的编译期枚举槽。用户不能声明对应变量、参数、返回值或结构体
字段，也不能重新定义同名常量。

### 6.1 专用排序成员函数

常用接口把排序字段和方向编码到函数名中，接受 0～3 个 `radar_filter` 参数：

```cpp
unit target = turret1.radar_nearest(
    radar_enemy,
    radar_flying,
    radar_attacker
);
```

它降低为：

```text
radar enemy flying attacker distance turret1 1 result
```

不足三个筛选条件时自动在末尾补 `any`。例如：

```cpp
turret1.radar_nearest();
turret1.radar_nearest(radar_enemy);
turret1.radar_nearest(radar_enemy, radar_flying);
```

现有专用函数如下：

| 函数 | 原生 sort/order | 含义 |
| --- | --- | --- |
| `radar_nearest` | `distance 1` | 最近目标 |
| `radar_farthest` | `distance 0` | 最远目标 |
| `radar_max_health` | `health 1` | 当前生命值最高 |
| `radar_min_health` | `health 0` | 当前生命值最低 |
| `radar_max_shield` | `shield 1` | 护盾最高 |
| `radar_min_shield` | `shield 0` | 护盾最低 |
| `radar_max_armor` | `armor 1` | 护甲最高 |
| `radar_min_armor` | `armor 0` | 护甲最低 |
| `radar_max_max_health` | `maxHealth 1` | 最大生命值最高 |
| `radar_min_max_health` | `maxHealth 0` | 最大生命值最低 |

`radar_max_health_limit` 和 `radar_min_health_limit` 分别是最后两项的可读性别名。

### 6.2 通用成员函数

需要运行时选择方向时使用通用版本：

```cpp
int order = 1;
unit target = turret1.radar(
    radar_enemy,
    radar_flying,
    radar_attacker,
    radar_distance,
    order
);
```

它降低为：

```text
radar enemy flying attacker distance turret1 order result
```

通用版本同样支持 0～3 个筛选条件：

```cpp
turret1.radar(radar_distance, order);
turret1.radar(radar_enemy, radar_distance, order);
turret1.radar(radar_enemy, radar_flying, radar_distance, order);
```

`order` 必须是 `int`，游戏按零/非零解释方向。`sort` 必须是直接写出的内置
`radar_sort` 常量，不能动态传递：原生 mlogic 把 sort 和 filter 编码为指令枚举，而不是
变量操作数，因此底层不存在动态排序字段。编译器无法在不生成分支和多条 Radar 指令的
情况下支持动态 sort。

## 7. 后续问题

1. 建筑 Radar 已返回独立的 `unit` 类型；后续实现单位 Radar 时保持相同返回类型。
2. 是否编译期拒绝明显互斥条件。拒绝 `enemy + ally`、`flying + ground` 很安全；其他
   组合应交给运行时。
3. 是否把 `uradar` 与普通 `radar` 同时实现。两者 lowering 很接近，但前者依赖尚未完整
   封装的单位绑定状态。
