%{
#include "sysy/ast.hpp"
#include "sysy/driver.hpp"

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

int yylex();
void yyerror(const char *message);

template <typename T>
static std::unique_ptr<T> own(T *ptr) {
    return std::unique_ptr<T>(ptr);
}

template <typename T>
static std::vector<T> takeVector(std::vector<T> *ptr) {
    std::vector<T> result = std::move(*ptr);
    delete ptr;
    return result;
}

static std::string takeString(char *text) {
    std::string result(text);
    std::free(text);
    return result;
}
%}

%define parse.error verbose
%locations

%code requires {
#include "sysy/ast.hpp"
#include <vector>
}

%union {
    int int_val;
    float float_val;
    char *str_val;
    sysy::BaseType type;
    sysy::ast::Expr *expr;
    sysy::ast::InitVal *init;
    sysy::ast::Decl *decl;
    sysy::ast::VarDef *var_def;
    sysy::ast::FuncParam *param;
    sysy::ast::FuncDef *func;
    sysy::ast::Block *block;
    sysy::ast::BlockItem *block_item;
    sysy::ast::Stmt *stmt;
    sysy::ast::TopLevel *top;
    std::vector<sysy::ast::TopLevel> *top_items;
    std::vector<sysy::ast::VarDef> *var_defs;
    std::vector<sysy::ast::ExprPtr> *exprs;
    std::vector<sysy::ast::InitValPtr> *inits;
    std::vector<sysy::ast::FuncParam> *params;
    std::vector<sysy::ast::BlockItem> *block_items;
}

%token KW_CONST "const"
%token KW_INT "int"
%token KW_FLOAT "float"
%token KW_VOID "void"
%token KW_IF "if"
%token KW_ELSE "else"
%token KW_WHILE "while"
%token KW_BREAK "break"
%token KW_CONTINUE "continue"
%token KW_RETURN "return"

%token <str_val> IDENT
%token <int_val> INT_LITERAL
%token <float_val> FLOAT_LITERAL
%token <str_val> STRING_LITERAL

%token EQ "=="
%token NE "!="
%token LE "<="
%token GE ">="
%token AND "&&"
%token OR "||"

%type <top_items> CompItems
%type <top> TopLevel
%type <decl> Decl ConstDecl VarDecl
%type <var_defs> ConstDefList VarDefList VarDefSuffixList
%type <var_def> ConstDef VarDef
%type <type> BType
%type <exprs> Dimensions ParamDimensions FuncRParamsOpt FuncRParams IndexList
%type <init> InitVal VarInitOpt
%type <inits> InitValListOpt InitValList
%type <params> FuncFParamsOpt FuncFParams
%type <param> FuncFParam
%type <block> Block
%type <block_items> BlockItems
%type <block_item> BlockItem
%type <stmt> Stmt
%type <expr> Exp Cond LVal PrimaryExp UnaryExp MulExp AddExp RelExp EqExp LAndExp LOrExp

%nonassoc LOWER_THAN_ELSE
%nonassoc KW_ELSE

%start Program

%%

Program
    : CompItems
      {
          auto program = std::make_unique<sysy::ast::Program>();
          program->items = takeVector($1);
          sysy::parsedProgram = std::move(program);
      }
    ;

CompItems
    : /* empty */
      { $$ = new std::vector<sysy::ast::TopLevel>(); }
    | CompItems TopLevel
      {
          $1->push_back(std::move(*$2));
          delete $2;
          $$ = $1;
      }
    ;

