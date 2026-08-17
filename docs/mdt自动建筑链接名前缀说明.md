# Mindustry 自动建筑链接变量前缀说明

## 结论

Mindustry 的自动链接名不是完整建筑内部名加序号，而是先通过 `LogicBlock.getLinkName(Block)` 从内部名提取一个较短前缀，再由 `findLinkName(Block)` 补上从 1 开始的可用序号。因此，不同建筑经常得到相同前缀，例如 `logic-display`、`large-logic-display` 和 `tile-logic-display` 都会生成 `displayN`。

当前编译器把 [`src/compiler.cpp`](../src/compiler.cpp) 中 `implicitLinkType` 的固定前缀集合识别为隐式外部链接：`messageN` 的类型是 `message`，`displayN` 的类型是 `display`，其余是 `building`。这份集合混入了地板、矿脉、装饰物、导弹内部名等通常不可能产生建筑链接的内容，所以“被编译器接受”不等于“游戏中一定能出现该链接”。

## 游戏命名规则

规则依据 [`LogicBlock.java`](../mindustry/core/src/mindustry/world/blocks/logic/LogicBlock.java)：

1. `getLinkName` 先取 `block.name`。
2. 内部名不含 `-` 时，直接使用完整名称。例如 `message` 得到 `message`。
3. 内部名含 `-` 时，通常取最后一段。例如 `logic-display` 得到 `display`，`reinforced-liquid-container` 得到 `container`。
4. 如果最后一段是 `large` 或可解析为浮点数，则改取倒数第二段。例如 `battery-large` 得到 `battery`，`metal-floor-2` 得到 `floor`。
5. `findLinkName` 扫描已有链接。仅当已有名称以该前缀开头、且剩余部分能解析为整数时，才把该整数标为占用；随后从 1 起选择第一个未占用序号，返回“前缀 + 序号”。
6. 判断使用 `startsWith`，而不是严格验证“同种建筑”。因此共用前缀的不同建筑共享同一编号空间；已有的非标准名称若恰好满足“前缀 + 整数”，也会占号。

例如两个逻辑显示屏与一个大型逻辑显示屏可能依次得到 `display1`、`display2`、`display3`，名称本身不能区分具体建筑型号。

## 当前前缀清单

中文名优先取自 `mindustry/core/assets/bundles/bundle_zh_CN.properties` 的 `block.<内部名>.name`。表中的“来源”是把本地化文件中的所有 block 内部名按 `getLinkName` 规则反向归组得到的；这能揭示碰撞，但不代表每个内容都可被逻辑处理器链接。

