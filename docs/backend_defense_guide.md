# SysY 后端目标代码生成汇报讲稿与答辩问答

## 一、10 分钟汇报讲稿

### 0. 开场定位，约 1 分钟

我负责的是项目中的目标代码生成部分，也就是把前面阶段生成的 Koopa IR 进一步翻译成 RISC-V 汇编代码。整个项目主流程在 `src/main.cpp:66` 附近：前端先把 SysY 源码解析成 AST，语义分析再把 AST 编译成 Koopa IR。如果命令行选择 `-riscv`，程序就进入后端模块。

后端调用位置在 `src/main.cpp:72` 到 `src/main.cpp:78`：

```cpp
std::ofstream out(options.output);
sysy::backend::RiscVGenerator generator;
generator.generate(koopa.text, koopa.floatHelpers, out);
```

也就是说，我这部分的输入不是 SysY 源码，也不是 AST，而是语义阶段已经生成好的 Koopa IR 文本；输出是用户指定路径下的 `.S` 汇编文件。

### 1. 后端入口，约 1 分钟

后端接口定义在 `include/sysy/riscv.hpp:9`，真正入口在 `src/backend/RiscV.cpp:597` 的 `RiscVGenerator::generate`。

这个函数可以理解为后端模块的主入口。它主要做三件事：

1. 调用 `koopa_parse_from_string`，把 Koopa 文本解析成 Koopa program。
2. 调用 `koopa_build_raw_program`，得到更适合遍历的 raw program。
3. 创建 `RawEmitter`，调用 `emitter.generate()` 真正输出 RISC-V 汇编。

这里我没有手写 Koopa 解析器，而是直接使用 `libkoopa`。这样做的好处是后端只需要面对结构化后的 raw program，不需要处理文本解析细节。

### 2. 总体生成流程，约 1 分钟

后端核心类是 `RawEmitter`，定义在 `src/backend/RiscV.cpp:99`。它的总调度函数是 `generate`，位置在 `src/backend/RiscV.cpp:105`。

整体流程分三步：

```cpp
emitGlobals();
for (...) {
    emitFunction(func);
}
emitFloatHelpers();
```

第一步 `emitGlobals()` 生成全局变量的数据段；第二步遍历所有函数，调用 `emitFunction()` 生成代码段；第三步 `emitFloatHelpers()` 如果发现程序中用到了 float 运算，就追加内部浮点 helper 汇编。

因此，我的后端是按 Koopa raw program 的结构逐层展开的：

```text
Program
  -> 全局对象
  -> 函数
     -> 基本块
        -> 指令
```

### 3. 全局变量生成，约 1 分钟

全局变量生成在 `src/backend/RiscV.cpp:126` 的 `emitGlobals`，初始化器处理在 `src/backend/RiscV.cpp:147` 的 `emitInitializer`。

例如全局数组：

```asm
  .data
  .globl g
g:
  .word 1
  .word 2
  .word 0
```

这里的含义是：

- `.data` 表示下面进入数据段，不是代码段。
- `.globl g` 表示把符号 `g` 导出为全局可见，方便链接器识别。
- `g:` 是标签，代表变量 `g` 的内存地址。
- `.word 1` 表示在当前位置放一个 32 位整数。
- 如果是零初始化，会输出 `.zero N`，表示预留 N 字节并置零。

这部分对应课程 PPT 里的静态区空间。全局变量的大小和初值在编译时就能确定，所以后端把它们放到数据段，而不是放到函数的运行时栈帧里。

### 4. 函数生成和栈帧，约 2 分钟

函数生成入口在 `src/backend/RiscV.cpp:170` 的 `emitFunction`。每个函数生成前，会先调用 `src/backend/RiscV.cpp:208` 的 `buildFrame` 来计算栈帧。

栈帧信息结构是 `FrameInfo`，定义在 `src/backend/RiscV.cpp:24`。其中几个字段很关键：

- `frameSize`：当前函数总共需要多少栈空间。
- `outgoingArgSize`：当前函数调用其他函数时，超过 `a0-a7` 的实参预留区。
- `raOffset`：保存返回地址寄存器 `ra` 的位置。
- `valueOffsets`：每个 Koopa value 对应的栈偏移。
- `allocaValues`：记录 Koopa 里的 `alloc` 局部对象。

我采用的是全部临时值落栈策略。也就是说，Koopa IR 里的临时结果、局部变量、参数都会在 `buildFrame` 阶段分配一个栈槽。后面翻译指令时，只临时使用 `t0`、`t1`、`t2` 做计算，计算完就把结果存回栈。

函数开头会生成：

```asm
addi sp, sp, -frameSize
```

函数结尾会生成：

```asm
addi sp, sp, frameSize
ret
```

这正好对应课程 PPT 里的过程活动记录 AR 的申请和释放。`buildFrame` 做的是活动记录大小和各个对象偏移的静态计算；`emitFunction` 做的是运行时真正调整 `sp` 和恢复 `sp` 的指令输出。

### 5. 参数和调用约定，约 1 分钟

参数保存逻辑在 `src/backend/RiscV.cpp:265` 的 `spillIncomingArguments`，函数调用生成在 `src/backend/RiscV.cpp:432` 的 `emitCall`。

