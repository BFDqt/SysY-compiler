#include "sysy/riscv.hpp"

#include "sysy/common.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <koopa.h>

namespace sysy::backend {
namespace {

// 函数栈帧的布局信息，由 buildFrame 统一计算后供指令发射阶段查询。
// valueOffsets 记录 Koopa value 到 sp 相对偏移的映射；allocaValues 用于标记局部对象。
// outgoingArgSize 预留给本函数调用其他函数时超过 a0-a7 的实参区域。
struct FrameInfo {
    int frameSize = 0;
    int outgoingArgSize = 0;
    int raOffset = -1;
    std::map<koopa_raw_value_t, int> valueOffsets;
    std::set<koopa_raw_value_t> allocaValues;
};

// 按 RISC-V ABI 需要的边界对齐数值，主要用于栈帧大小和数组/局部对象空间。
int alignTo(int value, int align) {
    return (value + align - 1) / align * align;
}

// lw/sw/addi 的立即数为 12 位有符号数；超出范围时需要先 li 到临时寄存器。
bool fitsImm12(int value) {
    return value >= -2048 && value <= 2047;
}

// Koopa 符号名通常带有 @ 或 % 前缀，汇编标签中去掉该前缀更便于直接引用。
std::string rawName(const char *name) {
    if (!name) {
        return "";
    }
    std::string value(name);
    if (!value.empty() && (value[0] == '@' || value[0] == '%')) {
        value.erase(value.begin());
    }
    return value;
}

template <typename T>
T sliceAt(koopa_raw_slice_t slice, size_t index) {
    return reinterpret_cast<T>(slice.buffer[index]);
}

// 计算 Koopa 类型在目标机上的字节大小。
// SysY 的 int 和指针按 4 字节处理；数组大小递归等于元素大小乘以长度。
int sizeOfType(koopa_raw_type_t type) {
    switch (type->tag) {
    case KOOPA_RTT_UNIT:
        return 0;
    case KOOPA_RTT_INT32:
    case KOOPA_RTT_POINTER:
        return 4;
    case KOOPA_RTT_ARRAY:
        return static_cast<int>(type->data.array.len) * sizeOfType(type->data.array.base);
    case KOOPA_RTT_FUNCTION:
        return 4;
    }
    return 4;
}

// unit 类型的指令不产生可保存的结果，例如 void 函数调用。
bool hasReturnValue(koopa_raw_value_t value) {
    return value->ty->tag != KOOPA_RTT_UNIT;
}

// 判断一条 Koopa 指令是否需要在当前函数栈帧中分配结果槽。
// alloc 单独处理为局部对象空间；store、branch、jump、return 等不产生 SSA 结果。
bool isValueResultInstruction(koopa_raw_value_t value) {
    switch (value->kind.tag) {
    case KOOPA_RVT_LOAD:
    case KOOPA_RVT_BINARY:
    case KOOPA_RVT_GET_PTR:
    case KOOPA_RVT_GET_ELEM_PTR:
        return true;
    case KOOPA_RVT_CALL:
        return hasReturnValue(value);
    default:
        return false;
    }
}

// RawEmitter 是目标代码生成阶段的核心类：输入 Koopa raw program，输出 RISC-V 汇编。
// 前端和 IR 生成阶段已经完成语义检查、短路控制流和基本块组织；这里按 raw IR 顺序发射目标代码。
class RawEmitter {
public:
    RawEmitter(koopa_raw_program_t program, std::set<std::string> helpers, std::ostream &os)
        : program_(program), helpers_(std::move(helpers)), os_(os) {}

    // 先发射全局数据，再逐个发射有函数体的函数，最后补充按需使用的浮点辅助例程。
    void generate() {
        emitGlobals();
        for (size_t i = 0; i < program_.funcs.len; ++i) {
            koopa_raw_function_t func = sliceAt<koopa_raw_function_t>(program_.funcs, i);
            if (func->bbs.len == 0) {
                continue;
            }
            emitFunction(func);
        }
        emitFloatHelpers();
    }

private:
    koopa_raw_program_t program_;
    std::set<std::string> helpers_;
    std::ostream &os_;
    FrameInfo frame_;
    koopa_raw_function_t currentFunction_ = nullptr;
    std::string currentFunctionLabel_;
    std::string epilogueLabel_;

