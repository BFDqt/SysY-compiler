#include "sysy/semantic.hpp"

#include "sysy/common.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <numeric>
#include <optional>
#include <set>
#include <sstream>
#include <utility>

namespace sysy {
namespace {

struct TypeInfo {
    BaseType base = BaseType::Int;
    std::vector<int> dimensions;
    bool isArrayParam = false;

    bool isVoid() const { return base == BaseType::Void; }
    bool isString() const { return base == BaseType::String; }
    bool isScalar() const { return !isVoid() && !isString() && dimensions.empty() && !isArrayParam; }
    bool isArrayLike() const { return !dimensions.empty() || isArrayParam; }
    int rank() const { return static_cast<int>(dimensions.size()) + (isArrayParam ? 1 : 0); }
};

struct FunctionInfo {
    BaseType returnType = BaseType::Int;
    std::vector<TypeInfo> params;
    bool defined = false;
    bool external = false;
};

struct Symbol {
    std::string name;
    TypeInfo type;
    bool isConst = false;
    bool isGlobal = false;
    std::string irName;
    bool hasConstScalar = false;
    ConstValue constScalar;
    std::vector<ConstValue> constArrayValues;
};

struct Scope {
    std::map<std::string, Symbol> objects;
};

struct ExprResult {
    TypeInfo type;
    std::string value;
    bool isConst = false;
    ConstValue constValue;
};

struct LValueResult {
    TypeInfo type;
    std::string address;
    bool isConstObject = false;
    bool isConstValue = false;
    ConstValue constValue;
};

std::string sanitize(const std::string &name) {
    std::string result;
    for (char c : name) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_') {
            result.push_back(c);
        } else {
            result.push_back('_');
        }
    }
    if (result.empty() || (result[0] >= '0' && result[0] <= '9')) {
        result.insert(result.begin(), '_');
    }
    return result;
}

class Impl {
public:
    KoopaResult compile(const ast::Program &program) {
        installRuntimeFunctions();
        collectFunctionSignatures(program);
        pushScope();
        for (const auto &item : program.items) {
            if (item.kind == ast::TopLevelKind::Decl) {
                compileGlobalDecl(*item.decl);
            }
        }
        for (const auto &item : program.items) {
            if (item.kind == ast::TopLevelKind::FuncDef) {
                compileFunction(*item.func);
            }
        }
        if (mainCount_ != 1) {
            throw CompileError({1, 1}, "SysY program must contain exactly one int main() function");
        }
        std::ostringstream out;
        emitRuntimeDecls(out);
        out << globals_.str();
        out << functions_.str();
        result_.text = out.str();
        return result_;
    }

private:
    KoopaResult result_;
    std::ostringstream globals_;
    std::ostringstream functions_;
    std::ostringstream *currentOut_ = nullptr;
    std::vector<Scope> scopes_;
    std::map<std::string, FunctionInfo> functionTable_;
    std::set<std::string> topLevelNames_;
    BaseType currentReturnType_ = BaseType::Void;
    std::string currentFunctionName_;
    int tempId_ = 0;
    int labelId_ = 0;
    int symbolId_ = 0;
    bool terminated_ = false;
    std::vector<std::pair<std::string, std::string>> loopStack_;
    int mainCount_ = 0;

    void installRuntimeFunctions() {
        addRuntime("getint", BaseType::Int, {});
        addRuntime("getch", BaseType::Int, {});
        addRuntime("getarray", BaseType::Int, {arrayParam(BaseType::Int, {})});
        addRuntime("putint", BaseType::Void, {scalar(BaseType::Int)});
        addRuntime("putch", BaseType::Void, {scalar(BaseType::Int)});
        addRuntime("putarray", BaseType::Void, {scalar(BaseType::Int), arrayParam(BaseType::Int, {})});
        addRuntime("starttime", BaseType::Void, {});
        addRuntime("stoptime", BaseType::Void, {});
        addRuntime("getfloat", BaseType::Float, {});
        addRuntime("putfloat", BaseType::Void, {scalar(BaseType::Float)});
        addRuntime("getfarray", BaseType::Int, {arrayParam(BaseType::Float, {})});
        addRuntime("putfarray", BaseType::Void, {scalar(BaseType::Int), arrayParam(BaseType::Float, {})});
        addRuntime("putf", BaseType::Void, {TypeInfo{BaseType::String, {}, false}});
        addRuntime("__sysy_i2f", BaseType::Float, {scalar(BaseType::Int)});
        addRuntime("__sysy_f2i", BaseType::Int, {scalar(BaseType::Float)});
        addRuntime("__sysy_fadd", BaseType::Float, {scalar(BaseType::Float), scalar(BaseType::Float)});
        addRuntime("__sysy_fsub", BaseType::Float, {scalar(BaseType::Float), scalar(BaseType::Float)});
        addRuntime("__sysy_fmul", BaseType::Float, {scalar(BaseType::Float), scalar(BaseType::Float)});
        addRuntime("__sysy_fdiv", BaseType::Float, {scalar(BaseType::Float), scalar(BaseType::Float)});
        addRuntime("__sysy_feq", BaseType::Int, {scalar(BaseType::Float), scalar(BaseType::Float)});
        addRuntime("__sysy_flt", BaseType::Int, {scalar(BaseType::Float), scalar(BaseType::Float)});
        addRuntime("__sysy_fle", BaseType::Int, {scalar(BaseType::Float), scalar(BaseType::Float)});
    }

    void addRuntime(std::string name, BaseType ret, std::vector<TypeInfo> params) {
        FunctionInfo fn;
        fn.returnType = ret;
        fn.params = std::move(params);
        fn.defined = false;
        fn.external = true;
        functionTable_.emplace(std::move(name), std::move(fn));
    }

    void collectFunctionSignatures(const ast::Program &program) {
        for (const auto &item : program.items) {
            if (item.kind == ast::TopLevelKind::Decl) {
                for (const auto &def : item.decl->definitions) {
                    if (!topLevelNames_.insert(def.name).second) {
                        throw CompileError(def.loc, "duplicate top-level symbol '" + def.name + "'");
                    }
                    if (functionTable_.count(def.name) != 0) {
                        throw CompileError(def.loc, "top-level variable conflicts with runtime function '" + def.name + "'");
                    }
                }
                continue;
            }

            const auto &func = *item.func;
            if (!topLevelNames_.insert(func.name).second) {
                throw CompileError(func.loc, "duplicate top-level symbol '" + func.name + "'");
            }
            if (functionTable_.count(func.name) != 0) {
                throw CompileError(func.loc, "function conflicts with runtime function '" + func.name + "'");
            }
            FunctionInfo info;
            info.returnType = func.returnType;
            info.defined = true;
            for (const auto &param : func.params) {
                TypeInfo type;
                type.base = param.baseType;
                type.isArrayParam = param.isArray;
                if (param.isArray) {
                    for (const auto &dimExpr : param.dimensionsAfterFirst) {
                        int dim = constInt(*dimExpr, dimExpr->loc);
                        if (dim <= 0) {
                            throw CompileError(dimExpr->loc, "array parameter dimension must be positive");
                        }
                        type.dimensions.push_back(dim);
                    }
                }
                info.params.push_back(std::move(type));
            }
            if (func.name == "main") {
                ++mainCount_;
                if (func.returnType != BaseType::Int || !func.params.empty()) {
                    throw CompileError(func.loc, "main must have type int main()");
                }
            }
            functionTable_[func.name] = std::move(info);
        }
    }

