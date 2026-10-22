# BDSL 1.0 文本语法

## 设计与旧项目的关系

参考上级 `BlacksmithFramework-BDSL/BDSLSyntax.md` 的块结构及无分号、与缩进无关的原则。
当前包关键字为 `[skillset]`，技能参数为 `__N__`；`[skill]`、`[check]`、`[declare]`、
`<require>`、`<attack>` 等结构保留。旧包关键字和旧技能参数字段不再接受。
这是针对当前 C++ 核心的新版本，**不宣称兼容旧 C# 校验器的全部输入**。
旧规范未完整定义的标记、时钟、效果、枚举映射在此明确规定；未列出的字段报错。

UTF-8 文本，可带 BOM，支持 LF / CRLF。空白和换行除字符串、注释外均无语义，
所有块可写在同一行。`//` 注释至行尾，`/* ... */` 多行注释不嵌套。
字符串用双引号，支持 `\"`、`\\`、`\n`、`\r`、`\t` 和直接 Unicode 字符。
标识符为 `[A-Za-z_][A-Za-z_0-9]*`。关键字、资源及枚举区分大小写。

## 包与技能

```bdsl
[skillset]("warlock") -> {
    [skill]([name] "magicattack" [id] MAGIC_ATTACK) -> {
        [check]() -> {
            <require> __N__ > 0
            <require> Magic >= __N__
        }
        [declare]() -> {
            <use> __N__ Magic
            <repeat>(3) -> {
                <attack>([type] Magical [power] 2 * __N__ [delay] __index__)
            }
        }
    }
}
```

- 每文件恰好一个包，外部不允许有其他内容。包名生成 `get_warlock()`，必须为
  ASCII 标识符，不能以下划线开头或包含连续双下划线。
- 主动技能恰好包含一个 `[check]`、一个 `[declare]`，顺序不限。
- `[skill]("magicattack")` 为简写：忽略名称中的 `_` 后，按不区分大小写匹配现有
  `skill` 枚举。`"magic_attack"` 也匹配 `MAGIC_ATTACK`。
  自定义中文名称使用显式 `[name]`、`[id]`。
- 技能名直接作为 `REGISTER*` 宏的标识符参数；必须是合法 C++20 标识符，
  不能包含空格、标点或控制字符，也不能以下划线开头、包含双下划线或使用宏保留名称。
  名称大小写和下划线原样保留，只在推断技能枚举时忽略大小写和下划线。
- 同包技能名称、技能枚举不能重复。`SIZE` 不是可用枚举。
- `[skill]("auto")` 是唯一的被动技能，只允许 `[declare]`，不允许 `[id]`、`[check]`
  或 `__N__`。包最多一个 auto。空包合法。
- `[check]()` 中只允许 `<require> 布尔表达式`，全部满足时通过，空检查通过。
  `[check](!)` 对整个合取结果取反，空反向检查不通过。
- 编译器检查显式条件，不隐式推断、添加资源预算条件；应在检查块中写清技能消耗要求。

## 表达式

数值支持整数、小数、科学计数法；字面量须能表示为有限 double，不另设游戏数值上限。
一元正负号属于运算符。无法表示的常量、常量除零会报错。

优先级由低至高：

| 层级 | 运算符 | 说明 |
|---|---|---|
| 1 | `\|\|` | 逻辑或，短路 |
| 2 | `&&` | 逻辑与，短路 |
| 3 | `== !=` | 同类型相等比较 |
| 4 | `< > <= >=` | 数值比较 |
| 5 | `+ -` | 加减 |
| 6 | `* / %` | 乘、浮点除法、浮点余数 |
| 7 | 一元 `+ - !` | `!` 只接受布尔值 |
| 8 | `^` | 乘方，右结合，`2^3^2 = 512`，`-2^2 = -4` |

括号改变优先级。数值与布尔值不能隐式转换，禁止 `1 && true`、链式 `1 < HP < 5`。
表达式按 double 运算，写入资源时转 float；生命值、攻击/防御力量和时钟转 int，
截断小数。转换直接使用 `static_cast`，信任玩家提供的数据范围，不生成范围钳制、
有限值回退或动态范围检查。技能条件仅来自作者的 `<require>`。

