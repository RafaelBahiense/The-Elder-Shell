#pragma once

#include "Evaluation.h"
#include "Fragment.h"

namespace elder_shell {
    enum class Operation { Methods, Variables, Unset, ClearVariables, Evaluate };

    struct Command {
        Operation operation = Operation::Evaluate;
        std::string target, script, assignment;
        EvaluationStatement evaluation;
    };

    using ParsedCommand = std::variant<Command, std::string>;

    inline ParsedCommand Parse(std::string_view input, bool multiline = false) {
        if (input.size() > MaximumCommandLength)
            return "Command exceeds 4096 characters";
        while (!input.empty() && Whitespace(input.front()))
            input.remove_prefix(1);

        const auto end = input.find_first_of(" \t\r\n");
        const auto first = input.substr(0, end);
        Command command;
        if (!multiline && Equal(first, "console")) {
            return "Use ElderShell.Console.Execute(\"command\") instead of the console shortcut.";
        }
        if (!multiline &&
            (Equal(first, "vars") || Equal(first, "clearvars") || Equal(first, "unset") || Equal(first, "methods"))) {
            std::vector<ExpressionToken> tokens;
            try {
                tokens = ExpressionTokens(input);
            } catch (const std::exception& error) {
                return std::string(error.what());
            }
            if (Equal(first, "vars") || Equal(first, "clearvars")) {
                if (tokens.size() != 1)
                    return "vars and clearvars do not accept arguments";
                command.operation = Equal(first, "vars") ? Operation::Variables : Operation::ClearVariables;
            } else {
                if (tokens.size() != 2 || tokens[1].quoted || !Identifier(tokens[1].text))
                    return Equal(first, "unset") ? "Use: unset name" : "Use: methods ScriptName";
                if (Equal(first, "unset")) {
                    command.operation = Operation::Unset;
                    command.target = tokens[1].text;
                } else {
                    command.operation = Operation::Methods;
                    command.script = tokens[1].text;
                }
            }
            return command;
        }

        const auto parsed = multiline ? ParseFragment(input) : ParseEvaluation(input);
        if (const auto* error = std::get_if<std::string>(&parsed))
            return *error;
        command.evaluation = std::get<EvaluationStatement>(parsed);
        command.assignment = command.evaluation.assignment;
        return command;
    }
}
