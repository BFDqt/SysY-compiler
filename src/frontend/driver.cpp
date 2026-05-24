#include "sysy/driver.hpp"

#include "sysy/common.hpp"

#include "compiler2022_x/FrontEnd/Parser.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using ObjTree::ObjUuid;
using ObjTree::ObjUuidList;

class ObjTreeAdapter {
public:
    explicit ObjTreeAdapter(const ObjTree::ObjManager &manager) : manager_(manager) {}

    std::unique_ptr<sysy::ast::Program> convert(ObjUuid root) {
        expect(root, "CompUnitAst");
        auto program = std::make_unique<sysy::ast::Program>();
        for (ObjUuid item : flattenLeftList(root, "CompUnitAst")) {
            program->items.push_back(convertTopLevel(item));
        }
        return program;
    }

private:
    const ObjTree::ObjManager &manager_;
    const sysy::SourceLocation loc_ = sysy::ast::makeLoc(1, 1);

    const ObjUuidList &sons(ObjUuid uuid) const {
        return manager_.get_son_uuid_list(uuid);
    }

    std::string type(ObjUuid uuid) const {
        return manager_.get_type(uuid);
    }

    std::string info(ObjUuid uuid) const {
        return manager_.get_info(uuid);
    }

    [[noreturn]] void fail(ObjUuid uuid, const std::string &message) const {
        throw sysy::CompileError(loc_, "compiler2022 frontend adapter: " + message + " near " + type(uuid));
    }

    void expect(ObjUuid uuid, const std::string &expected) const {
        if (type(uuid) != expected) {
            fail(uuid, "expected " + expected + ", got " + type(uuid));
        }
    }

    ObjUuid onlySon(ObjUuid uuid) const {
        const auto &children = sons(uuid);
        if (children.size() != 1) {
            fail(uuid, "expected exactly one child");
        }
        return children.front();
    }

    std::vector<ObjUuid> flattenLeftList(ObjUuid uuid, const std::string &listType) const {
        expect(uuid, listType);
        const auto &children = sons(uuid);
        if (children.empty()) {
            return {};
        }
        if (children.size() == 1) {
            return {children.front()};
        }
        if (children.size() == 2 && type(children.front()) == listType) {
            auto result = flattenLeftList(children.front(), listType);
            result.push_back(children.back());
            return result;
        }
        return std::vector<ObjUuid>(children.begin(), children.end());
    }

    sysy::ast::TopLevel convertTopLevel(ObjUuid uuid) {
        expect(uuid, "CompUnitDefAst");
        ObjUuid child = onlySon(uuid);

        sysy::ast::TopLevel top;
        top.loc = loc_;
        if (type(child) == "DeclAst") {
            top.kind = sysy::ast::TopLevelKind::Decl;
            top.decl = convertDecl(child);
            top.loc = top.decl->loc;
        } else if (type(child) == "FuncDefAst") {
            top.kind = sysy::ast::TopLevelKind::FuncDef;
            top.func = convertFunc(child);
            top.loc = top.func->loc;
        } else {
            fail(child, "expected declaration or function definition");
        }
        return top;
    }

    std::unique_ptr<sysy::ast::Decl> convertDecl(ObjUuid uuid) {
        expect(uuid, "DeclAst");
        ObjUuid child = onlySon(uuid);
        if (type(child) == "ConstDeclAst") {
            return convertConstDecl(child);
        }
        if (type(child) == "VarDeclAst") {
            return convertVarDecl(child);
        }
        fail(child, "expected const or var declaration");
    }

    std::unique_ptr<sysy::ast::Decl> convertConstDecl(ObjUuid uuid) {
        expect(uuid, "ConstDeclAst");
        const auto &children = sons(uuid);
        if (children.size() != 2) {
            fail(uuid, "malformed const declaration");
        }

        auto decl = std::make_unique<sysy::ast::Decl>();
        decl->loc = loc_;
        decl->isConst = true;
        decl->baseType = convertBaseType(children[0]);
        for (ObjUuid def : flattenLeftList(children[1], "ConstDefListAst")) {
            decl->definitions.push_back(convertVarDef(def));
        }
        return decl;
    }