| 前缀 | 对应内部名与中文名/可能来源 | 核对结果 |
| --- | --- | --- |
| `accelerator` | `interplanetary-accelerator`（行星际加速器） | 单一来源 |
| `acropolis` | `core-acropolis`（卫城核心） | 单一来源 |
| `afflict` | `afflict`（劫难） | 单一来源 |
| `air` | `air`（空气/空方块（源码哨兵，无 block.air.name）） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `arc` | `arc`（电弧） | 单一来源 |
| `assembler` | `tank-assembler`（坦克组装厂）<br>`ship-assembler`（飞船组装厂）<br>`mech-assembler`（机甲组装厂） | **碰撞：3 个来源** |
| `bank` | `memory-bank`（内存库） | 单一来源 |
| `basalt` | `basalt`（玄武岩） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `bastion` | `core-bastion`（城堡核心） | 单一来源 |
| `battery` | `battery`（电池）<br>`battery-large`（大型电池） | **碰撞：2 个来源** |
| `beryllium` | `ore-beryllium`（铍矿（由 item.beryllium.name“铍”派生；无对应 block 名称键）） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `blocks` | `crystal-blocks`（风化晶体） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `bluemat` | `bluemat`（蓝地垫） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `bore` | `plasma-bore`（等离子钻机）<br>`large-plasma-bore`（高级等离子钻机） | **碰撞：2 个来源** |
| `boulder` | `sand-boulder`（砂岩）<br>`basalt-boulder`（玄武岩石块）<br>`boulder`（石块）<br>`snow-boulder`（雪石块）<br>`shale-boulder`（页岩石块）<br>`dacite-boulder`（安山石块）<br>`carbon-boulder`（碳石块）<br>`ferric-boulder`（铁石块）<br>`beryllic-boulder`（铍石块）<br>`yellow-stone-boulder`（黄石块）<br>`arkyic-boulder`（芳石块）<br>`crystalline-boulder`（晶石块）<br>`red-ice-boulder`（红冰石块）<br>`rhyolite-boulder`（流纹石块）<br>`red-stone-boulder`（红石块） | **碰撞：15 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `breach` | `breach`（撕裂） | 单一来源 |
| `bridge` | `duct-bridge`（物品管道桥） | 单一来源 |
| `bush` | `pur-bush`（紫灌木丛） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `canvas` | `canvas`（画板）<br>`large-canvas`（大型画板） | **碰撞：2 个来源** |
| `cell` | `world-cell`（世界内存元）<br>`memory-cell`（内存元） | **碰撞：2 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `centrifuge` | `coal-centrifuge`（煤炭离心机）<br>`slag-centrifuge`（矿渣离心机） | **碰撞：2 个来源** |
| `chamber` | `oxidation-chamber`（氧化室）<br>`chemical-combustion-chamber`（化学燃烧室） | **碰撞：2 个来源** |
| `char` | `char`（焦土） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `citadel` | `core-citadel`（堡垒核心） | 单一来源 |
| `cliff` | `cliff`（悬崖） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `cluster` | `spore-cluster`（孢子簇）<br>`crystal-cluster`（水晶簇）<br>`vibrant-crystal-cluster`（鲜艳水晶簇） | **碰撞：3 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `compressor` | `plastanium-compressor`（塑钢压缩机） | 单一来源 |
| `concentrator` | `atmospheric-concentrator`（大气收集器） | 单一来源 |
| `condenser` | `turbine-condenser`（涡轮冷凝器）<br>`vent-condenser`（排气冷凝器） | **碰撞：2 个来源** |
| `conduit` | `conduit`（导管）<br>`pulse-conduit`（脉冲导管）<br>`plated-conduit`（电镀导管）<br>`phase-conduit`（相织布导管桥）<br>`bridge-conduit`（导管桥）<br>`reinforced-conduit`（强化导管）<br>`reinforced-bridge-conduit`（强化流体带桥） | **碰撞：7 个来源** |
| `constructor` | `constructor`（构筑器）<br>`large-constructor`（大型构筑器） | **碰撞：2 个来源** |
| `container` | `liquid-container`（流体容器）<br>`container`（容器）<br>`reinforced-liquid-container`（强化流体容器）<br>`reinforced-container`（强化容器） | **碰撞：4 个来源** |
| `conveyor` | `conveyor`（传送带）<br>`titanium-conveyor`（钛传送带）<br>`plastanium-conveyor`（塑钢传送带）<br>`armored-conveyor`（装甲传送带）<br>`phase-conveyor`（相织布传送带桥）<br>`bridge-conveyor`（传送带桥）<br>`payload-conveyor`（载荷传送带）<br>`surge-conveyor`（合金传送带）<br>`reinforced-payload-conveyor`（强化载荷传送带） | **碰撞：9 个来源** |
| `crater` | `rhyolite-crater`（流纹岩坑） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `craters` | `ferric-craters`（铁陨石坑） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `crucible` | `silicon-crucible`（热能坩埚）<br>`carbide-crucible`（碳化物坩埚）<br>`surge-crucible`（合金坩埚） | **碰撞：3 个来源** |
| `crusher` | `cliff-crusher`（墙壁粉碎机）<br>`large-cliff-crusher`（高级墙壁粉碎机） | **碰撞：2 个来源** |
| `crux` | `rune-overlay-crux`（符文贴片 （红队）） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `cryofluid` | `pooled-cryofluid`（冷冻液） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `cultivator` | `cultivator`（培养机） | 单一来源 |
| `cyclone` | `cyclone`（气旋） | 单一来源 |
| `dacite` | `dacite`（安山岩） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `damaged` | `metal-floor-damaged`（损坏的金属地板） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `darksand` | `darksand`（黑沙） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `dead` | `white-tree-dead`（枯萎的白树） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `deconstructor` | `deconstructor`（大型解构器）<br>`small-deconstructor`（解构器） | **碰撞：2 个来源** |
| `diffuse` | `diffuse`（扩散） | 单一来源 |
| `diode` | `diode`（二极管） | 单一来源 |
| `dirt` | `dirt`（泥土） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `disassembler` | `disassembler`（解离机） | 单一来源 |
| `disperse` | `disperse`（驱离） | 单一来源 |
| `display` | `logic-display`（逻辑显示屏）<br>`large-logic-display`（大型逻辑显示屏）<br>`tile-logic-display`（逻辑显示单元） | **碰撞：3 个来源** |
| `distributor` | `distributor`（分配器） | 单一来源 |
| `dome` | `overdrive-dome`（超速穹顶） | 单一来源 |
| `door` | `door`（门）<br>`door-large`（大门）<br>`blast-door`（防爆闸门） | **碰撞：3 个来源** |
| `drill` | `mechanical-drill`（机械钻头）<br>`pneumatic-drill`（气动钻头）<br>`laser-drill`（激光钻头）<br>`blast-drill`（爆破钻头）<br>`impact-drill`（冲击钻头）<br>`eruption-drill`（爆裂钻头） | **碰撞：6 个来源** |
| `driver` | `mass-driver`（质量驱动器）<br>`large-payload-mass-driver`（大型载荷质量驱动器）<br>`payload-mass-driver`（载荷质量驱动器） | **碰撞：3 个来源** |
| `duct` | `duct`（物品管道）<br>`armored-duct`（装甲管道）<br>`overflow-duct`（溢流管道）<br>`underflow-duct`（反向溢流管） | **碰撞：4 个来源** |
| `duo` | `duo`（双管） | 单一来源 |
| `electrolyzer` | `electrolyzer`（电解机） | 单一来源 |
| `empty` | `empty`（空） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `extractor` | `water-extractor`（抽水机）<br>`oil-extractor`（石油钻井） | **碰撞：2 个来源** |
| `fabricator` | `tank-fabricator`（坦克制造厂）<br>`mech-fabricator`（机甲制造厂）<br>`ship-fabricator`（飞船制造厂） | **碰撞：3 个来源** |
| `factory` | `ground-factory`（陆军工厂）<br>`air-factory`（空军工厂）<br>`naval-factory`（海军工厂） | **碰撞：3 个来源** |
| `floor` | `sand-floor`（沙子）<br>`metal-floor`（金属地板 1）<br>`metal-floor-2`（金属地板 2）<br>`metal-floor-3`（金属地板 3）<br>`metal-floor-4`（金属地板 4）<br>`metal-floor-5`（金属地板 5）<br>`colored-floor`（染色地板）<br>`crystal-floor`（晶石地板）<br>`arkycite-floor`（芳油） | **碰撞：9 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `foreshadow` | `foreshadow`（厄兆） | 单一来源 |
| `foundation` | `core-foundation`（次代核心） | 单一来源 |
| `furnace` | `silicon-arc-furnace`（电弧硅炉） | 单一来源 |
| `fuse` | `fuse`（雷光） | 单一来源 |
| `gate` | `overflow-gate`（溢流门）<br>`underflow-gate`（反向溢流门） | **碰撞：2 个来源** |
| `generator` | `combustion-generator`（火力发电机）<br>`steam-generator`（涡轮发电机）<br>`differential-generator`（温差发电机）<br>`thermal-generator`（热能发电机）<br>`rtg-generator`（RTG 发电机）<br>`pyrolysis-generator`（热解发生器） | **碰撞：6 个来源** |
| `gigantic` | `scrap-wall-gigantic`（超巨型废墙） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `graphite` | `ore-graphite`（石墨矿（由 item.graphite.name“石墨”派生；无对应 block 名称键）） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `grass` | `grass`（草地） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `hail` | `hail`（冰雹） | 单一来源 |
| `heater` | `electric-heater`（电制热机）<br>`slag-heater`（矿渣制热机）<br>`phase-heater`（相织制热机） | **碰撞：3 个来源** |
| `hotrock` | `hotrock`（灼热岩石） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `huge` | `scrap-wall-huge`（巨型废墙） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `ice` | `ice`（冰）<br>`red-ice`（红冰） | **碰撞：2 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `illuminator` | `illuminator`（照明器） | 单一来源 |
| `incinerator` | `incinerator`（焚化炉）<br>`slag-incinerator`（矿渣焚化炉） | **碰撞：2 个来源** |
| `junction` | `junction`（交叉器）<br>`liquid-junction`（流体交叉器）<br>`reinforced-liquid-junction`（强化流体交叉器） | **碰撞：3 个来源** |
| `kiln` | `kiln`（窑炉） | 单一来源 |
| `lancer` | `lancer`（蓝瑟） | 单一来源 |
| `link` | `beam-link`（激光连接器） | 单一来源 |
| `loader` | `payload-loader`（载荷装载器）<br>`unit-cargo-loader`（单位物流装载器） | **碰撞：2 个来源** |
| `lustre` | `lustre`（光辉） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `magmarock` | `magmarock`（熔融岩石） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `malign` | `malign`（魔灵） | 单一来源 |
| `meltdown` | `meltdown`（熔毁） | 单一来源 |
| `melter` | `melter`（熔炉） | 单一来源 |
| `mender` | `mender`（修理器） | 单一来源 |
| `message` | `message`（信息板）<br>`reinforced-message`（强化信息板）<br>`world-message`（世界信息板） | **碰撞：3 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `metal` | `dark-metal`（暗金属） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `mine` | `shock-mine`（脉冲地雷） | 单一来源 |
| `missile` | `*-missile-*`（导弹单位/贴图内部名，不是可链接建筑；无 block 名称键） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `mixer` | `cryofluid-mixer`（冷冻液混合器）<br>`pyratite-mixer`（硫化物混合器）<br>`blast-mixer`（爆炸物混合器） | **碰撞：3 个来源** |
| `module` | `basic-assembler-module`（基本装配厂模块） | 单一来源 |
| `moss` | `moss`（苔藓地）<br>`spore-moss`（孢子苔藓地） | **碰撞：2 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `mud` | `mud`（泥巴） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `node` | `power-node`（电力节点）<br>`power-node-large`（大型电力节点）<br>`beam-node`（激光节点） | **碰撞：3 个来源** |
| `nucleus` | `core-nucleus`（终代核心） | 单一来源 |
| `orbs` | `crystal-orbs`（晶石球） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `ore` | `remove-ore`（移除矿） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `overlay` | `character-overlay`（标识贴片）<br>`rune-overlay`（符文贴片） | **碰撞：2 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `pad` | `launch-pad`（发射台[lightgray]（旧版））<br>`advanced-launch-pad`（发射台）<br>`landing-pad`（接收台） | **碰撞：3 个来源** |
| `panel` | `dark-panel-1`（暗面板 1）<br>`dark-panel-2`（暗面板 2）<br>`dark-panel-3`（暗面板 3）<br>`dark-panel-4`（暗面板 4）<br>`dark-panel-5`（暗面板 5）<br>`dark-panel-6`（暗面板 6）<br>`solar-panel`（太阳能板）<br>`solar-panel-large`（大型太阳能板） | **碰撞：8 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `parallax` | `parallax`（差扰） | 单一来源 |
| `pebbles` | `pebbles`（鹅卵石） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `phase` | `scathe-missile-phase 等`（导弹变体末段，不是建筑前缀；无 block 名称键） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `pine` | `spore-pine`（孢子树）<br>`snow-pine`（雪树）<br>`pine`（松树） | **碰撞：3 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `plates` | `yellow-stone-plates`（黄石地板） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `point` | `repair-point`（维修点）<br>`unit-cargo-unload-point`（单位物流卸载点） | **碰撞：2 个来源** |
| `press` | `graphite-press`（石墨压缩机）<br>`multi-press`（多重压缩机）<br>`spore-press`（孢子压缩机） | **碰撞：3 个来源** |
| `processor` | `world-processor`（世界处理器）<br>`micro-processor`（微型处理器）<br>`logic-processor`（逻辑处理器）<br>`hyper-processor`（超核处理器） | **碰撞：4 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `projector` | `mend-projector`（修理投影）<br>`overdrive-projector`（超速投影）<br>`force-projector`（力墙投影）<br>`regen-projector`（再生投影器）<br>`shield-projector`（护盾投影器）<br>`large-shield-projector`（大型护盾投影器） | **碰撞：6 个来源** |
| `pulverizer` | `pulverizer`（粉碎机） | 单一来源 |
| `pump` | `mechanical-pump`（机械泵）<br>`rotary-pump`（回转泵）<br>`impulse-pump`（脉冲泵）<br>`reinforced-pump`（强化泵） | **碰撞：4 个来源** |
| `radar` | `radar`（雷达） | 单一来源 |
| `reactor` | `impact-reactor`（冲击反应堆）<br>`thorium-reactor`（钍反应堆）<br>`heat-reactor`（热量反应堆）<br>`flux-reactor`（通量反应堆）<br>`neoplasia-reactor`（瘤变反应堆） | **碰撞：5 个来源** |
| `reconstructor` | `additive-reconstructor`（数增级单位重构工厂）<br>`multiplicative-reconstructor`（倍乘级单位重构工厂）<br>`exponential-reconstructor`（多幂级单位重构工厂）<br>`tetrative-reconstructor`（无量级单位重构工厂） | **碰撞：4 个来源** |
| `redirector` | `heat-redirector`（热量传输机）<br>`small-heat-redirector`（小型热量传输机） | **碰撞：2 个来源** |
| `redmat` | `redmat`（红地垫） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `redweed` | `redweed`（赤藻） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `refabricator` | `tank-refabricator`（坦克重构厂）<br>`mech-refabricator`（机甲重构厂）<br>`ship-refabricator`（飞船重构厂）<br>`prime-refabricator`（高级再重构工厂） | **碰撞：4 个来源** |
| `regolith` | `regolith`（风化岩） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `rhyolite` | `rhyolite`（流纹岩）<br>`rough-rhyolite`（粗糙流纹岩） | **碰撞：2 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `ripple` | `ripple`（浪涌） | 单一来源 |
| `router` | `router`（路由器）<br>`liquid-router`（流体路由器）<br>`payload-router`（载荷路由器）<br>`duct-router`（物品管道路由器）<br>`heat-router`（热量路由器）<br>`surge-router`（合金路由器）<br>`reinforced-liquid-router`（强化流体路由器）<br>`reinforced-payload-router`（强化载荷路由器） | **碰撞：8 个来源** |
| `salt` | `salt`（盐碱地） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `salvo` | `salvo`（齐射） | 单一来源 |
| `scathe` | `scathe`（创伤） | 单一来源 |
| `scatter` | `scatter`（分裂） | 单一来源 |
| `scorch` | `scorch`（火焰） | 单一来源 |
| `segment` | `segment`（裂解） | 单一来源 |
| `separator` | `separator`（分离机） | 单一来源 |
| `shale` | `shale`（页岩地） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `shard` | `core-shard`（初代核心） | 单一来源 |
| `shrubs` | `shrubs`（灌木丛） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `slag` | `molten-slag`（矿渣液） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `smelter` | `silicon-smelter`（硅冶炼厂）<br>`surge-smelter`（合金冶炼厂） | **碰撞：2 个来源** |
| `smite` | `smite`（天谴） | 单一来源 |
| `snow` | `snow`（雪）<br>`ice-snow`（冰雪地） | **碰撞：2 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `sorter` | `sorter`（分类器）<br>`inverted-sorter`（反向分类器） | **碰撞：2 个来源** |
| `source` | `item-source`（物品源）<br>`liquid-source`（液体源）<br>`power-source`（电力源）<br>`payload-source`（载荷源）<br>`heat-source`（热量源） | **碰撞：5 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `space` | `space`（太空） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `spawn` | `spawn`（敌人出生点） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `spectre` | `spectre`（幽灵） | 单一来源 |
| `split` | `scathe-missile-surge-split`（分裂导弹单位末段，不是建筑前缀；无 block 名称键） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `stone` | `stone`（石头）<br>`crater-stone`（陨石坑）<br>`yellow-stone`（黄石）<br>`carbon-stone`（碳石）<br>`ferric-stone`（铁石）<br>`beryllic-stone`（铍石）<br>`crystalline-stone`（晶石）<br>`red-stone`（红石）<br>`dense-red-stone`（高密红石）<br>`arkyic-stone`（芳石） | **碰撞：10 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `sublimate` | `sublimate`（升华） | 单一来源 |
| `surge` | `scathe-missile-surge 等`（导弹变体末段，不是建筑前缀；无 block 名称键） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `swarmer` | `swarmer`（蜂群） | 单一来源 |
| `switch` | `world-switch`（世界开关）<br>`switch`（开关） | **碰撞：2 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `synthesizer` | `cyanogen-synthesizer`（氰合成机）<br>`phase-synthesizer`（相织布合成机） | **碰撞：2 个来源** |
| `tank` | `liquid-tank`（流体储罐）<br>`reinforced-liquid-tank`（强化流体储罐） | **碰撞：2 个来源** |
| `tar` | `tar`（石油） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `tendrils` | `tendrils`（卷须） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `thorium` | `ore-thorium`（钍矿（由 item.thorium.name“钍”派生；无对应 block 名称键））<br>`ore-crystal-thorium`（晶体钍矿（动态矿物内容；无对应 block 名称键）） | **碰撞：2 个来源**；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `thruster` | `thruster`（推进器残骸） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `tiles` | `metal-tiles-1`（金属地基 1）<br>`metal-tiles-2`（金属地基 2）<br>`metal-tiles-3`（金属地基 3）<br>`metal-tiles-4`（金属地基 4）<br>`metal-tiles-5`（金属地基 5）<br>`metal-tiles-6`（金属地基 6）<br>`metal-tiles-7`（金属地基 7）<br>`metal-tiles-8`（金属地基 8）<br>`metal-tiles-9`（金属地基 9）<br>`metal-tiles-10`（金属地基 10）<br>`metal-tiles-11`（金属地基 11）<br>`metal-tiles-12`（金属地基 12）<br>`metal-tiles-13`（金属地基 13） | **碰撞：13 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `titan` | `titan`（泰坦） | 单一来源 |
| `tower` | `surge-tower`（巨浪电力塔）<br>`build-tower`（建造塔）<br>`shockwave-tower`（震爆塔）<br>`beam-tower`（激光塔）<br>`unit-repair-tower`（单位维修塔） | **碰撞：5 个来源** |
| `tree` | `white-tree`（白树） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `tsunami` | `tsunami`（海啸） | 单一来源 |
| `tungsten` | `ore-tungsten`（钨矿（由 item.tungsten.name“钨”派生；无对应 block 名称键）） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building`；未由 `block.*.name` 中文键确认 |
| `turret` | `repair-turret`（维修塔） | 单一来源 |
| `unloader` | `unloader`（装卸器）<br>`payload-unloader`（载荷卸载器）<br>`duct-unloader`（管道装卸器） | **碰撞：3 个来源** |
| `vault` | `vault`（仓库）<br>`reinforced-vault`（强化仓库） | **碰撞：2 个来源** |
| `vent` | `rhyolite-vent`（流纹石喷口）<br>`carbon-vent`（碳石喷口）<br>`arkyic-vent`（芳石喷口）<br>`yellow-stone-vent`（黄石喷口）<br>`red-stone-vent`（红石喷口）<br>`crystalline-vent`（晶石喷口）<br>`stone-vent`（岩石喷口）<br>`basalt-vent`（玄武岩喷口） | **碰撞：8 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `void` | `item-void`（物品黑洞）<br>`liquid-void`（液体黑洞）<br>`power-void`（电力黑洞）<br>`payload-void`（载荷黑洞） | **碰撞：4 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `wall` | `salt-wall`（盐墙）<br>`sand-wall`（沙墙）<br>`spore-wall`（孢子墙）<br>`shale-wall`（页岩墙）<br>`scrap-wall`（废墙）<br>`scrap-wall-large`（大型废墙）<br>`remove-wall`（移除墙体）<br>`dacite-wall`（安山岩墙）<br>`stone-wall`（石墙）<br>`ice-wall`（冰墙）<br>`snow-wall`（雪墙）<br>`dune-wall`（沙丘岩）<br>`dirt-wall`（泥土墙）<br>`metal-wall-1`（金属墙 1）<br>`metal-wall-2`（金属墙 2）<br>`metal-wall-3`（金属墙 3）<br>`colored-wall`（染色墙壁）<br>`copper-wall`（铜墙）<br>`copper-wall-large`（大型铜墙）<br>`titanium-wall`（钛墙）<br>`titanium-wall-large`（大型钛墙）<br>`plastanium-wall`（塑钢墙）<br>`plastanium-wall-large`（大型塑钢墙）<br>`phase-wall`（相织布墙）<br>`phase-wall-large`（大型相织布墙）<br>`thorium-wall`（钍墙）<br>`thorium-wall-large`（大型钍墙）<br>`surge-wall`（合金墙）<br>`surge-wall-large`（大型合金墙）<br>`regolith-wall`（风化墙）<br>`yellow-stone-wall`（黄石墙）<br>`rhyolite-wall`（流纹岩墙）<br>`carbon-wall`（碳石墙）<br>`ferric-stone-wall`（铁石墙）<br>`beryllic-stone-wall`（铍石墙）<br>`arkyic-wall`（芳石墙）<br>`crystalline-stone-wall`（晶石墙）<br>`red-ice-wall`（红冰墙）<br>`red-stone-wall`（红石墙）<br>`red-diamond-wall`（红钻墙）<br>`graphitic-wall`（石墨墙）<br>`beryllium-wall`（铍墙）<br>`beryllium-wall-large`（大型铍墙）<br>`tungsten-wall`（钨墙）<br>`tungsten-wall-large`（大型钨墙）<br>`carbide-wall`（碳化物墙）<br>`carbide-wall-large`（大型碳化物墙）<br>`reinforced-surge-wall`（强化合金墙）<br>`reinforced-surge-wall-large`（大型强化合金墙）<br>`shielded-wall`（盾墙） | **碰撞：50 个来源**；含世界编辑器内容或建筑类别混合，需实际链接验证 |
| `water` | `deep-water`（深水）<br>`shallow-water`（水）<br>`tainted-water`（污水）<br>`deep-tainted-water`（深污水）<br>`darksand-tainted-water`（黑沙污水）<br>`sand-water`（浅滩）<br>`darksand-water`（黑沙浅滩） | **碰撞：7 个来源**；环境/矿物/内部内容，通常没有可链接 `Building` |
| `wave` | `wave`（波浪） | 单一来源 |
| `weaver` | `phase-weaver`（相织布编织器） | 单一来源 |
| `white` | `character-overlay-white`（标识贴片 (白色)） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `yellowcoral` | `yellowcoral`（黄珊瑚） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |
| `zone` | `core-zone`（核心区） | 单一来源；环境/矿物/内部内容，通常没有可链接 `Building` |

## 原版、模组与自定义名称边界

- **原版自动链接名**：游戏为实际链接到处理器的 `Building` 调用上述规则。只有存在建筑实例并允许链接的内容，才会真实产生 `prefixN`；地板、矿物覆盖层、树木、装饰物、空气和导弹单位不会因为出现在清单中就自动成为链接。
- **模组建筑**：模组仍会经过同一个 `getLinkName`。但当前编译器使用固定原版前缀集合，无法自动认识新前缀。模组建筑若末段碰巧与清单中的前缀相同，会被隐式接受；否则应使用显式 `extern building 自定义名;`。
- **自定义链接名**：游戏保存的逻辑链接可以带自定义名称，加载时只用 `startsWith(getLinkName(block))` 粗略检查是否仍匹配；不匹配时才重新分配名称。当前编译器不会读取地图或处理器的链接表，因此无法证明某个隐式名称真实存在。
- **编译器保留策略**：匹配“已知前缀 + 不以 0 开头的正整数字面后缀”的标识符会被保留，不能用作变量、参数或函数名。这个规则是静态便利功能，不是游戏链接存在性检查。
- **类型边界**：`messageN` 被静态视为 `message`，`displayN` 被静态视为 `display`，其他前缀统一视为 `building`。即便同一前缀下只有某种专用建筑，当前也不会继续细分类型。

## 已知风险与后续建议

1. 当前集合显然包含误收录项，例如 `air`、`floor`、`ore`、`missile`、`phase`、`split`、`surge` 等；它们可能让正常用户变量无必要地成为保留标识符。
2. 本地化反向归组只能说明名称碰撞，不能判断 `Block` 是否有 `Building` 实例、是否允许处理器链接，或是否只在世界编辑器中使用。
3. 更可靠的清单应从原版内容对象中筛选实际可链接建筑，再执行 `getLinkName`，而不是从所有内容名或 bundle 键推导。
4. 若未来支持读取地图/处理器配置，应以实际 `LogicLink.name` 表作为最高优先级，并把固定清单降级为补全或诊断用途。

## 数据提取说明

- 命名算法：直接核对 `LogicBlock.getLinkName` 和 `LogicBuild.findLinkName`。
- 当前集合：直接读取 `src/compiler.cpp` 中 `implicitLinkType` 的 `prefixes`。
- 中文名称：读取简体中文 bundle 的 `block.*.name`，对每个内部名复现 `getLinkName` 后按前缀分组。
- 无 block 中文键的矿物项：参考同一中文 bundle 的 `item.graphite.name`、`item.beryllium.name`、`item.thorium.name`、`item.tungsten.name`，并明确标为派生名称。
- `air` 与导弹相关项没有可用的 `block.*.name` 中文键，表中只给出源码角色说明，没有把推测名称冒充官方建筑译名。
