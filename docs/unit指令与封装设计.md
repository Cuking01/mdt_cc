# Unit 指令与封装设计

本文根据仓库内 Mindustry 的 `LUnitControl`、`LLocate`、`LStatements` 和 `LExecutor` 源码，梳理单位相关的四条逻辑指令：`ubind`、`ucontrol`、`uradar`、`ulocate`。

这四条指令共享一个重要状态：处理器内置变量 `@unit`。`ubind` 改写它，其他三条指令隐式读取它。因此它们不是对任意 `unit` 对象执行操作的普通函数集合，而是一套带有全局绑定状态的单位控制 API。

## 1. 共同前提

普通处理器执行这些指令时，必须满足：

* 地图规则 `logicUnitControl` 已启用，或处理器是特权处理器；
* 单位通常必须属于处理器队伍；
* 单位类型必须允许逻辑控制（`logicControllable`）；
* 单位对象必须有效，且控制器可以被 `LogicAI` 接管。

权限或对象检查失败时，大多数指令静默无操作，不会返回错误。`uradar` 会把输出写成 `null`；`ulocate` 在没有有效单位时只保证 `found=false`，其余输出可能保留旧值。因此高级封装不能把“调用完成”解释成“动作成功”。

单位控制还具有持续性：成功的 `ucontrol` 通常会刷新约 10 秒的逻辑控制计时器；如果处理器长期不再控制该单位，单位会恢复原有控制器。物品转移操作另有约 1.5 秒的限频。

坐标参数使用逻辑世界坐标，即地图瓦片中心为整数坐标；游戏内部会把逻辑坐标乘以瓦片尺寸转换为世界坐标。`build`、`deconstruct`、`getBlock` 等命令最终会按瓦片坐标取整。

## 2. `ubind`

原生格式：

```text
ubind <unit-type-or-unit>
```

### 2.1 绑定单位类型

当参数是 `UnitType`（汇编中通常为 `@dagger`、`@poly` 等）时，执行器从处理器队伍的该类型单位缓存中选取下一个单位。重复执行同一条 `ubind @dagger` 会轮询这些单位，而不是每次都得到同一个单位。

没有该类型单位、单位不可逻辑控制、参数不是合法单位类型时，`@unit` 被写成 `null`。

### 2.2 绑定具体单位

当参数本身是单位对象时，执行器可以直接绑定该对象，但要求单位属于处理器队伍（特权处理器可绕过队伍限制）且类型允许逻辑控制。编译器使用两个独立类型区分种类和实例：

* `unit_kind`：单位类型，例如 `@dagger`；
* `unit`：运行时单位实例，例如 `@unit` 或雷达结果。

语言层的 `unit_bind(unit_kind)` 和 `unit_bind(unit)` 在执行 `ubind` 后复制 `@unit` 并返回
独立的 `unit` 句柄。没有可绑定单位或权限检查失败时返回 `null`。

## 3. `ucontrol`

原生格式为固定五参数槽：

```text
ucontrol <subcommand> <p1> <p2> <p3> <p4> <p5>
```

未使用的参数仍然存在，通常填 `0`。以下表格只列出实际使用的槽位。

