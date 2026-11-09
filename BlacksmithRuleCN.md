# Blacksmith核心规则手册 v1.4

> 平衡性快照：2026-11-09。数值记录当前训练构建实际使用的职业规则；每次职业平衡调整都必须同步更新本手册。迭代只修改职业内部技能，根节点保持不变。
> 本手册只记录当前启用的职业和技能。

---

## 1. 游戏基础逻辑

- **模式：** 双人同步回合制博弈
- **动作经济：** 每回合双方必须且只能声明 1 个技能
- **胜负：** 初始 10 HP / 10 Max HP。HP ≤ 0 死亡，同回合双死为平局

### 回合结算流程

1. **声明与支付：** 双方亮出技能，立刻扣除资源/生命成本
2. **效果及部署：** 结算非攻击性效果——获取资源、回复生命、建立装甲（永久）、部署伤减（单回合）
3. **交锋与伤害：** 对冲（按优先级 1:1 抵消）→ 突防（剩余攻击力穿透伤减 → 装甲 → HP）→ 结算打断/吸血/纹章
4. **结束：** 清理单回合 Buff，判定生死

---

## 2. 资源与数值

- **基础资源：** Iron（最小单位 0.5）、Time、Space
- **内置专属资源：** Magic（Warlock）、gIron（金铁/Alchemy）、Marks（Lancer纹章状态）
- **Mod 专属资源：** Dream（PhantomBook）、Spirit（Nightmare）、Fire/Water/Wood/Earth（Cauldron）、Bolt（Crossbow）
- **HP 作为成本：** BloodSigil、Warlock、PhantomBook均可消耗 HP

### 伤害与防御优先级

- **伤害：** Real(0) > Magical(128) > Physical(256)
- **伤减（单回合）：** PercentageReduction(24) < CommonReduction(16) < ThornReduction(8) < RealReduction(0)
- **装甲（永久）：** CommonArmor(128) < RealArmor(64) < StoneShell(32)
- **特殊防御：** PhysicalImmunity(-16) / MagicalImmunity(-16) 完全免疫对应类型

ThornReduction 吸收物理攻击时反弹 50%（向上取整）为 Magical 伤害。PercentageReduction 以百分比减免伤害（baseline 为分母基准，如 baseline:100 时每点防御 = 1% 减伤）。

### C++ 实现结算约定

- 直接伤害指 Physical / Magical；Real 不经过普通防御、闪避或分身转移。
- 攻击、防御和对冲使用原始 C++ 结算：穿甲进入适用防御前向上取整放大，离开后向上取整还原；普通伤减、ThornShield与装甲按整数结算；攻击按写入顺序对冲。新增百分比伤减在单道防御内计算后将剩余伤害向下取整。分身转移也视作防御，每个分身逐次承接向上取整的 50% 伤害（迷雾期间为 90%）。
- C++ 核心的实际实现为准；若本文与实现不一致，应更新本文以反映实现及已确认的约定。
- 防御在回合更新时先执行自身的 `update_`，再统一按时钟 `is_dead()` 清理。`default_update` 推进时钟，默认时钟只持续一回合，无限时钟持续保留；装甲的自然衰减、破裂判定和权限恢复由该装甲自己的更新钩子完成。

---

## 3. 技能与职业数据

### 3.1 基础公共技能

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Iron | 0 | 1 Iron |
| Stick | 0.5 Iron | 1 ATK（BloodSigil不可用） |
| Drill | 1.5 Iron | 3 ATK（BloodSigil不可用） |
| Slash | 2.5 Iron | 5 ATK（BloodSigil不可用） |
| Shield(n) | 0 + 0.5n Iron | 2 + n DEF |
| ThornShield(n) | 1 + 0.5n Iron | 4 + n CDEF |
| Recovery(n) | (0.5 + 0.5n) Iron | 回复 (1 + n) HP |
| Time | 3 Iron | 1 Time |
| Space | 3 Iron | 1 Space |
| Tear | 1 Space | 8 ATK |
| Reflect | 2 Space | 本回合所有即时攻击和指向敌方的效果推迟一回合生效 |

### 3.2 职业购买

购买职业视为本回合动作。根节点职业限购一次，绑定后不可更改，其他根节点职业选项永久失效。

技能权限按树形入口组织。每个节点下的职业分支互斥，副装备可以与该节点可达的职业及其他副装备共存；副装备本身不占用职业身份。Cauldron是根节点副装备，Alchemy是Warlock节点副装备。

每个主动技能独立累计禁用计数，只有计数归零时才能使用。多个来源同时禁用同一技能时，各自解除一层禁用；ElementalArmor只解除自身施加的一层，不会重新解锁仍受其他来源禁用的技能。被动单独保存其检查、声明函数指针对，ElementalArmor不影响被动的执行。