    void compileGlobalDecl(const ast::Decl &decl) {
        compileDecl(decl, true);
    }

    void compileFunction(const ast::FuncDef &func) {
        currentFunctionName_ = func.name;
        currentReturnType_ = func.returnType;
        tempId_ = 0;
        labelId_ = 0;
        terminated_ = false;
        std::ostringstream body;
        currentOut_ = &body;

        body << "fun @" << func.name << "(";
        std::vector<std::string> paramNames;
        for (size_t i = 0; i < func.params.size(); ++i) {
            if (i != 0) {
                body << ", ";
            }
            std::string paramName = "@p_" + sanitize(func.params[i].name) + "_" + std::to_string(symbolId_++);
            paramNames.push_back(paramName);
            TypeInfo paramType;
            paramType.base = func.params[i].baseType;
            paramType.isArrayParam = func.params[i].isArray;
            for (const auto &dimExpr : func.params[i].dimensionsAfterFirst) {
                paramType.dimensions.push_back(constInt(*dimExpr, dimExpr->loc));
            }
            body << paramName << ": " << koopaParamType(paramType);
        }
        body << ")";
        if (func.returnType != BaseType::Void) {
            body << ": i32";
        }
        body << " {\n";
        emitLabel("%entry");

        pushScope();
        for (size_t i = 0; i < func.params.size(); ++i) {
            TypeInfo paramType;
            paramType.base = func.params[i].baseType;
            paramType.isArrayParam = func.params[i].isArray;
            for (const auto &dimExpr : func.params[i].dimensionsAfterFirst) {
                paramType.dimensions.push_back(constInt(*dimExpr, dimExpr->loc));
            }
            std::string local = newLocalName(func.params[i].name);
            emit(local + " = alloc " + koopaParamType(paramType));
            emit("store " + paramNames[i] + ", " + local);
            Symbol symbol;
            symbol.name = func.params[i].name;
            symbol.type = paramType;
            symbol.irName = local;
            insertObject(std::move(symbol), func.params[i].loc);
        }

        compileBlock(*func.body, false);
        if (!terminated_) {
            if (currentReturnType_ == BaseType::Void) {
                emit("ret");
            } else if (currentReturnType_ == BaseType::Float) {
                emit("ret " + std::to_string(floatToBits(0.0f)));
            } else {
                emit("ret 0");
            }
        }
        body << "}\n\n";
        popScope();
        functions_ << body.str();
        currentOut_ = nullptr;
    }

    void compileBlock(const ast::Block &block, bool createScope) {
        if (createScope) {
            pushScope();
        }
        for (const auto &item : block.items) {
            if (terminated_) {
                continue;
            }
            if (item.decl) {
                compileDecl(*item.decl, false);
            } else if (item.stmt) {
                compileStmt(*item.stmt);
            }
        }
        if (createScope) {
            popScope();
        }
    }

    void compileDecl(const ast::Decl &decl, bool global) {
        for (const auto &def : decl.definitions) {
            TypeInfo type;
            type.base = decl.baseType;
            for (const auto &dimExpr : def.dimensions) {
                int dim = constInt(*dimExpr, dimExpr->loc);
                if (dim <= 0) {
                    throw CompileError(dimExpr->loc, "array dimension must be positive");
                }
                type.dimensions.push_back(dim);
            }

            Symbol symbol;
            symbol.name = def.name;
            symbol.type = type;
            symbol.isConst = decl.isConst;
            symbol.isGlobal = global;

            if (global) {
                compileGlobalObject(def, decl, type, symbol);
            } else {
                compileLocalObject(def, decl, type, symbol);
            }
            insertObject(std::move(symbol), def.loc);
        }
    }

    void compileGlobalObject(const ast::VarDef &def, const ast::Decl &decl, const TypeInfo &type, Symbol &symbol) {
        if (decl.isConst && type.isScalar()) {
            if (!def.init || def.init->isList) {
                throw CompileError(def.loc, "scalar const requires a scalar initializer");
            }
            symbol.hasConstScalar = true;
            symbol.constScalar = castConst(evalConstExpr(*def.init->expr), decl.baseType);
            return;
        }

        symbol.irName = "@" + def.name;
        std::vector<ConstValue> values;
        if (type.isScalar()) {
            ConstValue value = decl.baseType == BaseType::Float ? ConstValue::floatOf(0.0f) : ConstValue::intOf(0);
            if (def.init) {
                if (def.init->isList) {
                    throw CompileError(def.init->loc, "scalar initializer cannot be a list");
                }
                value = castConst(evalConstExpr(*def.init->expr), decl.baseType);
            }
            values.push_back(value);
        } else {
            values = flattenConstInitializer(def.init.get(), type, def.loc);
            if (decl.isConst) {
                symbol.constArrayValues = values;
            }
        }

        globals_ << "global " << symbol.irName << " = alloc " << koopaObjectType(type) << ", ";
        if (!def.init && !decl.isConst) {
            globals_ << "zeroinit\n";
        } else if (type.isScalar()) {
            globals_ << koopaConst(values.front()) << "\n";
        } else {
            globals_ << aggregateInitializer(values, type.dimensions, 0, 0) << "\n";
        }
    }

    void compileLocalObject(const ast::VarDef &def, const ast::Decl &decl, const TypeInfo &type, Symbol &symbol) {
        if (decl.isConst && type.isScalar()) {
            if (!def.init || def.init->isList) {
                throw CompileError(def.loc, "scalar const requires a scalar initializer");
            }
            symbol.hasConstScalar = true;
            symbol.constScalar = castConst(evalConstExpr(*def.init->expr), decl.baseType);
            return;
        }

        symbol.irName = newLocalName(def.name);
        emit(symbol.irName + " = alloc " + koopaObjectType(type));
        if (type.isScalar()) {
            if (def.init) {
                if (def.init->isList) {
                    throw CompileError(def.init->loc, "scalar initializer cannot be a list");
                }
                ExprResult init = castExpr(compileExpr(*def.init->expr), decl.baseType, def.init->loc);
                emit("store " + init.value + ", " + symbol.irName);
                if (decl.isConst && init.isConst) {
                    symbol.hasConstScalar = true;
                    symbol.constScalar = init.constValue;
                }
            }
            return;
        }

        std::vector<const ast::Expr *> leaves = flattenRuntimeInitializer(def.init.get(), type, def.loc);
        int count = totalElements(type.dimensions);
        for (int i = 0; i < count; ++i) {
            std::string elementPtr = elementAddress(symbol.irName, type.dimensions, i, def.loc);
            if (leaves[i]) {
                ExprResult value = castExpr(compileExpr(*leaves[i]), decl.baseType, leaves[i]->loc);
                emit("store " + value.value + ", " + elementPtr);
            } else if (decl.isConst || def.init) {
                emit("store " + zeroFor(decl.baseType) + ", " + elementPtr);
            }
        }
        if (decl.isConst) {
            symbol.constArrayValues = flattenConstInitializer(def.init.get(), type, def.loc);
        }
    }

