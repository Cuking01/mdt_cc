# Sensor 指令与封装设计

## 1. 底层语义

Mindustry 指令格式为：

```text
sensor result target property
```

执行器先判断 `target` 是否实现 `Senseable`。`property` 是内容对象时调用
`sense(Content)`，是 `LAccess` 时先尝试 `senseObject(LAccess)`，没有对象结果才调用
数值版 `sense(LAccess)`。目标或属性不受支持时，结果通常是 `null`；只有对 `null`
读取 `@dead` 会固定得到 `1`。

字符串是一个特殊接收者：即使它不实现 `Senseable`，`@size` 和 `@bufferSize` 仍返回
字符序列长度。

## 2. 语言接口

通用接口直接接受内置属性常量：

```cpp
number health = target.get(@health);
bool enabled = target.get(@enabled);
item first = target.get(@firstItem);
packed_color team_color = target.get(@color);
```

也可以感知目标中某种内容的数量：

```cpp
number copper = target.get(@copper);
number water = target.get(@water);
number daggers = target.get(@dagger);
number routers = target.get(@router);
```

内容参数只接受 `item`、`liquid`、`unit_kind` 和 `block`。`team` 不是 Content，因而
`target.get(@sharded)` 会在编译期报错。

所有可感知的 `LAccess` 都有无参数的 `get_xxx()` 别名：

```cpp
number health = target.get_health();
bool enabled = target.get_enabled();
string name = @router.get_name();
number length = "text".get_size();
```

两种写法生成相同的 `sensor` 指令。`get()` 当前要求属性参数是直接写出的内置 `@`
常量，不能先保存到 `sensor` 变量后动态传入。`@shoot` 和 `@shootp` 是控制参数而不是
可感知属性，因此不能用于 `get()`。

## 3. 属性与返回类型

下表覆盖当前 Mindustry `LAccess.senseable` 的全部属性。表中的返回类型是编译器为
表达式提供的静态类型；具体目标不支持该属性时，游戏仍可能返回 `null`。

| 属性常量 | 便捷接口 | 静态返回类型 |
| --- | --- | --- |
| `@totalItems` | `get_total_items()` | `number` |
| `@firstItem` | `get_first_item()` | `item` |
| `@totalLiquids` | `get_total_liquids()` | `number` |
| `@totalPower` | `get_total_power()` | `number` |
| `@itemCapacity` | `get_item_capacity()` | `number` |
| `@liquidCapacity` | `get_liquid_capacity()` | `number` |
| `@powerCapacity` | `get_power_capacity()` | `number` |
| `@powerNetStored` | `get_power_net_stored()` | `number` |
| `@powerNetCapacity` | `get_power_net_capacity()` | `number` |
| `@powerNetIn` | `get_power_net_in()` | `number` |
| `@powerNetOut` | `get_power_net_out()` | `number` |
| `@ammo` | `get_ammo()` | `number` |
| `@ammoCapacity` | `get_ammo_capacity()` | `number` |
| `@currentAmmoType` | `get_current_ammo_type()` | `sensor_value` |
| `@memoryCapacity` | `get_memory_capacity()` | `number` |
| `@health` | `get_health()` | `number` |
| `@maxHealth` | `get_max_health()` | `number` |
| `@heat` | `get_heat()` | `number` |
| `@shield` | `get_shield()` | `number` |
| `@armor` | `get_armor()` | `number` |
| `@efficiency` | `get_efficiency()` | `number` |
| `@progress` | `get_progress()` | `number` |
| `@timescale` | `get_timescale()` | `number` |
| `@rotation` | `get_rotation()` | `number` |
| `@x` / `@y` | `get_x()` / `get_y()` | `number` |
| `@velocityX` / `@velocityY` | `get_velocity_x()` / `get_velocity_y()` | `number` |
| `@shootX` / `@shootY` | `get_shoot_x()` / `get_shoot_y()` | `number` |
| `@cameraX` / `@cameraY` | `get_camera_x()` / `get_camera_y()` | `number` |
| `@cameraWidth` / `@cameraHeight` | `get_camera_width()` / `get_camera_height()` | `number` |
| `@displayWidth` / `@displayHeight` | `get_display_width()` / `get_display_height()` | `number` |
| `@bufferSize` | `get_buffer_size()` | `number` |
| `@operations` | `get_operations()` | `number` |
| `@size` | `get_size()` | `number` |
| `@solid` | `get_solid()` | `bool` |
| `@dead` | `get_dead()` | `bool` |
| `@range` | `get_range()` | `number` |
| `@shooting` | `get_shooting()` | `bool` |
| `@boosting` | `get_boosting()` | `bool` |
| `@mineX` / `@mineY` | `get_mine_x()` / `get_mine_y()` | `number` |
| `@mining` | `get_mining()` | `bool` |
| `@buildX` / `@buildY` | `get_build_x()` / `get_build_y()` | `number` |
| `@pingX` / `@pingY` | `get_ping_x()` / `get_ping_y()` | `number` |
| `@pingText` | `get_ping_text()` | `string` |
| `@building` | `get_building()` | 见下文 |
| `@breaking` | `get_breaking()` | 见下文 |
| `@speed` | `get_speed()` | `number` |
| `@team` | `get_team()` | `number` |
| `@type` | `get_type()` | 见下文 |
| `@flag` | `get_flag()` | `number` |
| `@flying` | `get_flying()` | `bool` |
| `@controlled` | `get_controlled()` | `number` |
| `@controller` | `get_controller()` | `posc` |
| `@name` | `get_name()` | `string` |
| `@payloadCount` | `get_payload_count()` | `number` |
| `@payloadType` | `get_payload_type()` | `sensor_value` |
| `@totalPayload` | `get_total_payload()` | `number` |
| `@payloadCapacity` | `get_payload_capacity()` | `number` |
| `@maxUnits` | `get_max_units()` | `number` |
| `@id` | `get_id()` | `number` |
| `@selectedBlock` | `get_selected_block()` | `block` |
| `@selectedRotation` | `get_selected_rotation()` | `number` |
| `@bulletLifetime` | `get_bullet_lifetime()` | `number` |
| `@bulletTime` | `get_bullet_time()` | `number` |
| `@enabled` | `get_enabled()` | `bool` |
| `@config` | `get_config()` | `sensor_value` |
| `@color` | `get_color()` | `packed_color` |

