#include <doctest/doctest.h>
#include "shell/Command.h"
#include "shell/ExpressionTypes.h"
#include "shell/Variables.h"

using namespace papyrus_console;

namespace {
    EvaluationEnvironment Environment() {
        return {
            {{"BaseRecord", {}, {}, {{"Enabled", "Bool", true, true}}},
             {"Record",
              "BaseRecord",
              {{"Describe", "String", {}, false}},
              {{"Value", "Float", true, true}, {"WriteOnly", "Float", false, true}, {"Missing", "none", true, false}}},
             {"Container",
              {},
              {{"GetRecord", "Record", {}, false},
               {"GetCount", "Int", {}, false},
               {"Clear", {}, {}, false},
               {"Accept", {}, {{"Int", "value"}}, false}},
              {{"Data", "Record", true, false}, {"Values", "Float[]", true, false}}},
             {"Service",
              {},
              {{"Lookup", "Container", {{"Int", "id"}}, true}, {"GetOptional", "Unavailable", {}, true}},
              {}},
             {"Unavailable", {}, {}, {}}},
            {{"Container", "container"}, {"Int", "count"}, {"Float[]", "values"}, {"Unavailable", "optional"}},
            {{"Unavailable", {}, "Skyrim could not link this script's metadata"}}};
    }

    Command Evaluation(std::string_view source) {
        const auto parsed = Parse(source);
        REQUIRE(std::holds_alternative<Command>(parsed));
        const auto command = std::get<Command>(parsed);
        CHECK(command.operation == Operation::Evaluate);
        return command;
    }

    EvaluationStatement Resolved(std::string_view source) {
        auto statement = Evaluation(source).evaluation;
        const auto environment = Environment();
        if (statement.reassignment) {
            const auto variable = std::ranges::find_if(
                environment.variables, [&](const auto& item) { return Equal(item.name, statement.assignment); });
            REQUIRE(variable != environment.variables.end());
            statement.resultType = variable->type;
        }
        ResolveStatement(statement, environment);
        return statement;
    }

    TEST_CASE("Var declarations infer fixed primitive, object and array types without executing calls") {
        for (const auto& [source, expected] : std::vector<std::pair<std::string, std::string>>{
                 {"var result = container.GetCount()", "Int"},
                 {"VAR result = container.Data.Value", "Float"},
                 {"var result = container.Data.Enabled", "Bool"},
                 {"var result = container.GetRecord().Describe()", "String"},
                 {"var result = Service.Lookup(1).Data", "Record"},
                 {"var result = new Float[3]", "Float[]"}}) {
            const auto statement = Resolved(source);
            CHECK(statement.assignment == "result");
            CHECK(statement.resultType == expected);
            const auto generated = EvaluationSource(statement, {}, "Evaluation");
            const auto first = generated.find(statement.expression);
            REQUIRE(first != std::string::npos);
            CHECK(generated.find(statement.expression, first + statement.expression.size()) == std::string::npos);
        }
        CHECK_THROWS(Resolved("var result = None"));
        CHECK_THROWS(Resolved("var result = container.Clear()"));
        CHECK(Resolved("Record result = None").resultType == "Record");
        CHECK(std::holds_alternative<std::string>(Parse("var result =")));
        CHECK(std::holds_alternative<std::string>(Parse("var[] result = values")));
    }

    TEST_CASE("Property and array writes use Papyrus statements and preserve setter permissions") {
        for (const auto text : {"container.Data.Value = 2.5", "container.Data.WriteOnly = 2.5", "values[count] = 3.0",
                                "container.Data.Value += 1.0"}) {
            const auto statement = Resolved(text);
            CHECK(statement.mutation);
            CHECK(statement.assignment.empty());
            CHECK(statement.resultType.empty());
            CHECK(EvaluationSource(statement, {}, "Evaluation").find("Return") == std::string::npos);
        }
        CHECK_THROWS(Resolved("container.Data = None"));
        CHECK_THROWS(Resolved("values.Length = 1"));
        CHECK_THROWS(Resolved("container.GetCount() = 1"));
        CHECK_THROWS(Resolved("(count) = 1"));
        CHECK_THROWS(Resolved("container.Data.WriteOnly"));
        CHECK_THROWS(Resolved("values[0] = container.Clear()"));
        CHECK(std::holds_alternative<std::string>(Parse("values[0] =")));
        const auto compound = Resolved("count += container.GetCount()");
        CHECK(compound.resultType == "Int");
        CHECK(compound.reassignment);
        CHECK(EvaluationSource(compound, {{"Int", "count"}}, "Evaluation").find("Return count") != std::string::npos);
    }

    TEST_CASE("Only session and metadata helpers have separate command handlers") {
        CHECK(std::get<Command>(Parse("vars")).operation == Operation::Variables);
        CHECK(std::get<Command>(Parse("unset count")).operation == Operation::Unset);
        CHECK(std::get<Command>(Parse("clearvars")).operation == Operation::ClearVariables);
        CHECK(std::get<Command>(Parse("methods Record")).operation == Operation::Methods);
        for (const auto source : {"vars count", "unset", "unset count.Value", "clearvars count", "methods"})
            CHECK(std::holds_alternative<std::string>(Parse(source)));
        CHECK(Evaluation("count").evaluation.inferResult);
        CHECK(std::holds_alternative<std::string>(Parse("")));
        CHECK(std::holds_alternative<std::string>(Parse(std::string(MaximumCommandLength + 1, 'x'))));
        CHECK(std::holds_alternative<std::string>(Parse("Int count = 1\nEndFunction")));
        CHECK(std::holds_alternative<std::string>(Parse("count ; comment")));
    }

