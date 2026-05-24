# AI 辅助代码生成记录

## 任务选择

选择任务 1：AI 辅助的代码生成。

生成代码块：

```text
src/semantic/SemanticAnalyzer.cpp
```

该文件为语义分析核心实现，代码规模超过 500 行，已嵌入项目并保持项目 C++17 风格。

## Prompt

```text
请为一个 C++17 SysY2022 编译器生成语义分析与 Koopa IR 生成模块。

项目约束：
1. 前端已经通过 Flex/Bison 构造 AST。
2. 需要支持 int、float、void、字符串字面量、标量变量、常量、多维数组、数组参数、函数定义与调用。
3. 需要实现栈式作用域符号表，记录变量名、常量名、函数名、类型、作用域、数组维度、函数参数。
4. 需要检查重复定义、未定义使用、const 赋值、函数参数匹配、return 类型、break/continue 位置、数组维度和初始化器。
5. 需要生成 Koopa IR。标准整数、数组、指针、控制流使用 Koopa；float 以 32 位 bit pattern 表示，并降低为 helper 调用。
6. 需要实现 if、while、break、continue、return、短路 && 和 ||。
7. 需要实现全局变量、局部变量、常量表达式求值、数组初始化按行优先展开。
8. 代码应可维护，函数职责清晰，错误通过 CompileError 携带 SourceLocation 抛出。

请输出完整的 C++ 源文件实现，注释保持克制，只在复杂逻辑附近说明。
```

## 交互记录摘要

以下内容摘自本机 Codex 会话日志：

```text
C:\Users\Lenovo\.codex\sessions\2026\05\10\rollout-2026-05-10T19-41-55-019e59ca-99bb-7b71-834f-da19d49db3d6.jsonl
```

说明：日志中的时间戳为 UTC；下文同时保留原始时间戳和北京时间。`SemanticAnalyzer.cpp` 的初始生成补丁超过 1000 行，正文摘录真实日志中的关键开头、工具返回和后续修正记录；完整最终代码见 `src/semantic/SemanticAnalyzer.cpp`。

### 1. 用户提交实现请求

原始日志时间：

```text
2026-05-10T11:54:54Z
北京时间：2026-05-10 19:54:54
type: event_msg
payload.type: user_message
```

用户请求摘录：

```text
PLEASE IMPLEMENT THIS PLAN:
# SysY2022 编译器从零实现计划

## Summary
- 从空目录重新搭建 C++17 项目，标准环境使用 Docker `maxxing/compiler-dev`，目标架构选 RISC-V。
- 必须使用 Flex + Bison 完成词法/语法分析，生成 AST 后做语义分析，再输出 Koopa IR，并由 Koopa raw program 后端生成 RISC-V 汇编。
- 最终可执行文件命名为 `compiler`，支持：
  - `./compiler -koopa input.sy -o output.koopa`
  - `./compiler -riscv input.sy -o output.S`

AI 附加任务：
- 选择“AI 辅助代码生成”。
- 目标代码块为 `src/semantic/SemanticAnalyzer.cpp` 中语义分析核心，最终不少于 500 行。
- `docs/ai_codegen.md` 保存 prompt、交互摘要、最终生成代码说明，报告中引用该文档。
```

### 2. Codex 生成语义分析主文件

原始日志时间：

