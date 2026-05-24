# SysY2022 编译器课程设计实验报告

## 1. 实验概述

本实验实现了一个面向 SysY2022 语言的教学编译器。编译器以 C++17 编写，使用 Flex 完成词法分析，使用 Bison 完成语法分析，前端构造抽象语法树后进入语义分析阶段；语义分析同时负责生成 Koopa IR 文本；后端再调用 `libkoopa` 将 Koopa 文本解析为 raw program，并遍历 raw program 输出 RISC-V 汇编。

最终可执行文件名为 `compiler`，支持两种输出模式：

```bash
./build/compiler -koopa input.sy -o output.koopa
./build/compiler -riscv input.sy -o output.S
```

整体流程如下：

```text
SysY2022 源文件
  -> Flex 词法分析
  -> Bison 语法分析
  -> AST
  -> 语义分析、类型检查、常量求值
  -> Koopa IR 文本
  -> libkoopa raw program
  -> RISC-V 汇编
```

本项目的实现重点不只是把样例编译通过，而是尽量把各阶段边界划清：前端只负责词法、语法和 AST 构造；语义分析集中处理符号、类型、常量、数组、控制流和 Koopa 文本；后端只消费 Koopa raw program，不再回看 SysY 源码或 AST。

## 2. 成员分工

本项目按前端、中端、后端和集成测试几个方向推进。实际分工参考课程设计报告中常见的模块边界，保持每个人负责的主线清晰，同时在接口联调和问题定位时互相配合。

| 成员 | 主要职责 | 具体工作 |
| --- | --- | --- |
| 全体成员 | 项目结构与总体方案 | 共同确定编译器基本文件结构、编译流程、模块边界、构建方式和阶段性验收目标。 |
| 王恕 | 前端与公共工具 | 负责全局对象管理器、前端工具类、Flex/Bison 词法语法分析、语法树构造和前端调试。 |
| 李一航 | 中端实现 | 负责语义分析、中间代码生成、符号表管理、类型检查、常量求值和 Koopa IR 输出；协助中端与前端接口联调。 |
| 李丛铭 | 后端与测试 | 负责 Koopa raw program 到 RISC-V 汇编的目标代码生成、栈帧和调用约定实现，以及项目集成测试和运行验证。 |

在实际开发中，分工不是完全割裂的。但整体上实现了解耦。

## 3. 实验环境与代码规模

标准构建环境采用课程常用的 `maxxing/compiler-dev` Docker 镜像。该环境中包含 Clang、CMake、Flex、Bison、Koopa 库和 RISC-V 相关工具链。构建脚本要求 CMake 版本不低于 3.16，并在 `CMakeLists.txt` 中查找 `/opt/include`、`/opt/lib/native`、`/opt/lib` 下的 `koopa.h` 和 `libkoopa`。

项目主要文件如下：

```text
include/sysy/ast.hpp          AST 数据结构
include/sysy/common.hpp       SourceLocation、CompileError、基础类型和常量值工具
include/sysy/semantic.hpp     语义分析接口
include/sysy/riscv.hpp        RISC-V 后端接口
src/frontend/.../YaccLex/src/sysy.l
                              Flex 词法规则
src/frontend/.../YaccLex/src/sysy.y
                              Bison 语法规则
src/frontend/driver.cpp       语法树到本项目 AST 的转换层
src/ast/ast.cpp               AST 构造、字符串转义、dump 工具
src/semantic/SemanticAnalyzer.cpp
                              符号表、类型检查、常量求值、Koopa IR 生成
src/backend/RiscV.cpp         Koopa raw program 到 RISC-V 汇编
src/main.cpp                  命令行入口
tests/positive                正向测试样例
tests/negative                负向测试样例
docs/ai_codegen.md            AI 辅助代码生成记录
```

使用当前工作区中的 `wc -l` 复核代码规模，几个核心文件行数为：

```text
src/semantic/SemanticAnalyzer.cpp  1520 行
src/backend/RiscV.cpp               550 行
src/frontend/driver.cpp             493 行
前端 Bison 语法规则文件          871 行
前端 Flex 词法规则文件           111 行
```

按实际参与构建的代码链路统计，不计 `tests`、`docs`、`build` 和 `temp`，主要模块代码量占比如下：