    void emitGlobals() {
        if (program_.values.len == 0) {
            return;
        }
        // 全局变量位于 .data 段，Koopa global_alloc 的初始化值会展开为 .word/.zero。
        os_ << "  .data\n";
        for (size_t i = 0; i < program_.values.len; ++i) {
            koopa_raw_value_t value = sliceAt<koopa_raw_value_t>(program_.values, i);
            if (value->kind.tag != KOOPA_RVT_GLOBAL_ALLOC) {
                continue;
            }
            std::string name = rawName(value->name);
            os_ << "  .globl " << name << "\n";
            os_ << name << ":\n";
            emitInitializer(value->kind.data.global_alloc.init, value->ty->data.pointer.base);
        }
        os_ << "\n";
    }

    // 递归发射全局初始化器。
    // 数组 aggregate 需要按元素顺序展开，zero_init/undef 可直接按期望类型大小清零。
    void emitInitializer(koopa_raw_value_t init, koopa_raw_type_t expectedType) {
        switch (init->kind.tag) {
        case KOOPA_RVT_ZERO_INIT:
        case KOOPA_RVT_UNDEF:
            os_ << "  .zero " << sizeOfType(expectedType) << "\n";
            return;
        case KOOPA_RVT_INTEGER:
            os_ << "  .word " << init->kind.data.integer.value << "\n";
            return;
        case KOOPA_RVT_AGGREGATE: {
            koopa_raw_type_t elementType = expectedType->data.array.base;
            for (size_t i = 0; i < init->kind.data.aggregate.elems.len; ++i) {
                emitInitializer(sliceAt<koopa_raw_value_t>(init->kind.data.aggregate.elems, i), elementType);
            }
            return;
        }
        default:
            throw std::runtime_error("unsupported global initializer in Koopa raw program");
        }
    }

    // 发射单个函数的完整汇编，包括函数序言、basic block 指令和统一尾声。
    // 统一尾声便于多个 return 跳转到同一处恢复 ra/sp，避免重复生成栈帧恢复代码。
    void emitFunction(koopa_raw_function_t function) {
        currentFunction_ = function;
        currentFunctionLabel_ = rawName(function->name);
        epilogueLabel_ = ".L_" + currentFunctionLabel_ + "_epilogue";
        frame_ = buildFrame(function);

        // 函数序言：建立栈帧，并在存在调用指令时保存返回地址。
        os_ << "  .text\n";
        os_ << "  .globl " << currentFunctionLabel_ << "\n";
        os_ << currentFunctionLabel_ << ":\n";
        emitAddi("sp", "sp", -frame_.frameSize);
        if (frame_.raOffset >= 0) {
            emitStoreWord("ra", "sp", frame_.raOffset);
        }
        spillIncomingArguments(function);

        // Koopa basic block 已经表达了 CFG；这里保持块顺序并为跳转目标生成稳定标签。
        for (size_t i = 0; i < function->bbs.len; ++i) {
            koopa_raw_basic_block_t bb = sliceAt<koopa_raw_basic_block_t>(function->bbs, i);
            os_ << asmBlockLabel(bb) << ":\n";
            for (size_t j = 0; j < bb->insts.len; ++j) {
                emitInstruction(sliceAt<koopa_raw_value_t>(bb->insts, j));
            }
        }

        // 函数尾声：恢复被本后端保存的 ra 和 sp，然后 ret 返回调用者。
        os_ << epilogueLabel_ << ":\n";
        if (frame_.raOffset >= 0) {
            emitLoadWord("ra", "sp", frame_.raOffset);
        }
        emitAddi("sp", "sp", frame_.frameSize);
        os_ << "  ret\n\n";
        currentFunction_ = nullptr;
    }