    void compileStmt(const ast::Stmt &stmt) {
        switch (stmt.kind) {
        case ast::StmtKind::Empty:
            return;
        case ast::StmtKind::Assign:
            compileAssignment(stmt);
            return;
        case ast::StmtKind::Expr:
            (void)compileExpr(*stmt.expr);
            return;
        case ast::StmtKind::Block:
            compileBlock(*stmt.block, true);
            return;
        case ast::StmtKind::If:
            compileIf(stmt);
            return;
        case ast::StmtKind::While:
            compileWhile(stmt);
            return;
        case ast::StmtKind::Break:
            compileBreak(stmt);
            return;
        case ast::StmtKind::Continue:
            compileContinue(stmt);
            return;
        case ast::StmtKind::Return:
            compileReturn(stmt);
            return;
        }
    }

    void compileAssignment(const ast::Stmt &stmt) {
        LValueResult lhs = compileLValue(*stmt.lval, true);
        if (lhs.isConstObject) {
            throw CompileError(stmt.loc, "cannot assign to const object");
        }
        if (!lhs.type.isScalar()) {
            throw CompileError(stmt.loc, "assignment target must be a scalar object");
        }
        ExprResult rhs = castExpr(compileExpr(*stmt.expr), lhs.type.base, stmt.expr->loc);
        emit("store " + rhs.value + ", " + lhs.address);
    }

    void compileIf(const ast::Stmt &stmt) {
        std::string thenLabel = newLabel("if_then");
        std::string elseLabel = stmt.elseStmt ? newLabel("if_else") : "";
        std::string endLabel = newLabel("if_end");
        ExprResult cond = castToBool(compileExpr(*stmt.cond), stmt.cond->loc);
        emit("br " + cond.value + ", " + thenLabel + ", " + (stmt.elseStmt ? elseLabel : endLabel));
        terminated_ = true;

        emitLabel(thenLabel);
        compileStmt(*stmt.thenStmt);
        if (!terminated_) {
            emit("jump " + endLabel);
            terminated_ = true;
        }

        if (stmt.elseStmt) {
            emitLabel(elseLabel);
            compileStmt(*stmt.elseStmt);
            if (!terminated_) {
                emit("jump " + endLabel);
                terminated_ = true;
            }
        }

        emitLabel(endLabel);
    }

    void compileWhile(const ast::Stmt &stmt) {
        std::string condLabel = newLabel("while_cond");
        std::string bodyLabel = newLabel("while_body");
        std::string endLabel = newLabel("while_end");
        emit("jump " + condLabel);
        terminated_ = true;

        emitLabel(condLabel);
        ExprResult cond = castToBool(compileExpr(*stmt.cond), stmt.cond->loc);
        emit("br " + cond.value + ", " + bodyLabel + ", " + endLabel);
        terminated_ = true;

        loopStack_.push_back({condLabel, endLabel});
        emitLabel(bodyLabel);
        compileStmt(*stmt.body);
        if (!terminated_) {
            emit("jump " + condLabel);
            terminated_ = true;
        }
        loopStack_.pop_back();

        emitLabel(endLabel);
    }

    void compileBreak(const ast::Stmt &stmt) {
        if (loopStack_.empty()) {
            throw CompileError(stmt.loc, "break used outside a loop");
        }
        emit("jump " + loopStack_.back().second);
        terminated_ = true;
    }

    void compileContinue(const ast::Stmt &stmt) {
        if (loopStack_.empty()) {
            throw CompileError(stmt.loc, "continue used outside a loop");
        }
        emit("jump " + loopStack_.back().first);
        terminated_ = true;
    }

    void compileReturn(const ast::Stmt &stmt) {
        if (currentReturnType_ == BaseType::Void) {
            if (stmt.expr) {
                throw CompileError(stmt.loc, "void function cannot return a value");
            }
            emit("ret");
            terminated_ = true;
            return;
        }
        if (!stmt.expr) {
            throw CompileError(stmt.loc, "non-void function must return a value");
        }
        ExprResult value = castExpr(compileExpr(*stmt.expr), currentReturnType_, stmt.expr->loc);
        emit("ret " + value.value);
        terminated_ = true;
    }

    ExprResult compileExpr(const ast::Expr &expr) {
        switch (expr.kind) {
        case ast::ExprKind::IntLiteral:
            return constExpr(scalar(BaseType::Int), ConstValue::intOf(expr.intValue), std::to_string(expr.intValue));
        case ast::ExprKind::FloatLiteral:
            return constExpr(scalar(BaseType::Float), ConstValue::floatOf(expr.floatValue),
                             std::to_string(floatToBits(expr.floatValue)));
        case ast::ExprKind::StringLiteral:
            return constExpr(TypeInfo{BaseType::String, {}, false}, ConstValue::stringOf(expr.text), "");
        case ast::ExprKind::LVal:
            return compileRValueFromLValue(expr);
        case ast::ExprKind::Unary:
            return compileUnary(expr);
        case ast::ExprKind::Binary:
            return compileBinary(expr);
        case ast::ExprKind::Call:
            return compileCall(expr);
        }
        throw CompileError(expr.loc, "unknown expression");
    }

    ExprResult compileRValueFromLValue(const ast::Expr &expr) {
        LValueResult lvalue = compileLValue(expr, false);
        if (lvalue.isConstValue && lvalue.type.isScalar()) {
            return constExpr(lvalue.type, lvalue.constValue, koopaConst(lvalue.constValue));
        }
        if (lvalue.type.isArrayLike()) {
            ExprResult result;
            result.type.base = lvalue.type.base;
            result.type.isArrayParam = true;
            if (lvalue.type.isArrayParam) {
                result.type.dimensions = lvalue.type.dimensions;
                result.value = lvalue.address;
            } else {
                if (lvalue.type.dimensions.empty()) {
                    throw CompileError(expr.loc, "array expression cannot decay from a scalar element");
                }
                result.type.dimensions.assign(lvalue.type.dimensions.begin() + 1, lvalue.type.dimensions.end());
                result.value = newTemp();
                emit(result.value + " = getelemptr " + lvalue.address + ", 0");
            }
            return result;
        }
        std::string temp = newTemp();
        emit(temp + " = load " + lvalue.address);
        ExprResult result;
        result.type = lvalue.type;
        result.value = temp;
        return result;
    }

