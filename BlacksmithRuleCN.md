# Blacksmith核心规则手册 v1.3

> 核对最新实现、技能数值或职业细节，请以 `src/blacksmith-core` 下实际代码为准。Mod 示例职业以 `src/blacksmith-core/src/skill-system/` 下实际代码为准。

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
- **Mod 专属资源：** Cross（HolyBook）、Dream（PhantomBook）、Spirit（Nightmare）、Fire/Water/Wood/Earth（Cauldron）、Bolt（Crossbow）、Jade（Monk）
- **HP 作为成本：** BloodSigil、Warlock、PhantomBook、Monk均可消耗 HP

### 伤害与防御优先级

- **伤害：** Real(0) > Magical(128) > Physical(256)
- **伤减（单回合）：** PercentageReduction(24) < CommonReduction(16) < ThornReduction(8) < RealReduction(0)
- **装甲（永久）：** CommonArmor(128) < RealArmor(64) < StoneShell(32)
- **特殊防御：** PhysicalImmunity(-16) / MagicalImmunity(-16) 完全免疫对应类型；GreyHP（Profanation）提供可成长的真实伤减；PermanentRealReduction（Absolution）提供永续真实伤减

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
| Cannon | 4 Iron | 获得Cannon特性 +3 DEF |
| Driver | 3 Iron | 获得Driver特性 |
| Warlock(法杖) | 1 Iron | 获得Warlock特性 |
| BloodSigil | 7 Iron | +4 HP, +4 Max HP，获得BloodSigil特性。禁止使用Stick/Drill/Slash |
| Lancer | 3 Iron | 获得Lancer特性 |
| HolyBook | 2 Iron | 获得HolyBook特性（Mod） |
| PhantomBook | 2.5 Iron | 获得PhantomBook特性（Mod） |
| Crossbow | 2 Iron | 获得Crossbow特性（Mod） |
| Monk（当前停用） | 3 Iron | 源码职业实现保留，当前核心未开放购买入口 |

| 副装备 | 所属节点 | 成本 | 效果 |
|:---|:---|:---|:---|
| Cauldron | 根节点 | 3 Iron | 获得Cauldron技能包，不影响根节点职业选择 |
| Alchemy | Warlock | 2.5 Iron | 解锁MidasTouch，不影响Warlock职业身份 |

### 3.3 职业专属技能

#### Cannon

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Strike | 1 Iron | 4 ATK |
| DoubleStrike | 2 Iron | 8 ATK |
| TripleStrike | 3 Iron | 11 ATK；结算后返还 0.5 Iron；下次Cannon攻击 +1 伤（消耗后失效，可叠加） |
| CannonBarrel | 0 | 1 ATK + 2 DEF |
| APShell | 1 Iron | 2 ATK。造成伤害则打断对手本回合"Iron"或"Magic"。对非真实防御时伤害临时 ×3 穿透，离开后 ÷3（向上取整）还原 |

#### Driver
**被动 - TimeShield：** 每回合自动产出 1 + 2n RDEF（n = 当前 Time 数，上限 5 RDEF）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| SpaceImpact(n) | n Space | 12n ATK |
| Space2Time | 1 Space | 1 Time + 3 RDEF；下次SpaceImpact +1 伤（消耗后失效，可叠加） |
| Time2Space | 1 Time | 1 Space + 3 RDEF；下次SpaceImpact +1 伤（消耗后失效，可叠加） |
| SpaceBarrier(n) | n Iron | -0.5n² + 5.5n RDEF (n ≤ 5) |

#### Warlock

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Magic | 1 Iron | 1 Magic |
| MagicAttack(n) | n Magic | 每回合 2n MATK，悬挂于攻击预备区，持续 3 回合 |
| Silence | 0 | 对手本回合时空获取无效 |
| Sacrifice | 1 HP + 1 Max HP | 7 RDEF + 2 Iron |

**二级装备：**

| 装备 | 成本 | 效果 |
|:---|:---|:---|
| Alchemy | 2.5 Iron | Warlock节点副装备，解锁"MidasTouch" |
| MidasTouch | 1 Iron | 5 gIron |

#### BloodSigil

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| BloodBlade | 4 HP | 6 ATK，造成伤害 75% 吸血 |
| BloodLust | 2 HP | 下次攻击伤害 +50%（向上取整） |
| Recovery | 0 | 回复 2 HP。前提：本回合结算前未受攻击伤害 |
| BloodShield | 1 HP | DEF = 当前 HP × 40%（向上取整） |
| BloodRage | 1 HP | 5 ATK（仅 HP ≤ 5 可用），造成伤害 150% 吸血 |

#### Lancer
**特性 - 纹章系统：** 技能造成伤害后获得特定纹章，下次攻击消耗并附加效果。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| SkyStrike | 1 Iron | 3 ATK + 打断。命中获【火纹】：下次攻击首段 +2 |
| DragonTooth | 1 Iron | 3 ATK + 3 DEF。命中获【冰纹】：下次攻击获 +2 AMR |
| TyrantDestruction | 1 Iron | 3 ATK + 两倍穿甲。命中获【光纹】：下次攻击获 +2 HP |
| TripleStab | 1 Iron | 2+2+1 ATK（三段同回合）。命中获【暗纹】：下次攻击附两回合 1 RATK |
| RisingDragon | 4 Iron | 10 MATK |
| Charge | 4 Iron | 下次RisingDragon +4 MATK。<br>减耗：下回合RisingDragon/Charge成本 0 Iron。<br>自动反击：本回合受攻击则视为Charge完成，立刻触发 0 成本RisingDragon。<br>最多连续Charge两次 |