| 模块 | 统计范围 | 行数 | 占比 |
| --- | --- | ---: | ---: |
| 前端与 AST 构造 | 词法、语法、Parser、ObjTree、字符串工具和 `src/frontend/driver.cpp` | 1751 | 38.3% |
| 语义分析与 Koopa IR 生成 | `src/semantic/SemanticAnalyzer.cpp` | 1520 | 33.3% |
| RISC-V 后端 | `src/backend/RiscV.cpp` | 550 | 12.0% |
| AST、公共接口与入口 | `src/ast/ast.cpp`、`src/main.cpp`、`include/sysy/*.hpp` | 747 | 16.4% |
| 合计 | 当前构建链路中的主要实现代码 | 4568 | 100.0% |

其中 `SemanticAnalyzer.cpp` 是语义分析和 Koopa IR 生成的核心文件，也是本实验选择的 AI 辅助代码生成目标代码块。

## 4. 前端设计与实现过程

### 4.1 词法分析

本项目前端由 Flex 词法器、Bison 语法器和 AST 转换层组成。Flex/Bison 阶段先构造 ObjTree 形式的语法树，再由 `src/frontend/driver.cpp` 中的转换逻辑生成语义阶段使用的 AST。

词法分析器按 SysY2022 语言定义整理 token 类别，再把词素字符串通过 `yylval.raw_string` 传给 Bison 侧的 ObjTree 构造动作。

已实现的词法类别包括：

- 关键字：`const`、`int`、`float`、`void`、`if`、`else`、`while`、`break`、`continue`、`return`。
- 标识符：以字母或下划线开头，后接字母、数字或下划线。
- 整型常量：支持十进制、八进制和十六进制，转换 AST 时使用 `std::strtol(raw.c_str(), nullptr, 0)`。
- 浮点常量：支持十进制浮点和十六进制浮点，转换 AST 时使用 `std::strtof`。
- 字符串字面量：先保存原始字符串，转换为本项目 AST 时调用 `decodeSysYString` 处理转义。
- 运算符和分隔符：包括算术、关系、逻辑、赋值、括号、方括号、大括号、逗号和分号。
- 注释和空白：跳过单行注释、多行注释、空格、制表符和换行。

词法器维护了 `line_number`，多行注释使用 Flex start condition `C_COMMENT` 处理，遇到 EOF 仍处在注释状态时抛出 `unterminated block comment`。

词法阶段也处理了两个容易遗漏的错误：未闭合的块注释和未闭合的字符串字面量。遇到这些情况时直接抛出 `CompileError`，错误信息带源码行列号。

### 4.2 语法分析

语法分析器的 Bison 动作向 `ObjTree::ObjManager` 申请节点，设置节点类型、节点信息和子节点列表。典型节点名包括 `CompUnitAst`、`DeclAst`、`FuncDefAst`、`StmtAst`、`LValAst`、`LAndExpAst`、`LOrExpAst` 等。

`src/frontend/driver.cpp` 中的转换层负责把 ObjTree 转换成本项目 AST。转换逻辑按照节点名分派：

- `CompUnitAst` 展平为 `ast::Program`。
- `ConstDeclAst`、`VarDeclAst` 转成 `ast::Decl`。
- `FuncDefAst` 转成 `ast::FuncDef`。
- `StmtAst` 根据节点信息转成 `If`、`While`、`Break`、`Continue`、`Return`、`Assign` 等语句。
- `IntConstAst`、`FloatConstAst`、`StringConstAst`、`LValAst`、`UnaryExpAst` 和各类二元表达式节点转成 `ast::Expr`。

语法规则覆盖了以下结构：

- 顶层常量声明、变量声明、函数定义。
- 标量和数组定义。
- 函数形参，包括数组参数 `int a[]` 和 `int a[][3]`。
- 初始化列表，包括嵌套大括号。
- 块、空语句、赋值语句、表达式语句、`if`、`while`、`break`、`continue`、`return`。
- 表达式优先级：一元、乘除模、加减、关系、相等、逻辑与、逻辑或。

实现过程中主要处理了四个问题。

第一，顶层 `BType IDENT` 既可能是变量定义，也可能是函数定义。前端在词法阶段区分了 `KEYWORD_INT_FUNC`、`KEYWORD_FLOAT_FUNC` 这类函数返回类型 token，用于辅助顶层函数定义规约，避免在语义阶段再猜测节点含义。