可用名称与函数：

- `HP`、`MHP`：当前/最大生命值。
- `Iron`：普通铁与金铁之和；`CommonIron`：普通铁；`GoldIron`：金铁。
- `Space Time Magic Cross Dream Spirit Fire Water Wood Earth Bolt Jade Crystal CrystalBall`：对应资源总量。
- `__N__`：当前主动技能 `context.action_.param_`；不自动限制为正数。
- `__index__`：最近一层 repeat 的索引，从 0 开始。
- `@name@`：此前在当前声明作用域取得的标记数量。
- `count(CANNON)`：只读查询标记数量，检查块中也可用。
- `min(a, b)`、`max(a, b)`、`abs(x)`、`floor(x)`、`ceil(x)`。

## 声明语句

结构化语句参数写在圆括号内，格式是 `[字段] 值`，顺序不限，字段不可重复。
不使用逗号分隔字段，逗号仅用于表达式函数实参。
下表中的可选字段省略后使用默认值。

| 语句 | 必需参数或内容 | 可选字段及默认值 |
|---|---|---|
| `<use> 数值 资源` | 消耗数值 | `CommonIron` 只耗普通铁，`Iron` 按核心规则先消耗金铁 |
| `<recover> 数值` | 立即恢复生命 | 无 |
| `<resource>(...)` | `[type] 资源 [power] 数值` | `[delay] 0 [remain] 1 [isinfinite] false` |
| `<attack>(...)` | `[type] 攻击类型 [power] 数值` | `[delay] 0 [remain] 1 [isinfinite] false [ap] 1 [interrupt] false` |
| `<defense>(...)` | `[type] 防御类型 [power] 数值` | `[delay] 0 [clock]([delay] 0 [remain] 1 [isinfinite] false)` |
| `<takemark> @变量@ 标记枚举` | 返回并删除该标记的**全部**层数 | 无 |
| `<countmark> @变量@ 标记枚举` | 返回层数，保留标记 | 无 |
| `<addmark>(...)` | `[id] 标记枚举` | `[clock]([delay] 0 [remain] 1 [isinfinite] true)` |
| `<equip>("alchemy")` | 加入现有技能包 | 无 |
| `<disable> ALCHEMY` | 禁用现有技能枚举 | 无 |
| `<effect>(...)` | `[type] EraseResource [resource] Time [target] Enemy` | `[delay] 0 [clock]([delay] 0 [remain] 1 [isinfinite] false)` |

`<use>` 接受上述所有资源及 HP / MHP；`<resource>` 和 EraseResource 不接受 HP、MHP、CommonIron。
`<resource>` 是判定阶段写入收益，`<use>` 是声明阶段立即消耗，`<recover>` 也是立即执行。
`<use> ... GoldIron` 沿用核心 API 的消耗规则，应显式检查金铁余额后使用；
铁的底层消耗 API 共用同一个普通/金资源池。

攻击类型为 `Physical`、`Magical`、`Real`。`[ap]` 是穿甲倍率，必须为正；
`[interrupt]` 是核心的打断标志。攻击的 `[delay]` 会保留并叠加核心的幻觉延迟。

支持的防御类型及自动选择的回调：

| 类型 | 回调 | 更新 |
|---|---|---|
| `CommonReduction`、`RealReduction` | `default_reduction` | `default_update` |
| `ThornReduction` | `thorn_reduction` | `default_update` |
| `PercentageReduction` | `percentage_reduction` | `default_update` |
| `CommonArmor`、`RealArmor` | `default_armor` | `armor_update` |
| `PhysicalImmunity` | `physical_immunity` | `default_update` |
| `MagicalImmunity` | `magical_immunity` | `default_update` |

防御的外层 `[delay]` 是入场延迟，内层 `[clock]` 是防御实体自身时钟。
省略实体时钟时使用核心默认值。为延续旧写法，减伤类可显式写
`[analyzer] DefaultReduction`；其他类型由生成器选择，不能配错回调。
`[name]` 等核心没有对应语义的旧字段不接受。