    std::unique_ptr<sysy::ast::Decl> convertVarDecl(ObjUuid uuid) {
        expect(uuid, "VarDeclAst");
        const auto &children = sons(uuid);
        if (children.size() != 2) {
            fail(uuid, "malformed var declaration");
        }

        auto decl = std::make_unique<sysy::ast::Decl>();
        decl->loc = loc_;
        decl->isConst = false;
        decl->baseType = convertBaseType(children[0]);
        for (ObjUuid def : flattenLeftList(children[1], "VarDefListAst")) {
            decl->definitions.push_back(convertVarDef(def));
        }
        return decl;
    }

    sysy::BaseType convertBaseType(ObjUuid uuid) const {
        expect(uuid, "BTypeAst");
        std::string value = info(uuid);
        if (value == "KEYWORD_INT" || value == "int") {
            return sysy::BaseType::Int;
        }
        if (value == "KEYWORD_FLOAT" || value == "float") {
            return sysy::BaseType::Float;
        }
        fail(uuid, "unsupported base type '" + value + "'");
    }

    sysy::BaseType convertFuncType(ObjUuid uuid) const {
        expect(uuid, "FuncTypeAst");
        std::string value = info(uuid);
        if (value == "void") {
            return sysy::BaseType::Void;
        }
        if (value == "int" || value == "KEYWORD_INT") {
            return sysy::BaseType::Int;
        }
        if (value == "float" || value == "KEYWORD_FLOAT") {
            return sysy::BaseType::Float;
        }
        fail(uuid, "unsupported function type '" + value + "'");
    }

    sysy::ast::VarDef convertVarDef(ObjUuid uuid) {
        std::string nodeType = type(uuid);
        if (nodeType != "ConstDefAst" && nodeType != "VarDefAst") {
            fail(uuid, "expected variable definition");
        }
        const auto &children = sons(uuid);
        if (children.empty() || children.size() > 2) {
            fail(uuid, "malformed variable definition");
        }

        sysy::ast::VarDef def;
        def.loc = loc_;
        def.name = info(uuid);
        for (ObjUuid dim : flattenLeftList(children[0], "ArrayIndexListAst")) {
            def.dimensions.push_back(convertExpr(dim));
        }
        if (children.size() == 2) {
            def.init = convertInit(children[1]);
        }
        return def;
    }

    sysy::ast::InitValPtr convertInit(ObjUuid uuid) {
        expect(uuid, "InitValAst");
        auto init = std::make_unique<sysy::ast::InitVal>();
        init->loc = loc_;

        if (info(uuid) == "Exp") {
            init->isList = false;
            init->expr = convertExpr(onlySon(uuid));
            return init;
        }

        init->isList = true;
        const auto &children = sons(uuid);
        if (children.empty()) {
            return init;
        }
        if (children.size() != 1) {
            fail(uuid, "malformed initializer list");
        }
        for (ObjUuid item : flattenLeftList(children.front(), "InitValListAst")) {
            init->elements.push_back(convertInit(item));
        }
        return init;
    }

    std::unique_ptr<sysy::ast::FuncDef> convertFunc(ObjUuid uuid) {
        expect(uuid, "FuncDefAst");
        const auto &children = sons(uuid);
        if (children.size() != 2 && children.size() != 3) {
            fail(uuid, "malformed function definition");
        }

        auto func = std::make_unique<sysy::ast::FuncDef>();
        func->loc = loc_;
        func->name = info(uuid);
        func->returnType = convertFuncType(children[0]);
        if (children.size() == 2) {
            func->body = convertBlock(children[1]);
        } else {
            for (ObjUuid param : flattenLeftList(children[1], "FuncFParamsAst")) {
                func->params.push_back(convertParam(param));
            }
            func->body = convertBlock(children[2]);
        }
        return func;
    }

    sysy::ast::FuncParam convertParam(ObjUuid uuid) {
        expect(uuid, "FuncFParamAst");
        const auto &children = sons(uuid);
        if (children.size() != 2) {
            fail(uuid, "malformed function parameter");
        }

        sysy::ast::FuncParam param;
        param.loc = loc_;
        param.baseType = convertBaseType(children[0]);
        param.name = info(uuid);
        param.isArray = isArrayParam(children[1]);
        if (param.isArray) {
            for (ObjUuid dim : flattenParamArrayDimensions(children[1])) {
                param.dimensionsAfterFirst.push_back(convertExpr(dim));
            }
        }
        return param;
    }