    ExprResult compileUnary(const ast::Expr &expr) {
        ExprResult value = compileExpr(*expr.lhs);
        if (expr.op == "+") {
            if (!value.type.isScalar()) {
                throw CompileError(expr.loc, "unary + requires a scalar operand");
            }
            return value;
        }
        if (expr.op == "-") {
            if (!value.type.isScalar()) {
                throw CompileError(expr.loc, "unary - requires a scalar operand");
            }
            if (value.isConst) {
                if (value.type.base == BaseType::Float) {
                    return constExpr(value.type, ConstValue::floatOf(-value.constValue.floatValue),
                                     std::to_string(floatToBits(-value.constValue.floatValue)));
                }
                return constExpr(value.type, ConstValue::intOf(-value.constValue.intValue),
                                 std::to_string(-value.constValue.intValue));
            }
            if (value.type.base == BaseType::Float) {
                ExprResult zero = constExpr(scalar(BaseType::Float), ConstValue::floatOf(0.0f),
                                            std::to_string(floatToBits(0.0f)));
                return emitFloatHelper("__sysy_fsub", zero, value, BaseType::Float, expr.loc);
            }
            std::string temp = newTemp();
            emit(temp + " = sub 0, " + value.value);
            return valueExpr(scalar(BaseType::Int), temp);
        }
        if (expr.op == "!") {
            return logicalNot(value, expr.loc);
        }
        throw CompileError(expr.loc, "unsupported unary operator '" + expr.op + "'");
    }

    ExprResult compileBinary(const ast::Expr &expr) {
        if (expr.op == "&&") {
            return compileShortCircuitAnd(expr);
        }
        if (expr.op == "||") {
            return compileShortCircuitOr(expr);
        }

        ExprResult lhs = compileExpr(*expr.lhs);
        ExprResult rhs = compileExpr(*expr.rhs);
        if (!lhs.type.isScalar() || !rhs.type.isScalar()) {
            throw CompileError(expr.loc, "binary operator requires scalar operands");
        }
        if (expr.op == "%" && (lhs.type.base != BaseType::Int || rhs.type.base != BaseType::Int)) {
            throw CompileError(expr.loc, "operator % requires integer operands");
        }

        if (lhs.isConst && rhs.isConst) {
            return constExpr(resultTypeForConstBinary(expr.op, lhs, rhs), evalConstBinary(expr.op, lhs.constValue, rhs.constValue, expr.loc),
                             "");
        }

        bool comparison = isComparison(expr.op);
        if (expr.op == "%") {
            std::string temp = newTemp();
            emit(temp + " = mod " + lhs.value + ", " + rhs.value);
            return valueExpr(scalar(BaseType::Int), temp);
        }

        BaseType common = (lhs.type.base == BaseType::Float || rhs.type.base == BaseType::Float) ? BaseType::Float : BaseType::Int;
        lhs = castExpr(std::move(lhs), common, expr.lhs->loc);
        rhs = castExpr(std::move(rhs), common, expr.rhs->loc);

        if (common == BaseType::Float) {
            if (comparison) {
                return emitFloatComparison(expr.op, lhs, rhs, expr.loc);
            }
            if (expr.op == "+") {
                return emitFloatHelper("__sysy_fadd", lhs, rhs, BaseType::Float, expr.loc);
            }
            if (expr.op == "-") {
                return emitFloatHelper("__sysy_fsub", lhs, rhs, BaseType::Float, expr.loc);
            }
            if (expr.op == "*") {
                return emitFloatHelper("__sysy_fmul", lhs, rhs, BaseType::Float, expr.loc);
            }
            if (expr.op == "/") {
                return emitFloatHelper("__sysy_fdiv", lhs, rhs, BaseType::Float, expr.loc);
            }
            throw CompileError(expr.loc, "unsupported float operator '" + expr.op + "'");
        }

        std::string temp = newTemp();
        if (expr.op == "+") {
            emit(temp + " = add " + lhs.value + ", " + rhs.value);
        } else if (expr.op == "-") {
            emit(temp + " = sub " + lhs.value + ", " + rhs.value);
        } else if (expr.op == "*") {
            emit(temp + " = mul " + lhs.value + ", " + rhs.value);
        } else if (expr.op == "/") {
            emit(temp + " = div " + lhs.value + ", " + rhs.value);
        } else if (expr.op == "==") {
            emit(temp + " = eq " + lhs.value + ", " + rhs.value);
        } else if (expr.op == "!=") {
            emit(temp + " = ne " + lhs.value + ", " + rhs.value);
        } else if (expr.op == "<") {
            emit(temp + " = lt " + lhs.value + ", " + rhs.value);
        } else if (expr.op == "<=") {
            emit(temp + " = le " + lhs.value + ", " + rhs.value);
        } else if (expr.op == ">") {
            emit(temp + " = gt " + lhs.value + ", " + rhs.value);
        } else if (expr.op == ">=") {
            emit(temp + " = ge " + lhs.value + ", " + rhs.value);
        } else {
            throw CompileError(expr.loc, "unsupported binary operator '" + expr.op + "'");
        }
        return valueExpr(scalar(BaseType::Int), temp);
    }

    ExprResult compileShortCircuitAnd(const ast::Expr &expr) {
        std::string resultAddr = newTemp();
        emit(resultAddr + " = alloc i32");
        emit("store 0, " + resultAddr);
        ExprResult lhs = castToBool(compileExpr(*expr.lhs), expr.lhs->loc);
        std::string rhsLabel = newLabel("land_rhs");
        std::string endLabel = newLabel("land_end");
        emit("br " + lhs.value + ", " + rhsLabel + ", " + endLabel);
        terminated_ = true;
        emitLabel(rhsLabel);
        ExprResult rhs = castToBool(compileExpr(*expr.rhs), expr.rhs->loc);
        emit("store " + rhs.value + ", " + resultAddr);
        emit("jump " + endLabel);
        terminated_ = true;
        emitLabel(endLabel);
        std::string loaded = newTemp();
        emit(loaded + " = load " + resultAddr);
        return valueExpr(scalar(BaseType::Int), loaded);
    }

    ExprResult compileShortCircuitOr(const ast::Expr &expr) {
        std::string resultAddr = newTemp();
        emit(resultAddr + " = alloc i32");
        emit("store 1, " + resultAddr);
        ExprResult lhs = castToBool(compileExpr(*expr.lhs), expr.lhs->loc);
        std::string rhsLabel = newLabel("lor_rhs");
        std::string endLabel = newLabel("lor_end");
        emit("br " + lhs.value + ", " + endLabel + ", " + rhsLabel);
        terminated_ = true;
        emitLabel(rhsLabel);
        ExprResult rhs = castToBool(compileExpr(*expr.rhs), expr.rhs->loc);
        emit("store " + rhs.value + ", " + resultAddr);
        emit("jump " + endLabel);
        terminated_ = true;
        emitLabel(endLabel);
        std::string loaded = newTemp();
        emit(loaded + " = load " + resultAddr);
        return valueExpr(scalar(BaseType::Int), loaded);
    }

    ExprResult compileCall(const ast::Expr &expr) {
        if (expr.name == "putf") {
            return compilePutf(expr);
        }
        auto it = functionTable_.find(expr.name);
        if (it == functionTable_.end()) {
            throw CompileError(expr.loc, "undefined function '" + expr.name + "'");
        }
        const FunctionInfo &fn = it->second;
        if (fn.params.size() != expr.args.size()) {
            throw CompileError(expr.loc, "function '" + expr.name + "' called with wrong number of arguments");
        }

        std::vector<std::string> args;
        for (size_t i = 0; i < expr.args.size(); ++i) {
            const TypeInfo &paramType = fn.params[i];
            if (paramType.isArrayLike()) {
                args.push_back(compileArrayArgument(*expr.args[i], paramType));
            } else {
                ExprResult arg = castExpr(compileExpr(*expr.args[i]), paramType.base, expr.args[i]->loc);
                args.push_back(arg.value);
            }
        }

        std::string callText = "call @" + expr.name + "(" + join(args) + ")";
        if (fn.returnType == BaseType::Void) {
            emit(callText);
            return valueExpr(TypeInfo{BaseType::Void, {}, false}, "");
        }
        std::string temp = newTemp();
        emit(temp + " = " + callText);
        return valueExpr(scalar(fn.returnType), temp);
    }