    // 预扫描函数体并确定栈帧布局。
    // 布局顺序为：调用外部函数的溢出实参区、形参备份区、alloc 局部对象、SSA 临时结果、ra。
    // 返回的 frameSize 按 16 字节对齐，满足 RISC-V ABI 对调用点栈对齐的要求。
    FrameInfo buildFrame(koopa_raw_function_t function) {
        FrameInfo frame;
        bool hasCall = false;
        int maxExtraArgs = 0;
        // 先找出本函数内调用指令的最大实参数，确定需要为被调用者预留多少栈上传参空间。
        for (size_t i = 0; i < function->bbs.len; ++i) {
            koopa_raw_basic_block_t bb = sliceAt<koopa_raw_basic_block_t>(function->bbs, i);
            for (size_t j = 0; j < bb->insts.len; ++j) {
                koopa_raw_value_t inst = sliceAt<koopa_raw_value_t>(bb->insts, j);
                if (inst->kind.tag == KOOPA_RVT_CALL) {
                    hasCall = true;
                    int argc = static_cast<int>(inst->kind.data.call.args.len);
                    maxExtraArgs = std::max(maxExtraArgs, std::max(0, argc - 8));
                }
            }
        }
        frame.outgoingArgSize = maxExtraArgs * 4;
        int offset = frame.outgoingArgSize;

        // 将所有形参都落到本函数栈帧中，后续 loadValue 可用统一方式读取参数。
        for (size_t i = 0; i < function->params.len; ++i) {
            koopa_raw_value_t param = sliceAt<koopa_raw_value_t>(function->params, i);
            frame.valueOffsets[param] = offset;
            offset += 4;
        }

        // alloc 对应 SysY 局部变量/数组，需要按实际对象大小分配；
        // 其他产生值的指令只需保存一个 32 位结果，作为简单的栈上寄存器分配策略。
        for (size_t i = 0; i < function->bbs.len; ++i) {
            koopa_raw_basic_block_t bb = sliceAt<koopa_raw_basic_block_t>(function->bbs, i);
            for (size_t j = 0; j < bb->insts.len; ++j) {
                koopa_raw_value_t inst = sliceAt<koopa_raw_value_t>(bb->insts, j);
                if (inst->kind.tag == KOOPA_RVT_ALLOC) {
                    frame.valueOffsets[inst] = offset;
                    frame.allocaValues.insert(inst);
                    offset += alignTo(sizeOfType(inst->ty->data.pointer.base), 4);
                } else if (isValueResultInstruction(inst)) {
                    frame.valueOffsets[inst] = offset;
                    offset += 4;
                }
            }
        }

        // 只有当前函数会再调用其他函数时，ra 才可能被 call 覆盖，因此按需保存。
        if (hasCall) {
            frame.raOffset = offset;
            offset += 4;
        }
        frame.frameSize = alignTo(offset, 16);
        if (frame.frameSize == 0) {
            frame.frameSize = 16;
        }
        return frame;
    }

    // 按 RISC-V 调用约定接收形参：前 8 个来自 a0-a7，其余由调用者放在栈上传参区。
    // 统一 spill 到本函数栈帧后，后续 IR 指令无需区分参数来源。
    void spillIncomingArguments(koopa_raw_function_t function) {
        for (size_t i = 0; i < function->params.len; ++i) {
            koopa_raw_value_t param = sliceAt<koopa_raw_value_t>(function->params, i);
            int offset = frame_.valueOffsets[param];
            if (i < 8) {
                emitStoreWord("a" + std::to_string(i), "sp", offset);
            } else {
                emitLoadWord("t0", "sp", frame_.frameSize + static_cast<int>(i - 8) * 4);
                emitStoreWord("t0", "sp", offset);
            }
        }
    }

    // 根据 Koopa 指令种类分派到具体的目标代码发射逻辑。
    // alloc 已经在 buildFrame 中转化为栈空间分配，不需要生成运行时指令。
    void emitInstruction(koopa_raw_value_t inst) {
        switch (inst->kind.tag) {
        case KOOPA_RVT_ALLOC:
            return;
        case KOOPA_RVT_LOAD:
            emitLoad(inst);
            return;
        case KOOPA_RVT_STORE:
            emitStore(inst);
            return;
        case KOOPA_RVT_BINARY:
            emitBinary(inst);
            return;
        case KOOPA_RVT_GET_PTR:
            emitGetPtr(inst, false);
            return;
        case KOOPA_RVT_GET_ELEM_PTR:
            emitGetPtr(inst, true);
            return;
        case KOOPA_RVT_BRANCH:
            emitBranch(inst);
            return;
        case KOOPA_RVT_JUMP:
            os_ << "  j " << asmBlockLabel(inst->kind.data.jump.target) << "\n";
            return;
        case KOOPA_RVT_CALL:
            emitCall(inst);
            return;
        case KOOPA_RVT_RETURN:
            emitReturn(inst);
            return;
        default:
            throw std::runtime_error("unsupported Koopa instruction in RISC-V backend");
        }
    }

