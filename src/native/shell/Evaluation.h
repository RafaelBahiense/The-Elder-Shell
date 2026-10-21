#pragma once
#include "ModInfo.h"

#include "Syntax.h"
#include "ExpressionTokens.h"

#include <algorithm>

namespace elder_shell {
    struct EvaluationStatement {
        std::string expression;
        std::string resultType;
        std::string assignment;
        bool inferResult = true;
        bool reassignment = false;
        std::string destination, sourceValue;
        bool mutation = false;
        bool multiline = false;
    };

    struct SourceParameter {
        std::string type, name;
        std::string defaultValue;
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
        std::vector<ExpressionToken> lexical;
        try {
            lexical = ExpressionTokens(normalized);
        } catch (const std::exception& error) {
            return std::string(error.what());
        }
        std::vector<std::string> tokens;
        for (const auto& token : lexical)
            tokens.push_back(token.text);
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
            if (Equal(type, "none") || Equal(type, "var[]"))
                return "Use an explicit type or var name = expression";
            statement.inferResult = Equal(type, "var");
            statement.resultType = statement.inferResult ? "" : type;
            statement.assignment = tokens[nameIndex];
            const auto equals = normalized.find('=');
            statement.expression = normalized.substr(equals + 1);
            if (statement.expression.find_first_not_of(" \t") == std::string::npos)
                return "Provide an initializer after =";
        } else {
            int depth = 0;
            for (std::size_t i = 0; i < lexical.size(); ++i) {
                const auto& token = lexical[i];
                if (token.quoted)
                    continue;
                if (token.text == "(" || token.text == "[")
                    ++depth;
                else if (token.text == ")" || token.text == "]")
                    --depth;
                else if (depth == 0 && (token.text == "=" || token.text == "+=" || token.text == "-=" ||
                                        token.text == "*=" || token.text == "/=" || token.text == "%=")) {
                    statement.inferResult = false;
                    statement.destination = normalized.substr(0, token.begin);
                    statement.sourceValue = normalized.substr(token.end);
                    if (i == 1 && Identifier(tokens[0])) {
                        statement.assignment = tokens[0];
                        statement.reassignment = true;
                    }
                    statement.mutation = !statement.reassignment || token.text != "=";
                    if (!statement.mutation)
                        statement.expression = statement.sourceValue;
                    if (statement.sourceValue.find_first_not_of(" \t") == std::string::npos)
                        return "Provide a value after the assignment operator";
                    break;
                }
            }
        }
        return statement;
    }

    inline std::vector<std::string> SourceRoots(std::string_view source) {
        std::vector<std::string> roots;
        const auto tokens = ExpressionTokens(source);
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            if (token.quoted || !Identifier(token.text) || (i > 0 && tokens[i - 1].text == "."))
                continue;
            if (std::ranges::none_of(roots, [&](const auto& existing) { return Equal(existing, token.text); }))
                roots.push_back(token.text);
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
        if (!statement.resultType.empty() && !statement.mutation)
            source += "Return ";
        source += statement.expression;
        if (statement.mutation && !statement.assignment.empty())
            source += "\n    Return " + statement.assignment;
        source += "\nEndFunction\n";
        return source;
    }
}
