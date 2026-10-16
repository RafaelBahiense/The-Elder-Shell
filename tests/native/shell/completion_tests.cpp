#include <doctest/doctest.h>
#include "shell/Completion.h"

using namespace elder_shell;

namespace {
    EvaluationEnvironment CompletionEnvironment() {
        return {{{"BaseRecord", {}, {{"Reset", {}, {}, false}}, {{"Enabled", "Bool", true, true}}},
                 {"Record",
                  "BaseRecord",
                  {{"Describe", "String", {{"String", "prefix"}, {"Int", "count"}}, false},
                   {"Create", "Record", {}, true},
                   {"Broken", {}, {{"none", "missing"}}, false}},
                  {{"Value", "Float", true, true}}},
                 {"Service", {}, {{"Lookup", "Record", {{"Int", "id"}}, true}}, {}}},
                {{"Record", "record"}, {"Record[]", "records"}, {"Int", "count"}},
                {}};
    }

    CompletionResult Suggestions(std::string_view text) {
        return Complete(text, text.size(), CompletionEnvironment());
    }

    bool Contains(const CompletionResult& result, std::string_view name) {
        return std::ranges::any_of(result.items, [&](const auto& item) { return item.name == name; });
    }

    TEST_CASE("Completion offers case-insensitive variables and inherited instance members") {
        CHECK(Contains(Suggestions("REC"), "record"));
        const auto result = Suggestions("record.");
        CHECK(Contains(result, "Describe"));
        CHECK(Contains(result, "Enabled"));
        CHECK(Contains(result, "Reset"));
        CHECK(Contains(result, "Value"));
        CHECK_FALSE(Contains(result, "Create"));
        CHECK_FALSE(Contains(result, "Broken"));
        CHECK(Contains(Suggestions("record.des"), "Describe"));
        CHECK(Contains(Suggestions("record . des"), "Describe"));
        CHECK(Suggestions("record.Broken(").signature.empty());
    }

    TEST_CASE("Globals, array members and chained results use their declared receiver type") {
        CHECK(Contains(Suggestions("Service."), "Lookup"));
        CHECK_FALSE(Contains(Suggestions("Service."), "Value"));
        CHECK(Contains(Suggestions("records."), "Length"));
        CHECK(Contains(Suggestions("records[0]."), "Value"));
        CHECK(Contains(Suggestions("Service.Lookup(1)."), "Describe"));
        CHECK(Contains(Suggestions("var value = Service.Lookup(count)."), "Value"));
        CHECK(Contains(Suggestions("(records[0] as Record)."), "Value"));
    }

    TEST_CASE("Signature hints identify the current argument without evaluating calls") {
        auto result = Suggestions("record.Describe(");
        CHECK(result.signature == "Describe(String prefix, Int count) -> String");
        CHECK(result.parameter == "Argument 1: String prefix");
        result = Suggestions("record.Describe(\"hello, world\", ");
        CHECK(result.parameter == "Argument 2: Int count");
        result = Suggestions("record.Describe(Service.Lookup(1).Describe(\"x\", 2), ");
        CHECK(result.parameter == "Argument 2: Int count");
        CHECK(Suggestions("Service.Lookup(").parameter == "Argument 1: Int id");
        CHECK(Suggestions("record.Describe(\"x\", records[0].").parameter == "Argument 2: Int count");
        CHECK(Suggestions("record.Reset(").signature == "Reset() -> None");
        CHECK(Suggestions("record.Describe(\"x\", 1)").signature.empty());
    }

    TEST_CASE("Quoted text suppresses suggestions but preserves the surrounding signature") {
        const auto result = Suggestions("record.Describe(\"prefix, record.");
        CHECK(result.items.empty());
        CHECK(result.parameter == "Argument 1: String prefix");
        CHECK(Suggestions("\"record.\"").items.empty());
        CHECK(Suggestions("record.Describe(\"escaped \\\" quote").items.empty());
    }

    TEST_CASE("Replacement ranges preserve suffixes, following arguments and cursor position") {
        const std::string source = "record.Describe(\"text\", count)";
        const auto cursor = source.find("Describe") + 3;
        const auto result = Complete(source, cursor, CompletionEnvironment());
        CHECK(result.context.prefix == "Des");
        CHECK(source.substr(result.context.begin, result.context.end - result.context.begin) == "Describe");
        CHECK(Contains(result, "Describe"));
        CHECK(source.substr(result.context.end) == "(\"text\", count)");
    }

    TEST_CASE("Unknown receivers and cyclic metadata cannot produce unsafe suggestions") {
        CHECK(Suggestions("unknown.").items.empty());
        CHECK(Suggestions("record.Unknown().").items.empty());
        auto environment = CompletionEnvironment();
        environment.scripts[0].parent = "Record";
        CHECK(Contains(Complete("record.", 7, environment), "Value"));
        environment.issues.push_back({"Record", {}, "Unlinked metadata"});
        CHECK(Complete("record.", 7, environment).items.empty());
    }
}
