#pragma once

#include <iosfwd>
#include <set>
#include <string>

namespace sysy::backend {

class RiscVGenerator {
public:
    void generate(const std::string &koopaText, const std::set<std::string> &floatHelpers, std::ostream &os);
};

} // namespace sysy::backend