第二，`if-else` 的悬挂 else 问题需要由语法规则保证最近匹配。当前 Bison 构建中仍报告 shift/reduce 冲突，现有测试可以通过，但这说明语法规则仍有进一步收敛空间。

第三，Flex/Bison 的 C 风格接口和 C++ AST 所有权需要衔接。前端先把节点交给 `ObjManager` 管理，转换成本项目 AST 时再进入 `std::unique_ptr` 所有权模型，避免在 Bison 动作中直接暴露复杂的 C++ 对象生命周期。

第四，ObjTree 中只有字符串形式的节点类型和节点信息，本项目语义分析需要的是强类型 AST，因此转换层中写了较多结构校验逻辑，例如 `expect`、`onlySon`、`flattenLeftList`。如果节点形态和预期不一致，会抛出带前端转换上下文的 `CompileError`，方便区分是语义错误还是前端结构错误。

### 4.3 AST 设计

AST 定义集中在 `include/sysy/ast.hpp`，实现集中在 `src/ast/ast.cpp`。节点类型没有做很深的继承层次，而是用 `ExprKind`、`StmtKind`、`TopLevelKind` 这类枚举区分具体形态。这样做的好处是数据结构清楚，语义分析时用 `switch` 能直接覆盖所有情况。

主要节点包括：

- `Program`：编译单元。
- `TopLevel`：顶层声明或函数定义。
- `Decl`、`VarDef`：变量/常量声明及维度、初始化器。
- `FuncDef`、`FuncParam`：函数定义和形参。
- `Block`、`BlockItem`、`Stmt`：块和语句。
- `Expr`：字面量、左值、一元表达式、二元表达式、函数调用。
- `InitVal`：标量初始化或列表初始化。

所有节点结构都带 `SourceLocation`，语义阶段的错误类型也统一通过该字段输出。需要注意的是，当前前端转换层暂时把部分节点位置固定为 `1:1`，所以语义错误位置还没有完全发挥这套字段的作用。

## 5. 语义分析与 Koopa IR 生成

语义分析器位于 `src/semantic/SemanticAnalyzer.cpp`。当前文件 1520 行，内部以 `Impl` 类承载具体实现，对外只暴露 `SemanticAnalyzer::compile`。

### 5.1 符号表和函数表

语义阶段维护两套核心信息：

- `functionTable_`：保存函数返回类型、参数类型、是否外部函数等信息。
- `scopes_`：栈式作用域，每层作用域中保存对象符号。

对象符号 `Symbol` 中记录了名称、类型、是否 const、是否全局、Koopa 名称、标量常量值和常量数组展开结果。类型使用 `TypeInfo` 表示，包含基础类型、数组维度，以及是否为数组参数。数组参数和普通数组在 Koopa 类型和寻址方式上不同，因此这里单独记录 `isArrayParam`。

编译入口的顺序是：

1. 安装 SysY 运行时库函数和内部 float helper 函数。
2. 预扫描所有顶层函数签名和顶层名字，检查顶层重名。
3. 建立全局作用域，编译全局变量和常量。
4. 编译函数体。
5. 检查 `main` 是否恰好为一个 `int main()`。
6. 拼接运行时声明、全局对象和函数 Koopa 文本。

预扫描函数签名的原因是支持函数在定义前被调用，也支持递归函数。顶层名字集合 `topLevelNames_` 用于避免全局变量和函数同名，同时也禁止用户定义与运行时库函数同名的顶层符号。

### 5.2 类型检查和常量表达式

本项目支持 `int`、`float`、`void` 和字符串字面量。字符串主要用于 `putf` 格式串，不作为普通变量类型使用。

语义阶段检查的错误包括：

- 重复定义。
- 未定义变量或函数。
- 顶层符号与运行时库函数冲突。
- `main` 不是唯一的 `int main()`。
- 标量被下标访问，数组下标过多。
- const 对象被赋值。
- 非标量被用于赋值右侧、条件或算术运算。
- `break`、`continue` 出现在循环外。
- 函数调用参数个数不匹配。
- 数组实参维度或元素类型不匹配。
- 数组维度不是正整数常量。
- 数组初始化器层数或元素数量非法。
- `%` 操作数不是整数。
- 非 `void` 函数缺少返回值或 `void` 函数返回值。

