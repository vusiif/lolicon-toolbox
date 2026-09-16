#include "Base64Codec.h"
#include "base64.hpp"

std::string Base64Codec::encode(const std::string& input)
{
    if (input.empty()) {
        return "";
    }
    return base64::to_base64(input);
}

std::optional<std::string> Base64Codec::decode(const std::string& input)
{
    if (input.empty()) {
        return std::string();
    }

    try {
        return base64::from_base64(input);
    } catch (const std::runtime_error&) {
        return std::nullopt;
    }
}
