#pragma once

#include <charconv>
#include <cstddef>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace papyrus_console {
    inline constexpr std::size_t MaximumCommandLength = 4096;
    inline constexpr std::size_t MaximumArguments = 32;

    inline bool Equal(std::string_view left, std::string_view right) {
        if (left.size() != right.size())
            return false;
        auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c; };
        for (std::size_t i = 0; i < left.size(); ++i)
            if (lower(left[i]) != lower(right[i]))
                return false;
        return true;
    }

    inline bool Whitespace(char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    }

    inline bool Identifier(std::string_view value) {
        if (value.empty())
            return false;
        auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
        if (!letter(value.front()))
            return false;
        for (char c : value)
            if (!letter(c) && !(c >= '0' && c <= '9'))
                return false;
        return true;
    }

    inline std::optional<std::uint32_t> FormID(std::string_view value) {
        if (value.starts_with('@'))
            value.remove_prefix(1);
        if (Equal(value, "player"))
            return 0x14;
        if (value.starts_with("0x") || value.starts_with("0X"))
            value.remove_prefix(2);
        if (value.empty() || value.size() > 8)
            return std::nullopt;
        std::uint32_t id = 0;
        const auto result = std::from_chars(value.data(), value.data() + value.size(), id, 16);
        return result.ec == std::errc{} && result.ptr == value.data() + value.size() && id != 0 ? std::optional(id)
                                                                                                : std::nullopt;
    }

}