    bool isArrayParam(ObjUuid uuid) const {
        expect(uuid, "FuncFParamArrayIndexListAst");
        return info(uuid) != "Empty";
    }

    std::vector<ObjUuid> flattenParamArrayDimensions(ObjUuid uuid) const {
        expect(uuid, "FuncFParamArrayIndexListAst");
        const auto &children = sons(uuid);
        if (children.empty()) {
            return {};
        }
        if (children.size() == 2 && type(children.front()) == "FuncFParamArrayIndexListAst") {
            auto result = flattenParamArrayDimensions(children.front());
            result.push_back(children.back());
            return result;
        }
        fail(uuid, "malformed array parameter dimensions");
    }

    sysy::ast::BlockPtr convertBlock(ObjUuid uuid) {
        expect(uuid, "BlockAst");
        auto block = std::make_unique<sysy::ast::Block>();
        block->loc = loc_;

        ObjUuid list = onlySon(uuid);
        for (ObjUuid itemNode : flattenLeftList(list, "BlockItemListAst")) {
            block->items.push_back(convertBlockItem(itemNode));
        }
        return block;
    }

    sysy::ast::BlockItem convertBlockItem(ObjUuid uuid) {
        expect(uuid, "BlockItemAst");
        ObjUuid child = onlySon(uuid);

        sysy::ast::BlockItem item;
        item.loc = loc_;
        if (type(child) == "DeclAst") {
            item.decl = convertDecl(child);
        } else if (type(child) == "StmtAst") {
            item.stmt = convertStmt(child);
        } else {
            fail(child, "expected block item");
        }
        return item;
    }

    sysy::ast::StmtPtr convertStmt(ObjUuid uuid) {
        expect(uuid, "StmtAst");
        const auto &children = sons(uuid);
        const std::string marker = info(uuid);

        auto stmt = std::make_unique<sysy::ast::Stmt>();
        stmt->loc = loc_;

        if (marker == "If" || marker == "IfElse") {
            if (children.size() != (marker == "If" ? 2U : 3U)) {
                fail(uuid, "malformed if statement");
            }
            stmt->kind = sysy::ast::StmtKind::If;
            stmt->cond = convertExpr(children[0]);
            stmt->thenStmt = convertStmt(children[1]);
            if (marker == "IfElse") {
                stmt->elseStmt = convertStmt(children[2]);
            }
            return stmt;
        }
        if (marker == "While") {
            if (children.size() != 2) {
                fail(uuid, "malformed while statement");
            }
            stmt->kind = sysy::ast::StmtKind::While;
            stmt->cond = convertExpr(children[0]);
            stmt->body = convertStmt(children[1]);
            return stmt;
        }
        if (marker == "Break") {
            stmt->kind = sysy::ast::StmtKind::Break;
            return stmt;
        }
        if (marker == "Continue") {
            stmt->kind = sysy::ast::StmtKind::Continue;
            return stmt;
        }
        if (marker == "Return") {
            stmt->kind = sysy::ast::StmtKind::Return;
            return stmt;
        }
        if (marker == "ReturnExp") {
            stmt->kind = sysy::ast::StmtKind::Return;
            stmt->expr = convertExpr(onlySon(uuid));
            return stmt;
        }

        if (children.empty()) {
            stmt->kind = sysy::ast::StmtKind::Empty;
            return stmt;
        }
        if (children.size() == 1) {
            if (type(children[0]) == "BlockAst") {
                stmt->kind = sysy::ast::StmtKind::Block;
                stmt->block = convertBlock(children[0]);
            } else {
                stmt->kind = sysy::ast::StmtKind::Expr;
                stmt->expr = convertExpr(children[0]);
            }
            return stmt;
        }
        if (children.size() == 2) {
            stmt->kind = sysy::ast::StmtKind::Assign;
            stmt->lval = convertExpr(children[0]);
            stmt->expr = convertExpr(children[1]);
            return stmt;
        }
        fail(uuid, "malformed statement");
    }

