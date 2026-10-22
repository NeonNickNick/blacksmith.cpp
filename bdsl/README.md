# BDSL → C++ 与 VSCode 扩展

此目录是根 CMake 项目的可选模块：参考本仓库上级的 `BlacksmithFramework-BDSL`
的块结构，面向当前 `blacksmith.cpp` 的 C++20 核心重新定义完整的文本语义，
实现 C++ 编译器和使用同一编译器的 VSCode 扩展。
新增源码、文档、示例和测试位于 `bdsl/`；根 CMake 通过 `BLACKSMITH_BUILD_BDSL`
控制是否构建此模块，默认关闭，与 `BLACKSMITH_BUILD_BINDINGS` 相互独立。
本模块不提供独立 CMake 项目，构建产物随根项目进入其构建目录，不修改现有核心 C++。

## 快速使用

编译器和编译器回归测试使用 C++20 标准库，无第三方 C++ 依赖；
构建、CLI 测试和 VSIX 打包使用根项目已有的 CMake ≥ 3.21，不需要 Python。
扩展测试需要 Node.js ≥ 18，未安装时 CMake 跳过扩展测试。
生成代码对接本仓库 C++20 核心，其浮点模板参数等特性
要求使用兼容编译器，例如 MSVC。

在仓库根目录的 Visual Studio Developer PowerShell / 开发者命令行中：

```powershell
cmake -S . -B out/build/bdsl -DCMAKE_BUILD_TYPE=Release -DBLACKSMITH_BUILD_BDSL=ON -DBLACKSMITH_BUILD_BINDINGS=OFF
cmake --build out/build/bdsl --config Release --target bdslc bdsl-compiler-test bdsl-core-test bdsl-features-test --parallel
ctest --test-dir out/build/bdsl -C Release -R '^bdsl-' --output-on-failure
```

或执行 `bdsl/build.ps1 -Package`：构建、测试并打包扩展。
该脚本也从根项目配置，只构建和测试 BDSL 目标。启用模块后，普通根项目全量构建也包含 BDSL。
Visual Studio 多配置生成器的可执行文件位于 `out/build/bdsl/bdsl/Release/bdslc.exe`；
Ninja 生成器则位于 `out/build/bdsl/bdsl/bdslc.exe`。下文以 Ninja 路径为例：

```powershell
out/build/bdsl/bdsl/bdslc.exe bdsl/examples/warlock.bdsl -o out/build/bdsl/bdsl/warlock.cpp
out/build/bdsl/bdsl/bdslc.exe bdsl/examples/cannon.bdsl -o out/build/bdsl/bdsl/cannon.cpp
out/build/bdsl/bdsl/bdslc.exe --check --json bdsl/examples/warlock.bdsl
out/build/bdsl/bdsl/bdslc.exe --format --indent 4 bdsl/examples/warlock.bdsl
cmake --build out/build/bdsl --config Release --target bdsl-vsix
```

关闭 BDSL 模块（不创建其目标，也不查找 BDSL 测试依赖）：

```powershell
cmake -S . -B out/build/native -DBLACKSMITH_BUILD_BDSL=OFF
```

`-DBUILD_TESTING=OFF` 关闭根项目测试；
`-DBDSL_TEST_CORE=OFF` 可仅跳过生成代码对游戏核心的集成测试。
Linux/macOS 同样从根目录配置，可执行文件没有 `.exe` 后缀。

## VSCode

用 `bdsl-vsix` 目标生成的 `bdsl/dist/*.vsix` 已包含本机的原生编译器。
该目标依赖 `bdslc`，使用 CMake 内置 ZIP 打包，无需安装 npm 打包工具。
平台由 CMake 的目标系统和架构推断，也可用 `-DBDSL_VSIX_TARGET=win32-x64` 显式指定；
支持 `win32`、`linux`、`darwin` 的 `x64` / `arm64`，所打包的编译器必须与该平台一致。
在 VSCode 扩展页面菜单执行 **Install from VSIX...** 即可安装；或：

```powershell
code --install-extension bdsl/dist/blacksmith-bdsl-1.0.0-win32-x64.vsix
```

打开 `.bdsl` 后可获得语法高亮、未保存文本实时检查、Problems 错误位置、
文档格式化和保存自动格式化。命令面板 **BDSL: Compile Current File to C++**
编译当前缓冲区，然后选择 `.cpp` 保存位置。语法无效时不修改文档。

