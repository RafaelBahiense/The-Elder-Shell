#pragma once

#include "Values.h"
#include "Evaluation.h"

namespace papyrus_console {
    enum class Operation { Global, Method, Property, Methods, Value, Set, Variables, Unset, ClearVariables, Evaluate };

    struct Command {
        Operation operation = Operation::Global;
        std::string target, script, function, assignment;
        ValueExpression value;
        ValueExpression destination;
        EvaluationStatement evaluation;
        std::vector<ValueExpression> arguments;
    };

    using ParsedCommand = std::variant<Command, std::string>;

    inline ParsedCommand Parse(std::string_view input) {
        if (input.size() > MaximumCommandLength)
            return "Command exceeds 4096 characters";
        if (input.size() >= 4 && Equal(input.substr(0, 4), "eval") && (input.size() == 4 || Whitespace(input[4]))) {
            const auto parsed = ParseEvaluation(input.substr(4));
            if (const auto* error = std::get_if<std::string>(&parsed))
                return *error;
            Command evaluation;
            evaluation.operation = Operation::Evaluate;
            evaluation.evaluation = std::get<EvaluationStatement>(parsed);
            evaluation.assignment = evaluation.evaluation.assignment;
            return evaluation;
        }
        std::vector<bool> quotes;
        const auto tokenized = Tokenize(input, &quotes);
        if (const auto* error = std::get_if<std::string>(&tokenized))
            return *error;
        const auto& tokens = std::get<std::vector<std::string>>(tokenized);
        if (tokens.empty())
            return "Enter a command";
        Command command;
        std::size_t start = 0;
        if (Equal(tokens[start], "let")) {
            if (tokens.size() < 4 || quotes[1] || !Identifier(tokens[1]) || quotes[2] || tokens[2] != "=")
                return "Use: let name = value | global|call|get ...";
            command.assignment = tokens[1];
            start = 3;
            if (quotes[start] ||
                (!Equal(tokens[start], "global") && !Equal(tokens[start], "call") && !Equal(tokens[start], "get"))) {
                ValueParser parser(tokens, quotes, start);
                auto value = parser.Parse();
                if (!value)
                    return parser.Error();
                if (parser.Position() != tokens.size() || value->kind == ValueKind::BareText)
                    return "Assign a literal, $variable, property/index path or function result";
                command.operation = Operation::Value;
                command.value = std::move(*value);
                return command;
            }
        }
        if (Equal(tokens[start], "show") || Equal(tokens[start], "set")) {
            const bool set = Equal(tokens[start], "set");
            ValueParser parser(tokens, quotes, start + 1);
            auto value = parser.Parse(true);
            if (!value)
                return parser.Error();
            command.operation = set ? Operation::Set : Operation::Value;
            if (!set) {
                if (parser.Position() != tokens.size())
                    return "show accepts one value or property/index path";
                command.value = std::move(*value);
                return command;
            }
            if (value->kind != ValueKind::Reference || value->accesses.empty() ||
                value->accesses.back().kind != AccessKind::Property)
                return "Use: set $object.Property = value; direct array element writes are unsupported";
            command.destination = std::move(*value);
            const auto equals = parser.Position();
            if (equals == tokens.size() || quotes[equals] || tokens[equals] != "=")
                return "Separate the destination and value with =";
            ValueParser right(tokens, quotes, equals + 1);
            auto source = right.Parse();
            if (!source)
                return right.Error();
            if (right.Position() != tokens.size() || source->kind == ValueKind::BareText)
                return "set accepts one literal, $variable or property/index path";
            command.value = std::move(*source);
            return command;
        }
        if (Equal(tokens[start], "vars") || Equal(tokens[start], "clearvars")) {
            if (tokens.size() != start + 1)
                return "vars and clearvars do not accept arguments";
            command.operation = Equal(tokens[start], "vars") ? Operation::Variables : Operation::ClearVariables;
            return command;
        }
        if (Equal(tokens[start], "unset")) {
            if (tokens.size() != start + 2 || quotes[start + 1])
                return "Use: unset name";
            auto name = std::string_view(tokens[start + 1]);
            if (name.starts_with('$'))
                name.remove_prefix(1);
            if (!Identifier(name))
                return "Invalid variable name";
            command.operation = Operation::Unset;
            command.target = name;
            return command;
        }
        std::size_t functionIndex = start + 1;
        if (Equal(tokens[start], "global"))
            command.operation = Operation::Global;
        else if (Equal(tokens[start], "call") || Equal(tokens[start], "get")) {
            command.operation = Equal(tokens[start], "call") ? Operation::Method : Operation::Property;
            if (tokens.size() < start + 3 || quotes[start + 1] ||
                (!FormID(tokens[start + 1]) && !VariableReference(tokens[start + 1])))
                return "Use: call|get <FormID|@player|$variable> Script.Function";
            command.target = tokens[start + 1];
            functionIndex = start + 2;
        } else if (Equal(tokens[start], "methods")) {
            if (tokens.size() != start + 2 || !Identifier(tokens[start + 1]))
                return "Use: methods ScriptName";
            command.operation = Operation::Methods;
            command.script = tokens[start + 1];
            return command;
        } else {
            return "Commands: global, call, get, methods, let, set, vars, show, unset, clearvars";
        }
        if (tokens.size() <= functionIndex)
            return "Provide Script.Function";
        const auto& qualified = tokens[functionIndex];
        const auto dot = qualified.find('.');
        if (quotes[functionIndex] || dot == std::string::npos ||
            !Identifier(std::string_view(qualified).substr(0, dot)) ||
            !Identifier(std::string_view(qualified).substr(dot + 1)))
            return "Use ScriptName.FunctionName";
        command.script = qualified.substr(0, dot);
        command.function = qualified.substr(dot + 1);
        std::size_t position = functionIndex + 1;
        const bool parentheses = position < tokens.size() && !quotes[position] && tokens[position] == "(";
        if (parentheses) {
            if (command.operation == Operation::Property)
                return "get reads a property; omit parentheses";
            ++position;
        }
        bool closed = !parentheses;
        while (position < tokens.size()) {
            if (parentheses && !quotes[position] && tokens[position] == ")") {
                closed = true;
                ++position;
                break;
            }
            ValueParser parser(tokens, quotes, position);
            auto value = parser.Parse();
            if (!value)
                return parser.Error();
            command.arguments.push_back(std::move(*value));
            position = parser.Position();
            if (command.arguments.size() > MaximumArguments)
                return "At most 32 arguments are supported";
            if (!parentheses)
                continue;
            if (position < tokens.size() && !quotes[position] && tokens[position] == ")")
                continue;
            if (position == tokens.size() || quotes[position] || tokens[position] != ",")
                return "Separate arguments with commas and close the call with )";
            ++position;
            if (position == tokens.size() || (!quotes[position] && tokens[position] == ")"))
                return "A trailing comma needs an argument";
        }
        if (!closed || position != tokens.size())
            return "Close the function call with ) and remove trailing tokens";
        if (command.operation == Operation::Property && !command.arguments.empty())
            return "get does not accept arguments";
        return command;
    }
}