    sysy::ast::ExprPtr convertExpr(ObjUuid uuid) {
        const std::string nodeType = type(uuid);
        const auto &children = sons(uuid);

        if (nodeType == "ExpAst" || nodeType == "CondAst" || nodeType == "PrimaryExpAst" || nodeType == "NumberAst") {
            return convertExpr(onlySon(uuid));
        }
        if (nodeType == "IntConstAst") {
            std::string raw = info(uuid);
            int value = static_cast<int>(std::strtol(raw.c_str(), nullptr, 0));
            return sysy::ast::Expr::intLiteral(loc_, value, std::move(raw));
        }
        if (nodeType == "FloatConstAst") {
            std::string raw = info(uuid);
            float value = std::strtof(raw.c_str(), nullptr);
            return sysy::ast::Expr::floatLiteral(loc_, value, std::move(raw));
        }
        if (nodeType == "StringConstAst") {
            return sysy::ast::Expr::stringLiteral(loc_, sysy::ast::decodeSysYString(info(uuid)));
        }
        if (nodeType == "LValAst") {
            std::vector<sysy::ast::ExprPtr> indices;
            if (children.size() != 1) {
                fail(uuid, "malformed lvalue");
            }
            for (ObjUuid index : flattenLeftList(children.front(), "ArrayIndexListAst")) {
                indices.push_back(convertExpr(index));
            }
            return sysy::ast::Expr::lval(loc_, info(uuid), std::move(indices));
        }
        if (nodeType == "UnaryExpAst") {
            return convertUnaryExpr(uuid);
        }
        if (nodeType == "MulExpAst" || nodeType == "AddExpAst" || nodeType == "RelExpAst" || nodeType == "EqExpAst") {
            return convertBinaryExpr(uuid);
        }
        if (nodeType == "LAndExpAst" || nodeType == "LOrExpAst") {
            return convertLogicalExpr(uuid);
        }
        fail(uuid, "unsupported expression node");
    }

    sysy::ast::ExprPtr convertUnaryExpr(ObjUuid uuid) {
        expect(uuid, "UnaryExpAst");
        const auto &children = sons(uuid);
        const std::string functionName = info(uuid);

        if (!functionName.empty()) {
            std::vector<sysy::ast::ExprPtr> args;
            if (children.size() > 1) {
                fail(uuid, "malformed function call");
            }
            if (!children.empty()) {
                for (ObjUuid arg : flattenLeftList(children.front(), "FuncRPAREmsAst")) {
                    args.push_back(convertExpr(arg));
                }
            }
            return sysy::ast::Expr::call(loc_, functionName, std::move(args));
        }

        if (children.size() == 1) {
            return convertExpr(children.front());
        }
        if (children.size() == 2) {
            expect(children[0], "UnaryOpAst");
            return sysy::ast::Expr::unary(loc_, info(children[0]), convertExpr(children[1]));
        }
        fail(uuid, "malformed unary expression");
    }

    sysy::ast::ExprPtr convertBinaryExpr(ObjUuid uuid) {
        const auto &children = sons(uuid);
        if (children.size() == 1) {
            return convertExpr(children.front());
        }
        if (children.size() != 3) {
            fail(uuid, "malformed binary expression");
        }
        auto left = convertExpr(children[0]);
        auto right = convertExpr(children[2]);
        return sysy::ast::Expr::binary(left->loc, info(children[1]), std::move(left), std::move(right));
    }

    sysy::ast::ExprPtr convertLogicalExpr(ObjUuid uuid) {
        const auto &children = sons(uuid);
        if (children.size() == 1) {
            return convertExpr(children.front());
        }
        if (children.size() != 2) {
            fail(uuid, "malformed logical expression");
        }
        auto left = convertExpr(children[0]);
        auto right = convertExpr(children[1]);
        std::string op = type(uuid) == "LAndExpAst" ? "&&" : "||";
        return sysy::ast::Expr::binary(left->loc, std::move(op), std::move(left), std::move(right));
    }
};

} // namespace

namespace sysy {

std::unique_ptr<ast::Program> parsedProgram;
std::string currentInputFile;

std::unique_ptr<ast::Program> Driver::parseFile(const std::string &path) {
    ObjTree::ObjManager manager;
    currentInputFile = path;
    ObjUuid root = FrontEnd::parse_from_file(&manager, path);
    return ObjTreeAdapter(manager).convert(root);
}

} // namespace sysy