RISC-V 调用约定规定：

- 前 8 个参数放在 `a0` 到 `a7`。
- 第 9 个及以后参数放在调用者栈上。
- 返回值放在 `a0`。
- `ra` 保存返回地址。

所以在生成 call 时，后端会把前 8 个参数依次移动到 `a0-a7`，超过 8 个的参数写入当前函数提前预留的 outgoing argument 区域。call 之后，如果这个调用有返回值，返回值就在 `a0`，后端会立刻把 `a0` 存回当前 Koopa call 结果对应的栈槽。

如果当前函数内部还有别的函数调用，`ra` 可能被新的 call 覆盖，所以 `buildFrame` 会给 `ra` 分配保存位置，函数入口保存，函数出口恢复。

### 6. 指令翻译，约 2 分钟

指令总分发在 `src/backend/RiscV.cpp:280` 的 `emitInstruction`。我按 Koopa 指令类型分别处理：

- `alloc`：不输出指令，因为已经在 `buildFrame` 中变成栈空间。
- `load`：在 `src/backend/RiscV.cpp:317` 的 `emitLoad`，生成 `lw`。
- `store`：在 `src/backend/RiscV.cpp:326` 的 `emitStore`，生成 `sw`。
- `binary`：在 `src/backend/RiscV.cpp:335` 的 `emitBinary`，生成 `add/sub/mul/div/rem/slt` 等。
- `getptr/getelemptr`：在 `src/backend/RiscV.cpp:402` 的 `emitGetPtr`，用于数组地址计算。
- `branch`：在 `src/backend/RiscV.cpp:422` 的 `emitBranch`，生成 `bnez` 和 `j`。
- `call`：在 `src/backend/RiscV.cpp:432` 的 `emitCall`。
- `return`：在 `src/backend/RiscV.cpp:450` 的 `emitReturn`。

例如 Koopa 中有：

```koopa
%t = add %a, %b
```

后端翻译逻辑是：把 `%a` 从栈加载到 `t0`，把 `%b` 从栈加载到 `t1`，执行 `add t0, t0, t1`，再把结果存回 `%t` 对应的栈槽。

### 7. 数组地址和大偏移，约 1 分钟

数组访问主要依赖 `getptr` 和 `getelemptr`，实现位置在 `src/backend/RiscV.cpp:402`。

它的核心公式是：

```text
目标地址 = 基地址 + 下标 * 元素大小
```

所以汇编里会看到：

```asm
li t2, elementSize
mul t1, t1, t2
add t0, t0, t1
```

另外，RISC-V 的 `lw`、`sw`、`addi` 等指令的立即数范围有限，通常是 12 位。项目在 `src/backend/RiscV.cpp:503` 的 `emitLoadWord`、`src/backend/RiscV.cpp:514` 的 `emitStoreWord`、`src/backend/RiscV.cpp:525` 的 `emitAddi` 中做了判断。如果偏移超出范围，就先用 `li t6, offset` 加载大立即数，再用 `add` 计算地址，避免大栈帧或大数组时生成非法汇编。

### 8. Float helper，约 1 分钟

浮点 helper 在 `src/backend/RiscV.cpp:541` 的 `emitFloatHelpers`。语义阶段如果遇到 float 运算，会记录 helper 名称，例如 `__sysy_fdiv`。后端只输出实际用到的 helper。

整数主体面向 RV32IM，其中 `M` 扩展提供 `mul/div/rem`。float helper 会用到 RV32F 指令，例如 `fdiv.s`、`fcvt.w.s`。所以准确地说，本项目的整数后端是 RV32IM，float 是通过 RV32F helper 扩展支持。

### 9. 总结，约 30 秒

我的后端以 Koopa raw program 为输入，按全局变量、函数、基本块、指令逐层遍历生成 RISC-V 汇编。核心设计是先用 `buildFrame` 计算完整栈帧，把参数、局部变量和临时值都映射到栈槽；再逐条翻译 Koopa 指令。这个实现没有做复杂寄存器分配，而是采用全部落栈策略，优点是结构清晰、正确性好，代价是生成代码中 `lw/sw` 较多，性能不是最优。

### 10. 后端测试策略和设计取舍，约 1 分钟

后端测试的核心目标不是只看能不能生成一个 `.S` 文件，而是验证三件事：

1. Koopa raw program 中出现的主要指令，后端都能识别并翻译。
2. 生成的 RISC-V 汇编符合基本调用约定和栈帧规则。
3. 汇编经过链接和 qemu 运行后，返回值或输出结果与 SysY 源程序预期一致。

测试策略上，我把它分成三层。

第一层是生成测试，也就是对 `tests/positive/*.sy` 全部执行 `-riscv`，确认后端不会遇到 unsupported instruction，也不会因为栈槽、地址计算、全局初始化等问题崩溃。这一层主要验证后端指令覆盖范围。

第二层是运行测试，也就是把生成的 `.S` 用 RISC-V 工具链汇编、链接，再用 qemu 运行，检查退出码或输出。例如 `04_control.sy` 预期返回 24，验证分支和循环；`05_function_array.sy` 验证函数调用和数组参数；`06_global_multi_array.sy` 验证全局多维数组和地址计算；`07_float.sy` 验证 float helper；`08_putf.sy` 验证运行时库调用和输出。新增的 `09_floyd_warshall.sy` 是一个更完整的小程序，用 Floyd-Warshall 最短路验证全局二维数组、数组参数、三重循环、条件更新和多函数调用，预期返回 30。

