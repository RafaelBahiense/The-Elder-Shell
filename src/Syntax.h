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

    inline bool CallPunctuation(char c) {
        return c == '(' || c == ')' || c == ',' || c == '[' || c == ']' || c == '=';
    }

    inline std::variant<std::vector<std::string>, std::string> Tokenize(std::string_view input,
                                                                        std::vector<bool>* quotes = nullptr) {
        std::vector<std::string> tokens;
        std::size_t position = 0;
        while (position < input.size()) {
            if (Whitespace(input[position])) {
                ++position;
                continue;
            }
            if (CallPunctuation(input[position])) {
                tokens.emplace_back(1, input[position++]);
                if (quotes)
                    quotes->push_back(false);
                continue;
            }
            std::string token;
            const bool quoted = input[position] == '"';
            if (quoted)
                ++position;
            bool closed = !quoted;
            while (position < input.size()) {
                char c = input[position++];
                if (quoted && c == '"') {
                    closed = true;
                    if (position < input.size() && !Whitespace(input[position]) && !CallPunctuation(input[position]))
                        return "Expected whitespace after a quoted argument";
                    break;
                }
                if (!quoted && Whitespace(c))
                    break;
                if (quoted && c == '\\') {
                    if (position == input.size())
                        return "Unfinished string escape";
                    c = input[position++];
                    switch (c) {
                        case 'n':
                            c = '\n';
                            break;
                        case 't':
                            c = '\t';
                            break;
                        case '"':
                        case '\\':
                            break;
                        default:
                            return "Unsupported string escape";
                    }
                }
                if (!quoted && CallPunctuation(c)) {
                    --position;
                    break;
                }
                token += c;
            }
            if (!closed)
                return "Unclosed quoted argument";
            tokens.push_back(std::move(token));
            if (quotes)
                quotes->push_back(quoted);
            if (tokens.size() > 2048)
                return "Too many arguments";
        }
        return tokens;
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

    inline bool VariableReference(std::string_view value) {
        return value.starts_with('$') && Identifier(value.substr(1));
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

    template <class Number>
    inline std::optional<Number> NumberArgument(std::string_view value) {
        Number number{};
        const auto result = std::from_chars(value.data(), value.data() + value.size(), number);
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            return std::nullopt;
        if constexpr (std::is_floating_point_v<Number>)
            if (!std::isfinite(number))
                return std::nullopt;
        return number;
    }
}
