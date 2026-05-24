#include "sysy/driver.hpp"

#include "sysy/common.hpp"

#include <cstdio>
#include <utility>

extern FILE *yyin;
extern int yyparse();
extern int yylineno;
extern int yycolumn;
extern void yyrestart(FILE *);

namespace sysy {

std::unique_ptr<ast::Program> parsedProgram;
std::string currentInputFile;

std::unique_ptr<ast::Program> Driver::parseFile(const std::string &path) {
    FILE *input = std::fopen(path.c_str(), "r");
    if (!input) {
        throw CompileError({1, 1}, "failed to open input file: " + path);
    }

    parsedProgram.reset();
    currentInputFile = path;
    yyin = input;
    yylineno = 1;
    yycolumn = 1;
    yyrestart(input);

    int status = yyparse();
    std::fclose(input);
    yyin = nullptr;

    if (status != 0 || !parsedProgram) {
        throw CompileError({1, 1}, "parse failed");
    }
    return std::move(parsedProgram);
}

} // namespace sysy
