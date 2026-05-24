%option noyywrap
%option nounput
%option noinput
%option yylineno

%{
#include "sysy/ast.hpp"
#include "sysy/common.hpp"

#include "sysy_parser.hpp"

#include <cstdlib>
#include <cstring>
#include <string>

int yycolumn = 1;

static void updateBlockCommentColumn(const char *text, int length) {
    int column = yylloc.first_column;
    for (int i = 0; i < length; ++i) {
        if (text[i] == '\n') {
            column = 1;
        } else {
            ++column;
        }
    }
    yycolumn = column;
}

#define YY_USER_ACTION                                      \
    do {                                                    \
        yylloc.first_line = yylloc.last_line = yylineno;    \
        yylloc.first_column = yycolumn;                     \
        yylloc.last_column = yycolumn + yyleng - 1;         \
        yycolumn += yyleng;                                 \
    } while (0);
%}

DIGIT       [0-9]
HEX         [0-9a-fA-F]
IDENT       [A-Za-z_][A-Za-z0-9_]*
INT         (0[xX]{HEX}+|0[0-7]*|[1-9]{DIGIT}*)
EXP         [eE][+-]?{DIGIT}+
HEXP        [pP][+-]?{DIGIT}+
DFLOAT      (({DIGIT}+"."{DIGIT}*|"."{DIGIT}+){EXP}?|{DIGIT}+{EXP})[fFlL]*
HFLOAT      0[xX](({HEX}+"."{HEX}*|"."{HEX}+|{HEX}+){HEXP})[fFlL]*
STRING      \"([^\\\"\n]|\\.)*\"

%%

[ \t\r]+                    { }
\n                          { yycolumn = 1; }
"//"[^\n]*                  { }
"/*"([^*]|\*+[^*/])*\*+\/  { updateBlockCommentColumn(yytext, yyleng); }
"/*"                        {
                                throw sysy::CompileError(sysy::ast::makeLoc(yylloc.first_line, yylloc.first_column),
                                                         "unterminated block comment");
                            }

"const"                     { return KW_CONST; }
"int"                       { return KW_INT; }
"float"                     { return KW_FLOAT; }
"void"                      { return KW_VOID; }
"if"                        { return KW_IF; }
"else"                      { return KW_ELSE; }
"while"                     { return KW_WHILE; }
"break"                     { return KW_BREAK; }
"continue"                  { return KW_CONTINUE; }
"return"                    { return KW_RETURN; }

"=="                        { return EQ; }
"!="                        { return NE; }
"<="                        { return LE; }
">="                        { return GE; }
"&&"                        { return AND; }
"||"                        { return OR; }

{HFLOAT}                    { yylval.float_val = std::strtof(yytext, nullptr); return FLOAT_LITERAL; }
{DFLOAT}                    { yylval.float_val = std::strtof(yytext, nullptr); return FLOAT_LITERAL; }
{INT}                       { yylval.int_val = static_cast<int>(std::strtol(yytext, nullptr, 0)); return INT_LITERAL; }
{STRING}                    {
                                std::string raw(yytext + 1, yyleng - 2);
                                yylval.str_val = static_cast<char *>(std::malloc(raw.size() + 1));
                                std::memcpy(yylval.str_val, raw.c_str(), raw.size() + 1);
                                return STRING_LITERAL;
                            }
\"([^\\\"\n]|\\.)*          {
                                throw sysy::CompileError(sysy::ast::makeLoc(yylloc.first_line, yylloc.first_column),
                                                         "unterminated string literal");
                            }
{IDENT}                     {
                                yylval.str_val = static_cast<char *>(std::malloc(yyleng + 1));
                                std::memcpy(yylval.str_val, yytext, yyleng);
                                yylval.str_val[yyleng] = '\0';
                                return IDENT;
                            }

[+\-*/%=<>{}()[\],;!]       { return yytext[0]; }

.                           {
                                throw sysy::CompileError(sysy::ast::makeLoc(yylloc.first_line, yylloc.first_column),
                                                         std::string("unexpected character '") + yytext[0] + "'");
                            }

%%
