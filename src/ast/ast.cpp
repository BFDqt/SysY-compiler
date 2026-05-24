#include "sysy/ast.hpp"

#include <iomanip>
#include <sstream>

namespace sysy::ast {

SourceLocation makeLoc(int line, int column) {
    return {line, column};
}

ExprPtr Expr::intLiteral(SourceLocation loc, int32_t value, std::string raw) {
    auto expr = std::make_unique<Expr>();
    expr->loc = loc;
    expr->kind = ExprKind::IntLiteral;
    expr->intValue = value;
    expr->floatValue = static_cast<float>(value);
    expr->text = std::move(raw);
    return expr;
}

ExprPtr Expr::floatLiteral(SourceLocation loc, float value, std::string raw) {
    auto expr = std::make_unique<Expr>();
    expr->loc = loc;
    expr->kind = ExprKind::FloatLiteral;
    expr->floatValue = value;
    expr->intValue = floatToBits(value);
    expr->text = std::move(raw);
    return expr;
}

ExprPtr Expr::stringLiteral(SourceLocation loc, std::string value) {
    auto expr = std::make_unique<Expr>();
    expr->loc = loc;
    expr->kind = ExprKind::StringLiteral;
    expr->text = std::move(value);
    return expr;
}

ExprPtr Expr::lval(SourceLocation loc, std::string name, std::vector<ExprPtr> indices) {
    auto expr = std::make_unique<Expr>();
    expr->loc = loc;
    expr->kind = ExprKind::LVal;
    expr->name = std::move(name);
    expr->indices = std::move(indices);
    return expr;
}

ExprPtr Expr::unary(SourceLocation loc, std::string op, ExprPtr child) {
    auto expr = std::make_unique<Expr>();
    expr->loc = loc;
    expr->kind = ExprKind::Unary;
    expr->op = std::move(op);
    expr->lhs = std::move(child);
    return expr;
}

ExprPtr Expr::binary(SourceLocation loc, std::string op, ExprPtr left, ExprPtr right) {
    auto expr = std::make_unique<Expr>();
    expr->loc = loc;
    expr->kind = ExprKind::Binary;
    expr->op = std::move(op);
    expr->lhs = std::move(left);
    expr->rhs = std::move(right);
    return expr;
}

ExprPtr Expr::call(SourceLocation loc, std::string name, std::vector<ExprPtr> args) {
    auto expr = std::make_unique<Expr>();
    expr->loc = loc;
    expr->kind = ExprKind::Call;
    expr->name = std::move(name);
    expr->args = std::move(args);
    return expr;
}

std::string escapeString(const std::string &value) {
    std::ostringstream os;
    for (unsigned char c : value) {
        switch (c) {
        case '\n':
            os << "\\n";
            break;
        case '\t':
            os << "\\t";
            break;
        case '\r':
            os << "\\r";
            break;
        case '\\':
            os << "\\\\";
            break;
        case '"':
            os << "\\\"";
            break;
        default:
            if (c < 32 || c >= 127) {
                os << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
            } else {
                os << static_cast<char>(c);
            }
            break;
        }
    }
    return os.str();
}

std::string decodeSysYString(const std::string &raw) {
    std::string out;
    for (size_t i = 0; i < raw.size(); ++i) {
        char c = raw[i];
        if (c != '\\' || i + 1 >= raw.size()) {
            out.push_back(c);
            continue;
        }
        char escaped = raw[++i];
        switch (escaped) {
        case 'n':
            out.push_back('\n');
            break;
        case 't':
            out.push_back('\t');
            break;
        case 'r':
            out.push_back('\r');
            break;
        case '0':
            out.push_back('\0');
            break;
        case '\\':
            out.push_back('\\');
            break;
        case '"':
            out.push_back('"');
            break;
        default:
            out.push_back(escaped);
            break;
        }
    }
    return out;
}

namespace {

void indent(std::ostream &os, int depth) {
    for (int i = 0; i < depth; ++i) {
        os << "  ";
    }
}

void dumpExpr(const Expr &expr, std::ostream &os, int depth) {
    indent(os, depth);
    switch (expr.kind) {
    case ExprKind::IntLiteral:
        os << "IntLiteral " << expr.intValue << '\n';
        break;
    case ExprKind::FloatLiteral:
        os << "FloatLiteral " << expr.floatValue << '\n';
        break;
    case ExprKind::StringLiteral:
        os << "StringLiteral \"" << escapeString(expr.text) << "\"\n";
        break;
    case ExprKind::LVal:
        os << "LVal " << expr.name << '\n';
        for (const auto &index : expr.indices) {
            dumpExpr(*index, os, depth + 1);
        }
        break;
    case ExprKind::Unary:
        os << "Unary " << expr.op << '\n';
        dumpExpr(*expr.lhs, os, depth + 1);
        break;
    case ExprKind::Binary:
        os << "Binary " << expr.op << '\n';
        dumpExpr(*expr.lhs, os, depth + 1);
        dumpExpr(*expr.rhs, os, depth + 1);
        break;
    case ExprKind::Call:
        os << "Call " << expr.name << '\n';
        for (const auto &arg : expr.args) {
            dumpExpr(*arg, os, depth + 1);
        }
        break;
    }
}

void dumpInit(const InitVal &init, std::ostream &os, int depth) {
    indent(os, depth);
    if (!init.isList) {
        os << "InitExpr\n";
        dumpExpr(*init.expr, os, depth + 1);
        return;
    }
    os << "InitList\n";
    for (const auto &element : init.elements) {
        dumpInit(*element, os, depth + 1);
    }
}

void dumpBlock(const Block &block, std::ostream &os, int depth);

void dumpStmt(const Stmt &stmt, std::ostream &os, int depth) {
    indent(os, depth);
    switch (stmt.kind) {
    case StmtKind::Empty:
        os << "EmptyStmt\n";
        break;
    case StmtKind::Assign:
        os << "Assign\n";
        dumpExpr(*stmt.lval, os, depth + 1);
        dumpExpr(*stmt.expr, os, depth + 1);
        break;
    case StmtKind::Expr:
        os << "ExprStmt\n";
        dumpExpr(*stmt.expr, os, depth + 1);
        break;
    case StmtKind::Block:
        os << "BlockStmt\n";
        dumpBlock(*stmt.block, os, depth + 1);
        break;
    case StmtKind::If:
        os << "If\n";
        dumpExpr(*stmt.cond, os, depth + 1);
        dumpStmt(*stmt.thenStmt, os, depth + 1);
        if (stmt.elseStmt) {
            dumpStmt(*stmt.elseStmt, os, depth + 1);
        }
        break;
    case StmtKind::While:
        os << "While\n";
        dumpExpr(*stmt.cond, os, depth + 1);
        dumpStmt(*stmt.body, os, depth + 1);
        break;
    case StmtKind::Break:
        os << "Break\n";
        break;
    case StmtKind::Continue:
        os << "Continue\n";
        break;
    case StmtKind::Return:
        os << "Return\n";
        if (stmt.expr) {
            dumpExpr(*stmt.expr, os, depth + 1);
        }
        break;
    }
}

void dumpDecl(const Decl &decl, std::ostream &os, int depth) {
    indent(os, depth);
    os << (decl.isConst ? "ConstDecl " : "VarDecl ") << toString(decl.baseType) << '\n';
    for (const auto &def : decl.definitions) {
        indent(os, depth + 1);
        os << "Def " << def.name << '\n';
        for (const auto &dim : def.dimensions) {
            dumpExpr(*dim, os, depth + 2);
        }
        if (def.init) {
            dumpInit(*def.init, os, depth + 2);
        }
    }
}

void dumpBlock(const Block &block, std::ostream &os, int depth) {
    indent(os, depth);
    os << "Block\n";
    for (const auto &item : block.items) {
        if (item.decl) {
            dumpDecl(*item.decl, os, depth + 1);
        } else if (item.stmt) {
            dumpStmt(*item.stmt, os, depth + 1);
        }
    }
}

} // namespace

void dumpProgram(const Program &program, std::ostream &os) {
    os << "Program\n";
    for (const auto &item : program.items) {
        if (item.kind == TopLevelKind::Decl) {
            dumpDecl(*item.decl, os, 1);
        } else {
            indent(os, 1);
            os << "FuncDef " << item.func->name << " -> " << toString(item.func->returnType) << '\n';
            for (const auto &param : item.func->params) {
                indent(os, 2);
                os << "Param " << param.name << ": " << toString(param.baseType);
                if (param.isArray) {
                    os << "[]";
                }
                os << '\n';
            }
            dumpBlock(*item.func->body, os, 2);
        }
    }
}

} // namespace sysy::ast