第三层是人工检查关键汇编片段。例如查看函数开头是否正确调整 `sp`，非叶子函数是否保存和恢复 `ra`，call 前参数是否放入 `a0-a7`，数组访问是否生成了“基地址 + 下标 * 元素大小”的地址计算。

设计取舍上，后端选择了全部临时值落栈，没有做寄存器分配。这是一个有意识的取舍：课程设计阶段优先保证正确性和可调试性，减少寄存器覆盖、调用约定破坏这类隐蔽 bug。代价是生成的汇编更长，`lw/sw` 更多，性能不是最优。后续如果继续完善，第一步会加入活跃变量分析，再做简单寄存器分配和更多后端压力测试。

## 二、现场演示建议

建议优先演示 `tests/positive/09_floyd_warshall.sy`。它比单点样例更完整，能说明后端不是只通过了碎片化测试。如果时间紧，再用 `tests/positive/04_control.sy` 或 `tests/positive/06_global_multi_array.sy` 补充解释控制流和数组地址计算。

演示完整小程序：

```bash
cat tests/positive/09_floyd_warshall.sy
./build/compiler -koopa tests/positive/09_floyd_warshall.sy -o /tmp/floyd.koopa
./build/compiler -riscv tests/positive/09_floyd_warshall.sy -o /tmp/floyd.S
cat /tmp/floyd.S
```

重点指出：

- 这个程序实现 Floyd-Warshall 最短路，不是单一语法点测试。
- `.data` 中有全局二维数组 `graph` 和全局结果数组 `dist`。
- 汇编中会生成 `copy_graph`、`floyd`、`reachable_sum` 和 `main` 多个函数。
- `floyd` 中有三重 `while` 循环，对应多个基本块和大量分支跳转。
- 数组访问会触发 `getptr/getelemptr` 到 RISC-V 地址计算。
- 程序预期返回 30，即所有可达最短路长度之和。

演示控制流：

```bash
./build/compiler -riscv tests/positive/04_control.sy -o /tmp/demo.S
cat /tmp/demo.S
```

重点指出：

- `.text` 和 `.globl main`：函数代码段和函数入口。
- `addi sp, sp, -80`：建立栈帧。
- `.L_main_while_cond_0`：基本块标签。
- `bnez` 和 `j`：条件跳转与无条件跳转。
- 大量 `lw/sw`：体现全部临时值落栈策略。
- `a0`：函数返回值寄存器。

演示数组：

```bash
./build/compiler -riscv tests/positive/06_global_multi_array.sy -o /tmp/array.S
cat /tmp/array.S
```

重点指出：

- `.data` 中的 `g:` 是全局数组。
- `.word 1 .word 2 .word 0 .word 3 .word 4 .word 5` 体现多维数组展开和补零。
- 访问 `a[1][2]` 时，汇编中通过 `index * elementSize` 计算地址。

## 三、后端答辩可能问题与参考回答

### 1. 你这部分到底输入输出是什么？

输入是语义阶段生成的 Koopa IR 文本和 float helper 集合，输出是 RISC-V 汇编文件。调用点在 `src/main.cpp:72` 到 `src/main.cpp:78`，后端入口在 `src/backend/RiscV.cpp:597`。

可以回答：

“我的后端不直接处理 SysY 源码和 AST，而是消费 Koopa IR。这样前端、中端和后端解耦，只要 Koopa 合法，后端就能独立生成汇编。”

### 2. 为什么要用 `libkoopa`？

因为 Koopa 文本本身是字符串，直接手写解析器容易出错。`libkoopa` 可以把 Koopa 文本转成 raw program，后端只需要遍历结构体。

可以回答：

“使用 `libkoopa` 后，后端面对的是函数、基本块、指令这些结构化对象，而不是字符串，所以翻译逻辑更稳定，也更符合编译器分层设计。”

### 3. 什么是 Koopa value？

Koopa value 在代码里就是 `koopa_raw_value_t`。它可以是整数常量、函数参数、局部变量地址、全局变量、二元运算结果、load 结果、call 结果等。

可以回答：

“我把 Koopa value 理解为 IR 里的值或能产生值的节点。后端会给需要保存的 Koopa value 分配栈槽，后续通过 `valueOffsets` 找到它在栈上的位置。”

### 4. 什么是栈帧？你的栈帧里放了什么？

栈帧是一个函数运行时在栈上占用的一段连续空间。它对应课程里的过程活动记录。

本项目的栈帧中主要放：

- outgoing argument 区域；
- 形参副本；
- 局部变量和数组；
- Koopa 临时结果；
- 必要时保存的 `ra`。

相关代码在 `FrameInfo` 和 `buildFrame`，位置是 `src/backend/RiscV.cpp:24` 和 `src/backend/RiscV.cpp:208`。

### 5. `buildFrame` 为什么要提前扫描整个函数？

因为生成函数开头的 `addi sp, sp, -frameSize` 时，就必须已经知道整个函数需要多少栈空间。

