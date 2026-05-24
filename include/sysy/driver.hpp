#pragma once

#include "sysy/ast.hpp"

#include <memory>
#include <string>

namespace sysy {

class Driver {
public:
    std::unique_ptr<ast::Program> parseFile(const std::string &path);
};

extern std::unique_ptr<ast::Program> parsedProgram;
extern std::string currentInputFile;

} // namespace sysy
