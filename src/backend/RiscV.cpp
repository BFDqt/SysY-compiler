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

struct FrameInfo {
    int frameSize = 0;
    int outgoingArgSize = 0;
    int raOffset = -1;
    std::map<koopa_raw_value_t, int> valueOffsets;
    std::set<koopa_raw_value_t> allocaValues;
};

int alignTo(int value, int align) {
    return (value + align - 1) / align * align;
}

bool fitsImm12(int value) {
    return value >= -2048 && value <= 2047;
}

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

bool hasReturnValue(koopa_raw_value_t value) {
    return value->ty->tag != KOOPA_RTT_UNIT;
}

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

class RawEmitter {
public:
    RawEmitter(koopa_raw_program_t program, std::set<std::string> helpers, std::ostream &os)
        : program_(program), helpers_(std::move(helpers)), os_(os) {}

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

    void emitFunction(koopa_raw_function_t function) {
        currentFunction_ = function;
        currentFunctionLabel_ = rawName(function->name);
        epilogueLabel_ = ".L_" + currentFunctionLabel_ + "_epilogue";
        frame_ = buildFrame(function);

        os_ << "  .text\n";
        os_ << "  .globl " << currentFunctionLabel_ << "\n";
        os_ << currentFunctionLabel_ << ":\n";
        emitAddi("sp", "sp", -frame_.frameSize);
        if (frame_.raOffset >= 0) {
            emitStoreWord("ra", "sp", frame_.raOffset);
        }
        spillIncomingArguments(function);

        for (size_t i = 0; i < function->bbs.len; ++i) {
            koopa_raw_basic_block_t bb = sliceAt<koopa_raw_basic_block_t>(function->bbs, i);
            os_ << asmBlockLabel(bb) << ":\n";
            for (size_t j = 0; j < bb->insts.len; ++j) {
                emitInstruction(sliceAt<koopa_raw_value_t>(bb->insts, j));
            }
        }

        os_ << epilogueLabel_ << ":\n";
        if (frame_.raOffset >= 0) {
            emitLoadWord("ra", "sp", frame_.raOffset);
        }
        emitAddi("sp", "sp", frame_.frameSize);
        os_ << "  ret\n\n";
        currentFunction_ = nullptr;
    }

    FrameInfo buildFrame(koopa_raw_function_t function) {
        FrameInfo frame;
        bool hasCall = false;
        int maxExtraArgs = 0;
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

        for (size_t i = 0; i < function->params.len; ++i) {
            koopa_raw_value_t param = sliceAt<koopa_raw_value_t>(function->params, i);
            frame.valueOffsets[param] = offset;
            offset += 4;
        }

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

    void emitLoad(koopa_raw_value_t inst) {
        koopa_raw_value_t src = inst->kind.data.load.src;
        loadAddress(src, "t0");
        emitLoadWord("t1", "t0", 0);
        storeResult(inst, "t1");
    }

    void emitStore(koopa_raw_value_t inst) {
        koopa_raw_value_t value = inst->kind.data.store.value;
        koopa_raw_value_t dest = inst->kind.data.store.dest;
        loadValue(value, "t0");
        loadAddress(dest, "t1");
        emitStoreWord("t0", "t1", 0);
    }

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

    void emitBranch(koopa_raw_value_t inst) {
        const auto &branch = inst->kind.data.branch;
        loadValue(branch.cond, "t0");
        os_ << "  bnez t0, " << asmBlockLabel(branch.true_bb) << "\n";
        os_ << "  j " << asmBlockLabel(branch.false_bb) << "\n";
    }

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

    void emitReturn(koopa_raw_value_t inst) {
        koopa_raw_value_t value = inst->kind.data.ret.value;
        if (value) {
            loadValue(value, "a0");
        }
        os_ << "  j " << epilogueLabel_ << "\n";
    }

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

    void storeResult(koopa_raw_value_t value, const std::string &reg) {
        auto it = frame_.valueOffsets.find(value);
        if (it == frame_.valueOffsets.end()) {
            throw std::runtime_error("result has no stack slot");
        }
        emitStoreWord(reg, "sp", it->second);
    }

    void emitLoadWord(const std::string &reg, const std::string &base, int offset) {
        if (fitsImm12(offset)) {
            os_ << "  lw " << reg << ", " << offset << "(" << base << ")\n";
            return;
        }
        os_ << "  li t6, " << offset << "\n";
        os_ << "  add t6, " << base << ", t6\n";
        os_ << "  lw " << reg << ", 0(t6)\n";
    }

    void emitStoreWord(const std::string &reg, const std::string &base, int offset) {
        if (fitsImm12(offset)) {
            os_ << "  sw " << reg << ", " << offset << "(" << base << ")\n";
            return;
        }
        os_ << "  li t6, " << offset << "\n";
        os_ << "  add t6, " << base << ", t6\n";
        os_ << "  sw " << reg << ", 0(t6)\n";
    }

    void emitAddi(const std::string &rd, const std::string &rs, int imm) {
        if (fitsImm12(imm)) {
            os_ << "  addi " << rd << ", " << rs << ", " << imm << "\n";
            return;
        }
        os_ << "  li t6, " << imm << "\n";
        os_ << "  add " << rd << ", " << rs << ", t6\n";
    }

    std::string asmBlockLabel(koopa_raw_basic_block_t bb) const {
        return ".L_" + currentFunctionLabel_ + "_" + rawName(bb->name);
    }

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
