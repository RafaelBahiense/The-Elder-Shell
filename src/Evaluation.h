#pragma once

#include "Syntax.h"

#include <algorithm>
#include <set>

namespace papyrus_console {
    struct EvaluationStatement {
        std::string expression;
        std::string resultType;
        std::string assignment;
    };

    struct SourceParameter {
        std::string type, name;
    };

    // This scanner only isolates a statement and finds variable roots. Caprica
    // owns expression parsing, type checking and Papyrus operator semantics.
    inline std::variant<std::string, EvaluationStatement> ParseEvaluation(std::string_view source) {
        if (source.empty() || source.size() > MaximumCommandLength)
            return "Provide one Papyrus statement (at most 4096 characters)";
        bool quoted = false, escaped = false;
        std::string normalized;
        for (char c : source) {
            if (c == '\r' || c == '\n' || c == '\0')
                return "Evaluation accepts one line; code blocks are unsupported";
            if (!quoted && (c == ';' || c == '{' || c == '}'))
                return "Comments and code blocks are unsupported";
            if (!quoted && c == '$')
                continue;
            normalized += c;
            if (escaped) {
                escaped = false;
                continue;
            }
            if (quoted && c == '\\') {
                escaped = true;
                continue;
            }
            if (c == '"')
                quoted = !quoted;
        }
        if (quoted)
            return "Unclosed string literal";
        const auto tokenized = Tokenize(normalized);
        if (const auto* error = std::get_if<std::string>(&tokenized))
            return *error;
        const auto& tokens = std::get<std::vector<std::string>>(tokenized);
        if (tokens.empty())
            return "Provide a Papyrus statement";
        EvaluationStatement statement;
        statement.expression = normalized;
        std::size_t nameIndex = 1;
        std::string type = tokens[0];
        if (tokens.size() > 3 && tokens[1] == "[" && tokens[2] == "]") {
            type += "[]";
            nameIndex = 3;
        }
        if (tokens.size() > nameIndex + 1 && Identifier(tokens[0]) && Identifier(tokens[nameIndex]) &&
            tokens[nameIndex + 1] == "=") {
            if (Equal(type, "none") || Equal(type, "var"))
                return "Skyrim declarations require an explicit value type";
            statement.resultType = type;
            statement.assignment = tokens[nameIndex];
            const auto equals = normalized.find('=');
            statement.expression = normalized.substr(equals + 1);
            if (statement.expression.find_first_not_of(" \t") == std::string::npos)
                return "Provide an initializer after =";
        }
        return statement;
    }

    inline std::vector<std::string> SourceRoots(std::string_view source) {
        std::vector<std::string> roots;
        bool quoted = false, escaped = false;
        char previous = 0;
        for (std::size_t i = 0; i < source.size();) {
            const char c = source[i];
            if (escaped) {
                escaped = false;
                ++i;
                continue;
            }
            if (quoted && c == '\\') {
                escaped = true;
                ++i;
                continue;
            }
            if (c == '"') {
                quoted = !quoted;
                ++i;
                continue;
            }
            if (quoted) {
                ++i;
                continue;
            }
            const auto digit = [](char value) { return value >= '0' && value <= '9'; };
            if (digit(c) || (c == '.' && i + 1 < source.size() && digit(source[i + 1]))) {
                // Numeric literals are one token. In particular, the x and A-F
                // in a hexadecimal FormID are not script or variable names.
                if (c == '0' && i + 1 < source.size() && (source[i + 1] == 'x' || source[i + 1] == 'X')) {
                    i += 2;
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
                previous = '0';
                continue;
            }
            if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_') {
                const auto begin = i++;
                while (i < source.size() && (Identifier(source.substr(begin, i - begin + 1))))
                    ++i;
                if (previous != '.') {
                    const std::string name(source.substr(begin, i - begin));
                    if (std::ranges::none_of(roots, [&](const auto& existing) { return Equal(existing, name); }))
                        roots.push_back(name);
                }
                previous = 'a';
                continue;
            }
            if (!Whitespace(c))
                previous = c;
            ++i;
        }
        return roots;
    }

    inline std::string EvaluationSource(const EvaluationStatement& statement,
                                        const std::vector<SourceParameter>& parameters, std::string_view script) {
        std::string source = "Scriptname " + std::string(script) + " Hidden\n\n";
        if (!statement.resultType.empty())
            source += statement.resultType + " ";
        source += "Function Run(";
        for (std::size_t i = 0; i < parameters.size(); ++i) {
            if (i)
                source += ", ";
            source += parameters[i].type + " " + parameters[i].name;
        }
        source += ") Global\n    ";
        if (!statement.resultType.empty())
            source += "Return ";
        source += statement.expression + "\nEndFunction\n";
        return source;
    }
}
