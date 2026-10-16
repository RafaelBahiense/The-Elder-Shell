#pragma once

#include "ExpressionTokens.h"
#include "compiler/ScriptDeclarations.h"

namespace elder_shell {
    struct ImportIssue {
        std::string script, member, reason;
    };

    struct EvaluationEnvironment {
        std::vector<ScriptDeclaration> scripts;
        std::vector<SourceParameter> variables;
        std::vector<ImportIssue> issues;

        const ScriptDeclaration* Script(std::string_view name) const {
            const auto found =
                std::ranges::find_if(scripts, [&](const auto& script) { return Equal(script.name, name); });
            return found == scripts.end() ? nullptr : &*found;
        }

        std::string MissingMember(std::string_view script, std::string_view member) const {
            for (const auto& issue : issues)
                if (Equal(issue.script, script) && (issue.member.empty() || Equal(issue.member, member)))
                    return "Cannot use " + std::string(script) + "." + std::string(member) + ": " + issue.reason;
            return "Member not found: " + std::string(script) + "." + std::string(member) +
                   ". Check its spelling or inspect the script with methods.";
        }

        std::string UnavailableType(std::string_view type) const {
            if (type.ends_with("[]"))
                type.remove_suffix(2);
            for (const auto& issue : issues)
                if (issue.member.empty() && Equal(issue.script, type))
                    return issue.reason;
            return {};
        }
    };

    // Determines the wrapper's return type and supplies early diagnostics.
    // Caprica still performs authoritative Papyrus parsing and type checking.
    class ExpressionTypes {
    public:
        ExpressionTypes(std::string_view expression, const EvaluationEnvironment& environment)
            : tokens_(ExpressionTokens(expression)), environment_(environment) {}

        std::string Result() {
            const auto result = CompleteExpression();
            RequireReadable(result);
            return result.type;
        }

        std::string AssignmentType(bool allowVariable = false) {
            const auto result = CompleteExpression();
            if (!result.writable || (!allowVariable && !result.externalStorage))
                throw std::runtime_error("Assignment requires a writable variable, property or array element");
            return result.type;
        }

    private:
        struct Value {
            std::string type;
            bool script = false;
            bool readable = true;
            bool writable = false;
            bool externalStorage = false;
        };

        static void RequireReadable(const Value& value) {
            if (!value.readable)
                throw std::runtime_error("Property is not readable");
        }

        Value CompleteExpression() {
            auto result = Expression(0);
            if (position_ != tokens_.size())
                throw std::runtime_error("Unexpected token '" + tokens_[position_].text + "' after the expression");
            if (result.script)
                throw std::runtime_error("A script type is not a value; call a global function or use a variable");
            if (const auto reason = environment_.UnavailableType(result.type); !reason.empty())
                throw std::runtime_error("Result type unavailable: " + result.type + ". " + reason);
            return result;
        }

        bool At(std::string_view token) const {
            return position_ < tokens_.size() && !tokens_[position_].quoted && Equal(tokens_[position_].text, token);
        }

        void Require(std::string_view token) {
            if (!At(token))
                throw std::runtime_error("Expected '" + std::string(token) + "' in the expression");
            ++position_;
        }

        static int Precedence(std::string_view token) {
            if (token == "||")
                return 1;
            if (token == "&&")
                return 2;
            if (token == "==" || token == "!=")
                return 3;
            if (token == "<" || token == ">" || token == "<=" || token == ">=")
                return 4;
            if (token == "+" || token == "-")
                return 5;
            if (token == "*" || token == "/" || token == "%")
                return 6;
            if (Equal(token, "as"))
                return 7;
            return -1;
        }

        Value Expression(int minimum) {
            if (++depth_ > 64)
                throw std::runtime_error("Expression nesting exceeds 64 levels");
            auto left = Primary();
            while (position_ < tokens_.size() && !tokens_[position_].quoted) {
                const auto operation = tokens_[position_].text;
                const auto precedence = Precedence(operation);
                if (precedence < minimum)
                    break;
                RequireReadable(left);
                ++position_;
                if (Equal(operation, "as")) {
                    left = {TypeName()};
                    continue;
                }
                const auto right = Expression(precedence + 1);
                RequireReadable(right);
                if (left.type.empty() || right.type.empty())
                    throw std::runtime_error("A function without a return value cannot be used in an expression");
                if (precedence <= 4)
                    left = {"Bool"};
                else if (operation == "+" && (Equal(left.type, "String") || Equal(right.type, "String")))
                    left = {"String"};
                else
                    left = {Equal(left.type, "Float") || Equal(right.type, "Float") ? "Float" : "Int"};
            }
            --depth_;
            return left;
        }

        std::string TypeName() {
            if (position_ == tokens_.size() || !Identifier(tokens_[position_].text))
                throw std::runtime_error("Provide a type name after the cast or new operator");
            auto type = tokens_[position_++].text;
            if (!Equal(type, "Int") && !Equal(type, "Float") && !Equal(type, "Bool") && !Equal(type, "String") &&
                !environment_.Script(type))
                throw std::runtime_error("Script type unavailable: " + type + ". Check that its script is installed.");
            if (At("[") && position_ + 1 < tokens_.size() && tokens_[position_ + 1].text == "]") {
                position_ += 2;
                type += "[]";
            }
            return type;
        }

