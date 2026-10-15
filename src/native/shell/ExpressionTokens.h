#pragma once

#include "Syntax.h"
#include <stdexcept>

namespace papyrus_console {
    struct ExpressionToken {
        std::string text;
        bool quoted = false;
        std::size_t begin = 0, end = 0;
    };

    inline std::vector<ExpressionToken> ExpressionTokens(std::string_view source) {
        std::vector<ExpressionToken> tokens;
        const auto digit = [](char c) { return c >= '0' && c <= '9'; };
        const auto letter = [](char c) { return Identifier(std::string_view(&c, 1)); };
        for (std::size_t i = 0; i < source.size();) {
            if (Whitespace(source[i])) {
                ++i;
                continue;
            }
            const auto begin = i;
            const auto c = source[i++];
            if (c == '"') {
                bool closed = false;
                while (i < source.size()) {
                    if (source[i] == '\\') {
                        i += 2;
                        continue;
                    }
                    if (source[i++] == '"') {
                        closed = true;
                        break;
                    }
                }
                if (!closed)
                    throw std::runtime_error("Close the string literal with a quotation mark");
                tokens.push_back({std::string(source.substr(begin, i - begin)), true, begin, i});
                continue;
            }
            if (letter(c)) {
                while (i < source.size() && (letter(source[i]) || digit(source[i])))
                    ++i;
            } else if (digit(c) || (c == '.' && i < source.size() && digit(source[i]))) {
                const bool hex = c == '0' && i < source.size() && (source[i] == 'x' || source[i] == 'X');
                if (hex) {
                    ++i;
                    while (i < source.size() && (digit(source[i]) || (source[i] >= 'a' && source[i] <= 'f') ||
                                                 (source[i] >= 'A' && source[i] <= 'F')))
                        ++i;
                } else {
                    while (i < source.size() && digit(source[i]))
                        ++i;
                    if (i < source.size() && source[i] == '.') {
                        ++i;
                        while (i < source.size() && digit(source[i]))
                            ++i;
                    }
                    if (i < source.size() && (source[i] == 'e' || source[i] == 'E')) {
                        ++i;
                        if (i < source.size() && (source[i] == '+' || source[i] == '-'))
                            ++i;
                        while (i < source.size() && digit(source[i]))
                            ++i;
                    }
                }
            } else if (i < source.size() && source[i] == '=' &&
                       (c == '=' || c == '!' || c == '<' || c == '>' || c == '+' || c == '-' || c == '*' || c == '/' ||
                        c == '%')) {
                ++i;
            } else if (i < source.size() && source[i] == c && (c == '&' || c == '|')) {
                ++i;
            }
            tokens.push_back({std::string(source.substr(begin, i - begin)), false, begin, i});
        }
        return tokens;
    }
}
