#include "sysy/driver.hpp"
#include "sysy/riscv.hpp"
#include "sysy/semantic.hpp"

#include "sysy/common.hpp"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

struct Options {
    std::string mode;
    std::string input;
    std::string output;
};

Options parseArgs(int argc, char **argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-koopa" || arg == "-riscv") {
            options.mode = arg;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing output path after -o");
            }
            options.output = argv[++i];
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "usage: compiler (-koopa|-riscv) input.sy -o output\n";
            std::exit(0);
        } else if (options.input.empty()) {
            options.input = arg;
        } else {
            throw std::runtime_error("unexpected argument: " + arg);
        }
    }
    if (options.mode.empty()) {
        throw std::runtime_error("missing output mode: expected -koopa or -riscv");
    }
    if (options.input.empty()) {
        throw std::runtime_error("missing input .sy file");
    }
    if (options.output.empty()) {
        throw std::runtime_error("missing output path after -o");
    }
    return options;
}

void writeTextFile(const std::string &path, const std::string &text) {
    std::ofstream out(path);
    if (!out) {
        throw std::runtime_error("failed to open output file: " + path);
    }
    out << text;
}

} // namespace

int main(int argc, char **argv) {
    try {
        Options options = parseArgs(argc, argv);
        sysy::Driver driver;
        auto ast = driver.parseFile(options.input);
        sysy::SemanticAnalyzer semantic;
        sysy::KoopaResult koopa = semantic.compile(*ast);

        if (options.mode == "-koopa") {
            writeTextFile(options.output, koopa.text);
        } else if (options.mode == "-riscv") {
            std::ofstream out(options.output);
            if (!out) {
                throw std::runtime_error("failed to open output file: " + options.output);
            }
            sysy::backend::RiscVGenerator generator;
            generator.generate(koopa.text, koopa.floatHelpers, out);
        } else {
            throw std::runtime_error("unsupported output mode: " + options.mode);
        }
        return 0;
    } catch (const sysy::CompileError &error) {
        std::cerr << "compile error: " << error.what() << "\n";
    } catch (const std::exception &error) {
        std::cerr << "error: " << error.what() << "\n";
    }
    return 1;
}