    ExprResult compilePutf(const ast::Expr &expr) {
        if (expr.args.empty() || expr.args[0]->kind != ast::ExprKind::StringLiteral) {
            throw CompileError(expr.loc, "putf requires a string literal as its first argument");
        }
        std::vector<ExprResult> values;
        for (size_t i = 1; i < expr.args.size(); ++i) {
            values.push_back(castExpr(compileExpr(*expr.args[i]), BaseType::Int, expr.args[i]->loc));
        }
        size_t valueIndex = 0;
        const std::string &format = expr.args[0]->text;
        for (size_t i = 0; i < format.size(); ++i) {
            if (format[i] == '%' && i + 1 < format.size() && (format[i + 1] == 'd' || format[i + 1] == 'c')) {
                if (valueIndex >= values.size()) {
                    throw CompileError(expr.loc, "putf format expects more arguments");
                }
                std::string callee = format[i + 1] == 'd' ? "putint" : "putch";
                emit("call @" + callee + "(" + values[valueIndex++].value + ")");
                ++i;
            } else {
                emit("call @putch(" + std::to_string(static_cast<unsigned char>(format[i])) + ")");
            }
        }
        if (valueIndex != values.size()) {
            throw CompileError(expr.loc, "putf received unused arguments");
        }
        return valueExpr(TypeInfo{BaseType::Void, {}, false}, "");
    }

    std::string compileArrayArgument(const ast::Expr &expr, const TypeInfo &expected) {
        if (expr.kind != ast::ExprKind::LVal) {
            throw CompileError(expr.loc, "array argument must be an lvalue");
        }
        LValueResult lvalue = compileLValue(expr, false);
        if (!lvalue.type.isArrayLike()) {
            throw CompileError(expr.loc, "array argument expected");
        }

        std::string address = lvalue.address;
        std::vector<int> actualParamDims;
        if (lvalue.type.isArrayParam) {
            actualParamDims = lvalue.type.dimensions;
        } else {
            if (lvalue.type.dimensions.empty()) {
                throw CompileError(expr.loc, "array argument expected");
            }
            std::string decayed = newTemp();
            emit(decayed + " = getelemptr " + address + ", 0");
            address = decayed;
            actualParamDims.assign(lvalue.type.dimensions.begin() + 1, lvalue.type.dimensions.end());
        }

        if (actualParamDims != expected.dimensions) {
            throw CompileError(expr.loc, "array argument dimensions do not match parameter type");
        }
        if (lvalue.type.base != expected.base) {
            throw CompileError(expr.loc, "array argument element type does not match parameter type");
        }
        return address;
    }

    LValueResult compileLValue(const ast::Expr &expr, bool forStore) {
        if (expr.kind != ast::ExprKind::LVal) {
            throw CompileError(expr.loc, "expected an lvalue");
        }
        Symbol *symbol = findObject(expr.name);
        if (!symbol) {
            throw CompileError(expr.loc, "undefined symbol '" + expr.name + "'");
        }
        LValueResult result;
        result.isConstObject = symbol->isConst;
        result.type = typeAfterIndex(symbol->type, expr.indices.size(), expr.loc);

        bool allConstIndices = true;
        int linearIndex = 0;
        if (symbol->type.isArrayLike() && !expr.indices.empty()) {
            std::vector<int> concreteDims = symbol->type.dimensions;
            if (symbol->type.isArrayParam) {
                concreteDims.insert(concreteDims.begin(), 0);
            }
            int stride = 1;
            for (size_t i = 1; i < concreteDims.size(); ++i) {
                if (concreteDims[i] > 0) {
                    stride *= concreteDims[i];
                }
            }
            for (size_t i = 0; i < expr.indices.size(); ++i) {
                ConstValue idxValue;
                try {
                    idxValue = evalConstExpr(*expr.indices[i]);
                } catch (const CompileError &) {
                    allConstIndices = false;
                    break;
                }
                int idx = castConst(idxValue, BaseType::Int).intValue;
                linearIndex += idx * stride;
                if (i + 1 < concreteDims.size() && concreteDims[i + 1] > 0) {
                    stride /= concreteDims[i + 1];
                }
            }
        }
        if (!forStore && symbol->isConst && result.type.isScalar()) {
            if (symbol->hasConstScalar && expr.indices.empty()) {
                result.isConstValue = true;
                result.constValue = symbol->constScalar;
            } else if (allConstIndices && !symbol->constArrayValues.empty() && linearIndex >= 0 &&
                       linearIndex < static_cast<int>(symbol->constArrayValues.size())) {
                result.isConstValue = true;
                result.constValue = symbol->constArrayValues[linearIndex];
            }
        }

        if (symbol->type.isScalar()) {
            if (!expr.indices.empty()) {
                throw CompileError(expr.loc, "scalar object cannot be indexed");
            }
            result.address = symbol->irName;
            return result;
        }

        result.address = addressForIndexedObject(*symbol, expr.indices, expr.loc);
        return result;
    }

    std::string addressForIndexedObject(const Symbol &symbol, const std::vector<ast::ExprPtr> &indices, SourceLocation loc) {
        (void)loc;
        if (!symbol.type.isArrayLike()) {
            return symbol.irName;
        }
        std::string address;
        if (symbol.type.isArrayParam) {
            std::string loaded = newTemp();
            emit(loaded + " = load " + symbol.irName);
            address = loaded;
            for (size_t i = 0; i < indices.size(); ++i) {
                ExprResult index = castExpr(compileExpr(*indices[i]), BaseType::Int, indices[i]->loc);
                std::string temp = newTemp();
                if (i == 0) {
                    emit(temp + " = getptr " + address + ", " + index.value);
                } else {
                    emit(temp + " = getelemptr " + address + ", " + index.value);
                }
                address = temp;
            }
            return address;
        }

        address = symbol.irName;
        if (indices.empty()) {
            return address;
        }
        for (const auto &indexExpr : indices) {
            ExprResult index = castExpr(compileExpr(*indexExpr), BaseType::Int, indexExpr->loc);
            std::string temp = newTemp();
            emit(temp + " = getelemptr " + address + ", " + index.value);
            address = temp;
        }
        return address;
    }

    TypeInfo typeAfterIndex(const TypeInfo &type, size_t indexCount, SourceLocation loc) const {
        if (indexCount > static_cast<size_t>(type.rank())) {
            throw CompileError(loc, "too many array indices");
        }
        TypeInfo result;
        result.base = type.base;
        if (!type.isArrayParam) {
            result.dimensions.assign(type.dimensions.begin() + static_cast<long long>(indexCount), type.dimensions.end());
            return result;
        }
        if (indexCount == 0) {
            result = type;
            return result;
        }
        size_t consumedAfterFirst = indexCount - 1;
        result.dimensions.assign(type.dimensions.begin() + static_cast<long long>(consumedAfterFirst), type.dimensions.end());
        return result;
    }

