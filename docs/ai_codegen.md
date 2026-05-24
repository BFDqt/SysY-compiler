# AI 辅助代码生成记录

## 任务选择

本实验选择任务 1：AI 辅助的代码生成。

```text
文件：src/semantic/SemanticAnalyzer.cpp
生成范围：Impl::compileExpr 至 Impl::subElementCount
当前行号：约 533-1234
代码规模：702 行
```

这段代码负责表达式翻译、左值/右值处理、函数调用、数组实参退化、常量表达式求值、数组初始化展开和局部数组初始化辅助逻辑。它是语义分析中规则密集、分支较多、且能独立作为一个连续代码块说明的部分，满足“生成代码不少于 500 行”的要求。

## 交给 AI 生成这一代码块的考量

选择这段代码交给 Codex 辅助生成，主要有三点考虑。

第一，这部分规则明确但实现细节多。表达式、数组、常量求值和初始化展开都有清楚的输入输出：输入是 AST 和符号表，输出是类型检查结果、常量值或 Koopa IR 文本。相比后端栈帧和 Flex/Bison 冲突处理，它更适合通过明确 prompt 一次生成较完整的初稿。

第二，这部分存在大量结构相似的分支，例如一元/二元表达式、整型/浮点转换、数组左值寻址、常量表达式求值等。AI 适合先搭出统一风格和基本框架，人工再集中检查语言语义和 Koopa 合法性。

第三，这段代码容易通过测试反馈验证。短路求值、函数数组参数、多维数组初始化、`%` 运算类型限制等都可以用小样例直接暴露问题，因此适合作为“AI 生成初稿 + 人工复核修正”的记录对象。

实际效果是：Codex 初次生成后给出了可维护的主体结构，包括 `ExprResult`、`LValueResult`、`compileExpr`、`compileBinary`、`compileCall`、`compileLValue`、`evalConstExpr`、`flattenConstInitializer` 等核心函数；但初稿并非直接正确，后续通过编译检查和测试暴露并修正了数组退化、Koopa 函数形参命名、`%` 类型限制、多维数组初始化展开等问题。

## 交互记录摘要

以下摘要来自本机 Codex 会话日志：

```text
C:\Users\Lenovo\.codex\sessions\2026\05\10\rollout-2026-05-10T19-41-55-019e59ca-99bb-7b71-834f-da19d49db3d6.jsonl
```

日志中的时间戳为 UTC；北京时间为 UTC+8。下文只摘录与选定 702 行代码块直接相关的 prompt、Codex 输出、工具调用和验证结果，省略前端、后端和其他文件的大段无关内容。

### 1. 用户给出总任务和 AI 代码生成要求

原始日志时间：

```text
2026-05-10T11:54:54.860Z
北京时间：2026-05-10 19:54:54
type: event_msg
payload.type: user_message
```

用户要求中与本代码块直接相关的内容摘录如下：

```text
语义分析必须实现：
- 栈式作用域符号表，记录变量/常量/函数、类型、数组维度、参数、是否全局、是否 const。
- 常量表达式编译期求值；数组维度必须是非负整型常量。
- 赋值左值不能是 const；变量先定义后使用；函数调用检查参数数量和类型。
- int/float 隐式转换；% 只允许整数；条件表达式按非 0 为真。
- 数组初始化按行优先展开，缺省补 0，超量报错。
- break/continue 只能在循环内。

AI 附加任务：
- 选择“AI 辅助代码生成”。
- 目标代码块为 src/semantic/SemanticAnalyzer.cpp 中语义分析核心，最终不少于 500 行。
- docs/ai_codegen.md 保存 prompt、交互摘要、最终生成代码说明，报告中引用该文档。
```

该 prompt 没有要求 AI 只生成某个小函数，而是要求实现完整编译器。后来整理 AI 任务材料时，从 `SemanticAnalyzer.cpp` 中选取了表达式、左值、常量求值和初始化展开这一连续核心块作为“生成代码块”。

### 2. Codex 在生成前说明语义/Koopa 层的目标

原始日志时间：