| 职业 | 成本 | 效果 |
|:---|:---|:---|
| Cannon | 4 Iron | 获得Cannon特性 +3 RealReduction |
| Driver | 3 Iron | 获得Driver特性 |
| Warlock(法杖) | 1 Iron | 获得Warlock特性 |
| BloodSigil | 7 Iron | +4 HP, +4 Max HP，获得BloodSigil特性。禁止使用Stick/Drill/Slash |
| Lancer | 3 Iron | 获得Lancer特性 |
| PhantomBook | 2.5 Iron | 获得PhantomBook特性（Mod） |
| Crossbow | 2 Iron | 获得Crossbow特性（Mod） |

| 副装备 | 所属节点 | 成本 | 效果 |
|:---|:---|:---|:---|
| Cauldron | 根节点 | 3 Iron | 获得Cauldron技能包，不影响根节点职业选择 |
| Alchemy | Warlock | 2.5 Iron | 解锁MidasTouch，不影响Warlock职业身份 |

### 3.3 职业专属技能

#### Cannon

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Strike | 1 Iron | 3 ATK |
| DoubleStrike | 2 Iron | 7 ATK |
| TripleStrike | 3 Iron | 11 ATK；下次Cannon攻击 +1 伤（消耗后失效，可叠加）；不返还 Iron，不回复 HP |
| CannonBarrier | 0 | 1 ATK + 2 CommonReduction |
| APShell | 1 Iron | 2 ATK。造成伤害则打断对手本回合 Iron 或 Magic。对非真实防御时伤害临时 ×3 穿透，离开后 ÷3（向上取整）还原 |

#### Driver
**被动：** 每回合自动产出 `1 + 2 × 当前 Time 数` 点 RealReduction，无上限。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| SpaceAttack(n) | n Space（n>0） | `12n + 已储存标记数` ATK |
| Space2Time | 1 Space | 获得 1 Time、回复 1 HP、获得 3 RealReduction，并使下次 SpaceAttack +1 伤（可叠加） |
| Time2Space | 1 Time | 获得 1 Space、回复 1 HP、获得 3 RealReduction，并使下次 SpaceAttack +1 伤（可叠加） |
| SpaceBarrier(n) | n Iron（1≤n≤5） | 获得 `5.5n - 0.5n²` RealReduction |

#### Warlock

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Magic | 1 Iron | 1 Magic |
| MagicAttack(n) | n Magic（n>0） | 第 1 段 `2n - 1` Magical ATK，之后两回合各 2n Magical ATK |
| MagicShield(n) | n Magic（n>0） | 连续 3 回合各获得 `3n - 1` RealReduction |
| Silence | 0 | 对手本回合时空获取无效 |
| Sacrifice | 1 Max HP | 7 RealReduction + 1.5 Iron |

**二级装备：**

| 装备 | 成本 | 效果 |
|:---|:---|:---|
| Alchemy | 2.5 Iron | Warlock节点副装备，解锁"MidasTouch" |
| MidasTouch | 1 Iron | 5 gIron |

#### BloodSigil

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| BloodBlade | 4 HP | 6 ATK，造成伤害后按 100% 吸血；BloodSigil标记使后续攻击威力乘以 1.6 |
| BloodLust | 2 HP | 下次BloodSigil攻击威力乘以 1.6 |
| Recovery | 0 | 回复 2 HP |
| BloodRage | 1 HP | 5 ATK（当前 HP 为 2–5 时可用），造成伤害后按 150% 吸血；BloodSigil标记使攻击威力乘以 1.6 |

#### Lancer
**特性 - 纹章系统：** 技能造成伤害后获得特定纹章，下次攻击消耗并附加效果。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| SkyStrike | 1 Iron | 3 ATK并打断。命中后储存标记，使下次Lancer攻击 +2 伤 |
| DragonTooth | 1 Iron | 3 ATK + 3 RealReduction。命中后储存标记，使下次攻击获得 2 点永久 CommonArmor |
| TyrantDestruction | 1 Iron | 3 ATK、两倍穿甲。命中后储存标记，使下次攻击回复 2 HP |
| TripleStab | 1 Iron | 同回合造成 2+2+1 ATK。命中储存标记；下次Lancer攻击消耗标记并支付 1 Max HP，额外附加两段各 1 Real伤害（间隔一回合） |
| RisingDragon | 0 | 10 Magical ATK + 每层Charge标记额外 4 Magical ATK |
| Charge | 首次 4 Iron，之后连续蓄力不耗 Iron | 最多蓄力两层。之后RisingDragon按层数增加伤害；蓄力回合若受到攻击，会立即触发反击RisingDragon |

#### PhantomBook

> 源码：`src/blacksmith-core/src/skill-system/phantombook/phantombook.cpp`