常量表达式由 `evalConstExpr`、`evalConstUnary`、`evalConstBinary`、`evalConstLValue` 等函数处理。全局变量初始化、数组维度、const 标量和 const 数组访问都依赖这套逻辑。`ConstValue` 同时保存 int、float 和 string，float 常量在 Koopa 中用 32 位 bit pattern 表示。

### 5.3 变量、常量和数组初始化

标量 const 如果可以完全在编译期求值，就不一定生成 Koopa 对象，而是作为符号表中的 `constScalar` 保存。普通全局变量输出 `global @name = alloc ...`，无显式初始化时使用 `zeroinit`。局部变量使用 `alloc`，有初始化时生成 `store`。

数组初始化是本实验中最容易出错的部分之一。初版思路曾把初始化列表简单按叶子表达式展平，但这不符合 C/SysY 的嵌套大括号规则。例如：

```c
int g[2][3] = {{1, 2}, {3, 4, 5}};
```

正确结果应为：

```text
{{1, 2, 0}, {3, 4, 5}}
```

而不是把叶子值简单塞成 `{{1, 2, 3}, {4, 5, 0}}`。因此后续把初始化展开改为 `fillConstInitializer` 和 `fillRuntimeInitializer` 两套逻辑，通过 `subElementCount` 计算子数组大小，通过 `alignInitializerPosition` 在遇到嵌套列表时对齐到下一层数组边界。这样既能处理全局常量初始化，也能处理局部数组的运行期初始化。

该问题后来由 `tests/positive/06_global_multi_array.sy` 固定为回归测试：`pick(g)` 读取 `g[1][2]`，正确返回 5。

### 5.4 数组参数和数组退化

数组参数也是一个专门处理点。SysY 中函数形参 `int a[]` 或 `int a[][3]` 本质上是指针参数。语义层使用 `TypeInfo::isArrayParam` 区分：

- 普通数组作为右值或实参时，需要用 `getelemptr base, 0` 退化到首元素地址。
- 数组参数在函数内部先从形参槽中 `load` 出指针，再对第一维使用 `getptr`，对后续维度使用 `getelemptr`。
- 调用函数时，`compileArrayArgument` 检查实参数组退化后的剩余维度是否与形参维度一致。

这部分在 AI 生成后的修正中改动过。原来直接把数组表达式地址传给函数，在多维数组和数组参数组合时会出现层级不一致；修正后才明确区分普通数组退化、数组参数寻址和维度检查。

### 5.5 控制流和短路求值

`if`、`while`、`break`、`continue`、`return` 都在语义阶段直接生成 Koopa 基本块和跳转。实现中用 `newLabel` 生成块名，用 `terminated_` 标记当前基本块是否已经有终结指令，避免在同一基本块中继续输出普通指令。

`while` 语句生成条件块、循环体块和结束块，并把 `{continueTarget, breakTarget}` 压入 `loopStack_`。遇到 `break` 跳到结束块，遇到 `continue` 跳回条件块；如果栈为空就报错。

逻辑与、逻辑或没有直接当作普通二元运算处理，而是生成短路控制流：

- `a && b`：先把结果槽置 0，若 `a` 为真才进入右侧块计算 `b`。
- `a || b`：先把结果槽置 1，若 `a` 为假才进入右侧块计算 `b`。

短路表达式最后从临时结果槽 `load` 出 `i32` 布尔值。这种生成方式虽然多了一个临时 `alloc`，但语义清楚，能保证右侧表达式按短路规则决定是否执行。

### 5.6 float 支持策略

Koopa IR 在本项目中仍统一使用 `i32` 承载值。为了保留 SysY2022 的 float 语义，语义层把 float 常量转换为 IEEE 754 单精度的 32 位 bit pattern，运行期 float 运算则降低为内部 helper 调用：

```text
__sysy_i2f, __sysy_f2i,
__sysy_fadd, __sysy_fsub, __sysy_fmul, __sysy_fdiv,
__sysy_feq, __sysy_flt, __sysy_fle
```

例如 `int` 转 `float` 会生成 `call @__sysy_i2f(...)`，float 加法生成 `call @__sysy_fadd(...)`，float 比较生成 `__sysy_feq`、`__sysy_flt`、`__sysy_fle` 之一。后端根据 `KoopaResult::floatHelpers` 记录到的 helper 名称，只追加实际用到的 RV32F 汇编片段。