`@type` 对 `building`、`message`、`display` 和 `memory` 返回 `block`；对其余静态接收者
返回 `sensor_value`。`@building` 对 `building` 类接收者表示正在建造的 `block`，对
`posc` 接收者表示单位正在建造的 `building`。`@breaking` 对 `building` 类接收者是
布尔状态，对 `posc` 接收者则是单位正在拆除的 `building`。

## 4. 动态对象结果

`sensor_value` 是单槽、不透明的运行时对象值，用于无法静态确定具体对象种类的结果：

- `@currentAmmoType` 可能是 `item`、`liquid` 或 `null`；
- `@payloadType` 可能是 `unit_kind`、`block` 或 `null`；
- `@config` 取决于具体建筑，可能是内容对象、建筑、数值、布尔值或 `null`；
- 对静态类型不足以确定结果的 `@type` 也使用 `sensor_value`。

它不能参与算术、隐式转换或存入 `memory`/`arr<T>`，但可以继续作为 `sensor` 的接收者：

```cpp
sensor_value config = target.get_config();
string config_name = config.get_name();
```

若运行时值并非可感知对象，结果仍为 `null`。

## 5. 空值检查

Sensor 即使具有 `number`、`bool` 等静态返回类型，目标不支持属性时仍可能写入运行时
`null`。语言提供真正的 `null` 字面量：

```cpp
number health = target.get_health();
if (health == null) {
    // target 不支持 @health
}

sensor_value config = target.get_config();
if (config != null) {
    print(config.get_name());
}
```

任何单槽值都可以与 `null` 使用 `==` 或 `!=` 比较。这两种比较强制使用底层
`strictEqual`，所以 `0 == null` 为假，运行时对象态空值与 `null` 比较才为真。
`null` 可以赋给 `building`、`posc`、内容句柄、`string` 和 `sensor_value` 等对象类型，
但不能赋给数值、布尔值、结构体或数组。

## 6. 接收者与限制

当前允许的接收者包括 `building`、`posc`、`message`、`display`、`memory`、`item`、
`liquid`、`block`、`unit_kind`、`team`、`string` 和 `sensor_value`。编译器只检查接收者
具备潜在感知意义，不试图根据每一种具体建筑证明某个属性一定有效；这种检查需要保留
到 Mindustry 运行时。

`sensor` 和 `sensor_value` 都不能存入内存元。前者仅用于表示内置属性选择器，后者可能
持有对象引用，而 Mindustry 内存元只保存数值。