    // load 从地址型 value 中取出 32 位内容，结果写回当前指令的栈槽。
    void emitLoad(koopa_raw_value_t inst) {
        koopa_raw_value_t src = inst->kind.data.load.src;
        loadAddress(src, "t0");
        emitLoadWord("t1", "t0", 0);
        storeResult(inst, "t1");
    }

    // store 先计算右值，再计算目的地址，最后写入内存。
    // 目的地址可能来自局部 alloca、全局变量或 get_ptr/get_elem_ptr 的结果。
    void emitStore(koopa_raw_value_t inst) {
        koopa_raw_value_t value = inst->kind.data.store.value;
        koopa_raw_value_t dest = inst->kind.data.store.dest;
        loadValue(value, "t0");
        loadAddress(dest, "t1");
        emitStoreWord("t0", "t1", 0);
    }

    // 发射整数二元运算。关系运算统一生成 0/1，供后续 branch 或表达式使用。
    void emitBinary(koopa_raw_value_t inst) {
        const auto &binary = inst->kind.data.binary;
        loadValue(binary.lhs, "t0");
        loadValue(binary.rhs, "t1");
        switch (binary.op) {
        case KOOPA_RBO_ADD:
            os_ << "  add t0, t0, t1\n";
            break;
        case KOOPA_RBO_SUB:
            os_ << "  sub t0, t0, t1\n";
            break;
        case KOOPA_RBO_MUL:
            os_ << "  mul t0, t0, t1\n";
            break;
        case KOOPA_RBO_DIV:
            os_ << "  div t0, t0, t1\n";
            break;
        case KOOPA_RBO_MOD:
            os_ << "  rem t0, t0, t1\n";
            break;
        case KOOPA_RBO_EQ:
            os_ << "  xor t0, t0, t1\n";
            os_ << "  seqz t0, t0\n";
            break;
        case KOOPA_RBO_NOT_EQ:
            os_ << "  xor t0, t0, t1\n";
            os_ << "  snez t0, t0\n";
            break;
        case KOOPA_RBO_LT:
            os_ << "  slt t0, t0, t1\n";
            break;
        case KOOPA_RBO_GT:
            os_ << "  slt t0, t1, t0\n";
            break;
        case KOOPA_RBO_LE:
            os_ << "  slt t0, t1, t0\n";
            os_ << "  xori t0, t0, 1\n";
            break;
        case KOOPA_RBO_GE:
            os_ << "  slt t0, t0, t1\n";
            os_ << "  xori t0, t0, 1\n";
            break;
        case KOOPA_RBO_AND:
            os_ << "  and t0, t0, t1\n";
            break;
        case KOOPA_RBO_OR:
            os_ << "  or t0, t0, t1\n";
            break;
        case KOOPA_RBO_XOR:
            os_ << "  xor t0, t0, t1\n";
            break;
        case KOOPA_RBO_SHL:
            os_ << "  sll t0, t0, t1\n";
            break;
        case KOOPA_RBO_SHR:
            os_ << "  srl t0, t0, t1\n";
            break;
        case KOOPA_RBO_SAR:
            os_ << "  sra t0, t0, t1\n";
            break;
        }
        storeResult(inst, "t0");
    }

    // 计算数组/指针寻址结果。
    // get_elem_ptr 面向数组元素，索引步长是数组元素类型大小；
    // get_ptr 面向指针算术，索引步长是指针指向对象的大小。
    void emitGetPtr(koopa_raw_value_t inst, bool elemPtr) {
        koopa_raw_value_t src = elemPtr ? inst->kind.data.get_elem_ptr.src : inst->kind.data.get_ptr.src;
        koopa_raw_value_t index = elemPtr ? inst->kind.data.get_elem_ptr.index : inst->kind.data.get_ptr.index;
        loadAddress(src, "t0");
        loadValue(index, "t1");
        int elementSize = 4;
        koopa_raw_type_t base = src->ty->data.pointer.base;
        if (elemPtr && base->tag == KOOPA_RTT_ARRAY) {
            elementSize = sizeOfType(base->data.array.base);
        } else {
            elementSize = sizeOfType(base);
        }
        os_ << "  li t2, " << elementSize << "\n";
        os_ << "  mul t1, t1, t2\n";
        os_ << "  add t0, t0, t1\n";
        storeResult(inst, "t0");
    }

