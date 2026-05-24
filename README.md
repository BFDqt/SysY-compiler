# SysY2022 Compiler

本项目是编译原理课程设计实现：使用 C++17、Flex、Bison 和 Koopa IR，将 SysY2022 源程序编译为 RISC-V 汇编。

## 功能

- Flex 词法分析：关键字、标识符、整数/浮点常量、字符串、运算符、分隔符、注释和词法错误。
- Bison 语法分析：SysY2022 函数、声明、数组、初始化列表、表达式、if/while/break/continue/return。
- 语义分析：栈式符号表、作用域、常量求值、类型检查、函数签名检查、数组初始化展开、短路求值。
- Koopa IR：支持 `-koopa` 输出，整数、数组、控制流、函数调用和全局变量使用标准 Koopa。
- RISC-V 后端：基于 `libkoopa` raw program 遍历生成 RV32 汇编，采用全部临时值落栈的保守策略。
- SysY2022 float：语义层保留 float 类型，Koopa 中以 32 位 bit pattern 表示，RISC-V 阶段按需生成 RV32F helper。

## Docker 构建

```bash
docker pull maxxing/compiler-dev
docker run -it --rm -v "$PWD":/root/compiler maxxing/compiler-dev bash
cd /root/compiler
cmake -S . -B build
cmake --build build -j
```

生成的可执行文件为：

```bash
build/compiler
```

## 使用

输出 Koopa IR：

```bash
./build/compiler -koopa tests/positive/01_main.sy -o out.koopa
```

输出 RISC-V 汇编：

```bash
./build/compiler -riscv tests/positive/01_main.sy -o out.S
```

手工链接运行：

```bash
clang out.S -c -o out.o -target riscv32-unknown-linux-elf -march=rv32imf -mabi=ilp32
ld.lld out.o -L"$CDE_LIBRARY_PATH/riscv32" -lsysy -o out
qemu-riscv32-static out
echo $?
```

## 项目结构

```text
include/sysy/          公共类型、AST、语义和后端接口
src/frontend/sysy.l    Flex 词法规则
src/frontend/sysy.y    Bison 语法规则和 AST 构造动作
src/ast/               AST 构造与 dump
src/semantic/          符号表、类型检查、常量求值、Koopa IR 生成
src/backend/           libkoopa raw program 到 RISC-V 汇编
tests/                 正向和负向样例
docs/                  实验报告和 AI 辅助代码生成记录
```

## 说明

`-koopa` 模式用于展示中间代码；整数、数组、函数和控制流可直接交给 Koopa 工具链验证。float 运算通过项目内 RISC-V helper 完成，因此 float 用例建议使用 `-riscv` 模式验证。