    ExprResult castExpr(ExprResult expr, BaseType target, SourceLocation loc) {
        if (target == BaseType::Void || target == BaseType::String) {
            return expr;
        }
        if (!expr.type.isScalar()) {
            throw CompileError(loc, "scalar value expected");
        }
        if (expr.type.base == target) {
            return expr;
        }
        if (expr.isConst) {
            ConstValue value = castConst(expr.constValue, target);
            return constExpr(scalar(target), value, koopaConst(value));
        }
        if (target == BaseType::Float && expr.type.base == BaseType::Int) {
            return emitUnaryFloatHelper("__sysy_i2f", expr, BaseType::Float, loc);
        }
        if (target == BaseType::Int && expr.type.base == BaseType::Float) {
            return emitUnaryFloatHelper("__sysy_f2i", expr, BaseType::Int, loc);
        }
        throw CompileError(loc, "unsupported implicit conversion from " + toString(expr.type.base) + " to " + toString(target));
    }

    ExprResult castToBool(ExprResult expr, SourceLocation loc) {
        if (!expr.type.isScalar()) {
            throw CompileError(loc, "condition must be a scalar expression");
        }
        if (expr.isConst) {
            return constExpr(scalar(BaseType::Int), ConstValue::intOf(expr.constValue.truthy() ? 1 : 0),
                             expr.constValue.truthy() ? "1" : "0");
        }
        if (expr.type.base == BaseType::Float) {
            ExprResult zero = constExpr(scalar(BaseType::Float), ConstValue::floatOf(0.0f), std::to_string(floatToBits(0.0f)));
            return emitFloatComparison("!=", expr, zero, loc);
        }
        std::string temp = newTemp();
        emit(temp + " = ne " + expr.value + ", 0");
        return valueExpr(scalar(BaseType::Int), temp);
    }

    ExprResult logicalNot(ExprResult value, SourceLocation loc) {
        ExprResult boolean = castToBool(std::move(value), loc);
        if (boolean.isConst) {
            return constExpr(scalar(BaseType::Int), ConstValue::intOf(boolean.constValue.truthy() ? 0 : 1),
                             boolean.constValue.truthy() ? "0" : "1");
        }
        std::string temp = newTemp();
        emit(temp + " = eq " + boolean.value + ", 0");
        return valueExpr(scalar(BaseType::Int), temp);
    }

    ConstValue evalConstExpr(const ast::Expr &expr) {
        switch (expr.kind) {
        case ast::ExprKind::IntLiteral:
            return ConstValue::intOf(expr.intValue);
        case ast::ExprKind::FloatLiteral:
            return ConstValue::floatOf(expr.floatValue);
        case ast::ExprKind::StringLiteral:
            return ConstValue::stringOf(expr.text);
        case ast::ExprKind::LVal:
            return evalConstLValue(expr);
        case ast::ExprKind::Unary:
            return evalConstUnary(expr);
        case ast::ExprKind::Binary:
            return evalConstBinary(expr.op, evalConstExpr(*expr.lhs), evalConstExpr(*expr.rhs), expr.loc);
        case ast::ExprKind::Call:
            throw CompileError(expr.loc, "function call is not allowed in a constant expression");
        }
        throw CompileError(expr.loc, "invalid constant expression");
    }

    ConstValue evalConstLValue(const ast::Expr &expr) {
        const Symbol *symbol = findObject(expr.name);
        if (!symbol || !symbol->isConst) {
            throw CompileError(expr.loc, "constant expression requires a const object");
        }
        if (expr.indices.empty()) {
            if (!symbol->hasConstScalar) {
                throw CompileError(expr.loc, "array object is not a scalar constant");
            }
            return symbol->constScalar;
        }
        if (symbol->constArrayValues.empty()) {
            throw CompileError(expr.loc, "const array has no compile-time initializer");
        }
        std::vector<int> indices;
        for (const auto &indexExpr : expr.indices) {
            indices.push_back(castConst(evalConstExpr(*indexExpr), BaseType::Int).intValue);
        }
        if (indices.size() != symbol->type.dimensions.size()) {
            throw CompileError(expr.loc, "const array access must resolve to a scalar element");
        }
        int offset = linearOffset(symbol->type.dimensions, indices, expr.loc);
        return symbol->constArrayValues[offset];
    }

    ConstValue evalConstUnary(const ast::Expr &expr) {
        ConstValue value = evalConstExpr(*expr.lhs);
        if (expr.op == "+") {
            return value;
        }
        if (expr.op == "-") {
            if (value.type == BaseType::Float) {
                return ConstValue::floatOf(-value.floatValue);
            }
            return ConstValue::intOf(-value.intValue);
        }
        if (expr.op == "!") {
            return ConstValue::intOf(value.truthy() ? 0 : 1);
        }
        throw CompileError(expr.loc, "unsupported unary operator in constant expression");
    }

    ConstValue evalConstBinary(const std::string &op, ConstValue lhs, ConstValue rhs, SourceLocation loc) {
        if (op == "&&") {
            return ConstValue::intOf(lhs.truthy() && rhs.truthy() ? 1 : 0);
        }
        if (op == "||") {
            return ConstValue::intOf(lhs.truthy() || rhs.truthy() ? 1 : 0);
        }
        if (op == "%") {
            lhs = castConst(lhs, BaseType::Int);
            rhs = castConst(rhs, BaseType::Int);
            return ConstValue::intOf(lhs.intValue % rhs.intValue);
        }
        bool useFloat = lhs.type == BaseType::Float || rhs.type == BaseType::Float;
        if (useFloat) {
            float a = castConst(lhs, BaseType::Float).floatValue;
            float b = castConst(rhs, BaseType::Float).floatValue;
            if (op == "+") return ConstValue::floatOf(a + b);
            if (op == "-") return ConstValue::floatOf(a - b);
            if (op == "*") return ConstValue::floatOf(a * b);
            if (op == "/") return ConstValue::floatOf(a / b);
            if (op == "==") return ConstValue::intOf(a == b ? 1 : 0);
            if (op == "!=") return ConstValue::intOf(a != b ? 1 : 0);
            if (op == "<") return ConstValue::intOf(a < b ? 1 : 0);
            if (op == "<=") return ConstValue::intOf(a <= b ? 1 : 0);
            if (op == ">") return ConstValue::intOf(a > b ? 1 : 0);
            if (op == ">=") return ConstValue::intOf(a >= b ? 1 : 0);
            throw CompileError(loc, "unsupported float constant operator '" + op + "'");
        }
        int a = lhs.intValue;
        int b = rhs.intValue;
        if (op == "+") return ConstValue::intOf(a + b);
        if (op == "-") return ConstValue::intOf(a - b);
        if (op == "*") return ConstValue::intOf(a * b);
        if (op == "/") return ConstValue::intOf(a / b);
        if (op == "==") return ConstValue::intOf(a == b ? 1 : 0);
        if (op == "!=") return ConstValue::intOf(a != b ? 1 : 0);
        if (op == "<") return ConstValue::intOf(a < b ? 1 : 0);
        if (op == "<=") return ConstValue::intOf(a <= b ? 1 : 0);
        if (op == ">") return ConstValue::intOf(a > b ? 1 : 0);
        if (op == ">=") return ConstValue::intOf(a >= b ? 1 : 0);
        throw CompileError(loc, "unsupported integer constant operator '" + op + "'");
    }