可以回答：

“栈帧大小必须在函数入口确定，所以我需要提前扫描所有基本块和指令，统计参数、alloc 对象、临时结果、call 的额外参数区以及是否需要保存 `ra`。”

### 6. `outgoingArgSize` 是什么？

RISC-V 调用约定规定前 8 个参数放 `a0-a7`，第 9 个及以后的参数放在调用者栈上。`outgoingArgSize` 就是当前函数调用其他函数时，给这些额外参数预留的栈空间。

相关代码在 `src/backend/RiscV.cpp:208` 的 `buildFrame` 和 `src/backend/RiscV.cpp:432` 的 `emitCall`。

可以回答：

“如果当前函数内部最多调用过一个 10 参数函数，那么超过 8 个的部分有 2 个参数，需要预留 8 字节 outgoing argument 区域。”

### 7. `alloc` 为什么不输出汇编？

Koopa 中的 `alloc` 表示申请局部对象空间。后端在 `buildFrame` 阶段已经把它转换成栈帧中的固定空间了，所以运行到 `alloc` 指令时不需要再生成额外汇编。

相关代码在 `src/backend/RiscV.cpp:280` 的 `emitInstruction`：

```cpp
case KOOPA_RVT_ALLOC:
    return;
```

可以回答：

“alloc 的效果已经体现在函数入口的栈帧申请里，也就是 `addi sp, sp, -frameSize`，所以它不是一条需要运行时执行的真实机器指令。”

### 8. 什么是寄存器分配？你们做了吗？

寄存器分配是把变量或临时值长期放到物理寄存器中，以减少内存访问的优化技术。

本项目没有做完整寄存器分配，但不是完全不用寄存器。项目会使用：

- `t0/t1/t2/t6` 做临时计算；
- `a0-a7` 做参数和返回值；
- `sp` 做栈指针；
- `ra` 保存返回地址。

区别在于，Koopa 临时值不会长期绑定到寄存器，而是都放在栈槽里。

可以回答：

“我们的策略是全部临时值落栈。它牺牲了一些性能，但大幅降低实现复杂度，能更容易保证正确性。”

### 9. 为什么汇编里有很多 `lw` 和 `sw`？

因为采用了全部落栈策略。每个 Koopa 临时结果都有栈槽，计算前先 `lw` 到临时寄存器，计算后再 `sw` 回栈槽。

可以回答：

“这不是 bug，而是设计取舍。我们优先保证目标代码正确和调用约定稳定，没有做寄存器分配优化，所以内存访问较多。”

### 10. `a0` 不是参数寄存器吗，为什么又放返回值？

这是 RISC-V 调用约定规定的。调用前，`a0` 是第一个参数；调用返回后，`a0` 是返回值。

例如：

```asm
mv a0, t0
call f
sw a0, 36(sp)
```

可以回答：

“call 前我把第一个参数放到 `a0`，call 返回后，`a0` 的语义变成函数返回值，所以后端会马上把它存入 call 结果对应的栈槽。”

### 11. `ra` 是什么？为什么要保存？

`ra` 是 return address，返回地址寄存器。执行 `call` 时，硬件会把返回位置写入 `ra`。如果当前函数内部还会调用其他函数，新的 call 会覆盖 `ra`，所以必须先保存。

可以回答：

“叶子函数可以不保存 `ra`，但只要函数内部有 call，我就会在栈帧中给 `ra` 分配位置，入口保存，出口恢复。”

### 12. 多于 8 个参数时，被调用函数怎么拿到参数？

调用者把第 9 个及之后参数放在自己的 outgoing argument 区域。被调用者进入后，会建立自己的栈帧，此时这些额外参数位于被调用者新 `sp` 上方，所以 `spillIncomingArguments` 会用：

```cpp
frame_.frameSize + (i - 8) * 4
```

从调用者传来的栈参数位置读入，再存到本函数自己的参数栈槽。

相关代码在 `src/backend/RiscV.cpp:265`。

### 13. 什么是基本块？为什么按基本块遍历？

基本块是一段顺序执行的指令序列，通常最后以 `br`、`jump` 或 `ret` 结束。Koopa raw program 已经把函数组织成基本块，所以后端自然按基本块输出标签并翻译内部指令。

可以回答：

“基本块结构让控制流很清晰。每个 Koopa 基本块在汇编中对应一个 label，分支指令跳转到这些 label。”

### 14. `getptr` 和 `getelemptr` 是什么？

它们是 Koopa 中用于数组和指针寻址的指令。后端统一把它们翻译成：

```text
新地址 = 基地址 + 下标 * 元素大小
```

相关代码在 `src/backend/RiscV.cpp:402`。

可以回答：

“数组访问不是直接取值，而是先算地址。`getptr/getelemptr` 负责地址计算，后面如果需要值，再通过 `load` 读取。”

### 15. RV32IM 是什么？M 扩展是什么？

RISC-V 是模块化指令集。`RV32I` 是 32 位基础整数指令集，`M` 扩展提供整数乘除法指令，比如 `mul`、`div`、`rem`。本项目整数后端会生成这些指令，所以目标至少是 RV32IM。

可以回答：

“M 扩展不是额外软件库，而是 RISC-V 指令集的一个硬件扩展集合。”

