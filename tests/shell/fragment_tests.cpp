#include <doctest/doctest.h>
#include "shell/Command.h"

using namespace elder_shell;

TEST_CASE("Blocks require explicit multiline mode and cannot escape the generated function") {
    const std::string source = "Int count = 0\nWhile count < 3\ncount += 1\nEndWhile";
    CHECK(std::holds_alternative<std::string>(Parse(source)));
    const auto parsed = Parse(source, true);
    REQUIRE(std::holds_alternative<Command>(parsed));
    CHECK(std::get<Command>(parsed).evaluation.multiline);
    for (const auto invalid : {"If True\n", "EndWhile", "If True\nEndWhile", "Else", "While True\nEndIf",
                               "EndFunction\nFunction Other()", "Scriptname Other", "; only a comment"})
        CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(invalid, true)), invalid);
}

TEST_CASE("Fragment comments do not introduce metadata dependencies or alter string literals") {
    const auto code =
        FragmentCode("; IgnoreMissingType\nString text = \"semi; colon\" ; comment\n;/ hidden\nother /;\n");
    CHECK(code.find("IgnoreMissingType") == std::string::npos);
    CHECK(code.find("semi; colon") != std::string::npos);
    CHECK(code.find("hidden") == std::string::npos);
    CHECK(std::ranges::count(code, '\n') == 4);
    CHECK_THROWS(FragmentCode(";/ unclosed"));
    CHECK_THROWS(FragmentCode("String text = \"unclosed"));
}

TEST_CASE("Fragment top-level declarations and session assignments export typed fields") {
    const EvaluationEnvironment environment{{}, {{"Int", "count"}, {"String", "label"}}, {}};
    const auto plan =
        ResolveFragment("var total = count + 1\ncount = total\nString text\nlabel = \"done\"", environment);
    REQUIRE(plan.outputs.size() == 4);
    CHECK(plan.outputs[0].name == "total");
    CHECK(plan.outputs[0].type == "Int");
    CHECK(plan.outputs[1].name == "count");
    CHECK(plan.outputs[2].name == "text");
    CHECK(plan.outputs[2].type == "String");
    CHECK(plan.body.find("total =  count + 1") != std::string::npos);
    const auto source = FragmentSource(plan, "FragmentFixture");
    CHECK(source.find("Int total\n") != std::string::npos);
    CHECK(source.find("String text\n") != std::string::npos);
    CHECK(source.find("Function Run()\n") != std::string::npos);
    CHECK(source.find("Global") == std::string::npos);
    CHECK_THROWS(ResolveFragment("String count = \"changed type\"", environment));
    CHECK_THROWS(ResolveFragment("Int value = 1\nInt VALUE = 2", {}));
    CHECK_THROWS(ResolveFragment("var value = None", {}));
}

TEST_CASE("Block declarations stay local and each branch restores its enclosing scope") {
    const EvaluationEnvironment environment{{}, {{"Int", "count"}}, {}};
    const auto plan = ResolveFragment(
        "If True\nvar local = count + 1\nlocal += 1\nElse\nvar local = \"text\"\nEndIf\ncount += 1", environment);
    REQUIRE(plan.outputs.size() == 1);
    CHECK(plan.outputs[0].name == "count");
    CHECK(plan.body.find("Int ElderShellLocal0 = ") != std::string::npos);
    CHECK(plan.body.find("String ElderShellLocal1 = ") != std::string::npos);
    CHECK(plan.body.find("ElderShellLocal0 += 1") != std::string::npos);
    CHECK_THROWS(ResolveFragment("If True\nInt hidden = 1\nEndIf\nvar leaked = hidden", {}));
    CHECK(FragmentWrites("If True\nInt count = 1\ncount += 1\nEndIf").empty());
}