这种做法的优点是 Koopa 主体仍然简单，后端可以按 `i32` 统一传值；不足是 float 没有成为 Koopa 层的原生类型，后续如果要做优化或兼容更多外部 ABI，需要重新设计类型表示。

### 5.7 `putf` 降低

SysY 运行时的 `putf` 带格式串。本项目没有在后端特殊处理它，而是在语义阶段把 `putf` 降低为多个 `putint` 和 `putch` 调用。当前支持 `%d` 和 `%c`：

- 遇到普通字符，生成 `call @putch(ascii)`。
- 遇到 `%d`，消耗一个参数并生成 `call @putint(value)`。
- 遇到 `%c`，消耗一个参数并生成 `call @putch(value)`。
- 格式串参数不足或多余都会报错。

`tests/positive/08_putf.sy` 中的 `putf("%d:%c\n", n, n)` 会输出 `65:A` 并换行。

## 6. RISC-V 后端实现

后端位于 `src/backend/RiscV.cpp`。入口 `RiscVGenerator::generate` 先调用 `koopa_parse_from_string` 把 Koopa 文本解析为 Koopa program，再用 `koopa_build_raw_program` 得到 raw program，之后由 `RawEmitter` 遍历输出汇编。

### 6.1 栈帧布局

后端采用保守的全部落栈策略，没有实现复杂寄存器分配。每个函数在生成前先扫描一遍 raw program，构造 `FrameInfo`：

- `outgoingArgSize`：调用其他函数时，第 9 个及之后参数需要预留的栈空间。
- `valueOffsets`：形参、`alloc` 对象和有返回值的临时指令对应的栈偏移。
- `allocaValues`：记录 Koopa `alloc` 指令。
- `raOffset`：函数内出现调用时保存返回地址 `ra`。
- `frameSize`：所有空间汇总后按 16 字节对齐。

函数序言中调整 `sp`，必要时保存 `ra`；函数尾部统一跳到 epilogue，恢复 `ra` 和 `sp` 后 `ret`。如果函数没有任何局部空间，仍保留最小 16 字节栈帧，保证对齐策略一致。

### 6.2 参数传递和函数调用

后端按 RISC-V 调用约定处理整型寄存器参数：

- 调用者把前 8 个参数放入 `a0` 到 `a7`。
- 第 9 个及之后参数写入当前函数预留的 outgoing argument 区域。
- 被调用者入口把所有形参保存到自己的栈帧中，便于后续统一通过栈槽读取。
- 非叶子函数保存并恢复 `ra`。

返回值统一从 `a0` 取出。如果 Koopa call 指令有返回值，后端会把 `a0` 保存到该临时值对应的栈槽中。

### 6.3 指令覆盖范围

当前后端覆盖了项目生成 Koopa IR 所需的主要 raw 指令：

- `alloc`、`load`、`store`。
- 整数二元运算和比较。
- `getptr`、`getelemptr`。
- `branch`、`jump`、`return`。
- `call`。
- 全局变量初始化，包括整数、聚合初始化、`zeroinit`。

寻址时如果偏移超过 RISC-V 12 位立即数范围，`emitLoadWord`、`emitStoreWord`、`emitAddi` 会改用 `li t6, imm` 加 `add` 的方式生成大偏移访问，避免栈帧较大时汇编非法。

### 6.4 float helper 汇编

float helper 只在语义层记录到实际使用时追加。例如 `__sysy_fadd` 会把 `a0`、`a1` 中的 bit pattern 通过 `fmv.w.x` 移到浮点寄存器，执行 `fadd.s`，再通过 `fmv.x.w` 把结果放回 `a0`。`__sysy_i2f` 使用 `fcvt.s.w`，`__sysy_f2i` 使用 `fcvt.w.s` 并采用 `rtz` 舍入。

该策略让主后端仍按 `i32` 处理 Koopa 值，但生成的汇编需要 RV32F 支持，README 中也明确建议使用 `-march=rv32imf`。

## 7. AI 辅助代码生成记录

本实验选择的 AI 辅助任务是“AI 辅助的代码生成”，目标代码块为：

```text
src/semantic/SemanticAnalyzer.cpp
```