### 16. 为什么项目里又出现 RV32F？

`F` 是单精度浮点扩展。项目主体后端按 32 位整数值处理 Koopa，但 float 运算通过 helper 完成，helper 中会用到 `fadd.s`、`fdiv.s`、`fcvt.w.s` 等 RV32F 指令。

可以回答：

“整数主线是 RV32IM，float 作为扩展能力需要 RV32F，所以实际运行 float 样例时使用 `-march=rv32imf`。这不是冲突，而是 RISC-V 指令集扩展的组合。”

### 17. `__sysy_fdiv` 这段汇编怎么解释？

示例：

```asm
__sysy_fdiv:
  fmv.w.x ft0, a0
  fmv.w.x ft1, a1
  fdiv.s ft0, ft0, ft1
  fmv.x.w a0, ft0
  ret
```

含义是：

- `a0` 和 `a1` 中保存两个 float 的 32 位 bit pattern；
- `fmv.w.x` 把整数寄存器中的原始比特搬到浮点寄存器；
- `fdiv.s` 执行单精度浮点除法；
- `fmv.x.w` 把结果比特搬回 `a0`；
- `ret` 返回。

可以回答：

“这里的 `fmv` 是搬运比特，不是数值转换；真正的 int/float 数值转换使用 `fcvt`。”

### 18. `.text`、`.data`、`.globl`、label 分别是什么意思？

- `.text`：代码段。
- `.data`：数据段。
- `.globl name`：把符号导出为全局可见。
- `name:`：标签，表示一个地址，可以是函数入口或全局变量地址。

可以回答：

“汇编文件不仅包含指令，也包含给汇编器和链接器看的段信息和符号信息。”

### 19. 为什么要处理大偏移？

RISC-V 的一些指令不能直接编码任意大的立即数，比如 `lw offset(sp)` 的 offset 范围有限。如果栈帧很大，偏移可能超过范围。

项目在 `emitLoadWord`、`emitStoreWord`、`emitAddi` 中统一处理这个问题。小偏移直接输出，大偏移先 `li t6, imm`，再 `add` 得到真实地址。

可以回答：

“这是为了保证在大数组或大栈帧情况下，后端仍然生成合法 RISC-V 汇编。”

### 20. 你们和课程 PPT 的目标代码生成有什么关系？

课程 PPT 里讲的是从四元式生成虚拟目标机代码。我们的项目是从 Koopa IR 生成 RISC-V 汇编。输入形式和目标机不同，但本质一样：都是把中间表示翻译成目标指令。

可以回答：

“PPT 中的活动记录、栈式管理、参数传递、目标指令生成，在我们项目中分别对应 `buildFrame`、`sp` 调整、`a0-a7` 调用约定、`emitInstruction` 指令翻译。”

### 21. 为什么没有 Display 表或静态链？

Display 表和静态链主要用于嵌套过程访问非局部变量，例如 Pascal 风格嵌套函数。SysY 没有嵌套函数定义，所以变量访问只需要区分全局变量和当前函数栈帧变量。

可以回答：

“由于语言特性不同，我们不需要实现 Display 表和静态链。全局变量通过 `.data` 段访问，局部变量通过 `sp + offset` 访问。”

### 22. 后端有哪些不足？

可以主动说明三点：

1. 没有真正的寄存器分配，生成代码中内存访问较多。
2. 没有做数据流分析驱动的目标代码优化。
3. float 采用 helper 方案，不是 Koopa 层原生 float 类型。

可以回答：

“这些是为了在课程设计时间内优先保证正确性和完整链路。后续优化方向是加入活跃变量分析、寄存器分配和更系统的浮点类型支持。”

### 23. 如果老师问：你这部分最核心的设计点是什么？

可以回答：

“最核心的是 `buildFrame + emitInstruction` 两阶段设计。第一阶段把函数运行时需要的所有空间确定下来，第二阶段逐条 Koopa 指令翻译成汇编。这样运行时空间管理和指令选择的边界比较清楚。”

### 24. 如果老师问：你怎么证明后端是对的？

可以回答：

“我主要通过正向样例验证后端能够生成汇编，并通过链接运行验证返回值。样例覆盖了表达式、控制流、函数调用、数组参数、全局多维数组、float 和 putf。比如 `04_control.sy` 覆盖分支和循环，`06_global_multi_array.sy` 覆盖全局数组和数组寻址，`07_float.sy` 覆盖 float helper。”

### 25. 一句话总结后端

可以回答：

“这个后端以 Koopa raw program 为输入，通过完整栈帧计算和逐指令翻译，把中间表示转换成符合 RISC-V 调用约定的汇编代码；实现上选择全部落栈来换取结构简单和正确性稳定。”

## 四、整组汇报开头稿

这一段适合由第一位同学开场，控制在 1 分钟左右。

各位老师好，我们小组本次课程设计完成的是一个面向 SysY2022 语言的教学编译器。它的输入是 SysY 源程序，输出可以是 Koopa IR 中间表示，也可以进一步生成 RISC-V 汇编代码。

整个编译流程可以概括为：

```text
SysY 源码
  -> 词法分析
  -> 语法分析
  -> AST
  -> 语义分析和 Koopa IR 生成
  -> RISC-V 目标代码生成
```