专属资源：**Dream**（梦境）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Fantasia | 0.5 Iron | 1 Dream |
| Association | 1.5 Dream | 复制并施放对手一个当前可用、自己当前不可用的技能。不可复制Association自身；复制技能通过无成本声明映射施放，不支付其原技能资源成本 |
| Hallucination | 1.5 Dream | 对手本回合及之后所有攻击延迟 +1 回合（无限持续预处理钩子） |
| DreamWake | 1.5 Dream | 要求当前至少有 1.5 Dream；在回合末、时钟推进后（`BEFORE_END`），根据双方动作历史，从普通开局重建三回合前的自身，扣除 1.5 Dream，保留当前完整动作历史并替换自身。本回合已结算的自身变化会被恢复后的状态覆盖；恢复状态中的待执行动作从下一回合继续结算 |
| Illusion | 1.5 Dream | 回复 5 HP |
| Nightmare（装备） | 1 Dream + 4 HP | 装备当回合物理免疫 + 6点永久Nightmare装甲。装甲破裂时恢复Nightmare入口技能并移除子职业包 |

**二级装备/子职业 - Nightmare：**

> 源码：`src/blacksmith-core/src/skill-system/phantombook/nightmare.cpp`

专属资源：**Spirit**（魂）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| DreamDive | 1 Dream | 2 Real ATK + 5 CommonReduction + 回复 1 HP + 1 Spirit + 魔法免疫 |
| Materialization | 1 Dream + 1 Spirit | 4 Physical ATK + 4 Real ATK + 4 CommonReduction |
| Haunt | 1 Spirit | 本回合及之后两回合各造成 2 Magical ATK |
| Channeling | 1 Spirit | 回复 3 HP + 2 CommonReduction + 魔法免疫；若Nightmare装甲仍在，额外增加 2 点装甲 |

#### Cauldron

> 源码：`src/blacksmith-core/src/skill-system/cauldron/cauldron.cpp`

专属资源：**Fire**（火）、**Water**（水）、**Wood**（木）、**Earth**（土）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Fire | 1 Iron | 1 Fire |
| Water | 1 Iron | 1 Water |
| Wood | 1 Iron | 1 Wood |
| Earth | 1 Iron | 1 Earth |
| Explosion(n) | n Fire | 4n MATK |
| IceBlade(n) | n Water | 5n ATK |
| Regeneration | 1 Wood | 1→2→3 递增 HP Recovery × 3回合 |
| StoneShell | 1 Earth | 首次遭遇物理攻击时触发，当回合所有物理伤害完全免疫，回合末消失 |
| BurnLife | 1 Fire + 1 Wood | 50%伤减 × 4回合；真实伤害减半 × 4回合；1回合后全部攻击翻倍 × 3回合；3回合后自毁（扣 114514 MHP） |
| FireRain | 1 Fire + 1 Earth | 8 ATK + 2 RATK + 1 RATK（同回合三段） |
| ElementalArmor（装备） | 1 Earth + 1 Water | 装备当回合物免 + 8 元素装甲。之后装甲完全吸收物理攻击并消耗对应容量，按容量吸收魔法攻击，每回合结束衰减 2（包括装备回合）。容量耗尽当回合仍免疫后续所有物理伤害，回合末由 update 钩子标记死亡并解除自身施加的一层禁用，随后统一清理装甲。禁用所有其他主动技能，切换为ElementalArmor模式；不影响被动 |

**二级装备/子职业 - ElementalArmor：**

> 源码：`src/blacksmith-core/src/skill-system/cauldron/elementalarmor.cpp`

进入ElementalArmor模式后所有原有主动技能被禁用，被动照常执行，仅可使用以下主动技能。装甲破裂后，在回合末解除自身施加的一层禁用。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Hammer | 0 | 6 ATK |
| Guard | 0 | 8 DEF |

#### Crossbow

> 源码：`src/blacksmith-core/src/skill-system/crossbow.cpp`

专属资源：**Bolt**（弩箭）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| CraftBolt(n) | n Iron | 获得 3n Bolt |
| BoltRain(n) | 3n Bolt（n≥0） | 无标记时造成 `3n + 1` Physical ATK 并添加持续标记；已有标记时清除标记并造成 `3n` Physical ATK。标记按每次BoltRain交替生效；n=0也会交替标记并结算攻击 |
| Aim | 0 | 下次Critical强化：物理伤害归零，真实伤害从 1 提升至 2 |
| Critical | 1 Bolt | 未Aim：1 Physical ATK + 1 Real ATK；Aim后：2 Real ATK。使用时获得 1 CommonReduction，Aim状态消费后复位 |
| Block(n) | 0.5n Bolt | 获得 `n(9-n)/2` CommonReduction（n=1~4）。Bolt不足不可用 |
| MarkShot | 1 Bolt + 1 Iron | 1 Physical ATK。下回合攻击命中对手身体时身体伤害翻倍 |
