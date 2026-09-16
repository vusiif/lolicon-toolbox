#pragma once

#include <optional>
#include <string>

class Base64Codec {
public:
    static std::string encode(const std::string& input);
    static std::optional<std::string> decode(const std::string& input);
};