```text
2026-05-10T12:02:38.769Z
北京时间：2026-05-10 20:02:38
type: event_msg
payload.type: agent_message
```

Codex 当时的说明摘录：

```text
语义/Koopa 层会是项目里最重的一块：它同时做符号表、类型检查、常量求值、初始化展开和 IR 生成。我会把它集中成一个清晰的 SemanticAnalyzer，也正好满足后续 AI 辅助代码生成材料要引用 500 行以上语义代码的要求。
```

这条记录说明，当时的生成目标已经明确包含语义分析核心和 500 行以上代码块要求。

### 3. Codex 初次生成 `SemanticAnalyzer.cpp`

原始日志时间：

```text
2026-05-10T12:06:51.629Z
北京时间：2026-05-10 20:06:51
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

工具调用显示 Codex 使用补丁新增了 `src/semantic/SemanticAnalyzer.cpp`。补丁开头摘录：

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
```

同一次补丁中，选定代码块的表达式入口也被生成。摘录如下：

```diff
+    ExprResult compileExpr(const ast::Expr &expr) {
+        switch (expr.kind) {
+        case ast::ExprKind::IntLiteral:
+            return constExpr(scalar(BaseType::Int), ConstValue::intOf(expr.intValue), std::to_string(expr.intValue));
+        case ast::ExprKind::FloatLiteral:
+            return constExpr(scalar(BaseType::Float), ConstValue::floatOf(expr.floatValue),
+                             std::to_string(floatToBits(expr.floatValue)));
+        case ast::ExprKind::StringLiteral:
+            return constExpr(TypeInfo{BaseType::String, {}, false}, ConstValue::stringOf(expr.text), "");
+        case ast::ExprKind::LVal:
+            return compileRValueFromLValue(expr);
+        case ast::ExprKind::Unary:
+            return compileUnary(expr);
+        case ast::ExprKind::Binary:
+            return compileBinary(expr);
+        case ast::ExprKind::Call:
+            return compileCall(expr);
+        }
+        throw CompileError(expr.loc, "unknown expression");
+    }
```

工具返回：

```text
Exit code: 0
Wall time: 0 seconds
Output:
Success. Updated the following files:
A src/semantic/SemanticAnalyzer.cpp
```

这次初稿已经包含本文选定代码块的大部分结构：表达式编译、短路求值、函数调用、数组实参处理、左值寻址、类型转换、常量表达式求值、数组初始化展开等。但是后续验证证明，部分语义细节还需要修正。

### 4. 初次生成后的编译层修正

原始日志时间：

```text
2026-05-10T12:07:04Z
北京时间：2026-05-10 20:07:04
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

修正摘录：

```diff
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
```

原因是生成代码中的 `totalElements` 使用了 `std::multiplies<int>`，需要补充 `<functional>`；同时删除初稿里未使用的 `alignTo`，避免无意义工具函数残留。

### 5. 数组实参和数组右值退化修正

初稿中，数组作为函数参数时只是检查“实参是否数组”，没有把普通数组退化、数组参数再次传递、维度匹配分开处理。随后 Codex 对选定代码块中的函数调用和数组右值逻辑做了修正。

原始日志时间：

```text
2026-05-10T12:12:56Z
北京时间：2026-05-10 20:12:56
type: response_item
payload.type: custom_tool_call
name: apply_patch
```

补丁摘录：

```diff
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

随后继续修正数组表达式作为右值时的退化规则：

```text
2026-05-10T12:13:14Z
北京时间：2026-05-10 20:13:14
```

补丁摘录：

