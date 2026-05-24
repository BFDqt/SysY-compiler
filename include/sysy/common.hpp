#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>

namespace sysy {

struct SourceLocation {
    int line = 1;
    int column = 1;
};

class CompileError : public std::runtime_error {
public:
    CompileError(SourceLocation loc, const std::string &message)
        : std::runtime_error(format(loc, message)), loc_(loc) {}

    SourceLocation location() const { return loc_; }

private:
    SourceLocation loc_;

    static std::string format(SourceLocation loc, const std::string &message) {
        return std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": " + message;
    }
};

enum class BaseType {
    Void,
    Int,
    Float,
    String,
};

inline std::string toString(BaseType type) {
    switch (type) {
    case BaseType::Void:
        return "void";
    case BaseType::Int:
        return "int";
    case BaseType::Float:
        return "float";
    case BaseType::String:
        return "string";
    }
    return "<unknown>";
}

inline int32_t floatToBits(float value) {
    static_assert(sizeof(float) == sizeof(int32_t), "float must be 32 bits");
    int32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(float));
    return bits;
}

inline float bitsToFloat(int32_t bits) {
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(float));
    return value;
}

struct ConstValue {
    BaseType type = BaseType::Int;
    int32_t intValue = 0;
    float floatValue = 0.0f;
    std::string stringValue;

    static ConstValue intOf(int32_t value) {
        ConstValue result;
        result.type = BaseType::Int;
        result.intValue = value;
        result.floatValue = static_cast<float>(value);
        return result;
    }

    static ConstValue floatOf(float value) {
        ConstValue result;
        result.type = BaseType::Float;
        result.floatValue = value;
        result.intValue = floatToBits(value);
        return result;
    }

    static ConstValue stringOf(std::string value) {
        ConstValue result;
        result.type = BaseType::String;
        result.stringValue = std::move(value);
        return result;
    }

    int32_t koopaBits() const {
        if (type == BaseType::Float) {
            return floatToBits(floatValue);
        }
        return intValue;
    }

    bool truthy() const {
        if (type == BaseType::Float) {
            return floatValue != 0.0f;
        }
        if (type == BaseType::String) {
            return !stringValue.empty();
        }
        return intValue != 0;
    }
};

inline ConstValue castConst(ConstValue value, BaseType target) {
    if (target == value.type || target == BaseType::Void || target == BaseType::String) {
        return value;
    }
    if (target == BaseType::Float) {
        if (value.type == BaseType::Int) {
            return ConstValue::floatOf(static_cast<float>(value.intValue));
        }
        return value;
    }
    if (target == BaseType::Int) {
        if (value.type == BaseType::Float) {
            return ConstValue::intOf(static_cast<int32_t>(value.floatValue));
        }
        return value;
    }
    return value;
}

} // namespace sysy