#### Monk

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Jade | 1 Iron | 1 Jade |
| GhostStep | 1 Jade + 1HP| 创造一个与自身等生命值的分身。Monk受到的直接物理和魔法伤害将按照创造顺序从前到后逐次转移50%（向上取整，总和不变）至每个分身。最多同时存在两个分身。<br>注：当有两个分身时，逐次指第一个转移50%，第二个转移25% |
| GoldenBellCover | 1 Jade | 直接性物理和魔法伤害(100-60*n)%伤减一回合，n为已使用过此技能的次数。<br>注：n≥2时变为易伤 |
| MazeFist | 1 Clone | 消耗生命值最高的分身，6ATK，若击中甲，则下回合闪避所有直接伤害 |
| Mist | 1 Iron | 给予生命值最低的分身3 HP恢复*3回合，3回合内伤害逐次转移比例提升至90% <br>注：每三回合最多使用一次|
| Disillusionment | 2 Jade | 6ATK + 4MATK|
| 被动 | 0 | 每主动或被动失去一个分身，下回合追加一次伤害减半的Disillusionment |
| Clone | 0 | 每回合给Monk施加一次GoldenBellCover，Monk和每个分身独立计数<br>注：GhostStep创建Clone当回合伤害转移即生效，但Clone下回合才开始释放此技能|

#### Prophet（当前停用）

源码保留但当前核心未开放职业购买入口。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Crystal | 1.5 Iron | 1 Crystal |
| CrystalBall | 2 Crystal | 1 CrystalBall, 最多持有1个 |
| Foretold | 1 Iron | 闪避所有直接伤害 |
| GreatestCaution | 1 Crystal | 下次闪避时，如果闪避成功，下回合反伤4RATK |
| Revelation | 1 Iron | 下次闪避时，如果闪避成功，额外获得一个Crystal |
| Assertion | 0 | 预测对方本回合使用技能，如果预测正确，闪避所有直接伤害，且获得1 CrystalBall |
| CrystalWall | 1 CrystalBall | 7RDEF * 2回合，100%物理反伤（反为魔法伤害），且如果该RDEF受到攻击则返还1Crystal |
| Refraction | 要求持有CrystalBall | 50%伤减 |
| Ultimatum | 1 CrystalBall + 2 Crystal | 宣告一个正整数n，要求n > 5，n回合后追加12RATK |
| 被动 | 0 | 每次闪避成功后立即获得一个Crystal |

#### HolyBook

> 源码：`src/blacksmith-core/src/skill-system/holybook.cpp`

专属资源：**Cross**（圣痕）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Cross | 0.5 Iron | 1 Cross |
| Prayer | 0 | 3 DEF |
| Ark | 2 Cross | 8 ATK + 1 50%伤减 |
| Profanation | 1 Cross | 2 RATK + (2+ceil(n/3)) GreyHP。n 为此前已使用Profanation次数，不计本次 |
| Rebirth | 1 Cross | 3 HP恢复 × 3回合 + 25%伤减 × 3回合（每回合独立部署） |
| Absolution | 1 Cross | 1 永续真实伤减（PermanentRealReduction，跨回合不消失） |

**防御机制：**
- **GreyHP**：本回合共享的可消耗伤减池，在装甲之后承伤；Profanation使用次数越多越厚，回合结束清除
- **PermanentRealReduction**：与其他伤减不同，不会在回合结束时清除

#### PhantomBook

> 源码：`src/blacksmith-core/src/skill-system/phantombook/phantombook.cpp`

专属资源：**Dream**（梦境）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| Fantasia | 0.5 Iron | 1 Dream |
| Association | 2 Dream | 复制对手一个可用技能并施放（消耗由自己支付）。不可复制转职/装备技能/Association自身 |
| Hallucination | 2 Dream | 对手本回合及之后所有攻击延迟 +1 回合（无限持续预处理钩子） |
| DreamWake | 最高 2 Dream | 要求当前至少有 2 Dream；在回合末、时钟推进后（`BEFORE_END`），根据双方动作历史，从普通开局重建三回合前的自身，扣除资源后最多 2 Dream，保留当前完整动作历史并替换自身。本回合已结算的自身变化会被恢复后的状态覆盖；恢复状态中的待执行动作从下一回合继续结算 |
| Illusion | 1 Dream | 回复 5 HP |
| Nightmare（装备） | 1 Dream + 5 HP | 装备当回合物免 + 永久 6 Nightmare装甲。装甲破裂时恢复Nightmare入口技能并移除子职业包 |

**二级装备/子职业 - Nightmare：**

> 源码：`src/blacksmith-core/src/skill-system/phantombook/nightmare.cpp`

专属资源：**Spirit**（魂）。

| 技能 | 成本 | 效果 |
|:---|:---|:---|
| DreamDive | 1 Dream | 2 RATK + 5 DEF + 回复 1 HP + 1 Spirit + 魔法免疫 |
| Materialization | 1 Dream + 2 Spirit | 4 ATK + 4 RATK + 4 DEF |
| Haunt | 1 Spirit | 2 MATK（本回合）+ 2 MATK（下回合）+ 1 MATK（第三回合） |
| Channeling | 1 Spirit | 回复 3 HP + 2 DEF + 魔法免疫 |

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
| CraftBolt | 1 Iron | 3 Bolt |
| BoltRain(n) | n Bolt | n ATK |
| Aim | 0 | 下次Critical强化：物理伤害归零，真实伤害从 1 提升至 2 |
| Critical | 1 Bolt | 未Aim：1 ATK + 1 RATK。Aim后：0 ATK + 2 RATK。Aim状态消费后复位 |
| Block(n) | 0.5n Bolt | (4.5n - 0.5n²) DEF（n=1~4，Shield公式抛物线）。Bolt 不足不可用 |
| MarkShot | 1 Bolt + 1 Iron | 1 ATK。下回合即时攻击命中被标记的对手身体时伤害翻倍 |