```text
2026-05-10T12:06:51Z
北京时间：2026-05-10 20:06:51
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

真实补丁开头摘录：

```diff
*** Begin Patch
*** Add File: src/semantic/SemanticAnalyzer.cpp
+#include "sysy/semantic.hpp"
+
+#include "sysy/common.hpp"
+
+#include <algorithm>
+#include <cmath>
+#include <map>
+#include <numeric>
+#include <optional>
+#include <set>
+#include <sstream>
+#include <utility>
+
+namespace sysy {
+namespace {
+
+struct TypeInfo {
+    BaseType base = BaseType::Int;
+    std::vector<int> dimensions;
+    bool isArrayParam = false;
+
+    bool isVoid() const { return base == BaseType::Void; }
+    bool isString() const { return base == BaseType::String; }
+    bool isScalar() const { return !isVoid() && !isString() && dimensions.empty() && !isArrayParam; }
+    bool isArrayLike() const { return !dimensions.empty() || isArrayParam; }
+    int rank() const { return static_cast<int>(dimensions.size()) + (isArrayParam ? 1 : 0); }
+};
+
+struct FunctionInfo {
+    BaseType returnType = BaseType::Int;
+    std::vector<TypeInfo> params;
+    bool defined = false;
+    bool external = false;
+};
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
A src/semantic/SemanticAnalyzer.cpp
```

该次生成建立了语义分析主体，包括 `TypeInfo`、`FunctionInfo`、`Symbol`、`Scope`、`ExprResult`、`LValueResult`，以及 `Impl::compile`、运行时库函数安装、函数签名收集、声明/语句/表达式编译、常量表达式求值、Koopa IR 文本输出等核心逻辑。

### 3. 初次代码生成后的即时修正

原始日志时间：

```text
2026-05-10T12:07:04Z
北京时间：2026-05-10 20:07:04
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

修正记录：

```diff
*** Begin Patch
*** Update File: src/semantic/SemanticAnalyzer.cpp
@@
 #include <algorithm>
 #include <cmath>
+#include <functional>
 #include <map>
@@
-int alignTo(int value, int align) {
-    return (value + align - 1) / align * align;
-}
-
 class Impl {
*** End Patch
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
M src/semantic/SemanticAnalyzer.cpp
```

这次修正原因：语义分析中使用了 `std::multiplies<int>` 计算数组元素总数，需要 `<functional>`；同时删除语义模块内暂未使用的 `alignTo`，避免无意义工具函数残留。

### 4. 数组参数与数组表达式衰变修正

原始日志时间：

```text
2026-05-10T12:12:56Z
北京时间：2026-05-10 20:12:56
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

修正记录摘录：

```diff
*** Begin Patch
*** Update File: src/semantic/SemanticAnalyzer.cpp
@@
             const TypeInfo &paramType = fn.params[i];
             if (paramType.isArrayLike()) {
-                ExprResult arg = compileExpr(*expr.args[i]);
-                if (!arg.type.isArrayLike()) {
-                    throw CompileError(expr.args[i]->loc, "array argument expected");
-                }
-                args.push_back(arg.value);
+                args.push_back(compileArrayArgument(*expr.args[i], paramType));
             } else {
                 ExprResult arg = castExpr(compileExpr(*expr.args[i]), paramType.base, expr.args[i]->loc);
                 args.push_back(arg.value);
             }
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
M src/semantic/SemanticAnalyzer.cpp
```

随后继续修正数组表达式作为右值时的衰变规则：

```text
2026-05-10T12:13:14Z
北京时间：2026-05-10 20:13:14
```

补丁摘录：

```diff
*** Begin Patch
*** Update File: src/semantic/SemanticAnalyzer.cpp
@@
     if (lvalue.type.isArrayLike()) {
         ExprResult result;
-        result.type = lvalue.type;
-        result.value = lvalue.address;
+        result.type.base = lvalue.type.base;
+        result.type.isArrayParam = true;
+        if (lvalue.type.isArrayParam) {
+            result.type.dimensions = lvalue.type.dimensions;
+            result.value = lvalue.address;
+        } else {
+            if (lvalue.type.dimensions.empty()) {
+                throw CompileError(expr.loc, "array expression cannot decay from a scalar element");
+            }
+            result.type.dimensions.assign(lvalue.type.dimensions.begin() + 1, lvalue.type.dimensions.end());
+            result.value = newTemp();
+            emit(result.value + " = getelemptr " + lvalue.address + ", 0");
+        }
         return result;
     }
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
M src/semantic/SemanticAnalyzer.cpp
```

### 5. 静态语法检查与警告处理

原始日志时间：

```text
2026-05-10T12:14:05Z
北京时间：2026-05-10 20:14:05
type: response_item
payload.type: function_call
name: shell_command
```

执行命令：

```powershell
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude -fsyntax-only src/semantic/SemanticAnalyzer.cpp
```

真实输出摘录：

```text
Exit code: 0
Wall time: 1.5 seconds
Output:
src/semantic/SemanticAnalyzer.cpp: In member function 'std::__cxx11::string sysy::{anonymous}::Impl::addressForIndexedObject(...)':
src/semantic/SemanticAnalyzer.cpp:899:120: warning: unused parameter 'loc' [-Wunused-parameter]
```

对应修正：

```text
2026-05-10T12:14:14Z
北京时间：2026-05-10 20:14:14
```

补丁摘录：

```diff
*** Begin Patch
*** Update File: src/semantic/SemanticAnalyzer.cpp
@@
     std::string addressForIndexedObject(const Symbol &symbol, const std::vector<ast::ExprPtr> &indices, SourceLocation loc) {
+        (void)loc;
         if (!symbol.type.isArrayLike()) {
             return symbol.irName;
         }
*** End Patch
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
M src/semantic/SemanticAnalyzer.cpp
```

### 6. Koopa 函数参数命名修正

原始日志时间：

```text
2026-05-10T12:15:29Z
北京时间：2026-05-10 20:15:29
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

补丁摘录：

```diff
*** Begin Patch
*** Update File: src/semantic/SemanticAnalyzer.cpp
@@
-            std::string paramName = "%p_" + sanitize(func.params[i].name) + "_" + std::to_string(symbolId_++);
+            std::string paramName = "@p_" + sanitize(func.params[i].name) + "_" + std::to_string(symbolId_++);
*** End Patch
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
M src/semantic/SemanticAnalyzer.cpp
```

修正原因：Koopa 函数形参在函数签名中使用 `@name: type` 形式，而不是局部临时值 `%name`。

### 7. 代码规模确认

原始日志时间：

```text
2026-05-10T12:17:11Z
北京时间：2026-05-10 20:17:11
type: response_item
payload.type: function_call
name: shell_command
```

执行命令：

```powershell
(Get-Content src\semantic\SemanticAnalyzer.cpp | Measure-Object -Line).Lines
```

会话中随后记录的 Codex 输出：

```text
项目文件、测试和文档已补齐。`SemanticAnalyzer.cpp` 当前 1319 行，满足 AI 辅助代码生成任务对 500 行以上代码块的要求；我最后做一次本机可做的检查，并记录无法完整构建的环境原因。
```

### 8. 语义规则修正：`%` 只允许整数

原始日志时间：

```text
2026-05-10T12:19:02Z
北京时间：2026-05-10 20:19:02
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

补丁摘录：

```diff
*** Begin Patch
*** Update File: src/semantic/SemanticAnalyzer.cpp
@@
         if (!lhs.type.isScalar() || !rhs.type.isScalar()) {
             throw CompileError(expr.loc, "binary operator requires scalar operands");
         }
+        if (expr.op == "%" && (lhs.type.base != BaseType::Int || rhs.type.base != BaseType::Int)) {
+            throw CompileError(expr.loc, "operator % requires integer operands");
+        }
 
         if (lhs.isConst && rhs.isConst) {
             return constExpr(resultTypeForConstBinary(expr.op, lhs, rhs), evalConstBinary(expr.op, lhs.constValue, rhs.constValue, expr.loc),
                              "");
         }
@@
         if (expr.op == "%") {
-            if (lhs.type.base != BaseType::Int || rhs.type.base != BaseType::Int) {
-                throw CompileError(expr.loc, "operator % requires integer operands");
-            }
             std::string temp = newTemp();
             emit(temp + " = mod " + lhs.value + ", " + rhs.value);
             return valueExpr(scalar(BaseType::Int), temp);
         }
*** End Patch
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
M src/semantic/SemanticAnalyzer.cpp
```

### 9. Docker 验证暴露问题后的真实修正

原始 Docker 构建失败记录：

```text
2026-05-10T13:49:04Z
北京时间：2026-05-10 21:49:04
type: response_item
payload.type: function_call_output
```

真实输出摘录：

```text
Exit code: 1
Wall time: 10.2 seconds
Output:
-- The CXX compiler identification is Clang 21.1.6
-- Found FLEX: /usr/bin/flex (found version "2.6.4")
-- Found BISON: /usr/bin/bison (found version "3.8.2")

/root/compiler/build/sysy_lexer.cpp:881:14: error: expected ';' after do/while statement
/root/compiler/src/frontend/sysy.l:55:14: error: expected ';' after do/while statement
/root/compiler/src/backend/RiscV.cpp:16:10: fatal error: 'koopa.h' file not found
```

这部分不直接修改 `SemanticAnalyzer.cpp`，但属于完整生成过程中的真实调试记录：修正 Flex 宏分号、CMake 中 Koopa include/library 搜索路径，以及 Bison 顶层声明/函数定义二义性。随后 Docker 构建通过：

```text
2026-05-10T13:51:09Z
北京时间：2026-05-10 21:51:09

Exit code: 0
Wall time: 10.2 seconds
Output:
-- The CXX compiler identification is Clang 21.1.6
-- Found FLEX: /usr/bin/flex (found version "2.6.4")
-- Found BISON: /usr/bin/bison (found version "3.8.2")
-- Configuring done
-- Generating done
[100%] Built target compiler
```

### 10. 多维数组初始化 bug 修正

运行测试时发现 `tests/positive/06_global_multi_array.sy` 返回值错误。定位命令先查看 `flattenConstInitializer`、`collectInitializerLeaves`、`compileLocalObject` 相关代码：

```text
2026-05-10T14:00:04Z
北京时间：2026-05-10 22:00:04

Select-String -Path src\semantic\SemanticAnalyzer.cpp -Pattern "flattenConstInitializer|collectInitializerLeaves|compileLocalObject" -Context 2,4
```

随后修正数组初始化逻辑：

```text
2026-05-10T14:00:44Z
北京时间：2026-05-10 22:00:44
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

补丁摘录：

```diff
*** Begin Patch
*** Update File: src/semantic/SemanticAnalyzer.cpp
@@
-        std::vector<const ast::Expr *> leaves;
-        collectInitializerLeaves(def.init.get(), leaves);
+        std::vector<const ast::Expr *> leaves = flattenRuntimeInitializer(def.init.get(), type, def.loc);
         int count = totalElements(type.dimensions);
-        if (static_cast<int>(leaves.size()) > count) {
-            throw CompileError(def.loc, "too many elements in array initializer");
-        }
         for (int i = 0; i < count; ++i) {
             std::string elementPtr = elementAddress(symbol.irName, type.dimensions, i, def.loc);
-            if (i < static_cast<int>(leaves.size())) {
+            if (leaves[i]) {
                 ExprResult value = castExpr(compileExpr(*leaves[i]), decl.baseType, leaves[i]->loc);
                 emit("store " + value.value + ", " + elementPtr);
             } else if (decl.isConst || def.init) {
```

验证输出：

```text
2026-05-10T14:01:27Z
北京时间：2026-05-10 22:01:27

Exit code: 0
Wall time: 13.2 seconds
Output:
[ 10%] Building CXX object CMakeFiles/compiler.dir/src/semantic/SemanticAnalyzer.cpp.o
[ 20%] Linking CXX executable compiler
[100%] Built target compiler
global @g = alloc [[i32, 3], 2], {{1, 2, 0}, {3, 4, 5}}
exit=5
```

这个修正确认了 SysY/C 风格初始化规则：`int g[2][3] = {{1, 2}, {3, 4, 5}};` 应展开为 `{{1, 2, 0}, {3, 4, 5}}`，而不是简单按叶子顺序展平。

### 11. 最终测试输出

原始日志时间：

```text
2026-05-10T14:04:22Z
北京时间：2026-05-10 22:04:22
type: response_item
payload.type: function_call_output
```

真实输出摘录：

```text
Exit code: 0
Wall time: 9.7 seconds
Output:
[ 10%] Building CXX object CMakeFiles/compiler.dir/src/semantic/SemanticAnalyzer.cpp.o
[ 20%] Linking CXX executable compiler
[100%] Built target compiler
== positive 01_main ==
exit=0
== positive 02_expr ==
exit=253
== positive 03_scope_const ==
exit=42
== positive 04_control ==
exit=24
== positive 05_function_array ==
exit=10
== positive 06_global_multi_array ==
exit=5
== positive 07_float ==
exit=3
== positive 08_putf ==
65:A
exit=0
```

负向测试同一轮运行，真实输出摘录：

```text
== negative 01_redefine ==
compile error: 3:7: duplicate symbol 'a' in the same scope
== negative 02_const_assign ==
compile error: 3:3: cannot assign to const object
== negative 03_bad_break ==
compile error: 2:3: break used outside a loop
== negative 04_call_args ==
compile error: 6:10: function 'f' called with wrong number of arguments
```

## 最终代码

最终代码已保存为：

```text
src/semantic/SemanticAnalyzer.cpp
```

代码与项目其他模块使用同一命名风格：类型使用 `TypeInfo`、符号使用 `Symbol`、表达式结果使用 `ExprResult`，错误统一抛出 `CompileError`。生成的 Koopa IR 文本由语义分析模块直接输出，RISC-V 后端再通过 libkoopa raw program 读取。