    TypeInfo resultTypeForConstBinary(const std::string &op, const ExprResult &lhs, const ExprResult &rhs) const {
        if (isComparison(op) || op == "&&" || op == "||") {
            return scalar(BaseType::Int);
        }
        if (lhs.type.base == BaseType::Float || rhs.type.base == BaseType::Float) {
            return scalar(BaseType::Float);
        }
        return scalar(BaseType::Int);
    }

    int constInt(const ast::Expr &expr, SourceLocation loc) {
        ConstValue value = castConst(evalConstExpr(expr), BaseType::Int);
        if (value.intValue < 0) {
            throw CompileError(loc, "constant integer must be non-negative");
        }
        return value.intValue;
    }

    std::vector<ConstValue> flattenConstInitializer(const ast::InitVal *init, const TypeInfo &type, SourceLocation loc) {
        (void)loc;
        int count = type.isScalar() ? 1 : totalElements(type.dimensions);
        ConstValue zero = type.base == BaseType::Float ? ConstValue::floatOf(0.0f) : ConstValue::intOf(0);
        std::vector<ConstValue> values(count, zero);
        if (!init) {
            return values;
        }
        if (type.isScalar()) {
            if (init->isList) {
                throw CompileError(init->loc, "scalar initializer cannot be a list");
            }
            values[0] = castConst(evalConstExpr(*init->expr), type.base);
            return values;
        }
        if (!init->isList) {
            throw CompileError(init->loc, "array initializer must be a list");
        }
        size_t end = fillConstInitializer(*init, type, 0, 0, values);
        (void)end;
        return values;
    }

    std::vector<const ast::Expr *> flattenRuntimeInitializer(const ast::InitVal *init, const TypeInfo &type, SourceLocation loc) {
        int count = totalElements(type.dimensions);
        std::vector<const ast::Expr *> values(count, nullptr);
        if (!init) {
            return values;
        }
        if (!init->isList) {
            throw CompileError(init->loc, "array initializer must be a list");
        }
        size_t end = fillRuntimeInitializer(*init, type, 0, 0, values);
        (void)end;
        (void)loc;
        return values;
    }

    size_t fillConstInitializer(const ast::InitVal &init, const TypeInfo &type, size_t level, size_t start,
                                std::vector<ConstValue> &out) {
        size_t limit = start + subElementCount(type.dimensions, level);
        if (!init.isList) {
            if (start >= out.size()) {
                throw CompileError(init.loc, "too many elements in initializer");
            }
            out[start] = castConst(evalConstExpr(*init.expr), type.base);
            return start + 1;
        }
        size_t pos = start;
        for (const auto &element : init.elements) {
            if (pos >= limit) {
                throw CompileError(element->loc, "too many elements in initializer");
            }
            if (element->isList) {
                if (level + 1 > type.dimensions.size()) {
                    throw CompileError(element->loc, "too many nested braces in initializer");
                }
                size_t childSize = subElementCount(type.dimensions, level + 1);
                pos = alignInitializerPosition(pos, start, childSize);
                if (pos + childSize > limit) {
                    throw CompileError(element->loc, "too many elements in initializer");
                }
                fillConstInitializer(*element, type, level + 1, pos, out);
                pos += childSize;
            } else {
                pos = fillConstInitializer(*element, type, type.dimensions.size(), pos, out);
            }
        }
        return pos;
    }

    size_t fillRuntimeInitializer(const ast::InitVal &init, const TypeInfo &type, size_t level, size_t start,
                                  std::vector<const ast::Expr *> &out) {
        size_t limit = start + subElementCount(type.dimensions, level);
        if (!init.isList) {
            if (start >= out.size()) {
                throw CompileError(init.loc, "too many elements in initializer");
            }
            out[start] = init.expr.get();
            return start + 1;
        }
        size_t pos = start;
        for (const auto &element : init.elements) {
            if (pos >= limit) {
                throw CompileError(element->loc, "too many elements in initializer");
            }
            if (element->isList) {
                if (level + 1 > type.dimensions.size()) {
                    throw CompileError(element->loc, "too many nested braces in initializer");
                }
                size_t childSize = subElementCount(type.dimensions, level + 1);
                pos = alignInitializerPosition(pos, start, childSize);
                if (pos + childSize > limit) {
                    throw CompileError(element->loc, "too many elements in initializer");
                }
                fillRuntimeInitializer(*element, type, level + 1, pos, out);
                pos += childSize;
            } else {
                pos = fillRuntimeInitializer(*element, type, type.dimensions.size(), pos, out);
            }
        }
        return pos;
    }

    size_t subElementCount(const std::vector<int> &dims, size_t level) const {
        size_t count = 1;
        for (size_t i = level; i < dims.size(); ++i) {
            count *= static_cast<size_t>(dims[i]);
        }
        return count;
    }

    size_t alignInitializerPosition(size_t pos, size_t start, size_t childSize) const {
        if (childSize <= 1) {
            return pos;
        }
        size_t relative = pos - start;
        size_t aligned = ((relative + childSize - 1) / childSize) * childSize;
        return start + aligned;
    }

    std::string aggregateInitializer(const std::vector<ConstValue> &values, const std::vector<int> &dims, size_t level,
                                     size_t start) const {
        if (level == dims.size()) {
            return koopaConst(values[start]);
        }
        size_t stride = 1;
        for (size_t i = level + 1; i < dims.size(); ++i) {
            stride *= static_cast<size_t>(dims[i]);
        }
        std::ostringstream out;
        out << "{";
        for (int i = 0; i < dims[level]; ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << aggregateInitializer(values, dims, level + 1, start + static_cast<size_t>(i) * stride);
        }
        out << "}";
        return out.str();
    }

    std::string elementAddress(const std::string &base, const std::vector<int> &dims, int linearIndex, SourceLocation loc) {
        std::vector<int> indices = indicesFromLinear(dims, linearIndex);
        std::string address = base;
        for (int index : indices) {
            std::string temp = newTemp();
            emit(temp + " = getelemptr " + address + ", " + std::to_string(index));
            address = temp;
        }
        (void)loc;
        return address;
    }

    std::vector<int> indicesFromLinear(const std::vector<int> &dims, int linear) const {
        std::vector<int> indices(dims.size(), 0);
        for (int i = static_cast<int>(dims.size()) - 1; i >= 0; --i) {
            indices[i] = linear % dims[i];
            linear /= dims[i];
        }
        return indices;
    }

    int linearOffset(const std::vector<int> &dims, const std::vector<int> &indices, SourceLocation loc) const {
        int offset = 0;
        int stride = totalElements(dims);
        for (size_t i = 0; i < dims.size(); ++i) {
            stride /= dims[i];
            if (indices[i] < 0 || indices[i] >= dims[i]) {
                throw CompileError(loc, "constant array index is out of bounds");
            }
            offset += indices[i] * stride;
        }
        return offset;
    }

