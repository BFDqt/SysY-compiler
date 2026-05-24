# compiler2022_x 前端移植变更说明

本文档用于给汇报同学对稿：当前项目确实已经使用队员原先写的 `compiler2022_x` 前端作为主前端，但不是“原封不动直接替换”。为了接入当前项目已有的 AST、语义分析和后端，做了少量工程化适配和兼容性补丁。

汇报时建议说法：

> 我们保留了 compiler2022_x 前端的 ObjTree、Parser、Yacc/Lex 结构，并把它接入当前编译器。接入方式是在 Driver 层调用原前端生成 ObjTree，再转换为当前项目已有的 `sysy::ast::Program`，所以语义分析和后端基本不需要重写。

不要说：

> 直接把原来的前端文件复制进来就完成了替换。

也不要说：

> 语义分析器改成了 compiler2022_x 的语义分析。

实际上 `src/semantic/SemanticAnalyzer.cpp` 没有因这次移植被重写。

## 1. 总体架构变化

移植前，当前项目的前端流程是：

```text
main.cpp
  -> sysy::Driver::parseFile()
  -> 当前项目自己的 Flex/Bison 前端
  -> sysy::ast::Program
  -> SemanticAnalyzer::compile()
  -> Koopa / RISC-V
```

移植后，当前项目的前端流程变为：

```text
main.cpp
  -> sysy::Driver::parseFile()
  -> compiler2022_x::FrontEnd::parse_from_file()
  -> ObjTree::ObjManager 中的通用 AST 树
  -> ObjTreeAdapter 转换
  -> sysy::ast::Program
  -> SemanticAnalyzer::compile()
  -> Koopa / RISC-V
```

核心变化是多了一层适配：

```text
compiler2022_x ObjTree  --->  当前项目 sysy::ast::Program
```

这层适配写在 `src/frontend/driver.cpp` 中。

## 2. 原 compiler2022_x 代码保留了哪些

新增目录：

```text
src/frontend/compiler2022_x/
```

其中保留了原前端的主要结构：

```text
src/frontend/compiler2022_x/ErrorMessage.h
src/frontend/compiler2022_x/StrDump.h
src/frontend/compiler2022_x/ObjTree/ObjManager.h
src/frontend/compiler2022_x/FrontEnd/Parser.h
src/frontend/compiler2022_x/FrontEnd/Parser.cpp
src/frontend/compiler2022_x/FrontEnd/YaccLex/StrDump.cpp
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/YaccLex.cpp
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/sysy.l
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/sysy.y
```

原来的设计思想没有改：parser 不直接生成当前项目的强类型 AST，而是通过 `ObjTree::ObjManager` 管理节点，每个节点有：

- `uuid`
- `type_name`
- `node_info`
- `son_uuid_list`

也就是说，队员汇报原前端时讲“使用统一对象管理器保存 AST 节点”仍然是正确的。

## 3. CMake 构建入口变化

文件：`CMakeLists.txt`

现在 Bison/Flex 不再使用旧的：

```text
src/frontend/sysy.y
src/frontend/sysy.l
```

而是使用移植后的：

```text
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/sysy.y
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/sysy.l
```

同时新增编译单元：

```text
src/frontend/compiler2022_x/FrontEnd/Parser.cpp
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/YaccLex.cpp
src/frontend/compiler2022_x/FrontEnd/YaccLex/StrDump.cpp
```

并新增 include 路径：

```text
src/frontend/compiler2022_x
```

汇报时可以说：

> 构建系统已经切换到 compiler2022_x 的 yacc/lex 文件，旧前端文件仍留在仓库中作为历史代码，但当前目标 `compiler` 不再使用旧 `src/frontend/sysy.y` 和 `src/frontend/sysy.l`。

## 4. Driver 层做了什么

文件：`src/frontend/driver.cpp`

这是本次移植最大的适配点。

原 `compiler2022_x` 的 `parse_from_file()` 返回的是：

```cpp
ObjTree::ObjUuid
```

它表示 `ObjManager` 中 AST 根节点的编号。

但当前项目后续语义分析需要的是：

```cpp
std::unique_ptr<sysy::ast::Program>
```

因此新增了 `ObjTreeAdapter`，负责把 `ObjTree` 转换为当前项目 AST。

适配器处理的主要节点包括：