TopLevel
    : KW_CONST BType ConstDefList ';'
      {
          $$ = new sysy::ast::TopLevel();
          auto decl = std::make_unique<sysy::ast::Decl>();
          decl->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          decl->isConst = true;
          decl->baseType = $2;
          decl->definitions = takeVector($3);
          $$->loc = decl->loc;
          $$->kind = sysy::ast::TopLevelKind::Decl;
          $$->decl = std::move(decl);
      }
    | BType IDENT '(' FuncFParamsOpt ')' Block
      {
          $$ = new sysy::ast::TopLevel();
          auto func = std::make_unique<sysy::ast::FuncDef>();
          func->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          func->returnType = $1;
          func->name = takeString($2);
          func->params = takeVector($4);
          func->body.reset($6);
          $$->loc = func->loc;
          $$->kind = sysy::ast::TopLevelKind::FuncDef;
          $$->func = std::move(func);
      }
    | KW_VOID IDENT '(' FuncFParamsOpt ')' Block
      {
          $$ = new sysy::ast::TopLevel();
          auto func = std::make_unique<sysy::ast::FuncDef>();
          func->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          func->returnType = sysy::BaseType::Void;
          func->name = takeString($2);
          func->params = takeVector($4);
          func->body.reset($6);
          $$->loc = func->loc;
          $$->kind = sysy::ast::TopLevelKind::FuncDef;
          $$->func = std::move(func);
      }
    | BType IDENT Dimensions VarInitOpt VarDefSuffixList ';'
      {
          $$ = new sysy::ast::TopLevel();
          auto decl = std::make_unique<sysy::ast::Decl>();
          decl->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          decl->isConst = false;
          decl->baseType = $1;
          sysy::ast::VarDef first;
          first.loc = sysy::ast::makeLoc(@2.first_line, @2.first_column);
          first.name = takeString($2);
          first.dimensions = takeVector($3);
          first.init.reset($4);
          decl->definitions = takeVector($5);
          decl->definitions.insert(decl->definitions.begin(), std::move(first));
          $$->loc = decl->loc;
          $$->kind = sysy::ast::TopLevelKind::Decl;
          $$->decl = std::move(decl);
      }
    ;

Decl
    : ConstDecl { $$ = $1; }
    | VarDecl { $$ = $1; }
    ;

ConstDecl
    : KW_CONST BType ConstDefList ';'
      {
          $$ = new sysy::ast::Decl();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->isConst = true;
          $$->baseType = $2;
          $$->definitions = takeVector($3);
      }
    ;

VarDecl
    : BType VarDefList ';'
      {
          $$ = new sysy::ast::Decl();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->isConst = false;
          $$->baseType = $1;
          $$->definitions = takeVector($2);
      }
    ;

BType
    : KW_INT { $$ = sysy::BaseType::Int; }
    | KW_FLOAT { $$ = sysy::BaseType::Float; }
    ;

ConstDefList
    : ConstDef
      {
          $$ = new std::vector<sysy::ast::VarDef>();
          $$->push_back(std::move(*$1));
          delete $1;
      }
    | ConstDefList ',' ConstDef
      {
          $1->push_back(std::move(*$3));
          delete $3;
          $$ = $1;
      }
    ;

ConstDef
    : IDENT Dimensions '=' InitVal
      {
          $$ = new sysy::ast::VarDef();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->name = takeString($1);
          $$->dimensions = takeVector($2);
          $$->init.reset($4);
      }
    ;

VarDefList
    : VarDef
      {
          $$ = new std::vector<sysy::ast::VarDef>();
          $$->push_back(std::move(*$1));
          delete $1;
      }
    | VarDefList ',' VarDef
      {
          $1->push_back(std::move(*$3));
          delete $3;
          $$ = $1;
      }
    ;

VarDef
    : IDENT Dimensions
      {
          $$ = new sysy::ast::VarDef();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->name = takeString($1);
          $$->dimensions = takeVector($2);
      }
    | IDENT Dimensions '=' InitVal
      {
          $$ = new sysy::ast::VarDef();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->name = takeString($1);
          $$->dimensions = takeVector($2);
          $$->init.reset($4);
      }
    ;

VarInitOpt
    : /* empty */
      { $$ = nullptr; }
    | '=' InitVal
      { $$ = $2; }
    ;

VarDefSuffixList
    : /* empty */
      { $$ = new std::vector<sysy::ast::VarDef>(); }
    | VarDefSuffixList ',' VarDef
      {
          $1->push_back(std::move(*$3));
          delete $3;
          $$ = $1;
      }
    ;

Dimensions
    : /* empty */
      { $$ = new std::vector<sysy::ast::ExprPtr>(); }
    | Dimensions '[' Exp ']'
      {
          $1->push_back(own($3));
          $$ = $1;
      }
    ;

InitVal
    : Exp
      {
          $$ = new sysy::ast::InitVal();
          $$->loc = $1->loc;
          $$->isList = false;
          $$->expr.reset($1);
      }
    | '{' InitValListOpt '}'
      {
          $$ = new sysy::ast::InitVal();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->isList = true;
          $$->elements = takeVector($2);
      }
    ;

InitValListOpt
    : /* empty */
      { $$ = new std::vector<sysy::ast::InitValPtr>(); }
    | InitValList { $$ = $1; }
    ;