    TEST_CASE("Instance explorer IDs accept only bounded runtime hexadecimal identifiers") {
        CHECK(FormID("00000014") == 0x14);
        CHECK(FormID("0xFF000123") == 0xFF000123);
        for (const auto source : {"", "0", "123456789", "invalid", "-14", "14 extra"})
            CHECK_FALSE(FormID(source));
    }

    TEST_CASE("Reassignment retains its variable name and leaves type resolution to the binding") {
        const auto assignment = Evaluation("count = count + 1");
        CHECK(assignment.assignment == "count");
        CHECK(assignment.evaluation.reassignment);
        CHECK_FALSE(assignment.evaluation.inferResult);
        CHECK(assignment.evaluation.resultType.empty());
        CHECK(assignment.evaluation.expression == " count + 1");
        CHECK_FALSE(Evaluation("Int count = 2").evaluation.reassignment);
        CHECK(std::holds_alternative<std::string>(Parse("count =")));
    }

    TEST_CASE("Expression results follow calls, inherited properties, casts and array access") {
        const auto environment = Environment();
        const auto type = [&](std::string_view source) { return ExpressionTypes(source, environment).Result(); };
        CHECK(type("container.GetCount()") == "Int");
        CHECK(type("Service.Lookup(1).GetRecord().Describe()") == "String");
        CHECK(type("container.Data.Enabled") == "Bool");
        CHECK(type("container.Values[0]") == "Float");
        CHECK(type("values.Length") == "Int");
        CHECK(type("(Service.Lookup(1) as Container).Data.Value") == "Float");
        CHECK(type("new Float[3]") == "Float[]");
        CHECK(type("container.Clear()").empty());
    }

    TEST_CASE("Arithmetic, comparisons, strings and hex literals have useful result types") {
        const auto environment = Environment();
        const auto type = [&](std::string_view source) { return ExpressionTypes(source, environment).Result(); };
        CHECK(type("count + 1") == "Int");
        CHECK(type("-count * 1.5 + container.Data.Value") == "Float");
        CHECK(type("count > 0 && container.Data.Enabled") == "Bool");
        CHECK(type(R"("Count: " + count)") == "String");
        CHECK(type("0xFF000123") == "Int");
        CHECK(type("!container.Data.Enabled") == "Bool");
    }

    TEST_CASE("Diagnostics distinguish unknown variables, absent members and unavailable metadata") {
        const auto environment = Environment();
        CHECK_THROWS_WITH(
            ExpressionTypes("unknown.GetCount()", environment).Result(),
            "Unknown variable or script: unknown. Declare a variable, select an instance, or check the script name.");
        CHECK_THROWS_WITH(ExpressionTypes("optional.Read()", environment).Result(),
                          "Cannot use Unavailable.Read: Skyrim could not link this script's metadata");
        CHECK_THROWS_WITH(ExpressionTypes("Service.GetOptional()", environment).Result(),
                          "Result type unavailable: Unavailable. Skyrim could not link this script's metadata");
        CHECK_THROWS_WITH(ExpressionTypes("container.Data.Missing", environment).Result(),
                          "Cannot read Record.Missing: its type is missing from VM metadata.");
        CHECK_THROWS_WITH(ExpressionTypes("container.Accept()", environment).Result(),
                          "Container.Accept requires 1 arguments; supply optional arguments too.");
        CHECK_THROWS(ExpressionTypes("container.Clear() + 1", environment).Result());
        CHECK_THROWS(ExpressionTypes("count[0]", environment).Result());
        CHECK_THROWS(ExpressionTypes("Service", environment).Result());
    }

    TEST_CASE("Declared types survive nullable results and stale callbacks") {
        Variables<int> variables;
        const Ticket first{1, 0}, replacement{2, 0};
        REQUIRE(variables.Reserve("record", first, "Record"));
        variables.Complete("record", first, 0);
        CHECK(variables.Find("record")->declaredType == "Record");
        REQUIRE(variables.Reserve("record", replacement, "Record"));
        variables.Complete("record", first, 99);
        CHECK(variables.Find("record")->pending);
        CHECK(variables.Find("record")->declaredType == "Record");
        variables.Complete("record", replacement, 0);
        CHECK(*variables.Find("record")->value == 0);
    }

    TEST_CASE("Late inference cannot change a newer binding's type") {
        Variables<int> variables;
        const Ticket first{1, 0}, replacement{2, 0};
        REQUIRE(variables.Reserve("record", first));
        CHECK(variables.SetDeclaredType("record", first, "Record"));
        variables.Complete("record", first, 0);
        CHECK(variables.Find("record")->declaredType == "Record");
        REQUIRE(variables.Reserve("record", replacement));
        CHECK_FALSE(variables.SetDeclaredType("record", first, "Record"));
        CHECK(variables.SetDeclaredType("record", replacement, "Int"));
        variables.Complete("record", replacement, 3);
        CHECK(variables.Find("record")->declaredType == "Int");
        variables.Unset("record");
        CHECK_FALSE(variables.SetDeclaredType("record", replacement, "Float"));
    }
}