该文件最终 1520 行，超过任务要求的 500 行。完整 prompt、交互摘要和关键补丁已整理到 `docs/ai_codegen.md`。本次撰写报告时曾尝试检查其中记录的原始 Codex jsonl 路径，但该原始日志文件在当前机器路径下已经不存在；因此本报告关于 AI 交互过程的直接依据是已经纳入仓库的 `docs/ai_codegen.md`，并结合当前源码进行复核。

AI 初始生成的语义分析模块已经包含主要框架：`TypeInfo`、`FunctionInfo`、`Symbol`、`Scope`、`ExprResult`、`LValueResult`，以及运行时函数安装、函数签名收集、声明编译、语句编译、表达式编译、常量表达式求值和 Koopa 文本输出。

后续修正过程比较关键，主要包括：

1. 补充 `<functional>`，因为数组元素总数计算使用了 `std::multiplies<int>`；同时删除语义模块中未使用的 `alignTo`。
2. 修正数组实参传递，把普通数组作为实参时的退化、数组参数在函数体内的加载和多维维度检查分开处理。
3. 修正 Koopa 函数形参命名：函数签名中的参数应使用 `@p_name` 形式，而不是局部临时值的 `%name` 形式。
4. 增加 `%` 运算的语义检查，限制左右操作数必须为整数。
5. Docker 构建暴露了 Flex 宏分号、Koopa include/library 搜索路径、Bison 顶层声明和函数定义区分等问题，之后在相关文件中修正。
6. 测试 `06_global_multi_array.sy` 时发现多维数组初始化展开错误，最终通过 `fillConstInitializer` 和 `fillRuntimeInitializer` 修正。

从这个过程看，AI 更适合生成结构复杂但规则明确的初稿，例如符号表和表达式编译框架；但和语言标准细节相关的地方仍需要人工用测试逼近真实语义，尤其是数组初始化、数组参数退化和 Koopa 文本合法性。

## 8. 调试过程记录

### 8.1 构建环境问题

项目初期在 Docker 标准环境中构建时暴露出几个实际问题。

第一，Flex 词法规则中的宏和动作代码需要保证展开后仍是合法 C++ 语句。调试过程中曾遇到生成的 lexer C++ 代码报出 `expected ';' after do/while statement`，最终通过规范化宏展开形式解决。

第二，后端包含 `<koopa.h>`，本机普通环境不一定有 Koopa 头文件和库。`CMakeLists.txt` 后来显式查找 `/opt/include`、`/opt/lib/native`、`/opt/lib`，找不到就给出清晰的 fatal error，提示应在 `maxxing/compiler-dev` 内构建或手动设置 Koopa 路径。

第三，Bison 顶层规则如果直接按 EBNF 粗略翻译，`BType IDENT` 之后的分支容易混淆函数定义和变量声明。当前语法器在 Docker 构建时仍报告 2 个 shift/reduce 冲突和 2 个 reduce/reduce 冲突，同时提示 `KEYWORD_INT`、`KEYWORD_FLOAT` 两条规则因冲突而无用。虽然现有测试可以通过，但这说明前端语法仍有可清理空间。

第四，前端转换层当前没有把真实 token 位置完整传到本项目 AST。转换类使用固定的 `SourceLocation(1, 1)`，所以语义错误都能报出错误类型，但行列号会退化为 `1:1`。这一点需要在后续完善位置信息传递时补回。

### 8.2 语义阶段问题

语义阶段的调试重点集中在“看起来能生成 IR，但语义不完全正确”的问题。

数组参数最初只检查“实参是不是数组”，但没有准确比较退化后的维度。对于 `int a[][3]` 这类形参，实参 `g` 的类型是 `[2][3]`，作为参数传入后第一维退化，剩余维度应为 `[3]`。修正后 `compileArrayArgument` 会构造退化后的维度向量，并和形参记录的维度逐项比较。

多维数组初始化的 bug 更隐蔽。`tests/positive/06_global_multi_array.sy` 中 `g[1][2]` 应为 5，简单展平会得到不同布局。这个问题促使初始化逻辑从“收集叶子”改成“按层级填充”，并通过子数组大小对齐嵌套列表位置。