        Value Primary() {
            if (position_ == tokens_.size())
                throw std::runtime_error("Provide a value or function call");
            Value value;
            if (At("!") || At("-") || At("+")) {
                const bool boolean = At("!");
                ++position_;
                value = Expression(8);
                if (boolean)
                    value = {"Bool"};
            } else if (At("(")) {
                ++position_;
                value = Expression(0);
                Require(")");
            } else if (At("new")) {
                ++position_;
                auto type = TypeName();
                Require("[");
                Expression(0);
                Require("]");
                value = {type + "[]"};
            } else {
                const auto token = tokens_[position_++];
                if (token.quoted)
                    value = {"String"};
                else if (Equal(token.text, "true") || Equal(token.text, "false"))
                    value = {"Bool"};
                else if (Equal(token.text, "none"))
                    value = {"None"};
                else if (!token.text.empty() &&
                         ((token.text.front() >= '0' && token.text.front() <= '9') || token.text.front() == '.')) {
                    const bool hex = token.text.starts_with("0x") || token.text.starts_with("0X");
                    value = {!hex && token.text.find_first_of(".eE") != std::string::npos ? "Float" : "Int"};
                } else {
                    const auto variable = std::ranges::find_if(
                        environment_.variables, [&](const auto& item) { return Equal(item.name, token.text); });
                    if (variable != environment_.variables.end())
                        value = {variable->type, false, true, true};
                    else if (environment_.Script(token.text))
                        value = {token.text, true};
                    else
                        throw std::runtime_error("Unknown variable or script: " + token.text +
                                                 ". Declare a variable, select an instance, or check the script name.");
                }
            }
            return Access(std::move(value));
        }

        Value Access(Value value) {
            while (At(".") || At("[")) {
                RequireReadable(value);
                if (value.type.empty() || Equal(value.type, "None"))
                    throw std::runtime_error("This receiver has no usable type; use an explicit declaration or cast");
                if (At("[")) {
                    ++position_;
                    Expression(0);
                    Require("]");
                    if (!value.type.ends_with("[]"))
                        throw std::runtime_error("Indexing requires an array");
                    value.type.resize(value.type.size() - 2);
                    value.script = false;
                    value.writable = true;
                    value.externalStorage = true;
                    continue;
                }
                ++position_;
                if (position_ == tokens_.size() || !Identifier(tokens_[position_].text))
                    throw std::runtime_error("Provide a member name after the dot");
                const auto member = tokens_[position_++].text;
                if (value.type.ends_with("[]") && Equal(member, "Length") && !At("(")) {
                    value = {"Int"};
                    continue;
                }
                value = Member(value, member);
            }
            return value;
        }

        Value Member(const Value& receiver, const std::string& name) {
            const auto* script = environment_.Script(receiver.type);
            for (std::size_t depth = 0; script && depth < 256; ++depth, script = environment_.Script(script->parent)) {
                if (!environment_.UnavailableType(script->name).empty())
                    throw std::runtime_error(environment_.MissingMember(script->name, name));
                if (At("(")) {
                    const auto method = std::ranges::find_if(script->functions,
                                                             [&](const auto& item) { return Equal(item.name, name); });
                    if (method == script->functions.end())
                        continue;
                    if ((!method->result.empty() && !DeclarationType(method->result)) ||
                        !std::ranges::all_of(method->parameters,
                                             [](const auto& item) { return DeclarationType(item.type); }))
                        throw std::runtime_error("Cannot use " + script->name + "." + name +
                                                 ": a signature type is missing from VM metadata.");
                    if (receiver.script != method->global)
                        throw std::runtime_error(method->global
                                                     ? "Call this global function through its script name"
                                                     : "Select or declare an instance before calling this method");
                    ++position_;
                    std::size_t arguments = 0;
                    if (!At(")")) {
                        while (true) {
                            RequireReadable(Expression(0));
                            ++arguments;
                            if (!At(","))
                                break;
                            ++position_;
                        }
                    }
                    Require(")");
                    if (arguments != method->parameters.size())
                        throw std::runtime_error(script->name + "." + name + " requires " +
                                                 std::to_string(method->parameters.size()) +
                                                 " arguments; supply optional arguments too.");
                    return {method->result};
                }
                const auto property =
                    std::ranges::find_if(script->properties, [&](const auto& item) { return Equal(item.name, name); });
                if (property == script->properties.end())
                    continue;
                if (!DeclarationType(property->type))
                    throw std::runtime_error("Cannot read " + script->name + "." + name +
                                             ": its type is missing from VM metadata.");
                if (receiver.script)
                    throw std::runtime_error("Properties require an instance, not a script name");
                return {property->type, false, property->readable, property->writable, true};
            }
            throw std::runtime_error(environment_.MissingMember(receiver.type, name));
        }

        std::vector<ExpressionToken> tokens_;
        const EvaluationEnvironment& environment_;
        std::size_t position_ = 0, depth_ = 0;
    };

    inline void ResolveStatement(EvaluationStatement& statement, const EvaluationEnvironment& environment) {
        if (statement.mutation) {
            ExpressionTypes(statement.destination, environment).AssignmentType(!statement.assignment.empty());
            if (ExpressionTypes(statement.sourceValue, environment).Result().empty())
                throw std::runtime_error("A function without a return value cannot be assigned");
            return;
        }

        const auto result = ExpressionTypes(statement.expression, environment).Result();
        if (statement.inferResult) {
            if (Equal(result, "None"))
                throw std::runtime_error("None needs an explicit type, such as Actor target = None");
            if (!statement.assignment.empty() && result.empty())
                throw std::runtime_error("This function has no return value; invoke it without an assignment");
            statement.resultType = result;
        }
        if (!statement.resultType.empty() && result.empty())
            throw std::runtime_error("This function has no return value; invoke it without an assignment");
    }
}
