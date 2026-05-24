#include <cstdio>
#include <iostream>
#include <memory>
#include <string>

#include "../../../ObjTree/ObjManager.h"
#include "sysy/common.hpp"

using namespace std;

extern FILE *yyin;
extern int yyparse(ObjTree::ObjManager* obj_manager);
extern int yylex_destroy();
extern int line_number;
extern void yyrestart(FILE *);

typedef std::string String;

ObjTree::ObjUuid yacc_lex_get_ast(ObjTree::ObjManager* obj_manager, String input_file) {
    // read input file
    FILE *input = fopen(input_file.c_str(), "r");
    if (!input) {
        throw sysy::CompileError({1, 1}, "failed to open input file: " + input_file);
    }

    // scan and parse input file into AST
    yyin = input;
    line_number = 1;
    yyrestart(input);

    int ret = 0;
    try {
        ret = yyparse(obj_manager);
    } catch (...) {
        fclose(input);
        yyin = nullptr;
        yylex_destroy();
        throw;
    }

    fclose(input);
    yyin = nullptr;
    yylex_destroy();

    if (ret != 0) {
        throw sysy::CompileError({line_number, 1}, "parse failed");
    }

    return obj_manager->get_last_uuid();
}

#ifdef TEST_YACC_LEX

int main() {
    std::unique_ptr<ObjTree::ObjManager> obj_manager(new ObjTree::ObjManager());
    ObjTree::ObjUuid ans = yacc_lex_get_ast(obj_manager.get(), "test.sy");
    std::cout << obj_manager->get_node_info_string(ans) << std::endl;
    return 0;
}

#endif