| 子命令 | 参数 | 语义 |
|---|---|---|
| `idle` | 无 | 保持原地，但继续当前采矿/建造动作。 |
| `stop` | 无 | 停止移动相关动作，并清除采矿/建造状态。 |
| `move` | `x, y` | 移动到逻辑坐标。 |
| `approach` | `x, y, radius` | 接近坐标，直到距离不大于半径。 |
| `pathfind` | `x, y` | 移动到坐标；地面单位使用寻路，飞行单位按直线移动。 |
| `autoPathfind` | 无 | 自动寻找敌方核心或波次出生点并移动。 |
| `boost` | `enable` | 设置助推开关；只有支持助推的单位会产生效果。 |
| `target` | `x, y, shoot` | 设置瞄准坐标，并设置是否开火。 |
| `targetp` | `unit, shoot` | 以单位或其他实现 `Teamc` 的对象为目标，使用提前量瞄准，并设置是否开火。无效目标会使目标为空。 |
| `itemDrop` | `building, amount` | 向己方、有效且在转移范围内的建筑存入携带物品；`@air` 可用于清空携带物品。 |
| `itemTake` | `building, item, amount` | 从己方建筑取出指定物品，受容量、距离和限频限制。 |
| `payDrop` | 无 | 卸下当前载荷。 |
| `payTake` | `takeUnits` | 从当前位置拾取单位或建筑载荷；参数为真时优先尝试拾取单位。 |
| `payEnter` | 无 | 进入/降落到单位下方且允许控制的载荷建筑。 |
| `mine` | `x, y` | 设置采矿目标；目标必须在采矿范围内且确实存在可采资源。 |
| `flag` | `value` | 写入单位的数值标记。 |
| `build` | `x, y, block, rotation, config` | 建造指定方块。需要规则允许单位建造、方块可建造且单位满足建造条件；`rotation` 按 4 取模。配置可为内容对象或建筑对象。 |
| `deconstruct` | `x, y` | 拆除坐标处建筑，需要规则允许单位拆除且目标可拆除。 |
| `getBlock` | `x, y, type, building, floor` | 查询坐标处方块、建筑和地块/矿墙类型，写入三个输出槽。超出单位建造范围或没有 tile 时三者都写 `null`。 |
| `within` | `x, y, radius, result` | 判断单位是否在坐标半径内，把布尔结果写入 `result`。该查询不要求接管单位控制器。 |
| `unbind` | 无 | 如果单位当前由逻辑 AI 控制，则恢复其原有控制器。 |

`ucontrol` 没有统一的成功返回值。`within` 是唯一明确的布尔输出；`getBlock` 是三个对象输出。`itemDrop`、`itemTake`、`pay*`、`build` 和 `deconstruct` 失败时通常静默等待或无操作。

### 3.1 控制与查询的区别

`stop` 会清理单位的采矿和建造状态；`idle` 不会清理这些状态。`unbind` 则是控制器层面的操作，执行后后续 `ucontrol` 需要重新通过 `LogicAI` 接管。

`target` 的目标是坐标，`targetp` 的目标是实现 `Teamc` 的运行时对象（通常是单位或建筑）。后者不是“传入单位 ID”，也不是任意 `posc`；语言层应让 `unit`（若以后加入）以及其他明确实现 `Teamc` 的句柄作为参数。

## 4. `uradar`

原生格式：

```text
uradar <target1> <target2> <target3> <sort> <order> <result>
```

它与普通 `radar` 使用相同的筛选和排序规则，但来源固定为当前 `@unit`，没有建筑来源参数。三个筛选条件是逻辑 AND：

* `any`、`enemy`、`ally`、`player`、`attacker`、`flying`、`boss`、`ground`；
* 排序为 `distance`、`health`、`shield`、`armor`、`maxHealth`；
* `order` 为零/非零方向开关。对 `distance` 来说，非零选择最近目标；对其他指标，非零选择最大值。

绑定单位必须有效、可由当前处理器控制，并且具备 `Ranged` 能力。结果是单位对象或 `null`。单位雷达使用 `LogicAI` 的目标更新周期，同一条指令不会每次执行都重新扫描；缓存刷新周期由游戏 AI 控制，不能假定为每个逻辑 tick 一次。

建议接口与普通雷达保持一致，但去掉来源接收者：

```cpp
posc target = uradar_nearest(radar_enemy, radar_flying);
posc target = uradar(radar_enemy, radar_distance, order);
```

筛选器和排序器应继续使用不可自定义的内置类型及常量，避免把任意整数误当作枚举。

## 5. `ulocate`

原生格式按查询种类变化：

```text
ulocate <kind> ... <outX> <outY> <found> <outBuild>
```

查询种类如下：