- `CompUnitAst` -> `ast::Program`
- `CompUnitDefAst` -> `ast::TopLevel`
- `DeclAst` / `ConstDeclAst` / `VarDeclAst` -> `ast::Decl`
- `ConstDefAst` / `VarDefAst` -> `ast::VarDef`
- `FuncDefAst` -> `ast::FuncDef`
- `FuncFParamAst` -> `ast::FuncParam`
- `BlockAst` / `BlockItemAst` -> `ast::Block` / `ast::BlockItem`
- `StmtAst` -> `ast::Stmt`
- 表达式节点 -> `ast::Expr`

特别注意：这不是重写 parser，而是把 parser 产物转换成当前项目已有 AST。

汇报时不要把 `ObjTreeAdapter` 说成 compiler2022_x 原本就有的部分。它是为了接入当前项目新增的胶水层。

## 5. sysy.l 的变化

文件：

```text
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/sysy.l
```

相比原 `temp/compiler2022-x_frontend/src/FrontEnd/YaccLex/src/sysy.l`，主要补丁如下。

### 5.1 增加当前项目需要的错误类型

新增包含：

```cpp
#include "sysy/common.hpp"
```

作用是让 lexer 可以抛出当前项目统一的：

```cpp
sysy::CompileError
```

这样前端错误能被 `main.cpp` 的异常处理统一接住。

### 5.2 增强浮点字面量识别

原代码只识别较简单形式：

```text
[0-9][0-9]*\.[0-9]*
```

现在补充了：

- 十进制浮点，如 `2.0`、`.5`、`1e3`、`1.2e-3`
- 十六进制浮点，如 `0x1.2p3`
- 可选后缀 `f/F/l/L`

这是为了更接近 SysY 2022 对浮点常量的要求，并兼容当前项目已有测试。

### 5.3 增加字符串字面量

新增 `StringLiteral` 规则，用于支持：

```c
putf("%d:%c\n", n, n);
```

原 `compiler2022_x` 前端没有字符串 token；当前项目的语义分析里已经有 `BaseType::String` 和 `putf` 处理，所以 lexer/parser 需要补齐字符串入口。

### 5.4 注释和非法字符处理更严格

修改点：

- 多行注释中的换行会更新 `line_number`
- 未闭合的 `/* ...` 会抛出 `CompileError`
- 未闭合字符串会抛出 `CompileError`
- 不认识的字符会抛出 `CompileError`

这属于工程化接入补丁，方便错误被当前项目统一处理。

### 5.5 函数类型 lookahead 小修

原代码通过：

```text
int/[ \t]*identifier(
float/[ \t]*identifier(
```

区分函数返回类型和变量声明。

现在改成：

```text
int/[[:space:]]*identifier(
float/[[:space:]]*identifier(
```

主要是允许函数名和 `(` 之间出现更一般的空白字符，同时修正了原正则里的 `A-z` 范围写法。

## 6. sysy.y 的变化

文件：

```text
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/sysy.y
```

相比原 parser，主要补丁如下。

### 6.1 新增 `STRING_LITERAL`

新增 token：

```yacc
%token <raw_string> STRING_LITERAL
```

新增语法节点：

```yacc
StringConst
```

并允许 `PrimaryExp` 包含 `StringConst`。

用途：支持 `putf` 的格式串参数。

### 6.2 支持空初始化列表

新增：

```yacc
InitVal
    : LBRACE RBRACE
```

这样可以识别：

```c
int a[3] = {};
```

适配当前语义层的数组初始化逻辑。

### 6.3 parser 错误改为抛异常

原 `yyerror` 是打印错误信息：

```cpp
cerr << ...
```

现在改为：

```cpp
throw sysy::CompileError({line_number, 1}, s);
```

作用：让语法错误进入当前项目统一错误处理流程。

## 7. YaccLex.cpp 的变化

文件：

```text
src/frontend/compiler2022_x/FrontEnd/YaccLex/src/YaccLex.cpp
```

主要变化：

- 打开文件失败时抛 `sysy::CompileError`
- 每次解析前重置 `line_number = 1`
- 调用 `yyrestart(input)` 初始化 scanner
- 解析结束后关闭文件
- 调用 `yylex_destroy()` 清理 scanner 状态
- 异常路径也会关闭文件并清理 scanner