float 相关问题主要是 Koopa 层没有使用 float 原生类型。为了让前后端都能工作，语义层把所有 float 值按 `i32` 表示，后端再补 RV32F helper。这个策略在 `tests/positive/07_float.sy` 中得到验证：`half(7)` 得到 3.5，赋给 int 后返回 3。

### 8.3 后端问题

后端采用全部落栈后，主要风险不在寄存器分配，而在栈帧大小、参数位置和地址计算。

函数调用时必须先扫描函数内所有 call，算出最多有多少个超过 8 个的实参，从而在当前栈帧底部预留 outgoing argument 区域。否则后续 call 写第 9 个参数时可能覆盖本函数局部变量。

数组寻址也需要区分 `getptr` 和 `getelemptr`。数组参数本质是指针，访问第一维时使用 `getptr`；普通数组对象或后续维度使用 `getelemptr`。后端在 `emitGetPtr` 中根据 raw instruction 的类别和源类型计算元素大小，再生成地址加法。

另外，RISC-V 的 load/store 立即数范围有限。项目中补充了 `fitsImm12` 判断，大偏移用 `li t6` 和 `add` 展开，避免大数组或深调用导致偏移无法编码。

## 9. 测试与验证

测试样例位于 `tests/positive` 和 `tests/negative`。正向测试用于确认编译器能生成 Koopa 和 RISC-V，负向测试用于确认语义错误能被拒绝。

标准构建与烟测命令为：

```bash
cmake -S . -B build
cmake --build build -j
./build/compiler -koopa tests/positive/01_main.sy -o /tmp/01_main.koopa
./build/compiler -riscv tests/positive/01_main.sy -o /tmp/01_main.S
```