InitValList
    : InitVal
      {
          $$ = new std::vector<sysy::ast::InitValPtr>();
          $$->push_back(own($1));
      }
    | InitValList ',' InitVal
      {
          $1->push_back(own($3));
          $$ = $1;
      }
    ;

FuncFParamsOpt
    : /* empty */
      { $$ = new std::vector<sysy::ast::FuncParam>(); }
    | FuncFParams { $$ = $1; }
    ;

FuncFParams
    : FuncFParam
      {
          $$ = new std::vector<sysy::ast::FuncParam>();
          $$->push_back(std::move(*$1));
          delete $1;
      }
    | FuncFParams ',' FuncFParam
      {
          $1->push_back(std::move(*$3));
          delete $3;
          $$ = $1;
      }
    ;

FuncFParam
    : BType IDENT
      {
          $$ = new sysy::ast::FuncParam();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->baseType = $1;
          $$->name = takeString($2);
          $$->isArray = false;
      }
    | BType IDENT '[' ']' ParamDimensions
      {
          $$ = new sysy::ast::FuncParam();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->baseType = $1;
          $$->name = takeString($2);
          $$->isArray = true;
          $$->dimensionsAfterFirst = takeVector($5);
      }
    ;

ParamDimensions
    : /* empty */
      { $$ = new std::vector<sysy::ast::ExprPtr>(); }
    | ParamDimensions '[' Exp ']'
      {
          $1->push_back(own($3));
          $$ = $1;
      }
    ;

Block
    : '{' BlockItems '}'
      {
          $$ = new sysy::ast::Block();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->items = takeVector($2);
      }
    ;

BlockItems
    : /* empty */
      { $$ = new std::vector<sysy::ast::BlockItem>(); }
    | BlockItems BlockItem
      {
          $1->push_back(std::move(*$2));
          delete $2;
          $$ = $1;
      }
    ;

BlockItem
    : Decl
      {
          $$ = new sysy::ast::BlockItem();
          $$->loc = $1->loc;
          $$->decl.reset($1);
      }
    | Stmt
      {
          $$ = new sysy::ast::BlockItem();
          $$->loc = $1->loc;
          $$->stmt.reset($1);
      }
    ;

Stmt
    : ';'
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::Empty;
      }
    | LVal '=' Exp ';'
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = $1->loc;
          $$->kind = sysy::ast::StmtKind::Assign;
          $$->lval.reset($1);
          $$->expr.reset($3);
      }
    | Exp ';'
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = $1->loc;
          $$->kind = sysy::ast::StmtKind::Expr;
          $$->expr.reset($1);
      }
    | Block
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = $1->loc;
          $$->kind = sysy::ast::StmtKind::Block;
          $$->block.reset($1);
      }
    | KW_IF '(' Cond ')' Stmt %prec LOWER_THAN_ELSE
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::If;
          $$->cond.reset($3);
          $$->thenStmt.reset($5);
      }
    | KW_IF '(' Cond ')' Stmt KW_ELSE Stmt
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::If;
          $$->cond.reset($3);
          $$->thenStmt.reset($5);
          $$->elseStmt.reset($7);
      }
    | KW_WHILE '(' Cond ')' Stmt
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::While;
          $$->cond.reset($3);
          $$->body.reset($5);
      }
    | KW_BREAK ';'
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::Break;
      }
    | KW_CONTINUE ';'
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::Continue;
      }
    | KW_RETURN ';'
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::Return;
      }
    | KW_RETURN Exp ';'
      {
          $$ = new sysy::ast::Stmt();
          $$->loc = sysy::ast::makeLoc(@1.first_line, @1.first_column);
          $$->kind = sysy::ast::StmtKind::Return;
          $$->expr.reset($2);
      }
    ;

Exp
    : AddExp { $$ = $1; }
    ;

Cond
    : LOrExp { $$ = $1; }
    ;

LVal
    : IDENT IndexList
      {
          auto name = takeString($1);
          auto indices = takeVector($2);
          $$ = sysy::ast::Expr::lval(sysy::ast::makeLoc(@1.first_line, @1.first_column), std::move(name), std::move(indices)).release();
      }
    ;

IndexList
    : /* empty */
      { $$ = new std::vector<sysy::ast::ExprPtr>(); }
    | IndexList '[' Exp ']'
      {
          $1->push_back(own($3));
          $$ = $1;
      }
    ;