    ExprResult emitFloatHelper(const std::string &helper, ExprResult lhs, ExprResult rhs, BaseType resultType, SourceLocation loc) {
        (void)loc;
        result_.floatHelpers.insert(helper);
        std::string temp = newTemp();
        emit(temp + " = call @" + helper + "(" + lhs.value + ", " + rhs.value + ")");
        return valueExpr(scalar(resultType), temp);
    }

    ExprResult emitUnaryFloatHelper(const std::string &helper, ExprResult value, BaseType resultType, SourceLocation loc) {
        (void)loc;
        result_.floatHelpers.insert(helper);
        std::string temp = newTemp();
        emit(temp + " = call @" + helper + "(" + value.value + ")");
        return valueExpr(scalar(resultType), temp);
    }

    ExprResult emitFloatComparison(const std::string &op, ExprResult lhs, ExprResult rhs, SourceLocation loc) {
        if (op == "==") {
            return emitFloatHelper("__sysy_feq", lhs, rhs, BaseType::Int, loc);
        }
        if (op == "!=") {
            ExprResult eq = emitFloatHelper("__sysy_feq", lhs, rhs, BaseType::Int, loc);
            std::string temp = newTemp();
            emit(temp + " = eq " + eq.value + ", 0");
            return valueExpr(scalar(BaseType::Int), temp);
        }
        if (op == "<") {
            return emitFloatHelper("__sysy_flt", lhs, rhs, BaseType::Int, loc);
        }
        if (op == "<=") {
            return emitFloatHelper("__sysy_fle", lhs, rhs, BaseType::Int, loc);
        }
        if (op == ">") {
            return emitFloatHelper("__sysy_flt", rhs, lhs, BaseType::Int, loc);
        }
        if (op == ">=") {
            return emitFloatHelper("__sysy_fle", rhs, lhs, BaseType::Int, loc);
        }
        throw CompileError(loc, "unsupported float comparison '" + op + "'");
    }

    void pushScope() {
        scopes_.push_back(Scope{});
    }

    void popScope() {
        scopes_.pop_back();
    }

    Symbol *findObject(const std::string &name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->objects.find(name);
            if (found != it->objects.end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    const Symbol *findObject(const std::string &name) const {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->objects.find(name);
            if (found != it->objects.end()) {
                return &found->second;
            }
        }
        return nullptr;
    }

    void insertObject(Symbol symbol, SourceLocation loc) {
        Scope &scope = scopes_.back();
        if (scope.objects.count(symbol.name) != 0) {
            throw CompileError(loc, "duplicate symbol '" + symbol.name + "' in the same scope");
        }
        scope.objects.emplace(symbol.name, std::move(symbol));
    }

    static TypeInfo scalar(BaseType base) {
        TypeInfo type;
        type.base = base;
        return type;
    }

    static TypeInfo arrayParam(BaseType base, std::vector<int> dims) {
        TypeInfo type;
        type.base = base;
        type.dimensions = std::move(dims);
        type.isArrayParam = true;
        return type;
    }

    ExprResult constExpr(TypeInfo type, ConstValue value, std::string text) const {
        ExprResult result;
        result.type = std::move(type);
        result.isConst = true;
        result.constValue = value;
        result.value = text.empty() && value.type != BaseType::String ? koopaConst(value) : std::move(text);
        return result;
    }

    ExprResult valueExpr(TypeInfo type, std::string value) const {
        ExprResult result;
        result.type = std::move(type);
        result.value = std::move(value);
        return result;
    }

    static bool isComparison(const std::string &op) {
        return op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=";
    }

    std::string newTemp() {
        return "%t" + std::to_string(tempId_++);
    }

    std::string newLabel(const std::string &prefix) {
        return "%" + prefix + "_" + std::to_string(labelId_++);
    }

    std::string newLocalName(const std::string &name) {
        return "%v_" + sanitize(name) + "_" + std::to_string(symbolId_++);
    }

    void emit(const std::string &line) {
        if (terminated_) {
            return;
        }
        *currentOut_ << "  " << line << "\n";
    }

    void emitLabel(const std::string &label) {
        *currentOut_ << label << ":\n";
        terminated_ = false;
    }

    std::string koopaObjectType(const TypeInfo &type) const {
        if (type.dimensions.empty()) {
            return "i32";
        }
        return nestedArrayType(type.dimensions, 0);
    }

    std::string koopaParamType(const TypeInfo &type) const {
        if (!type.isArrayParam) {
            return "i32";
        }
        if (type.dimensions.empty()) {
            return "*i32";
        }
        return "*" + nestedArrayType(type.dimensions, 0);
    }

    std::string nestedArrayType(const std::vector<int> &dims, size_t index) const {
        if (index == dims.size()) {
            return "i32";
        }
        return "[" + nestedArrayType(dims, index + 1) + ", " + std::to_string(dims[index]) + "]";
    }

    std::string zeroFor(BaseType type) const {
        if (type == BaseType::Float) {
            return std::to_string(floatToBits(0.0f));
        }
        return "0";
    }

    static std::string koopaConst(ConstValue value) {
        return std::to_string(value.koopaBits());
    }

    int totalElements(const std::vector<int> &dims) const {
        if (dims.empty()) {
            return 1;
        }
        return std::accumulate(dims.begin(), dims.end(), 1, std::multiplies<int>());
    }

    static std::string join(const std::vector<std::string> &items) {
        std::ostringstream out;
        for (size_t i = 0; i < items.size(); ++i) {
            if (i != 0) {
                out << ", ";
            }
            out << items[i];
        }
        return out.str();
    }

    void emitRuntimeDecls(std::ostream &out) const {
        out << "decl @getint(): i32\n";
        out << "decl @getch(): i32\n";
        out << "decl @getarray(*i32): i32\n";
        out << "decl @putint(i32)\n";
        out << "decl @putch(i32)\n";
        out << "decl @putarray(i32, *i32)\n";
        out << "decl @starttime()\n";
        out << "decl @stoptime()\n";
        out << "decl @getfloat(): i32\n";
        out << "decl @putfloat(i32)\n";
        out << "decl @getfarray(*i32): i32\n";
        out << "decl @putfarray(i32, *i32)\n";
        out << "decl @__sysy_i2f(i32): i32\n";
        out << "decl @__sysy_f2i(i32): i32\n";
        out << "decl @__sysy_fadd(i32, i32): i32\n";
        out << "decl @__sysy_fsub(i32, i32): i32\n";
        out << "decl @__sysy_fmul(i32, i32): i32\n";
        out << "decl @__sysy_fdiv(i32, i32): i32\n";
        out << "decl @__sysy_feq(i32, i32): i32\n";
        out << "decl @__sysy_flt(i32, i32): i32\n";
        out << "decl @__sysy_fle(i32, i32): i32\n\n";
    }
};

} // namespace

KoopaResult SemanticAnalyzer::compile(const ast::Program &program) {
    Impl impl;
    return impl.compile(program);
}

} // namespace sysy