```diff
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

这一修正直接影响当前选定范围中的 `compileRValueFromLValue`、`compileCall` 和 `compileArrayArgument`。修正后，普通数组实参会通过 `getelemptr base, 0` 退化到首元素地址，数组参数则保留已加载的指针语义，并检查剩余维度是否匹配。

### 6. 静态检查暴露未使用参数

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

```diff
@@
     std::string addressForIndexedObject(const Symbol &symbol, const std::vector<ast::ExprPtr> &indices, SourceLocation loc) {
+        (void)loc;
         if (!symbol.type.isArrayLike()) {
             return symbol.irName;
         }
```

这说明 AI 生成的代码在语法层可通过，但仍需要用编译器警告清理边角问题。

### 7. Koopa 函数形参命名修正

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
@@
-            std::string paramName = "%p_" + sanitize(func.params[i].name) + "_" + std::to_string(symbolId_++);
+            std::string paramName = "@p_" + sanitize(func.params[i].name) + "_" + std::to_string(symbolId_++);
```

这个修正不在本文 702 行选定块内部，但会影响 `compileCall` 生成出的调用参数能否与函数签名中的形参合法衔接。原因是 Koopa 函数签名参数使用 `@name: type` 形式，而不是局部临时值 `%name`。

### 8. 代码规模确认

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

会话中随后记录的 Codex 输出摘录：

```text
项目文件、测试和文档已补齐。SemanticAnalyzer.cpp 当前 1319 行，满足 AI 辅助代码生成任务对 500 行以上代码块的要求。
```

后续项目继续调整后，当前工作区中的 `SemanticAnalyzer.cpp` 为 1401 行。本文最终没有用“整个文件”作为 AI 生成材料，而是选取其中 `compileExpr` 到 `subElementCount` 这一段 702 行连续代码块作为报告对象。

### 9. `%` 运算类型规则修正

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
```

这次修正确保常量折叠前也会检查 `%` 的操作数类型，避免 float 常量在 `evalConstBinary` 中被错误转换为 int 后继续通过。

### 10. 多维数组初始化展开修正

运行测试时发现 `tests/positive/06_global_multi_array.sy` 的返回值不符合预期。问题根源在于初稿把局部数组初始化器简单收集叶子节点，不能正确表达 C/SysY 风格嵌套大括号的补零和对齐规则。

定位命令记录：

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

这个修正后来沉淀为当前选定代码块中的 `flattenRuntimeInitializer`、`fillConstInitializer`、`fillRuntimeInitializer`、`subElementCount` 和 `alignInitializerPosition` 等函数。它保证：

```c
int g[2][3] = {{1, 2}, {3, 4, 5}};
```

会展开为：

```text
{{1, 2, 0}, {3, 4, 5}}
```

而不是错误地按叶子顺序展平成：

```text
{{1, 2, 3}, {4, 5, 0}}
```

### 11. 最终代表性测试输出

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

负向测试同一轮运行，输出摘录：

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

这些结果说明，AI 生成的语义核心代码块经过后续修正后，能够覆盖表达式、作用域常量、控制流、函数数组参数、多维数组初始化、float 和 `putf` 等代表性正向样例，也能拒绝重定义、const 赋值、循环外 `break`、函数实参数量不匹配等负向样例。

## 最终生成代码位置

最终嵌入项目的代码位于：

```text
src/semantic/SemanticAnalyzer.cpp
```

本文选定的 AI 辅助生成代码块为当前文件中的：

```text
Impl::compileExpr
Impl::compileRValueFromLValue
Impl::compileUnary
Impl::compileBinary
Impl::compileShortCircuitAnd
Impl::compileShortCircuitOr
Impl::compileCall
Impl::compilePutf
Impl::compileArrayArgument
Impl::compileLValue
Impl::addressForIndexedObject
Impl::typeAfterIndex
Impl::castExpr
Impl::castToBool
Impl::logicalNot
Impl::evalConstExpr
Impl::evalConstLValue
Impl::evalConstUnary
Impl::evalConstBinary
Impl::resultTypeForConstBinary
Impl::constInt
Impl::flattenConstInitializer
Impl::flattenRuntimeInitializer
Impl::fillConstInitializer
Impl::fillRuntimeInitializer
Impl::subElementCount
```

当前行号约为 533-1234，共 702 行。该代码块已经嵌入项目，并与其他模块保持相同的 C++17 风格、错误处理方式和 Koopa IR 文本生成接口。当前它不是未经修改的 AI 原始输出，而是 Codex 生成初稿后，经编译警告、Docker 构建和样例测试反馈迭代修正后的版本。
