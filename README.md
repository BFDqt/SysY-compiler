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

## 一键运行测试文件

如果助教给了一个新的 SysY 程序，例如 `case.sy`，推荐在 Docker 标准环境中直接用脚本跑完整链路：

```bash
bash scripts/run_case.sh case.sy
```

脚本会自动执行：

```text
构建 compiler -> 生成 Koopa -> 生成 RISC-V -> 链接 libsysy -> qemu 运行 -> 打印返回值
```

如果有输入文件，可以把输入文件作为第二个参数：

```bash
bash scripts/run_case.sh case.sy input.txt
```

在 Windows 下也可以直接把 `.sy` 文件拖到项目根目录的 `run-sysy.bat` 上运行；如果不拖文件，双击 `run-sysy.bat` 后按提示粘贴 `.sy` 文件路径也可以。该脚本会自动启动 `maxxing/compiler-dev` 容器执行测试，因此需要先打开 Docker Desktop。

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

## 答辩速通

如果你是第一次接触这个项目，建议先看以下三份专门为答辩准备的导读材料：
- **[docs/beginner_guide.md](docs/beginner_guide.md)**：按“整体流程 -> 编译原理基础 -> 关键实现 -> 测试样例 -> 高频问答”顺序整理，适合快速理解项目主线和应对提问。
- **[docs/code_structure_guide.md](docs/code_structure_guide.md)**：包含文件目录详解、核心函数速查以及用于拿分的技术亮点总结，极度适合“看着代码展示说明”使用。
- **[docs/defense_demo_script.md](docs/defense_demo_script.md)**：包含可以直接复制的现场演示脚本、汇编知识速查以及“防翻车”预案。
