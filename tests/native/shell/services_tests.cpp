#include <doctest/doctest.h>
#include "shell/Services.h"
#include "shell/Completion.h"
#include "shell/Fragment.h"

using namespace elder_shell;

namespace {
    EvaluationEnvironment Environment() {
        FunctionDeclaration execute{"Execute", "String", {{"String", "command"}, {"ObjectReference", "target"}}};
        services::ApplyDefaults(services::ConsoleScript, execute);
        FunctionDeclaration restart{"RestartQuest", "String", {{"Quest", "target"}, {"Bool", "clearAlreadyRun"}}};
        services::ApplyDefaults(services::DiagnosticsScript, restart);
        return {
            {{services::RootScript,
              "Quest",
              {},
              {{"Console", services::ConsoleScript, true, false},
               {"Diagnostics", services::DiagnosticsScript, true, false}}},
             {services::ConsoleScript, "Quest", {execute}, {}},
             {services::DiagnosticsScript, "Quest", {restart, {"InspectQuest", "String", {{"Quest", "target"}}}}, {}},
             {"Quest", "Form", {}, {}},
             {"ObjectReference", "Form", {}, {}},
             {"Form", {}, {}, {}}},
            {{services::RootScript, "ElderShell"}, {"Quest", "quest"}, {"ObjectReference", "actor"}},
            {}};
    }
}

TEST_CASE("Service objects use ordinary typed member access and source defaults") {
    const auto environment = Environment();
    CHECK(ExpressionTypes("ElderShell.Console.Execute(\"sqv ExampleQuest\")", environment).Result() == "String");
    CHECK(ExpressionTypes("eldershell.Console.Execute(\"getav health\", actor)", environment).Result() == "String");
    CHECK(ExpressionTypes("ElderShell.Diagnostics.RestartQuest(quest)", environment).Result() == "String");
    CHECK(ExpressionTypes("ElderShell.Diagnostics.RestartQuest(quest, True)", environment).Result() == "String");
    CHECK_THROWS(ExpressionTypes("ElderShell.Console.Execute()", environment).Result());
    CHECK_THROWS(ExpressionTypes("ElderShell.Console.Execute(\"help\", actor, actor)", environment).Result());
    CHECK_THROWS(ExpressionTypes("ElderShell.Console", environment).AssignmentType());
    auto statement =
        std::get<EvaluationStatement>(ParseEvaluation("var output = ElderShell.Console.Execute(\"help\")"));
    ResolveStatement(statement, environment);
    CHECK(statement.resultType == "String");
    const auto plan = ResolveFragment(
        "var output = ElderShell.Console.Execute(\"help\")\nIf output != \"\"\n"
        "output = ElderShell.Diagnostics.InspectQuest(quest)\nEndIf",
        environment);
    CHECK_FALSE(plan.fields.empty());
    const auto complete = Complete("ElderShell.", 11, environment);
    CHECK(complete.items.size() == 2);
    const std::string call = "ElderShell.Console.Execute(\"help\", ";
    const auto hint = Complete(call, call.size(), environment);
    CHECK(hint.signature.find("ObjectReference target = None") != std::string::npos);
    CHECK(hint.parameter.find("optional") != std::string::npos);
    const auto declaration = DeclarationSource(environment.scripts[1]);
    CHECK(declaration.find("ObjectReference target = None") != std::string::npos);
}

TEST_CASE("The built-in binding cannot be unset, reassigned or shadowed") {
    for (const auto text : {"unset ElderShell", "var eldershell = None", "ElderShell = None", "ElderShell += 1"}) {
        const auto command = Parse(text);
        REQUIRE(std::holds_alternative<Command>(command));
        CHECK(services::WritesRoot(std::get<Command>(command)));
    }
    for (const auto text : {"Quest ElderShell\n", "If True\nvar ElderShell = None\nEndIf", "ElderShell = None\n"}) {
        const auto command = Parse(text, true);
        REQUIRE(std::holds_alternative<Command>(command));
        CHECK(services::WritesRoot(std::get<Command>(command)));
    }
    CHECK_FALSE(services::WritesRoot(std::get<Command>(Parse("var output = ElderShell.Console.Execute(\"help\")"))));
    CHECK(std::holds_alternative<std::string>(Parse("console sqv ExampleQuest")));
}

TEST_CASE("Native console validation rejects multiple commands and embedded NUL") {
    CHECK(services::ConsoleError("help \"iron sword\" 4").empty());
    CHECK_FALSE(services::ConsoleError("").empty());
    CHECK_FALSE(services::ConsoleError(" \t").empty());
    CHECK_FALSE(services::ConsoleError("help\nquit").empty());
    CHECK_FALSE(services::ConsoleError(std::string_view("help\0quit", 9)).empty());
    CHECK(services::ConsoleError(std::string(4096, 'x')).empty());
    CHECK_FALSE(services::ConsoleError(std::string(4097, 'x')).empty());
}

TEST_CASE("Quest recovery requires a stopped ordinary quest and explicit permission to clear alreadyRun") {
    CHECK(services::RestartError(true, false, false, false, false).empty());
    CHECK(services::RestartError(true, false, false, true, true).empty());
    CHECK_FALSE(services::RestartError(true, false, false, true, false).empty());
    CHECK_FALSE(services::RestartError(false, false, false, false, true).empty());
    CHECK_FALSE(services::RestartError(true, true, false, false, true).empty());
    CHECK_FALSE(services::RestartError(true, false, true, false, true).empty());
}