我们小组按照编译器的典型阶段进行分工：前端同学主要负责词法分析、语法分析和 AST 构造；中端同学主要负责语义检查、符号表、类型系统和 Koopa IR 生成；后端同学主要负责把 Koopa IR 翻译成 RISC-V 汇编，并处理栈帧、函数调用、数组寻址和运行时库调用等目标代码生成问题。

这次汇报我们会先介绍项目整体架构，然后按前端、中端、后端三个模块说明实现思路，最后通过测试样例展示编译器从源码到汇编的完整链路。

如果时间较紧，可以使用下面这个更短版本：

各位老师好，我们小组实现的是一个 SysY2022 编译器，支持将 SysY 源码编译为 Koopa IR，并进一步生成 RISC-V 汇编。项目整体分为前端、中端和后端三个阶段：前端负责把源码解析成 AST，中端负责语义分析和 Koopa IR 生成，后端负责目标代码生成。下面我们会按这条编译流水线依次介绍实现，并用测试样例证明整个链路可以跑通。

## 五、模块串场稿

### 1. 从整体架构转到前端

接下来先从编译器的第一阶段，也就是前端开始介绍。前端的目标是把原始的 SysY 源码从字符串形式转换成结构化的程序表示。这里主要涉及词法分析、语法分析和 AST 构造。下面由负责前端的同学介绍这一部分。

### 2. 从前端转到中端

前端完成之后，我们已经得到了结构化的 AST。但是 AST 只能说明程序在语法上是合法的，还不能说明程序在语义上一定正确。例如变量是否重复定义、函数调用参数是否匹配、数组维度是否合法，这些都需要在中端完成。因此下面进入语义分析和 Koopa IR 生成部分。

### 3. 从中端转到后端

中端完成后，我们得到了 Koopa IR。Koopa IR 已经比 SysY 源码更接近机器执行模型，它把表达式、控制流、函数调用和数组访问都表示成了统一的中间指令。接下来后端要做的事情，就是把这些 Koopa 指令进一步翻译成真实目标机上的 RISC-V 汇编代码。

### 4. 后端讲完转到测试

后端完成后，整个编译链路就闭合了：源码可以经过前端和中端变成 Koopa IR，再由后端生成 RISC-V 汇编。接下来我们通过几个测试样例展示这个编译器的实际效果，包括正常程序的编译结果，以及错误程序的语义检查结果。

### 5. 测试讲完转到总结

通过这些测试可以看到，正向样例能够完整生成 Koopa IR 和 RISC-V 汇编，负向样例也能够在语义阶段被拒绝。最后我们对整个项目的完成情况、设计取舍和不足之处做一个总结。

## 六、测试演示稿

### 1. 测试部分开场

测试部分我们主要验证两类能力。第一类是正向样例，证明编译器能够正确处理表达式、控制流、函数调用、数组、float 和输出函数等语言结构。第二类是负向样例，证明编译器不仅能生成代码，也能识别并拒绝不合法的 SysY 程序。

### 2. 推荐现场演示样例一：完整 Floyd-Warshall 小程序

如果希望展示一个更完整的小程序，优先演示 `tests/positive/09_floyd_warshall.sy`。这个样例实现的是 Floyd-Warshall 最短路算法，规模不大，但比单点样例更接近真实程序。

可以先展示源码：

```bash
cat tests/positive/09_floyd_warshall.sy
```

讲解话术：

这个样例里，程序先把全局二维数组 `graph` 复制到 `dist`，再调用 `floyd` 做三重循环的最短路松弛，最后用 `reachable_sum` 统计所有可达最短路长度之和。它覆盖了全局二维数组、数组作为函数参数、多函数调用、嵌套循环、条件更新和数组寻址。预期返回值是 30。

然后生成 Koopa IR 和 RISC-V 汇编：

```bash
./build/compiler -koopa tests/positive/09_floyd_warshall.sy -o /tmp/floyd.koopa
./build/compiler -riscv tests/positive/09_floyd_warshall.sy -o /tmp/floyd.S
cat /tmp/floyd.S
```

讲解话术：

在汇编中可以看到 `.data` 段里有 `graph` 和 `dist` 两个全局对象；`.text` 段里有 `copy_graph`、`floyd`、`reachable_sum` 和 `main` 多个函数。`floyd` 的三重循环会生成多组基本块标签和分支跳转，数组访问会生成“基地址 + 下标 * 元素大小”的地址计算。这个样例能综合验证后端的全局数据、函数调用、栈帧、控制流和数组寻址。

如果现场环境支持链接和 qemu，可以继续运行：

```bash
clang /tmp/floyd.S -c -o /tmp/floyd.o -target riscv32-unknown-linux-elf -march=rv32imf -mabi=ilp32
ld.lld /tmp/floyd.o -L"$CDE_LIBRARY_PATH/riscv32" -lsysy -o /tmp/floyd
qemu-riscv32-static /tmp/floyd
echo $?
```

讲解话术：

最后输出的退出码应该是 30。这个返回值来自最短路矩阵中所有可达路径长度的求和，可以作为这个完整小程序的正确性校验。

### 3. 推荐现场演示样例二：控制流