    // Koopa 的短路求值和条件表达式在 IR 阶段已经拆成 basic block。
    // 后端只需根据 cond 的 0/非 0 结果跳转到 true_bb 或 false_bb。
    void emitBranch(koopa_raw_value_t inst) {
        const auto &branch = inst->kind.data.branch;
        loadValue(branch.cond, "t0");
        os_ << "  bnez t0, " << asmBlockLabel(branch.true_bb) << "\n";
        os_ << "  j " << asmBlockLabel(branch.false_bb) << "\n";
    }

    // 发射函数调用并遵守 RISC-V 调用约定。
    // 前 8 个实参放入 a0-a7，更多实参写入当前栈帧底部预留的 outgoingArgSize 区域。
    // 若被调用函数有返回值，则从 a0 保存到该 call 指令对应的 SSA 结果槽。
    void emitCall(koopa_raw_value_t inst) {
        const auto &call = inst->kind.data.call;
        for (size_t i = 0; i < call.args.len; ++i) {
            koopa_raw_value_t arg = sliceAt<koopa_raw_value_t>(call.args, i);
            loadValue(arg, "t0");
            if (i < 8) {
                os_ << "  mv a" << i << ", t0\n";
            } else {
                emitStoreWord("t0", "sp", static_cast<int>(i - 8) * 4);
            }
        }
        os_ << "  call " << rawName(call.callee->name) << "\n";
        if (hasReturnValue(inst)) {
            storeResult(inst, "a0");
        }
    }

    // return 将返回值放入 a0，然后跳到统一尾声恢复栈帧。
    void emitReturn(koopa_raw_value_t inst) {
        koopa_raw_value_t value = inst->kind.data.ret.value;
        if (value) {
            loadValue(value, "a0");
        }
        os_ << "  j " << epilogueLabel_ << "\n";
    }

    // 将 Koopa value 的“值”加载到寄存器。
    // 整数常量直接 li；alloc/global_alloc 作为表达式值时表示地址；普通 SSA 值从栈槽读取。
    void loadValue(koopa_raw_value_t value, const std::string &reg) {
        switch (value->kind.tag) {
        case KOOPA_RVT_INTEGER:
            os_ << "  li " << reg << ", " << value->kind.data.integer.value << "\n";
            return;
        case KOOPA_RVT_GLOBAL_ALLOC:
        case KOOPA_RVT_ALLOC:
            loadAddress(value, reg);
            return;
        default:
            break;
        }
        auto it = frame_.valueOffsets.find(value);
        if (it == frame_.valueOffsets.end()) {
            throw std::runtime_error("value has no stack slot");
        }
        emitLoadWord(reg, "sp", it->second);
    }

    // 将 Koopa value 对应的“地址”加载到寄存器。
    // 全局变量使用 la，局部 alloca 使用 sp 偏移，指针计算结果本身已经是地址值。
    void loadAddress(koopa_raw_value_t value, const std::string &reg) {
        if (value->kind.tag == KOOPA_RVT_GLOBAL_ALLOC) {
            os_ << "  la " << reg << ", " << rawName(value->name) << "\n";
            return;
        }
        if (value->kind.tag == KOOPA_RVT_ALLOC) {
            emitAddi(reg, "sp", frame_.valueOffsets[value]);
            return;
        }
        loadValue(value, reg);
    }

    // 保存一条产生 SSA 结果的指令值。所有结果槽都在 buildFrame 中预先分配。
    void storeResult(koopa_raw_value_t value, const std::string &reg) {
        auto it = frame_.valueOffsets.find(value);
        if (it == frame_.valueOffsets.end()) {
            throw std::runtime_error("result has no stack slot");
        }
        emitStoreWord(reg, "sp", it->second);
    }

    // 封装 lw 的偏移范围处理，避免大栈帧或大数组偏移超出 12 位立即数限制。
    void emitLoadWord(const std::string &reg, const std::string &base, int offset) {
        if (fitsImm12(offset)) {
            os_ << "  lw " << reg << ", " << offset << "(" << base << ")\n";
            return;
        }
        os_ << "  li t6, " << offset << "\n";
        os_ << "  add t6, " << base << ", t6\n";
        os_ << "  lw " << reg << ", 0(t6)\n";
    }

    // 封装 sw 的偏移范围处理，与 emitLoadWord 保持一致的大立即数访问策略。
    void emitStoreWord(const std::string &reg, const std::string &base, int offset) {
        if (fitsImm12(offset)) {
            os_ << "  sw " << reg << ", " << offset << "(" << base << ")\n";
            return;
        }
        os_ << "  li t6, " << offset << "\n";
        os_ << "  add t6, " << base << ", t6\n";
        os_ << "  sw " << reg << ", 0(t6)\n";
    }

