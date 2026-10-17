#pragma once

#include "ExpressionTypes.h"

namespace elder_shell {
    inline bool FragmentKeyword(std::string_view word) {
        for (const auto keyword : {"If", "ElseIf", "Else", "EndIf", "While", "EndWhile", "Return", "Var"})
            if (Equal(word, keyword))
                return true;
        return false;
    }

    // Preserve line boundaries while removing comments before metadata discovery.
    inline std::string FragmentCode(std::string_view source) {
        if (source.find('\0') != std::string_view::npos)
            throw std::runtime_error("Fragments cannot contain null characters");
        std::string code(source);
        bool quoted = false, lineComment = false, blockComment = false;
        for (std::size_t i = 0; i < code.size(); ++i) {
            const char c = code[i];
            if (lineComment) {
                lineComment = c != '\n';
                if (lineComment)
                    code[i] = ' ';
            } else if (blockComment) {
                if (c == '/' && i + 1 < code.size() && code[i + 1] == ';') {
                    code[i] = ' ';
                    code[++i] = ' ';
                    blockComment = false;
                } else if (c != '\n')
                    code[i] = ' ';
            } else if (quoted) {
                if (c == '\\' && i + 1 < code.size())
                    ++i;
                else if (c == '"')
                    quoted = false;
            } else if (c == '"')
                quoted = true;
            else if (c == ';') {
                blockComment = i + 1 < code.size() && code[i + 1] == '/';
                lineComment = !blockComment;
                code[i] = ' ';
                if (blockComment)
                    code[++i] = ' ';
            }
        }
        if (quoted || blockComment)
            throw std::runtime_error(quoted ? "Unclosed string literal" : "Unclosed block comment");
        return code;
    }

    struct FragmentLine {
        std::string code, keyword, type, name, initializer;
        std::size_t depth = 0;
    };

    inline std::vector<FragmentLine> FragmentLines(std::string_view source) {
        const auto code = FragmentCode(source);
        std::vector<FragmentLine> lines;
        std::vector<std::string> blocks;
        for (std::size_t start = 0; start < code.size();) {
            const auto end = code.find('\n', start);
            FragmentLine line;
            line.code = code.substr(start, end == std::string::npos ? code.size() - start : end - start);
            const auto tokens = ExpressionTokens(line.code);
            if (!tokens.empty()) {
                line.keyword = tokens.front().text;
                for (const auto forbidden : {"Function", "EndFunction", "Event", "EndEvent", "Scriptname", "Property",
                                             "EndProperty", "State", "EndState", "Import"})
                    for (const auto& token : tokens)
                        if (!token.quoted && Equal(token.text, forbidden))
                            throw std::runtime_error(
                                "Fragments accept statements and control-flow blocks, not script definitions");
                if (Equal(line.keyword, "EndIf") || Equal(line.keyword, "EndWhile")) {
                    const auto expected = Equal(line.keyword, "EndIf") ? "If" : "While";
                    if (blocks.empty() || !Equal(blocks.back(), expected))
                        throw std::runtime_error("Unmatched " + line.keyword);
                    blocks.pop_back();
                }
                if ((Equal(line.keyword, "Else") || Equal(line.keyword, "ElseIf")) &&
                    (blocks.empty() || !Equal(blocks.back(), "If")))
                    throw std::runtime_error("Unmatched " + line.keyword);
                line.depth = blocks.size();
                std::size_t name = 1;
                if (tokens.size() > 3 && tokens[1].text == "[" && tokens[2].text == "]")
                    name = 3;
                if (!FragmentKeyword(line.keyword) || Equal(line.keyword, "Var")) {
                    if (tokens.size() > name && Identifier(tokens[0].text) && Identifier(tokens[name].text) &&
                        (tokens.size() == name + 1 || tokens[name + 1].text == "=")) {
                        line.type = tokens[0].text + std::string(name == 3 ? "[]" : "");
                        line.name = tokens[name].text;
                        if (tokens.size() > name + 1)
                            line.initializer = line.code.substr(tokens[name + 1].end);
                    }
                }
                if (Equal(line.keyword, "If") || Equal(line.keyword, "While"))
                    blocks.push_back(line.keyword);
            }
            lines.push_back(std::move(line));
            start = end == std::string::npos ? code.size() : end + 1;
        }
        if (!blocks.empty())
            throw std::runtime_error("Close the " + blocks.back() + " block before running the fragment");
        return lines;
    }

    inline std::variant<std::string, EvaluationStatement> ParseFragment(std::string_view source) {
        try {
            if (source.empty() || source.size() > MaximumCommandLength)
                return "Provide a Papyrus fragment (at most 4096 characters)";
            const auto code = FragmentCode(source);
            if (code.find_first_not_of(" \t\r\n") == std::string::npos)
                return "Provide a Papyrus fragment";
            FragmentLines(code);
            EvaluationStatement result;
            result.expression = code;
            result.inferResult = false;
            result.multiline = true;
            return result;
        } catch (const std::exception& error) {
            return std::string(error.what());
        }
    }