推荐先演示 `tests/positive/04_control.sy`。这个样例包含 `while`、`if`、`continue` 和 `break`，适合展示从控制流到 RISC-V 分支跳转的过程。

可以先展示源码：

```bash
cat tests/positive/04_control.sy
```

讲解话术：

这个样例里，程序用 `while` 循环累加数字，中间遇到 `i == 4` 时 `continue`，遇到 `i == 8` 时 `break`。所以它可以覆盖循环条件、条件分支、continue 跳回循环条件、break 跳出循环这些控制流场景。

然后生成 Koopa IR：

```bash
./build/compiler -koopa tests/positive/04_control.sy -o /tmp/demo.koopa
cat /tmp/demo.koopa
```

讲解话术：

这里可以看到 Koopa IR 中已经出现了多个基本块，例如循环条件块、循环体块和循环结束块。`br` 表示条件分支，`jump` 表示无条件跳转。也就是说，中端已经把源码里的结构化控制流转换成了显式的基本块跳转。

再生成 RISC-V 汇编：

```bash
./build/compiler -riscv tests/positive/04_control.sy -o /tmp/demo.S
cat /tmp/demo.S
```

讲解话术：

在汇编里，`.L_main_while_cond_0` 这类标签对应 Koopa 中的基本块。`bnez` 对应条件跳转，`j` 对应无条件跳转。函数开头的 `addi sp, sp, -80` 是建立栈帧，结尾的 `addi sp, sp, 80` 和 `ret` 是释放栈帧并返回。大量 `lw` 和 `sw` 是因为后端采用了全部临时值落栈策略。

如果现场环境支持链接和 qemu，可以继续运行：

```bash
clang /tmp/demo.S -c -o /tmp/demo.o -target riscv32-unknown-linux-elf -march=rv32imf -mabi=ilp32
ld.lld /tmp/demo.o -L"$CDE_LIBRARY_PATH/riscv32" -lsysy -o /tmp/demo
qemu-riscv32-static /tmp/demo
echo $?
```

讲解话术：

最后输出的退出码就是 SysY `main` 函数的返回值。这个样例的预期返回值是 24，用它可以证明控制流和后端分支跳转生成是正确的。

### 4. 推荐现场演示样例三：全局多维数组

如果老师更关心数组，可以演示 `tests/positive/06_global_multi_array.sy`。

命令：

```bash
cat tests/positive/06_global_multi_array.sy
./build/compiler -riscv tests/positive/06_global_multi_array.sy -o /tmp/array.S
cat /tmp/array.S
```

讲解话术：

这个样例定义了全局二维数组 `g[2][3]`，并通过函数参数 `int a[][3]` 传入数组，最后访问 `a[1][2]`。它同时覆盖了全局数据段、多维数组初始化、数组作为函数参数传递和后端数组地址计算。

在汇编里可以看到：

```asm
.data
.globl g
g:
  .word 1
  .word 2
  .word 0
  .word 3
  .word 4
  .word 5
```

这说明数组初始化被展开到了 `.data` 段，未显式初始化的位置补 0。访问数组元素时，后端通过“基地址 + 下标 * 元素大小”的方式计算地址。

### 5. 推荐现场演示样例四：float helper

如果要展示 float，可以演示 `tests/positive/07_float.sy`。

命令：

```bash
./build/compiler -riscv tests/positive/07_float.sy -o /tmp/float.S
cat /tmp/float.S
```

讲解话术：

这个样例会触发 float 除法和 float 到 int 的转换。我们的 Koopa 主体仍然按 32 位值传递 float 的 bit pattern，后端只在确实使用 float 运算时追加 helper。例如可以看到 `__sysy_fdiv` 和 `__sysy_f2i`。这部分使用 RV32F 指令，所以 float 样例运行时需要 `-march=rv32imf`。

### 6. 负向样例演示

负向测试用于说明编译器不仅能生成代码，还能检查语义错误。

可以演示函数调用参数错误：

```bash
cat tests/negative/04_call_args.sy
./build/compiler -koopa tests/negative/04_call_args.sy -o /tmp/bad.koopa
```

讲解话术：

这个样例中函数调用的实参数量和函数定义不匹配。编译器应该在语义分析阶段报错，而不是继续生成 Koopa 或汇编。这个测试说明项目实现了函数签名检查。

也可以演示：

```bash
./build/compiler -koopa tests/negative/01_redefine.sy -o /tmp/bad.koopa
./build/compiler -koopa tests/negative/02_const_assign.sy -o /tmp/bad.koopa
./build/compiler -koopa tests/negative/03_bad_break.sy -o /tmp/bad.koopa
```

对应说明：

- `01_redefine.sy`：同一作用域重复定义。
- `02_const_assign.sy`：给 const 对象赋值。
- `03_bad_break.sy`：循环外使用 `break`。

### 7. 测试部分总结

可以这样收尾：

通过这些样例，我们验证了编译器的两类能力。正向样例覆盖表达式、控制流、函数调用、数组和 float，其中 `09_floyd_warshall.sy` 还提供了一个更完整的算法小程序，用来验证多个模块组合后的后端稳定性；负向样例覆盖重复定义、const 赋值、非法 break 和函数参数不匹配，能够在语义阶段正确报错。因此这个项目不是只完成了代码生成，还实现了从源码检查到目标代码生成的完整闭环。