    // addi 也受 12 位立即数限制；超范围时退化为 li + add。
    void emitAddi(const std::string &rd, const std::string &rs, int imm) {
        if (fitsImm12(imm)) {
            os_ << "  addi " << rd << ", " << rs << ", " << imm << "\n";
            return;
        }
        os_ << "  li t6, " << imm << "\n";
        os_ << "  add " << rd << ", " << rs << ", t6\n";
    }

    // basic block 标签附带函数名前缀，避免不同函数中的同名 Koopa 块发生汇编标签冲突。
    std::string asmBlockLabel(koopa_raw_basic_block_t bb) const {
        return ".L_" + currentFunctionLabel_ + "_" + rawName(bb->name);
    }

    // 仅在 IR 生成阶段记录了浮点 helper 使用需求时发射对应例程。
    // 这里通过整数寄存器传递浮点位模式，在 helper 内部使用浮点指令完成转换/运算。
    void emitFloatHelpers() {
        if (helpers_.empty()) {
            return;
        }
        os_ << "  .text\n";
        if (helpers_.count("__sysy_i2f")) {
            os_ << "__sysy_i2f:\n";
            os_ << "  fcvt.s.w ft0, a0\n";
            os_ << "  fmv.x.w a0, ft0\n";
            os_ << "  ret\n";
        }
        if (helpers_.count("__sysy_f2i")) {
            os_ << "__sysy_f2i:\n";
            os_ << "  fmv.w.x ft0, a0\n";
            os_ << "  fcvt.w.s a0, ft0, rtz\n";
            os_ << "  ret\n";
        }
        emitBinaryFloatHelper("__sysy_fadd", "fadd.s");
        emitBinaryFloatHelper("__sysy_fsub", "fsub.s");
        emitBinaryFloatHelper("__sysy_fmul", "fmul.s");
        emitBinaryFloatHelper("__sysy_fdiv", "fdiv.s");
        emitCompareFloatHelper("__sysy_feq", "feq.s");
        emitCompareFloatHelper("__sysy_flt", "flt.s");
        emitCompareFloatHelper("__sysy_fle", "fle.s");
    }

    // 发射二元浮点运算辅助函数：a0/a1 中保存 float 的位模式，返回值同样放回 a0。
    void emitBinaryFloatHelper(const std::string &name, const std::string &op) {
        if (!helpers_.count(name)) {
            return;
        }
        os_ << name << ":\n";
        os_ << "  fmv.w.x ft0, a0\n";
        os_ << "  fmv.w.x ft1, a1\n";
        os_ << "  " << op << " ft0, ft0, ft1\n";
        os_ << "  fmv.x.w a0, ft0\n";
        os_ << "  ret\n";
    }

    // 发射浮点比较辅助函数，比较结果为整数 0/1，便于与普通条件跳转衔接。
    void emitCompareFloatHelper(const std::string &name, const std::string &op) {
        if (!helpers_.count(name)) {
            return;
        }
        os_ << name << ":\n";
        os_ << "  fmv.w.x ft0, a0\n";
        os_ << "  fmv.w.x ft1, a1\n";
        os_ << "  " << op << " a0, ft0, ft1\n";
        os_ << "  ret\n";
    }
};

} // namespace

// 后端入口：将前一阶段生成的 Koopa 文本解析为 raw program，再交给 RawEmitter 输出 RISC-V。
// koopaText 是 IR 文本，floatHelpers 表示需要补发的浮点运行时辅助函数集合，os 为汇编输出流。
void RiscVGenerator::generate(const std::string &koopaText, const std::set<std::string> &floatHelpers, std::ostream &os) {
    koopa_program_t program = nullptr;
    koopa_error_code_t parseStatus = koopa_parse_from_string(koopaText.c_str(), &program);
    if (parseStatus != KOOPA_EC_SUCCESS) {
        throw CompileError({1, 1}, "failed to parse generated Koopa IR");
    }

    koopa_raw_program_builder_t builder = koopa_new_raw_program_builder();
    koopa_raw_program_t raw = koopa_build_raw_program(builder, program);
    RawEmitter emitter(raw, floatHelpers, os);
    emitter.generate();
    koopa_delete_raw_program_builder(builder);
    koopa_delete_program(program);
}

} // namespace sysy::backend
