#pragma once

#include "ExpressionTypes.h"
#include <set>

namespace elder_shell {
    struct CompletionContext {
        std::string receiver, prefix, callReceiver, function;
        std::size_t begin = 0, end = 0, argument = 0;
        bool inString = false;
    };

    inline std::string ReceiverExpression(std::string_view source, const std::vector<ExpressionToken>& tokens,
                                          std::size_t dot) {
        if (dot == 0)
            return {};
        std::size_t start = dot, depth = 0;
        while (start > 0) {
            const auto& token = tokens[start - 1];
            if (!token.quoted) {
                if (token.text == ")" || token.text == "]")
                    ++depth;
                else if (token.text == "(" || token.text == "[") {
                    if (depth == 0)
                        break;
                    --depth;
                } else if (depth == 0 && token.text != "." && !Identifier(token.text))
                    break;
            }
            --start;
        }
        return std::string(source.substr(tokens[start].begin, tokens[dot].begin - tokens[start].begin));
    }

    inline CompletionContext CompletionAt(std::string_view source, std::size_t cursor) {
        CompletionContext context;
        cursor = std::min(cursor, source.size());
        context.begin = context.end = cursor;
        auto before = source.substr(0, cursor);
        std::optional<std::size_t> quote;
        for (std::size_t i = 0; i < before.size(); ++i) {
            if (quote && before[i] == '\\')
                ++i;
            else if (before[i] == '"') {
                if (quote)
                    quote.reset();
                else
                    quote = i;
            }
        }
        context.inString = quote.has_value();
        if (quote)
            before = before.substr(0, *quote);
        const auto tokens = ExpressionTokens(before);
        if (!context.inString && !tokens.empty()) {
            auto member = tokens.size();
            if (tokens.back().end == cursor && Identifier(tokens.back().text)) {
                context.prefix = tokens.back().text;
                context.begin = tokens.back().begin;
                --member;
                while (context.end < source.size() && (Identifier(source.substr(context.end, 1)) ||
                                                       (source[context.end] >= '0' && source[context.end] <= '9')))
                    ++context.end;
            }
            if (member > 0 && tokens[member - 1].text == ".")
                context.receiver = ReceiverExpression(source, tokens, member - 1);
        }

        struct Frame {
            std::size_t open, argument = 0;
        };

        std::vector<Frame> stack;
        for (std::size_t i = 0; i < tokens.size(); ++i) {
            if (tokens[i].quoted)
                continue;
            if (tokens[i].text == "(" || tokens[i].text == "[")
                stack.push_back({i});
            else if (tokens[i].text == ")" || tokens[i].text == "]") {
                if (!stack.empty())
                    stack.pop_back();
            } else if (tokens[i].text == "," && !stack.empty())
                ++stack.back().argument;
        }
        for (auto frame = stack.rbegin(); frame != stack.rend(); ++frame) {
            const auto open = frame->open;
            if (tokens[open].text == "(" && open >= 2 && tokens[open - 2].text == "." &&
                Identifier(tokens[open - 1].text)) {
                context.callReceiver = ReceiverExpression(source, tokens, open - 2);
                context.function = tokens[open - 1].text;
                context.argument = frame->argument;
                break;
            }
        }
        return context;
    }

    struct CompletionItem {
        std::string name, description;
    };

    struct CompletionResult {
        CompletionContext context;
        std::vector<CompletionItem> items;
        std::string signature, parameter;
    };

    struct CompletionReceiver {
        std::string type;
        bool global = false;
    };

    inline CompletionReceiver ResolveReceiver(std::string_view expression, const EvaluationEnvironment& environment) {
        while (!expression.empty() && Whitespace(expression.back()))
            expression.remove_suffix(1);
        if (expression.empty())
            return {};
        const auto variable =
            std::ranges::find_if(environment.variables, [&](const auto& item) { return Equal(item.name, expression); });
        if (variable != environment.variables.end())
            return {variable->type};
        if (environment.Script(expression))
            return {std::string(expression), true};
        try {
            return {ExpressionTypes(expression, environment).Result()};
        } catch (const std::exception&) {
            return {};
        }
    }

    inline std::string FunctionSignature(const FunctionDeclaration& function) {
        auto text = function.name + "(";
        for (std::size_t i = 0; i < function.parameters.size(); ++i) {
            if (i)
                text += ", ";
            text += function.parameters[i].type + " " + function.parameters[i].name;
        }
        return text + ") -> " + (function.result.empty() ? "None" : function.result);
    }

    inline CompletionResult Complete(std::string_view source, std::size_t cursor,
                                     const EvaluationEnvironment& environment) {
        CompletionResult result;
        auto& context = result.context = CompletionAt(source, cursor);
        const auto add = [&](std::string name, std::string description) {
            if (name.size() >= context.prefix.size() && Equal(name.substr(0, context.prefix.size()), context.prefix) &&
                std::ranges::none_of(result.items, [&](const auto& item) { return Equal(item.name, name); }))
                result.items.push_back({std::move(name), std::move(description)});
        };
        if (!context.inString && (!context.prefix.empty() || !context.receiver.empty())) {
            if (context.receiver.empty()) {
                for (const auto& variable : environment.variables)
                    add(variable.name, variable.type);
                for (const auto& script : environment.scripts)
                    add(script.name, "script");
                for (const auto name : {"Game", "Debug"})
                    add(name, "script");
            } else {
                const auto receiver = ResolveReceiver(context.receiver, environment);
                if (receiver.type.ends_with("[]"))
                    add("Length", "Int (read-only)");
                auto* script = environment.Script(receiver.type);
                std::set<std::string> visited;
                while (script && visited.insert(script->name).second) {
                    if (!environment.UnavailableType(script->name).empty())
                        break;
                    for (const auto& function : script->functions)
                        if (function.global == receiver.global &&
                            (function.result.empty() || DeclarationType(function.result)) &&
                            std::ranges::all_of(function.parameters,
                                                [](const auto& p) { return DeclarationType(p.type); }))
                            add(function.name, FunctionSignature(function));
                    if (!receiver.global)
                        for (const auto& property : script->properties)
                            if (DeclarationType(property.type))
                                add(property.name, property.type + (property.writable ? "" : " (read-only)"));
                    script = environment.Script(script->parent);
                }
            }
        }
        std::ranges::sort(result.items, [](const auto& a, const auto& b) { return a.name < b.name; });
        const auto receiver = ResolveReceiver(context.callReceiver, environment);
        auto* script = environment.Script(receiver.type);
        std::set<std::string> visited;
        while (script && visited.insert(script->name).second) {
            if (!environment.UnavailableType(script->name).empty())
                break;
            const auto function = std::ranges::find_if(script->functions, [&](const auto& f) {
                return Equal(f.name, context.function) && f.global == receiver.global;
            });
            if (function != script->functions.end()) {
                if ((!function->result.empty() && !DeclarationType(function->result)) ||
                    !std::ranges::all_of(function->parameters, [](const auto& p) { return DeclarationType(p.type); }))
                    break;
                result.signature = FunctionSignature(*function);
                if (context.argument < function->parameters.size()) {
                    const auto& parameter = function->parameters[context.argument];
                    result.parameter = "Argument " + std::to_string(context.argument + 1) + ": " + parameter.type +
                                       " " + parameter.name;
                } else if (!function->parameters.empty())
                    result.parameter = "All parameters supplied; close the call with )";
                break;
            }
            script = environment.Script(script->parent);
        }
        return result;
    }
}