### 8. 后端测试策略专用稿

如果老师要求你单独说明后端怎么测试，可以这样讲：

我对后端的测试分为生成验证、运行验证和人工检查三层。

生成验证是最基础的一层。我会对所有正向样例执行 `-riscv`，确认后端可以把 Koopa IR 转成汇编，不会因为某类 Koopa 指令没有覆盖而失败。这一层主要覆盖 `load`、`store`、`binary`、`branch`、`jump`、`call`、`return`、`getptr/getelemptr` 和全局初始化。

运行验证是更关键的一层。汇编文件生成后，还需要用 RISC-V 工具链汇编、链接，并通过 qemu 运行。比如 `04_control.sy` 用返回值 24 验证循环和分支，`05_function_array.sy` 验证函数调用和数组传参，`06_global_multi_array.sy` 验证 `.data` 段和多维数组寻址，`07_float.sy` 验证 float helper，`08_putf.sy` 验证运行时库调用和输出，`09_floyd_warshall.sy` 用预期返回值 30 验证一个更完整的多函数算法程序。

人工检查主要是针对后端容易出错的地方。比如检查函数入口是否有 `addi sp, sp, -frameSize`，函数退出是否恢复 `sp`，有函数调用时是否保存 `ra`，调用前是否把参数放到 `a0-a7`，超过 8 个参数时是否使用 outgoing argument 区域，数组访问是否按元素大小计算偏移。

后端测试也有取舍。当前测试更偏功能正确性，没有做大规模随机测试、性能测试，也没有系统覆盖所有极端情况，例如非常大的栈帧、超过 8 个参数的大量组合、复杂嵌套数组访问等。这个取舍是因为课程设计阶段首先要保证主链路正确，后续如果继续做，会补充专门的后端压力样例和差分测试。

## 七、整组结尾总结稿

这一段适合最后一位同学收尾，控制在 1 分钟左右。

总的来说，我们这个项目完成了一个 SysY2022 编译器的完整主链路：从源程序出发，经过词法分析、语法分析、AST 构造、语义分析、Koopa IR 生成，最后输出 RISC-V 汇编。

在实现上，我们尽量保持阶段边界清晰。前端只负责把源码转换成结构化 AST；中端负责符号表、类型检查、常量求值、控制流和 Koopa IR；后端只消费 Koopa raw program，并按照 RISC-V 调用约定生成目标汇编。

项目中的几个重点难点包括：多维数组初始化和补零、数组参数退化、短路求值的控制流生成、函数调用约定、栈帧布局，以及 float helper 的折中实现。通过正向和负向测试，我们验证了主要语言结构可以编译通过，典型语义错误也可以被拒绝。

当然，项目仍然有一些不足。例如后端没有实现真正的寄存器分配，因此生成代码中内存访问较多；前端仍有语法冲突警告和错误位置不够精确的问题；float 目前也不是 Koopa 层原生类型，而是通过 helper 支持。如果后续继续完善，我们会优先考虑寄存器分配、更多数据流优化、错误定位增强和更系统的浮点支持。

以上就是我们小组本次编译原理课程设计的汇报，请各位老师批评指正。

如果时间很紧，可以使用下面这个短版本：

本项目完成了 SysY2022 到 RISC-V 汇编的完整编译链路。我们按前端、中端、后端分工实现了词法语法分析、AST 构造、语义检查、Koopa IR 生成和目标代码生成。项目覆盖了表达式、控制流、函数、数组、float 和运行时库调用等主要功能，也通过负向样例验证了语义错误检查。当前实现优先保证正确性和完整性，后续可以在寄存器分配、数据流优化和错误定位方面继续改进。谢谢各位老师。

## 八、突发情况备用话术

### 1. 现场 Docker 或 qemu 跑不起来

可以说：

“当前现场环境可能缺少 RISC-V 链接或 qemu 运行依赖，所以这里重点展示编译器生成 Koopa IR 和 RISC-V 汇编的过程。我们在报告中已经记录过 Docker 标准环境下的构建和 qemu 运行结果，正向样例返回值与预期一致。”

### 2. 老师问为什么汇编很长

可以说：

“这是因为后端采用了全部临时值落栈策略。每个 Koopa 临时值都有自己的栈槽，计算时再加载到 `t0/t1`。这样生成代码不够精简，但正确性更容易保证，也避免了寄存器分配中的复杂覆盖问题。”

### 3. 老师问项目最大不足

可以说：

“我认为最大不足是后端没有做寄存器分配和数据流优化。当前目标代码生成是直接翻译型，功能完整但性能不是最优。后续如果继续做，会先加入活跃变量分析，再基于活跃区间做简单寄存器分配。”

### 4. 老师问 AI 辅助部分怎么保证不是直接生成完就用

可以说：

“AI 主要用于生成语义分析模块的初稿和部分结构化代码，但最终能不能用还是靠人工复核、构建反馈和测试样例验证。比如数组初始化、数组参数退化、Koopa 合法性和后端汇编生成都经过了后续调试和修正。我们在 `docs/ai_codegen.md` 中记录了这部分过程。”