PrimaryExp
    : '(' Exp ')' { $$ = $2; }
    | LVal { $$ = $1; }
    | INT_LITERAL
      { $$ = sysy::ast::Expr::intLiteral(sysy::ast::makeLoc(@1.first_line, @1.first_column), $1, std::to_string($1)).release(); }
    | FLOAT_LITERAL
      { $$ = sysy::ast::Expr::floatLiteral(sysy::ast::makeLoc(@1.first_line, @1.first_column), $1, std::to_string($1)).release(); }
    | STRING_LITERAL
      {
          auto value = sysy::ast::decodeSysYString(takeString($1));
          $$ = sysy::ast::Expr::stringLiteral(sysy::ast::makeLoc(@1.first_line, @1.first_column), std::move(value)).release();
      }
    ;

UnaryExp
    : PrimaryExp { $$ = $1; }
    | IDENT '(' FuncRParamsOpt ')'
      {
          auto name = takeString($1);
          auto args = takeVector($3);
          $$ = sysy::ast::Expr::call(sysy::ast::makeLoc(@1.first_line, @1.first_column), std::move(name), std::move(args)).release();
      }
    | '+' UnaryExp
      { $$ = sysy::ast::Expr::unary(sysy::ast::makeLoc(@1.first_line, @1.first_column), "+", own($2)).release(); }
    | '-' UnaryExp
      { $$ = sysy::ast::Expr::unary(sysy::ast::makeLoc(@1.first_line, @1.first_column), "-", own($2)).release(); }
    | '!' UnaryExp
      { $$ = sysy::ast::Expr::unary(sysy::ast::makeLoc(@1.first_line, @1.first_column), "!", own($2)).release(); }
    ;

FuncRParamsOpt
    : /* empty */
      { $$ = new std::vector<sysy::ast::ExprPtr>(); }
    | FuncRParams { $$ = $1; }
    ;

FuncRParams
    : Exp
      {
          $$ = new std::vector<sysy::ast::ExprPtr>();
          $$->push_back(own($1));
      }
    | FuncRParams ',' Exp
      {
          $1->push_back(own($3));
          $$ = $1;
      }
    ;

MulExp
    : UnaryExp { $$ = $1; }
    | MulExp '*' UnaryExp
      { $$ = sysy::ast::Expr::binary($1->loc, "*", own($1), own($3)).release(); }
    | MulExp '/' UnaryExp
      { $$ = sysy::ast::Expr::binary($1->loc, "/", own($1), own($3)).release(); }
    | MulExp '%' UnaryExp
      { $$ = sysy::ast::Expr::binary($1->loc, "%", own($1), own($3)).release(); }
    ;

AddExp
    : MulExp { $$ = $1; }
    | AddExp '+' MulExp
      { $$ = sysy::ast::Expr::binary($1->loc, "+", own($1), own($3)).release(); }
    | AddExp '-' MulExp
      { $$ = sysy::ast::Expr::binary($1->loc, "-", own($1), own($3)).release(); }
    ;

RelExp
    : AddExp { $$ = $1; }
    | RelExp '<' AddExp
      { $$ = sysy::ast::Expr::binary($1->loc, "<", own($1), own($3)).release(); }
    | RelExp '>' AddExp
      { $$ = sysy::ast::Expr::binary($1->loc, ">", own($1), own($3)).release(); }
    | RelExp LE AddExp
      { $$ = sysy::ast::Expr::binary($1->loc, "<=", own($1), own($3)).release(); }
    | RelExp GE AddExp
      { $$ = sysy::ast::Expr::binary($1->loc, ">=", own($1), own($3)).release(); }
    ;

EqExp
    : RelExp { $$ = $1; }
    | EqExp EQ RelExp
      { $$ = sysy::ast::Expr::binary($1->loc, "==", own($1), own($3)).release(); }
    | EqExp NE RelExp
      { $$ = sysy::ast::Expr::binary($1->loc, "!=", own($1), own($3)).release(); }
    ;

LAndExp
    : EqExp { $$ = $1; }
    | LAndExp AND EqExp
      { $$ = sysy::ast::Expr::binary($1->loc, "&&", own($1), own($3)).release(); }
    ;

LOrExp
    : LAndExp { $$ = $1; }
    | LOrExp OR LAndExp
      { $$ = sysy::ast::Expr::binary($1->loc, "||", own($1), own($3)).release(); }
    ;

%%

void yyerror(const char *message) {
    throw sysy::CompileError(sysy::ast::makeLoc(yylloc.first_line, yylloc.first_column), message);
}