    inline std::vector<std::string> FragmentWrites(std::string_view source) {
        std::vector<std::string> writes;
        std::vector<std::vector<std::string>> locals(1);
        for (const auto& line : FragmentLines(source)) {
            if (Equal(line.keyword, "Else") || Equal(line.keyword, "ElseIf"))
                locals.back().clear();
            if (Equal(line.keyword, "EndIf") || Equal(line.keyword, "EndWhile"))
                locals.pop_back();
            std::string name;
            if (!line.name.empty()) {
                if (line.depth == 0)
                    name = line.name;
                else
                    locals.back().push_back(line.name);
            } else if (!FragmentKeyword(line.keyword)) {
                const auto parsed = ParseEvaluation(line.code);
                if (const auto* statement = std::get_if<EvaluationStatement>(&parsed))
                    name = statement->assignment;
            }
            const bool local = std::ranges::any_of(locals, [&](const auto& scope) {
                return std::ranges::any_of(scope, [&](const auto& item) { return Equal(item, name); });
            });
            if (!name.empty() && !local &&
                std::ranges::none_of(writes, [&](const auto& item) { return Equal(item, name); }))
                writes.push_back(name);
            if (Equal(line.keyword, "If") || Equal(line.keyword, "While"))
                locals.emplace_back();
        }
        return writes;
    }

    struct FragmentPlan {
        std::vector<SourceParameter> fields, outputs;
        std::string body;
    };

    using FragmentLocals = std::vector<std::vector<std::pair<std::string, std::string>>>;

    inline std::string RewriteFragmentLocals(std::string_view source, const FragmentLocals& scopes) {
        auto result = std::string(source);
        const auto tokens = ExpressionTokens(source);
        for (std::size_t i = tokens.size(); i > 0; --i) {
            const auto& token = tokens[i - 1];
            if (token.quoted || !Identifier(token.text) ||
                (i > 1 &&
                 (tokens[i - 2].text == "." || Equal(tokens[i - 2].text, "as") || Equal(tokens[i - 2].text, "new"))))
                continue;
            for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope) {
                const auto local =
                    std::ranges::find_if(*scope, [&](const auto& item) { return Equal(item.first, token.text); });
                if (local != scope->end()) {
                    result.replace(token.begin, token.end - token.begin, local->second);
                    break;
                }
            }
        }
        return result;
    }

    inline FragmentPlan ResolveFragment(std::string_view source, EvaluationEnvironment environment) {
        FragmentPlan plan;
        plan.fields = environment.variables;
        std::vector<std::vector<SourceParameter>> scopes;
        FragmentLocals locals(1);
        std::size_t nextLocal = 0;
        const auto roots = SourceRoots(source);
        std::vector<std::string> declarations;
        for (const auto& line : FragmentLines(source)) {
            if (Equal(line.keyword, "EndIf") || Equal(line.keyword, "EndWhile")) {
                environment.variables = scopes.back();
                scopes.pop_back();
                locals.pop_back();
            } else if (Equal(line.keyword, "Else") || Equal(line.keyword, "ElseIf")) {
                environment.variables = scopes.back();
                locals.back().clear();
            }
            auto body = RewriteFragmentLocals(line.code, locals);
            if (!line.name.empty()) {
                auto type = line.type;
                if (Equal(type, "var")) {
                    if (line.initializer.empty())
                        throw std::runtime_error("var " + line.name + " needs an initializer");
                    type = ExpressionTypes(line.initializer, environment).Result();
                    if (!DeclarationType(type))
                        throw std::runtime_error("Cannot infer " + line.name + "; provide an explicit value type");
                }
                if (line.depth == 0) {
                    if (std::ranges::any_of(declarations, [&](const auto& name) { return Equal(name, line.name); }))
                        throw std::runtime_error("Duplicate fragment declaration: " + line.name);
                    declarations.push_back(line.name);
                    const auto field = std::ranges::find_if(
                        plan.fields, [&](const auto& item) { return Equal(item.name, line.name); });
                    if (field == plan.fields.end())
                        plan.fields.push_back({type, line.name});
                    else if (!Equal(field->type, type))
                        throw std::runtime_error("Variable " + line.name + " already has type " + field->type +
                                                 "; unset it before changing its type");
                    body = line.initializer.empty() ? "" : line.name + " = " + line.initializer;
                } else {
                    if (std::ranges::any_of(locals.back(),
                                            [&](const auto& item) { return Equal(item.first, line.name); }))
                        throw std::runtime_error("Duplicate block declaration: " + line.name);
                    std::string generated;
                    do {
                        generated = "ElderShellLocal" + std::to_string(nextLocal++);
                    } while (std::ranges::any_of(roots, [&](const auto& root) { return Equal(root, generated); }));
                    // Keep block locals distinct from evaluator fields while
                    // resolving references through user-visible lexical scopes.
                    body = type + " " + generated +
                           (line.initializer.empty() ? "" : " = " + RewriteFragmentLocals(line.initializer, locals));
                    locals.back().emplace_back(line.name, generated);
                }
                std::erase_if(environment.variables, [&](const auto& item) { return Equal(item.name, line.name); });
                environment.variables.push_back({type, line.name});
            }
            plan.body += "    " + body + "\n";
            if (Equal(line.keyword, "If") || Equal(line.keyword, "While")) {
                scopes.push_back(environment.variables);
                locals.emplace_back();
            }
        }
        for (const auto& name : FragmentWrites(source)) {
            const auto field =
                std::ranges::find_if(plan.fields, [&](const auto& item) { return Equal(item.name, name); });
            if (field != plan.fields.end())
                plan.outputs.push_back(*field);
        }
        return plan;
    }

    inline std::string FragmentSource(const FragmentPlan& plan, std::string_view script) {
        std::string source = "Scriptname " + std::string(script) + " Hidden\n\n";
        for (const auto& field : plan.fields)
            source += field.type + " " + field.name + "\n";
        return source + "\nFunction Run()\n" + plan.body + "EndFunction\n";
    }
}
