%option noyywrap
%option nounput
%option noinput

%{

#include <cstdlib>
#include <cstring>
#include <string>
#include "StrDump.h"
#include "sysy/common.hpp"

// 因为 Flex 会用到 Bison 中关于 token 的定义
// 所以需要 include Bison 生成的头文件
#include "sysy.tab.hpp"
extern int line_number;

using namespace std;

%}

/* 空白符和注释 */
LineComment   "//".*

/* 整数字面量 */
Digit         [0-9]
HexDigit      [0-9a-fA-F]
Decimal       [1-9][0-9]*
Octal         0[0-7]*
Hexadecimal   0[xX][0-9a-fA-F]+
Exp           [eE][+-]?{Digit}+
HexExp        [pP][+-]?{Digit}+
DecimalFloat  (({Digit}+"."{Digit}*|"."{Digit}+){Exp}?|{Digit}+{Exp})[fFlL]*
HexFloat      0[xX](({HexDigit}+"."{HexDigit}*|"."{HexDigit}+|{HexDigit}+){HexExp})[fFlL]*
StringLiteral \"([^\\\"\n]|\\.)*\"

%x C_COMMENT
%x C_SINGLE_LINE_COMMENT

%%

"/*"            { BEGIN(C_COMMENT); }
<C_COMMENT>"*/" { BEGIN(INITIAL); /* match c multi-line comment */ }
<C_COMMENT>\n   { line_number += 1; }
<C_COMMENT>.    { }
<C_COMMENT><<EOF>> {
                    throw sysy::CompileError({line_number, 1}, "unterminated block comment");
                  }

{LineComment}   { }

int/[[:space:]]*[_a-zA-Z0-9]+\(    { yylval.raw_string = merged_str_dump(yytext); return KEYWORD_INT_FUNC    ; }
float/[[:space:]]*[_a-zA-Z0-9]+\(  { yylval.raw_string = merged_str_dump(yytext); return KEYWORD_FLOAT_FUNC  ; }
\;                           { yylval.raw_string = merged_str_dump(yytext); return SEMICOLON           ; }
\,                           { yylval.raw_string = merged_str_dump(yytext); return COMMA               ; }
\=                           { yylval.raw_string = merged_str_dump(yytext); return EQUAL               ; }
\[                           { yylval.raw_string = merged_str_dump(yytext); return LBRACKET            ; }
\]                           { yylval.raw_string = merged_str_dump(yytext); return RBRACKET            ; }
\{                           { yylval.raw_string = merged_str_dump(yytext); return LBRACE              ; }
\}                           { yylval.raw_string = merged_str_dump(yytext); return RBRACE              ; }
\(                           { yylval.raw_string = merged_str_dump(yytext); return LPARE               ; }
\)                           { yylval.raw_string = merged_str_dump(yytext); return RPARE               ; }
\!                           { yylval.raw_string = merged_str_dump(yytext); return OPE_NOT             ; }
\+                           { yylval.raw_string = merged_str_dump(yytext); return OPE_ADD             ; }
\-                           { yylval.raw_string = merged_str_dump(yytext); return OPE_SUB             ; }
\*                           { yylval.raw_string = merged_str_dump(yytext); return OPE_MUL             ; }
\/                           { yylval.raw_string = merged_str_dump(yytext); return OPE_DIV             ; }
\%                           { yylval.raw_string = merged_str_dump(yytext); return OPE_MOD             ; }
"&&"                         { yylval.raw_string = merged_str_dump(yytext); return OPE_AND             ; }
"||"                         { yylval.raw_string = merged_str_dump(yytext); return OPE_OR              ; }
"=="                         { yylval.raw_string = merged_str_dump(yytext); return OPE_EQU             ; }
"!="                         { yylval.raw_string = merged_str_dump(yytext); return OPE_NEQ             ; }
"<="                         { yylval.raw_string = merged_str_dump(yytext); return OPE_LEQ             ; }
">="                         { yylval.raw_string = merged_str_dump(yytext); return OPE_GEQ             ; }
"<"                          { yylval.raw_string = merged_str_dump(yytext); return OPE_LSS             ; }
">"                          { yylval.raw_string = merged_str_dump(yytext); return OPE_GTR             ; }
{HexFloat}                   { yylval.raw_string = merged_str_dump(yytext); return FLOAT_CONST         ; }
{DecimalFloat}               { yylval.raw_string = merged_str_dump(yytext); return FLOAT_CONST         ; }
{Decimal}                    { yylval.raw_string = merged_str_dump(yytext); return DECIMAL_CONST       ; }
{Octal}                      { yylval.raw_string = merged_str_dump(yytext); return OCTAL_CONST         ; }
{Hexadecimal}                { yylval.raw_string = merged_str_dump(yytext); return HEXADECIMAL_CONST   ; }
{StringLiteral}              {
                                std::string raw(yytext + 1, yyleng - 2);
                                yylval.raw_string = merged_str_dump(raw.c_str());
                                return STRING_LITERAL;
                             }
\"([^\\\"\n]|\\.)*           {
                                throw sysy::CompileError({line_number, 1}, "unterminated string literal");
                             }

[_a-zA-Z][_a-zA-Z0-9]*  { yylval.raw_string = merged_str_dump(yytext); 
        int typeId = IDENTIFIER; 
        if(0 == strcmp("const", yytext))    { typeId = KEYWORD_CONST;    }
        if(0 == strcmp("int", yytext))      { typeId = KEYWORD_INT;      }
        if(0 == strcmp("float", yytext))    { typeId = KEYWORD_FLOAT;    }
        if(0 == strcmp("void", yytext))     { typeId = KEYWORD_VOID;     }
        if(0 == strcmp("if", yytext))       { typeId = KEYWORD_IF;       }
        if(0 == strcmp("else", yytext))     { typeId = KEYWORD_ELSE;     }
        if(0 == strcmp("while", yytext))    { typeId = KEYWORD_WHILE;    }
        if(0 == strcmp("continue", yytext)) { typeId = KEYWORD_CONTINUE; }
        if(0 == strcmp("break", yytext))    { typeId = KEYWORD_BREAK;    }
        if(0 == strcmp("return", yytext))   { typeId = KEYWORD_RETURN;   }
        return typeId; }

[ \t]+ {  } 
\n     { line_number += 1; } /* new line */
.      {
         throw sysy::CompileError({line_number, 1},
                                  std::string("unexpected character '") + yytext[0] + "'");
       }
%%
