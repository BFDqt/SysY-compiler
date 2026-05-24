# SysY2022 编译器实验报告

## 1. 整体设计

本项目采用经典分阶段编译结构：

```text
SysY2022 源码
  -> Flex 词法分析
  -> Bison 语法分析
  -> AST
  -> 语义分析与 Koopa IR 生成
  -> libkoopa raw program
  -> RISC-V 汇编
```

实现语言为 C++17，目标架构为 RISC-V。项目标准运行环境为 PKU MiniC 文档推荐的 `maxxing/compiler-dev` Docker 镜像。

## 2. 词法分析

词法分析文件为 `src/frontend/sysy.l`。主要处理：

- 关键字：`const/int/float/void/if/else/while/break/continue/return`
- 标识符、整型常量、浮点常量、字符串常量
- 运算符和分隔符
- 单行注释、多行注释、空白字符
- 非法字符、未闭合注释、未闭合字符串错误

## 3. 语法分析和 AST

语法分析文件为 `src/frontend/sysy.y`，按 SysY2022 EBNF 改写为 Bison 可处理的 LALR 形式。语义动作只负责构造 AST，避免把语义分析逻辑混入语法文件。

AST 定义位于 `include/sysy/ast.hpp`，覆盖编译单元、声明、函数、块、语句、表达式和初始化列表。

## 4. 语义分析

语义分析文件为 `src/semantic/SemanticAnalyzer.cpp`。主要工作：

- 使用栈式作用域符号表管理局部变量、全局变量、常量和数组。
- 预置 SysY 运行时库函数。
- 检查 `main` 唯一、无参、返回 `int`。
- 检查重复定义、未定义使用、const 赋值、函数调用参数匹配。
- 常量表达式编译期求值，数组维度必须为正整数常量。
- 数组初始化列表按行优先展开，不足补 0，超量报错。
- 支持 `int` 和 `float` 隐式转换。
- 使用基本块生成 if/while/break/continue/return 和 `&&`/`||` 短路求值。

## 5. Koopa IR 生成

整数、数组、指针、全局变量、函数调用和控制流均输出标准 Koopa IR。float 在 Koopa 层以 `i32` bit pattern 表示，语义层仍保留 float 类型，float 运算降低为 helper 调用：

```text
__sysy_i2f, __sysy_f2i,
__sysy_fadd, __sysy_fsub, __sysy_fmul, __sysy_fdiv,
__sysy_feq, __sysy_flt, __sysy_fle
```

`putf` 由语义层按格式串降低为 `putch` 和 `putint` 调用。

## 6. RISC-V 目标代码生成

后端文件为 `src/backend/RiscV.cpp`，使用 `libkoopa` 将文本 Koopa IR 转为 raw program 后遍历。

实现策略：

- 扫描每个函数计算栈帧。
- 临时值、局部变量、参数统一落栈。
- 前 8 个参数使用 `a0-a7`，多余参数放调用者栈帧。
- 非叶子函数保存和恢复 `ra`。
- 栈帧按 16 字节对齐。
- 全局变量输出 `.data/.globl/.word/.zero`。
- 按需追加 RV32F float helper 汇编。

## 7. AI 辅助代码生成

本项目选择“AI 辅助的代码生成”。目标代码块为：

```text
src/semantic/SemanticAnalyzer.cpp
```

该文件包含语义分析核心，超过 500 行，内容包括符号表、类型检查、常量求值、数组初始化展开和 Koopa IR 生成。完整 prompt、交互记录摘要和最终代码说明见 `docs/ai_codegen.md`。

## 8. 测试

测试样例位于 `tests/positive` 和 `tests/negative`。

建议测试流程：

```bash
cmake -S . -B build
cmake --build build -j
./build/compiler -koopa tests/positive/01_main.sy -o /tmp/main.koopa
./build/compiler -riscv tests/positive/01_main.sy -o /tmp/main.S
```

代表性用例覆盖：

- main 函数和 return
- 表达式优先级
- 常量、变量和作用域
- if/while/break/continue
- 函数调用和数组参数
- 全局变量和多维数组
- float 运算和隐式类型转换
- 运行时库输出

## 9. 代码风格

项目按模块分层；前端只构 AST，语义分析集中在独立模块，后端只消费 Koopa raw program。错误统一通过 `CompileError` 携带源码位置上报。