这些改动是为了让前端可以被当前 `Driver::parseFile()` 稳定调用，避免多次编译不同文件时 scanner 状态残留。

## 8. 哪些部分没有改

以下内容不是本次移植重点，也不要在汇报中说成“重写了”：

- `src/semantic/SemanticAnalyzer.cpp`
- `src/backend/RiscV.cpp`
- `include/sysy/ast.hpp`
- `src/ast/ast.cpp`

当前策略是保留原项目的语义分析和后端，让新前端通过适配器产出它们能消费的 AST。

## 9. 与原 compiler2022_x 的关系

可以这样总结：

```text
原 compiler2022_x 前端主体：保留
原 ObjTree 设计：保留
原 Parser 对外接口 parse_from_file：保留
原 AST 节点类型名：基本保留
当前项目 AST：继续使用
当前项目语义分析：继续使用
当前项目后端：继续使用
新增内容：ObjTree -> sysy::ast::Program 的适配层
补丁内容：字符串、浮点、空初始化、错误处理、scanner 清理
```

## 10. 已知限制和汇报风险点

### 10.1 位置信息不完整

`compiler2022_x` 的 `ObjTree` 节点没有保存完整的 `line/column` 位置信息。因此 `ObjTreeAdapter` 转换成当前 AST 时，节点位置统一使用：

```text
1:1
```

lexer/parser 阶段的错误仍可通过 `line_number` 给出行号，但转换后进入语义分析的错误定位不如旧前端精确。

汇报时如果被问到错误定位，要说明：

> 目前移植重点是前端结构接入；ObjTree 没有逐节点位置，因此语义阶段错误位置暂时比较粗略。

### 10.2 Bison 仍有 shift/reduce conflict

在 `maxxing/compiler-dev` 中构建时，Bison 会提示：

```text
2 shift/reduce conflicts
```

这来自移植前端的语法写法，当前测试没有因此失败。不要说“parser 完全无冲突”。

### 10.3 `KEYWORD_INT_FUNC` / `KEYWORD_FLOAT_FUNC` 是原前端的特殊设计

原前端为了区分：

```c
int a;
int f() { ... }
```

在 lexer 层把函数返回类型识别为 `KEYWORD_INT_FUNC` 或 `KEYWORD_FLOAT_FUNC`。

这个设计仍然保留。汇报时如果讲 grammar，不要漏掉 lexer 对函数定义的这个特殊 lookahead 处理。

## 11. 验证结果

验证环境：

```text
docker image: maxxing/compiler-dev:latest
```

构建命令：

```bash
docker run --rm -v "${PWD}:/workspace" -w /workspace maxxing/compiler-dev \
  bash -lc 'rm -rf build-codex-docker &&
            cmake -S . -B build-codex-docker &&
            cmake --build build-codex-docker -j'
```

测试结果：

```text
tests/positive/*.sy  全部通过，可生成 Koopa 和 RISC-V
tests/negative/*.sy  全部被正确拒绝
```

已验证的正例包括：

- `01_main.sy`
- `02_expr.sy`
- `03_scope_const.sy`
- `04_control.sy`
- `05_function_array.sy`
- `06_global_multi_array.sy`
- `07_float.sy`
- `08_putf.sy`

已验证的反例包括：

- `01_redefine.sy`
- `02_const_assign.sy`
- `03_bad_break.sy`
- `04_call_args.sy`

## 12. 汇报时推荐表述

可以按下面这段讲：

> 本次前端移植采用了低侵入方式。我们没有重写语义分析和后端，而是把队员原先的 compiler2022_x 前端接入 CMake，让它负责词法、语法分析并生成 ObjTree。由于当前项目后续阶段已经依赖 `sysy::ast::Program`，所以在 Driver 层新增了一个 ObjTreeAdapter，把 compiler2022_x 的通用树转换成当前项目 AST。为了通过 SysY 2022 当前测试，还补齐了字符串字面量、较完整的浮点常量、空初始化列表以及统一异常处理。最终在 maxxing/compiler-dev 容器内构建通过，正负例测试均通过。

不建议这样讲：

> 我们完全没有改 compiler2022_x 前端。

更准确的说法是：

> 主体结构保留，但为接入当前工程和覆盖当前测试做了必要补丁。