仓库中的 `scripts/run_smoke.sh` 会对 tests/positive/*.sy 逐个生成 Koopa 和 RISC-V。报告撰写时先在 Windows/WSL 下用已有 `build/compiler` 复核过生成流程；之后又使用 Docker Desktop 和 `maxxing/compiler-dev` 镜像，从当前工作区源码重新配置、构建，并在容器内完成汇编、链接和 qemu 运行。Docker 构建命令使用临时目录 `/tmp/sysy-report-build`，没有改写仓库内的 `build` 目录。

当前源码的 Docker 构建结果为成功：

```text
-- Found FLEX: /usr/bin/flex (found version "2.6.4")
-- Found BISON: /usr/bin/bison (found version "3.8.2")
[100%] Built target compiler
```

同时构建中出现以下警告：Bison 语法有 2 个 shift/reduce 冲突、2 个 reduce/reduce 冲突；`ObjManager.h` 使用了 C99 compound literal；`ErrorMessage.h` 中有未使用参数和未使用函数。这些警告没有阻止构建，但应作为后续清理项记录。

正向样例覆盖如下：

| 样例 | 覆盖点 | 记录结果 |
| --- | --- | --- |
| `01_main.sy` | 最小 `main` 和 `return` | Docker qemu 退出码 0 |
| `02_expr.sy` | 表达式优先级、一元负号、除法、取模 | Docker qemu 退出码 253，即返回 -3 的 8 位退出码表示 |
| `03_scope_const.sy` | 全局 const、局部作用域遮蔽 | Docker qemu 退出码 42 |
| `04_control.sy` | `while`、`if`、`continue`、`break` | Docker qemu 退出码 24 |
| `05_function_array.sy` | 函数调用和一维数组参数 | Docker qemu 退出码 10 |
| `06_global_multi_array.sy` | 全局多维数组初始化、数组参数 `a[][3]` | Docker qemu 退出码 5 |
| `07_float.sy` | float 运算、int/float 隐式转换 | Docker qemu 退出码 3 |
| `08_putf.sy` | `putf` 降低到 `putint`/`putch` | 输出 `65:A`，Docker qemu 退出码 0 |
| `09_floyd_warshall.sy` | Floyd-Warshall 最短路完整小程序：全局二维数组、数组参数、三重循环、条件更新、多函数调用 | WSL 已验证 Koopa 和 RISC-V 汇编生成；预期 qemu 退出码 30 |

需要说明的是，`02_expr.sy` 的源码返回值为：

```c
1 + 2 * -3 + (10 / 2) % 3
```

计算结果为 `1 - 6 + 2 = -3`。测试记录中的退出码 253 按 Linux 退出码规则正好对应返回值 -3。报告撰写时也重新核对了源码表达式，确认这条记录与样例语义一致。

负向样例复核结果如下：

| 样例 | 触发错误 | 当前错误信息 |
| --- | --- | --- |
| `01_redefine.sy` | 同一作用域重复定义变量 | `compile error: 1:1: duplicate symbol 'a' in the same scope` |
| `02_const_assign.sy` | 给 const 对象赋值 | `compile error: 1:1: cannot assign to const object` |
| `03_bad_break.sy` | 循环外使用 `break` | `compile error: 1:1: break used outside a loop` |
| `04_call_args.sy` | 函数实参数量不匹配 | `compile error: 1:1: function 'f' called with wrong number of arguments` |

负向样例的错误类型均正确，但当前前端转换层没有完整保留真实源码位置，因此行列号统一显示为 `1:1`。当前报告按最终源码复核结果记录为 `1:1`。

本次复核还对原有 8 个正向样例执行了 RISC-V 生成、链接和 qemu 运行，关键输出如下：

```text
== 01_main ==
exit=0
== 02_expr ==
exit=253
== 03_scope_const ==
exit=42
== 04_control ==
exit=24
== 05_function_array ==
exit=10
== 06_global_multi_array ==
exit=5
== 07_float ==
exit=3
== 08_putf ==
65:A
exit=0
```

为避免测试样例过于碎片化，后续又新增了 `tests/positive/09_floyd_warshall.sy`。该样例实现了 Floyd-Warshall 最短路：先把全局二维数组 `graph` 复制到 `dist`，再通过 `floyd` 函数执行三重循环松弛，最后用 `reachable_sum` 统计所有可达最短路长度之和。该程序覆盖全局数据段、数组作为函数参数、多函数调用、嵌套循环、条件更新、数组寻址和后端栈帧生成等多个组合场景。当前环境下已复核它可以生成 Koopa IR 和 RISC-V 汇编；按源码语义，最终返回值应为 30，适合作为答辩现场的完整小程序演示样例。

## 10. 当前实现的不足

第一，后端没有真正的寄存器分配。所有中间值、参数和局部对象都落栈，代码生成简单可靠，但性能较低，生成的汇编也偏长。后续可以在 raw program 遍历时加入活跃区间分析或简单 LRU 寄存器分配。

第二，语义分析和 Koopa IR 生成目前在同一个文件内完成。这样开发速度快，也便于一次遍历 AST 输出 IR，但模块职责偏重。若继续扩展，可以先构造一层项目自有的 typed IR，再单独做 Koopa lowering。

第三，float 支持采用 `i32` bit pattern 加 helper 的折中方案。它能通过当前样例，但不适合进一步做 Koopa 层优化，也没有完整覆盖更复杂的浮点 ABI 场景。

第四，测试仍以烟测为主。当前样例覆盖了主要语法和典型错误，但还应补充递归函数、超过 8 个参数的调用、短路表达式副作用、大栈帧偏移、局部多维数组初始化、const 数组越界、未闭合字符串和未闭合注释等测试。

第五，当前前端能通过现有样例，但构建仍有 Bison 冲突和位置退化问题。后续应优先消除语法冲突，并把词法位置信息一路传递到 ObjTree 和本项目 AST。

## 11. 总结

本项目完成了从 SysY2022 源码到 RISC-V 汇编的完整链路。前端使用 Flex/Bison 构造 AST；语义阶段实现了作用域、函数表、常量表达式、数组初始化、数组参数、控制流、短路求值、运行时函数和 float helper；后端通过 `libkoopa` raw program 生成 RISC-V 汇编，并实现了栈帧、参数传递、全局数据、分支跳转和函数调用。

从开发过程看，最耗时的部分不是把某个语法规则写出来，而是让不同阶段的表示保持一致：数组在 AST、语义类型、Koopa 类型和 RISC-V 地址计算中含义必须一致；float 在语义值、Koopa `i32` 和 RV32F helper 中也必须一致。调试中发现的数组初始化和数组参数问题正好说明，编译器实验需要用小而精确的样例持续校验每条规则。

AI 辅助在本项目中主要用于生成语义分析核心初稿，显著提高了代码框架搭建速度；但最终可用性依赖人工复核、构建反馈和针对性测试。当前实现已经能覆盖仓库中的正向和负向样例，并保留了后续优化方向。