EraseResource 在效果生效时删除目标待结算资源队列里对应类型的条目，**不清空已持有资源**。
`[target]` 可为 `Enemy`、`Self`。仅此效果类型属于 1.0 支持范围。

延迟、持续回合、力量、消耗、恢复量及穿甲倍率的范围由作者保证，编译器只检查表达式
类型，不添加游戏范围限制；写入整数位置时直接 `static_cast<int>`。

标记、技能、装备包采用 `compiler/include/catalog.hpp` 中来自当前核心的有限目录。
名称不存在即报错，不把任意文本拼接为 C++。技能注册名称字符串允许 Unicode。

## 控制流与作用域

```bdsl
<countmark> @stacks@ CANNON
<if>(@stacks@ > 0) -> {
    <attack>([type] Physical [power] 5)
}
<else> -> {
    <attack>([type] Physical [power] 4)
}
<repeat>(3) -> {
    <defense>([type] RealReduction [power] 3 [delay] __index__)
}
```

repeat 次数须为可静态计算的整数，作为辅助函数的模板实参。
重复体不读取 `__N__` 时生成 `REPEAT`，读取时生成 `REPEAT_BATCH`，不生成运行时 for 循环。
辅助定义放在技能注册之前，声明体在原位置调用，例如 `magicattack_repeat_0<3>(N, player)`。
嵌套重复从内层开始定义；引用外层标记变量时，通过临时值对象将其传入宏辅助函数，
不使用共享或全局状态。`__index__` 对应当前宏提供的编译期 `index`。
`<else>` 仅可紧跟 `<if>`。
块中的变量不向外泄漏；同作用域和嵌套作用域中不允许重定义已有变量。
外层变量可在内层读取。表达式及控制块组合嵌套上限 128。
`__index__` 使用最近 repeat，退出后恢复外层索引。
`<require>` 只在检查块可用，其他声明语句只在声明块可用。

## CLI、诊断及格式化协议

```text
bdslc input.bdsl [-o output.cpp]
bdslc --check [--json] input.bdsl
bdslc --format [--indent 4 | --tabs] input.bdsl [-o formatted.bdsl]
```

普通文件编译未指定 `-o` 时输出同名 `.cpp`。`-` 输入代表 stdin，并默认将编译
结果输出 stdout；`-o -` 也可显式选择 stdout。编译不能覆盖输入本身。
格式化默认 stdout，只有指定 `-o` 才写文件；允许显式格式化回原文件。
所有检查和代码生成在打开输出文件之前完成，语法错误不会截断已有输出。

退出码 0 为成功，1 为 BDSL 词法/语法/语义错误，2 为参数或 I/O 错误。
检查模式不生成 C++ 文件。`--check --json` 输出：

```json
{"diagnostics":[{"line":1,"column":5,"length":3,"severity":"error","message":"..."}]}
```

无错误为 `{"diagnostics":[]}`；操作错误为 `{"error":"..."}` 且退出 2。
行、列从 1 开始；列和长度使用 UTF-16 单位，与 VSCode 一致；一次返回首个错误。
普通诊断输出 stderr，格式为 `路径:行:列: error: 描述`。

格式化先通过同一完整解析器检查，再处理保留注释的 token 流，调整大括号缩进、
语句换行和运算符空白。字符串、注释内容保留，不按字符猜测其中的大括号。
连续格式化结果不变，格式化前后生成的 C++ 相同。默认输出 LF 和末尾换行，
VSCode 端保留当前文档的 LF / CRLF 选择，支持空格或 tab 缩进。

扩展对单文档输入限制为 4 MiB，子进程超时 5 秒；异常在 Problems 中显示。
实时诊断采用防抖、版本核对、取消过期进程；关闭文档后清除诊断并释放资源。

VSCode API 参考：[Programmatic Language Features](https://code.visualstudio.com/api/language-extensions/programmatic-language-features)。