| kind | 额外参数 | 查找目标 |
|---|---|---|
| `ore` | `item` | 当前单位附近最近的指定矿物。 |
| `building` | `group, enemy` | 全图最近的指定建筑旗标；`enemy` 为真查敌方，否则查己方。 |
| `spawn` | 无 | 敌方出生点；可能是核心或地图出生坐标。 |
| `damaged` | 无 | 己方受损建筑。 |

输出是 `x`、`y`、`found`，以及除 `ore` 外的 `building`。坐标使用逻辑坐标。查找结果会缓存：未到更新时机时返回缓存坐标、建筑和 found。

失败语义需要特别注意：

* 没有有效绑定单位时只写 `found=false`，其他输出可能保留旧值；
* 没找到目标时写 `found=false`，建筑输出写 `null`，坐标仍可能保留上一次值；
* 找到坐标但建筑不在单位可访问范围内时，建筑输出可能为 `null`，坐标和 `found` 仍有效。

建筑筛选器使用固定 `block_flag` 常量，例如 `block_core`、`block_turret`、`block_factory` 等。实现 `ulocate` 时应返回一个结果结构体，而不是模拟 C++ 多返回值：

```cpp
struct locate_result {
    number x;
    number y;
    bool found;
    building build;
};
```

编译器生成代码前应初始化所有字段，避免原生指令的“旧值残留”泄漏到高级语言。

## 6. 语言层建议

第一阶段不把四条指令暴露为任意字符串形式，而提供类型化接口：

```cpp
unit worker = unit_bind(@dagger);
worker.move(10, 20);
worker.approach(point{10, 20}, 4);
worker.target(point{10, 20}, true);
worker.targetp(enemy, true);
bool close = worker.within(point{10, 20}, 8);
worker.unbind();
```

建议的最小内置类型：

| 类型 | 用途 |
|---|---|
| `unit_kind` | `@dagger` 等单位类型常量；不能与普通数值运算。 |
| `unit` | 运行时单位实例；初期可由 `posc` 代替，但应保留独立类型的设计空间。 |
| `block_flag` | `ulocate building` 的建筑分类常量。 |
| `unit_control` | 若保留通用 `ucontrol` 入口，用于不可自定义的子命令枚举；更推荐专用函数名。 |
| `locate_kind` | `ore/building/spawn/damaged` 编译期枚举。 |

`ucontrol` 统一封装为 `unit` 成员函数。编译器在动作前自动生成 `ubind receiver`，并由 IR
删除刚执行 `unit_bind` 后及连续控制同一单位时的冗余绑定：

```cpp
unit worker = unit_bind(@poly);
worker.move(x, y);
worker.mine(x, y);
worker.get_block(x, y, blockType, building, floor);
```

`get_block_type`、`get_block_building`、`get_block_floor` 分别返回原生 `getBlock` 的一个输出；
`within` 直接返回 `bool`。`item_drop(building, amount)` 用于向建筑卸货，
`discard_items(amount)` 明确生成以 `@air` 为目标的丢弃操作。建造旋转既可传数值，也可使用
`build_right/build_up/build_left/build_down`。

## 7. 实现顺序与限制

推荐顺序：

1. `[已完成]` 增加 `unit` 运行时句柄和 `unit_kind` 常量检查；
2. `[已完成]` 实现 `unit_bind` 和全部 `ucontrol` 成员接口；
3. `[计划中]` 实现 `uradar`；
4. `[计划中]` 实现返回结构体的 `ulocate`。

初期应明确暂不支持：

* 递归或异步单位控制；
* 把单位对象序列化、保存到数组后再恢复；
* 对 `ucontrol` 的动作成功与否作静态保证；
* 把 `ulocate` 的原生旧值残留直接暴露给用户代码。

测试可以继续使用项目现有的模拟执行器：记录并验证生成的 `ubind/ucontrol/uradar/ulocate` 指令、参数槽和控制流，不需要完整模拟游戏 AI、寻路或实际单位物理。
