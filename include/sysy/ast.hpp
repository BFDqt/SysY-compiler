#pragma once

#include "sysy/common.hpp"

#include <memory>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

namespace sysy::ast {

struct Expr;
struct InitVal;
struct Decl;
struct Stmt;
struct Block;
struct FuncDef;

using ExprPtr = std::unique_ptr<Expr>;
using InitValPtr = std::unique_ptr<InitVal>;
using DeclPtr = std::unique_ptr<Decl>;
using StmtPtr = std::unique_ptr<Stmt>;
using BlockPtr = std::unique_ptr<Block>;
using FuncDefPtr = std::unique_ptr<FuncDef>;

enum class ExprKind {
    IntLiteral,
    FloatLiteral,
    StringLiteral,
    LVal,
    Unary,
    Binary,
    Call,
};

struct Expr {
    SourceLocation loc;
    ExprKind kind = ExprKind::IntLiteral;
    std::string text;
    int32_t intValue = 0;
    float floatValue = 0.0f;
    std::string name;
    std::string op;
    ExprPtr lhs;
    ExprPtr rhs;
    std::vector<ExprPtr> args;
    std::vector<ExprPtr> indices;

    static ExprPtr intLiteral(SourceLocation loc, int32_t value, std::string raw);
    static ExprPtr floatLiteral(SourceLocation loc, float value, std::string raw);
    static ExprPtr stringLiteral(SourceLocation loc, std::string value);
    static ExprPtr lval(SourceLocation loc, std::string name, std::vector<ExprPtr> indices);
    static ExprPtr unary(SourceLocation loc, std::string op, ExprPtr expr);
    static ExprPtr binary(SourceLocation loc, std::string op, ExprPtr lhs, ExprPtr rhs);
    static ExprPtr call(SourceLocation loc, std::string name, std::vector<ExprPtr> args);
};

struct InitVal {
    SourceLocation loc;
    bool isList = false;
    ExprPtr expr;
    std::vector<InitValPtr> elements;
};

struct VarDef {
    SourceLocation loc;
    std::string name;
    std::vector<ExprPtr> dimensions;
    InitValPtr init;

    VarDef() = default;
    VarDef(VarDef &&) noexcept = default;
    VarDef &operator=(VarDef &&) noexcept = default;
    VarDef(const VarDef &) = delete;
    VarDef &operator=(const VarDef &) = delete;
};

struct Decl {
    SourceLocation loc;
    bool isConst = false;
    BaseType baseType = BaseType::Int;
    std::vector<VarDef> definitions;
};

struct FuncParam {
    SourceLocation loc;
    BaseType baseType = BaseType::Int;
    std::string name;
    bool isArray = false;
    std::vector<ExprPtr> dimensionsAfterFirst;

    FuncParam() = default;
    FuncParam(FuncParam &&) noexcept = default;
    FuncParam &operator=(FuncParam &&) noexcept = default;
    FuncParam(const FuncParam &) = delete;
    FuncParam &operator=(const FuncParam &) = delete;
};

enum class StmtKind {
    Empty,
    Assign,
    Expr,
    Block,
    If,
    While,
    Break,
    Continue,
    Return,
};

struct Stmt {
    SourceLocation loc;
    StmtKind kind = StmtKind::Empty;
    ExprPtr lval;
    ExprPtr expr;
    ExprPtr cond;
    BlockPtr block;
    StmtPtr thenStmt;
    StmtPtr elseStmt;
    StmtPtr body;
};

struct BlockItem {
    SourceLocation loc;
    DeclPtr decl;
    StmtPtr stmt;

    BlockItem() = default;
    BlockItem(BlockItem &&) noexcept = default;
    BlockItem &operator=(BlockItem &&) noexcept = default;
    BlockItem(const BlockItem &) = delete;
    BlockItem &operator=(const BlockItem &) = delete;
};

struct Block {
    SourceLocation loc;
    std::vector<BlockItem> items;
};

struct FuncDef {
    SourceLocation loc;
    BaseType returnType = BaseType::Int;
    std::string name;
    std::vector<FuncParam> params;
    BlockPtr body;
};

enum class TopLevelKind {
    Decl,
    FuncDef,
};

struct TopLevel {
    SourceLocation loc;
    TopLevelKind kind = TopLevelKind::Decl;
    DeclPtr decl;
    FuncDefPtr func;

    TopLevel() = default;
    TopLevel(TopLevel &&) noexcept = default;
    TopLevel &operator=(TopLevel &&) noexcept = default;
    TopLevel(const TopLevel &) = delete;
    TopLevel &operator=(const TopLevel &) = delete;
};

struct Program {
    std::vector<TopLevel> items;
};

SourceLocation makeLoc(int line, int column);
std::string escapeString(const std::string &value);
std::string decodeSysYString(const std::string &raw);
void dumpProgram(const Program &program, std::ostream &os);

} // namespace sysy::ast