包关键字为 `[skillset]`，参数化技能层数为 `__N__`，循环索引仍为 `__index__`。
扩展提供专用 TextMate 语法，区分块关键字、声明语句、参数名、魔术字段、标记变量、
资源、攻击/防御类型、枚举、函数、数字、字符串与注释。
安装后当前主题即可识别这些语法类别；在 **Preferences: Color Theme** 中选择
**BDSL Dark** 或 **BDSL Light**，可使用扩展定义的专门配色。

调试源码扩展：用 VSCode 打开 `bdsl/vscode/`，先构建编译器，再按 F5。
设置 `bdsl.compilerPath` 可指定编译器；相对路径相对于文档所属 workspace folder。
默认优先查找扩展内 `bin/`；开发源码时从根项目的 `build/bdsl/`、
`out/build/bdsl/` 和 `out/build/*/bdsl/` 查找编译器，包括各配置子目录，使用最新构建。
使用其他构建目录时可显式设置 `bdsl.compilerPath`。
远程 SSH / WSL 的编译器须与扩展宿主系统及架构一致。
扩展无 npm 依赖，`npm test` 可执行单元测试；CTest 还会设置 `BDSLC` 跑原生工具集成。

## 生成代码如何接入

一个文件只生成一个技能包，使用现有 `BEGIN_SKILL_SET` / `END` 及
`REGISTER`、`REGISTER_`、`REGISTER_BATCH`、`PASSIVE_` 宏，定义
`blacksmith::skill_system::get_<包名>()`。
技能名直接作为注册宏的首个参数，生成对应的 `<技能名>_check` / `<技能名>_declare`，
字符串映射和枚举注册由宏完成。含 `__N__` 的技能使用 `REGISTER_BATCH` 与 `DSL_BATCH`。
`<repeat>` 提取为注册宏之前的 `REPEAT` / `REPEAT_BATCH` 辅助定义，使用模板次数调用，
其中 `__index__` 对应宏提供的编译期 `index`，不生成运行时 for 循环。
嵌套重复及重复体引用外层标记变量时，也保留原有作用域与声明执行顺序。
数值写入直接 `static_cast`，无需额外转换辅助函数、范围钳制或运行时范围检查。

`warlock.bdsl` 与 `cannon.bdsl` 覆盖对应现有文件的全部技能，包括火炮标记的消耗、
术士的三回合攻击/防御、禁言以及炼金技能包切换。
构建时的 `bdsl-core-test` 在自己的目标中使用生成的两份源码替代对应原文件，
其余核心源码只读复用；该测试目标属于根构建，但不会改写或替换主程序使用的核心源码。
用户日后集成生成文件时，应让构建目标使用对应生成文件，避免同时链接原文件而重复注册。
新技能枚举、职业身份等核心模型仍由主项目定义；本工具不会替你扩展现有核心枚举。

## 目录与验证

- `compiler/`：词法分析、递归下降/优先级表达式解析、语义检查、生成器、格式化器及 CLI。
- `vscode/`：诊断、格式化、编译命令、TextMate 语法、深浅色主题和 snippets。
- `examples/`：`warlock.bdsl`、`cannon.bdsl`、独立演示 `features.bdsl`。
- `docs/SYNTAX.md`：完整语法、语义、与旧版差异、CLI 协议。
- `tests/compiler_test.cpp`：使用标准库的语言检查、截断输入、UTF-16 诊断列、格式化、
  注册/重复宏、数值转换区域设置和核心枚举一致性回归测试。
- `tests/cli_test.cmake`：标准输入输出、诊断 JSON、Unicode 路径、文件保护及 CLI 错误处理。
- `tests/core_test.cpp`：生成代码实际行为测试。
- `tools/package_extension.cmake`：使用 CMake 内置 ZIP 支持生成本机 VSIX；不下载依赖。

数值解析与输出使用 `std::from_chars` / `std::to_chars`，不受进程区域设置影响。
编译器的文件操作使用 `std::filesystem` 和标准流；Windows 入口保留最小的
`wmain` / `_setmode` 适配，以支持 Unicode 命令行参数和二进制标准输入输出，
这些能力在 C++20 标准库中没有可移植的接口。
VSCode 扩展仅保留调用其 API 与原生编译器所需的 JavaScript，使用 VSCode API
及 Node.js 内置模块，语法检查、格式化和代码生成均由 C++ 编译器完成。

当前检查器每次返回首个错误，修复后继续检查后续内容。自定义 C++ 回调、特殊防御
和任意效果不是 BDSL 1.0 的语法；不支持的输入会明确报错。
新增/重命名主项目枚举后，应同步 `compiler/include/catalog.hpp`；测试会检测技能/标记差异。

扩展 API 依据：[Diagnostics / Document Formatting](https://code.visualstudio.com/api/language-extensions/programmatic-language-features)。
