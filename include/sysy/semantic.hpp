#pragma once

#include "sysy/ast.hpp"

#include <set>
#include <string>

namespace sysy {

struct KoopaResult {
    std::string text;
    std::set<std::string> floatHelpers;
};

class SemanticAnalyzer {
public:
    KoopaResult compile(const ast::Program &program);
};

} // namespace sysy
