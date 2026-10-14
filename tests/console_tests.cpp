#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>
#include <array>

#include "Command.h"
#include "Requests.h"
#include "Variables.h"
#include "History.h"
#include "PropertyRead.h"
#include "ValueRules.h"
#include "EvaluationGate.h"
#include "StateFunctionLayout.h"

using namespace papyrus_console;

namespace {
    TEST_CASE("Command history restores the draft at the end and stops at its boundaries") {
        History history;
        CHECK_FALSE(history.Previous("draft"));
        CHECK_FALSE(history.Next());
        history.Add("first");
        history.Add("second");
        CHECK(history.Previous("unfinished") == "second");
        CHECK(history.Previous("second") == "first");
        CHECK_FALSE(history.Previous("first"));
        CHECK(history.Next() == "second");
        CHECK(history.Next() == "unfinished");
        CHECK_FALSE(history.Next());
    }

    TEST_CASE("History bounds entries, skips consecutive duplicates and resets after edits") {
        History history;
        history.Add("same");
        history.Add("same");
        CHECK(history.Previous("") == "same");
        CHECK_FALSE(history.Previous("same"));
        history.ResetNavigation();
        CHECK(history.Previous("edited") == "same");
        CHECK(history.Next() == "edited");
        for (int i = 0; i < 130; ++i)
            history.Add(std::to_string(i));
        int count = 0;
        while (history.Previous(""))
            ++count;
        CHECK(count == 128);
    }

    Command RequireCommand(std::string_view text) {
        const auto result = Parse(text);
        REQUIRE(std::holds_alternative<Command>(result));
        return std::get<Command>(result);
    }

    std::vector<std::string> ArgumentTexts(const Command& command) {
        std::vector<std::string> result;
        for (const auto& argument : command.arguments)
            result.push_back(argument.text);
        return result;
    }

    TEST_CASE("Property chains advance direct reads and await computed getter results") {
        PropertyRead<int> read(1, {"record", "values"});
        CHECK(read.NextProperty() == "record");
        CHECK(read.Advance(2));
        CHECK(read.NextProperty() == "values");
        const auto receipt = read.Suspend();
        REQUIRE(receipt);
        CHECK_FALSE(read.Advance(99));
        CHECK_FALSE(read.Suspend());
        CHECK(read.CurrentValue() == 2);
        CHECK(read.Resume(*receipt, 3));
        CHECK(read.CurrentValue() == 3);
        CHECK_FALSE(read.NextProperty());
        CHECK_FALSE(read.Suspend());
        CHECK_FALSE(read.Advance(4));
        CHECK_FALSE(read.Resume(*receipt, 5));
    }

    TEST_CASE("A duplicate getter callback cannot resume a later step") {
        PropertyRead<int> read(0, {"first", "second"});
        const auto first = read.Suspend();
        REQUIRE(first);
        CHECK(read.Resume(*first, 1));
        const auto second = read.Suspend();
        REQUIRE(second);
        CHECK_FALSE(read.Resume(*first, 99));
        CHECK(read.NextProperty() == "second");
        CHECK(read.Resume(*second, 2));
        CHECK(read.CurrentValue() == 2);
    }

    TEST_CASE("Property inspection resolves a variable root and an ordered chain") {
        const auto simple = RequireCommand("show container.record");
        CHECK(simple.operation == Operation::Value);
        CHECK(simple.value.text == "container");
        CHECK(simple.value.accesses == std::vector<AccessStep>{AccessStep::Property("record")});
        const auto chain = RequireCommand("show $container.record.Values");
        CHECK(chain.value.text == "container");
        CHECK(chain.value.accesses ==
              std::vector<AccessStep>{AccessStep::Property("record"), AccessStep::Property("Values")});
        CHECK(RequireCommand("show $container").value.accesses.empty());
        const auto assignment = RequireCommand("let record = $container.record");
        CHECK(assignment.assignment == "record");
        CHECK(assignment.value.text == "container");
        CHECK(assignment.value.accesses == simple.value.accesses);
        CHECK(RequireCommand("let copy = $container").value.accesses.empty());
        CHECK(RequireCommand("let record = container.record").value.accesses == simple.value.accesses);
    }

    TEST_CASE("Invalid property paths and dotted unset names are rejected") {
        for (const auto invalid :
             {"show container.", "show .record", "show container..record", "show container.record()",
              "show container.record extra", "unset container.record", "let record = $container.",
              "let record = $container.record extra", "show $container.bad-name"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(invalid)), invalid);
        std::string path = "show container";
        for (int i = 0; i < 16; ++i)
            path += ".value";
        CHECK(RequireCommand(path).value.accesses.size() == 16);
        CHECK(std::holds_alternative<std::string>(Parse(path + ".value")));
    }

    TEST_CASE("Parenthesized calls preserve types, quoting and the existing command syntax") {
        const auto random = RequireCommand("global Utility.RandomInt(1, 10)");
        CHECK(ArgumentTexts(random) == std::vector<std::string>{"1", "10"});
        CHECK(RequireCommand("call @player Actor.GetLevel()").arguments.empty());
        const auto method = RequireCommand("let results = call $service FixtureService.Transform(@FF000123, $inputs)");
        CHECK(method.assignment == "results");
        CHECK(method.target == "$service");
        CHECK(ArgumentTexts(method) == std::vector<std::string>{"@FF000123", "inputs"});
        const auto strings = RequireCommand(R"cmd(global Test.Run("a,(b)", "", "$literal"))cmd");
        CHECK(ArgumentTexts(strings) == std::vector<std::string>{"a,(b)", "", "$literal"});
        for (const auto& argument : strings.arguments)
            CHECK(argument.kind == ValueKind::String);
        CHECK(ArgumentTexts(RequireCommand("global Utility.RandomInt ( 1 , 10 )")) == ArgumentTexts(random));
        CHECK(ArgumentTexts(RequireCommand("global Utility.RandomInt 1 10")) == ArgumentTexts(random));
    }

    TEST_CASE("Parenthesized calls reject ambiguous separators and nested expressions") {
        for (const auto invalid :
             {"global Test.Run(1 2)", "global Test.Run(,1)", "global Test.Run(1,)", "global Test.Run(1,,2)",
              "global Test.Run((1))", "global Test.Run(1", "global Test.Run(1) extra", "global Test.Run 1, 2",
              "global Test.Run(1))", "get @player Actor.Name()"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(invalid)), invalid);
        std::string largest = "global Test.Run(";
        for (std::size_t i = 0; i < MaximumArguments; ++i) {
            if (i)
                largest += ',';
            largest += '1';
        }
        largest += ')';
        CHECK(RequireCommand(largest).arguments.size() == MaximumArguments);
        largest.insert(largest.size() - 1, ",1");
        CHECK(std::holds_alternative<std::string>(Parse(largest)));
    }

    TEST_CASE("String assignments preserve empty values, escapes and literal variable syntax") {
        const auto text = RequireCommand(R"(let name = "Sample User")");
        CHECK(text.operation == Operation::Value);
        CHECK(text.assignment == "name");
        CHECK(text.value.text == "Sample User");
        CHECK(RequireCommand(R"(let empty = "")").value.text.empty());
        CHECK(RequireCommand(R"(let literal = "$name")").value.text == "$name");
        const auto escaped = RequireCommand(R"(let text = "a\nb\t\"c\\")");
        CHECK(escaped.value.text == "a\nb\t\"c\\");
        CHECK(RequireCommand(R"(let text = "Olá")").value.text == "Olá");
        for (const auto invalid : {"let x = unquoted", "let x =", "let x = \"a\" extra", "let x \"=\" \"a\""})
            CHECK(std::holds_alternative<std::string>(Parse(invalid)));
    }

    TEST_CASE("Function string literals preserve whitespace and do not expand variables") {
        const auto call = RequireCommand(R"(global Debug.Notification "Hello $name")");
        CHECK(ArgumentTexts(call) == std::vector<std::string>{"Hello $name"});
        CHECK(call.arguments[0].kind == ValueKind::String);
        const auto empty = RequireCommand(R"(call @player Test.SetName "")");
        CHECK(empty.arguments[0].text.empty());
        CHECK(empty.arguments[0].kind == ValueKind::String);
    }

    TEST_CASE("Assignments preserve variable references and quoted literal dollars") {
        const auto assignment = RequireCommand("let inputs = call $service FixtureService.GetValues @player");
        CHECK(assignment.assignment == "inputs");
        CHECK(assignment.target == "$service");
        const auto call = RequireCommand(R"(call $service FixtureService.Transform @player $inputs)");
        CHECK(call.arguments[1].text == "inputs");
        CHECK(call.arguments[1].kind == ValueKind::Reference);
        const auto literal = RequireCommand(R"(global Debug.Notification "$inputs")");
        CHECK(literal.arguments[0].kind == ValueKind::String);
        CHECK(RequireCommand("let alias = get 00000042 FixtureController.Target").assignment == "alias");
        for (const auto text : {"let bad-name = global Game.GetPlayer", "let x global Game.GetPlayer", "let x = vars",
                                "let x = methods Actor", "vars extra", "show $", "unset a b"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(text)), text);
    }

    TEST_CASE("Variable management commands have exact arity") {
        CHECK(RequireCommand("vars").operation == Operation::Variables);
        CHECK(RequireCommand("show $inputs").value.text == "inputs");
        CHECK(RequireCommand("unset inputs").operation == Operation::Unset);
        CHECK(RequireCommand("clearvars").operation == Operation::ClearVariables);
    }

    TEST_CASE("Literal assignments preserve numeric, boolean and reference types") {
        CHECK(RequireCommand("let count = -42").value.kind == ValueKind::Integer);
        CHECK(RequireCommand("let ratio = 1e2").value.kind == ValueKind::Float);
        CHECK(RequireCommand("let active = TRUE").value.kind == ValueKind::Boolean);
        CHECK(RequireCommand("let empty = None").value.kind == ValueKind::None);
        CHECK(RequireCommand("let player = @player").value.kind == ValueKind::Form);
        const auto quoted = RequireCommand(R"(let text = "42")");
        CHECK(quoted.value.kind == ValueKind::String);
        for (const auto text : {"let x = 2147483648", "let x = 1e100", "let x = 1.2suffix", "let x = @missing"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(text)), text);
    }

    TEST_CASE("Array literals keep explicit types and separate their commas from call arguments") {
        const auto array = RequireCommand("let values = float[1, 0.66, 3]").value;
        CHECK(array.kind == ValueKind::Array);
        CHECK(array.elementType == "float");
        REQUIRE(array.elements.size() == 3);
        CHECK(array.elements[0].kind == ValueKind::Integer);
        CHECK(array.elements[1].kind == ValueKind::Float);
        CHECK(RequireCommand("let empty = Actor[]").value.elements.empty());
        const auto call = RequireCommand("call $core Test.Run([1, 2], float[], $container.record, true)");
        REQUIRE(call.arguments.size() == 4);
        CHECK(call.arguments[0].elements.size() == 2);
        CHECK(call.arguments[1].elementType == "float");
        CHECK(call.arguments[2].accesses == std::vector<AccessStep>{AccessStep::Property("record")});
        for (const auto text : {"let x = [1 2]", "let x = [1,]", "let x = [1", "let x = [[1]]"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(text)), text);
        std::string largest = "let x = [1";
        for (std::size_t i = 1; i < MaximumArrayElements; ++i)
            largest += ",1";
        CHECK(RequireCommand(largest + "]").value.elements.size() == MaximumArrayElements);
        CHECK(std::holds_alternative<std::string>(Parse(largest + ",1]")));
    }

    TEST_CASE("Array paths retain index and property order") {
        const auto value = RequireCommand("show $containers[0].record.values[2]").value;
        CHECK(value.text == "containers");
        CHECK(value.accesses == std::vector<AccessStep>{AccessStep::Index(0), AccessStep::Property("record"),
                                                        AccessStep::Property("values"), AccessStep::Index(2)});
        CHECK(RequireCommand("let size = $values.Length").value.accesses ==
              std::vector<AccessStep>{AccessStep::Property("Length")});
        for (const auto text : {"show $values[-1]", "show $values[1.0]", "show $values[]", "show $values[0",
                                "show $values[0].", "show $values[4294967296]"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(text)), text);
    }

    TEST_CASE("Property writes parse receiver chains and a single typed value") {
        const auto set = RequireCommand("set $containers[0].record.Values = float[1, 2]");
        CHECK(set.operation == Operation::Set);
        CHECK(set.destination.text == "containers");
        CHECK(set.destination.accesses == std::vector<AccessStep>{AccessStep::Index(0), AccessStep::Property("record"),
                                                                  AccessStep::Property("Values")});
        CHECK(set.value.elementType == "float");
        CHECK(RequireCommand("set container.Enabled = false").value.kind == ValueKind::Boolean);
        CHECK(RequireCommand("set $container.record = None").value.kind == ValueKind::None);
        for (const auto text : {"set container = 1", "set $container.record 1", "set $container.record = 1 extra",
                                "set $container.record = unquoted", "set $values[0] = 1"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(text)), text);
    }

    TEST_CASE("Array inference promotes numbers and permits nullable objects without unrelated coercions") {
        using C = ValueCategory;
        const std::array integers{C::Integer, C::Integer};
        const std::array numeric{C::Integer, C::Float, C::Integer};
        const std::array objects{C::None, C::Object, C::None};
        const std::array nulls{C::None};
        const std::array incompatible{C::Integer, C::Boolean};
        const std::array nested{C::Array};
        CHECK(InferArrayCategory(integers) == C::Integer);
        CHECK(InferArrayCategory(numeric) == C::Float);
        CHECK(InferArrayCategory(objects) == C::Object);
        CHECK(InferArrayCategory(nulls) == C::Object);
        CHECK_FALSE(InferArrayCategory(incompatible));
        CHECK_FALSE(InferArrayCategory(nested));
        CHECK_FALSE(InferArrayCategory({}));
    }

    TEST_CASE("Compiled statements keep Papyrus syntax and explicit result types") {
        const auto command = RequireCommand("eval FixtureRecord record = $container.GetRecord()");
        CHECK(command.operation == Operation::Evaluate);
        CHECK(command.assignment == "record");
        CHECK(command.evaluation.resultType == "FixtureRecord");
        CHECK(command.evaluation.expression == " container.GetRecord()");
        const auto array = RequireCommand("eval Float[] values = container.GetValues()");
        CHECK(array.evaluation.resultType == "Float[]");
        const auto quoted = RequireCommand("eval Debug.Notification(\"Hello $container\")");
        CHECK(quoted.evaluation.expression == " Debug.Notification(\"Hello $container\")");
        for (const auto text : {"eval", "eval Int x =", "eval Var x = 1", "eval Int x = 1\nEndFunction",
                                "eval Debug.Trace(\"x\"); comment"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(text)), text);
    }

    TEST_CASE("Source binding ignores strings and member names and keeps root order") {
        CHECK(SourceRoots("container.GetRecord().Describe(container.record, inputs[0], \"container fake\")") ==
              std::vector<std::string>{"container", "inputs"});
        CHECK(SourceRoots("Service.Get(service, SERVICE)") == std::vector<std::string>{"Service"});
    }

    TEST_CASE("Numeric literals never become evaluation import roots") {
        CHECK(SourceRoots("Game.GetFormEx(0xFF000123) as Actor") == std::vector<std::string>{"Game", "as", "Actor"});
        CHECK(SourceRoots("Game.GetFormEx(0Xff000123) as Actor") == std::vector<std::string>{"Game", "as", "Actor"});
        CHECK(SourceRoots("0xE-amount + 1.5e-3 * fcontainer + .25E+2") ==
              std::vector<std::string>{"amount", "fcontainer"});
        CHECK(SourceRoots("0xFF000123 + xFF000123") == std::vector<std::string>{"xFF000123"});
        CHECK(SourceRoots("123 + value[0] + 0.66") == std::vector<std::string>{"value"});
    }

    TEST_CASE("Compilation is pending and cannot revive after a save load") {
        Requests requests;
        const auto ticket = requests.Begin("eval Int x = 1");
        REQUIRE(ticket);
        requests.Compiling(*ticket);
        CHECK(requests.Current(*ticket));
        CHECK(requests.Entries()[0].status == Status::Compiling);
        requests.ClearFinished();
        CHECK(requests.Entries().size() == 1);
        requests.Loaded();
        requests.Submitted(*ticket);
        requests.Complete(*ticket, "1");
        CHECK(requests.Entries()[0].status == Status::Abandoned);
    }

    TEST_CASE("Late or duplicate completions cannot unlock a newer evaluation") {
        EvaluationGate gate;
        const Ticket old{1, 0}, fresh{2, 1};
        CHECK(gate.Begin(old));
        CHECK(gate.PreviousSession(fresh));
        CHECK_FALSE(gate.Begin(fresh));
        CHECK_FALSE(gate.Release(fresh));
        CHECK(gate.Release(old));
        CHECK(gate.Begin(fresh));
        CHECK_FALSE(gate.PreviousSession(fresh));
        CHECK_FALSE(gate.Release(old));
        CHECK_FALSE(gate.Begin(old));
        CHECK(gate.Release(fresh));
    }

    TEST_CASE("Named-state offsets preserve high bits independently of function count") {
        const auto layout = StateFunctionLayout::Decode(0x79014);
        CHECK(layout.count == 20);
        CHECK(layout.offset == 968);
        for (const std::uint32_t offset : {16u, 504u, 512u, 968u, 8192u, 0x7FFFF8u}) {
            for (const std::uint32_t count : {0u, 1u, 20u, 511u}) {
                const auto decoded = StateFunctionLayout::Decode((offset << 9) | count);
                CHECK(decoded.count == count);
                CHECK(decoded.offset == offset);
            }
        }
    }

    TEST_CASE("Named-state ranges reject truncated offsets into another state record") {
        // 40 state headers precede the function table. The old 9-bit offset
        // turns 968 into 456, which lands in those headers instead of functions.
        const auto valid = StateFunctionLayout::Decode(0x79014);
        CHECK(valid.Position(0, 40, 80) == 968);
        CHECK_FALSE((StateFunctionLayout{20, 456}.Position(0, 40, 80)));
        CHECK_FALSE((StateFunctionLayout{20, 969}.Position(0, 40, 80)));
        CHECK_FALSE((StateFunctionLayout{40, 1200}.Position(0, 40, 80)));
        CHECK_FALSE(valid.Position(40, 40, 80));
    }

    TEST_CASE("Named-state function ranges allow metadata before the function array") {
        // Fixture with ten headers, 36 bytes of additional
        // metadata, then 34 functions. Offsets are relative to each header.
        constexpr std::array counts{3u, 3u, 2u, 2u, 3u, 4u, 9u, 3u, 3u, 2u};
        constexpr std::array offsets{196u, 204u, 212u, 212u, 212u, 220u, 236u, 292u, 300u, 308u};
        std::size_t expected = 196;
        for (std::size_t i = 0; i < counts.size(); ++i) {
            const StateFunctionLayout layout{counts[i], offsets[i]};
            CHECK(layout.Position(i, counts.size(), 34, 196) == expected);
            expected += counts[i] * 8;
        }
        CHECK(expected == 468);
        CHECK_FALSE((StateFunctionLayout{3, 197}.Position(0, 10, 34, 196)));
        CHECK_FALSE((StateFunctionLayout{3, 156}.Position(0, 10, 34, 156)));
        CHECK_FALSE((StateFunctionLayout{3, 308}.Position(9, 10, 34, 196)));
    }

    TEST_CASE("Late returns cannot overwrite newer assignments or resurrect removed variables") {
        Variables<std::vector<float>> variables;
        const Ticket old{1, 0}, fresh{2, 0};
        REQUIRE(variables.Reserve("Inputs", old));
        CHECK(variables.Find("INPUTS")->pending);
        REQUIRE(variables.Reserve("inputs", fresh));
        variables.Complete("inputs", old, {1.f});
        CHECK(variables.Find("inputs")->pending);
        variables.Complete("inputs", fresh, {2.f, 3.f});
        REQUIRE(variables.Find("inputs")->value);
        CHECK(*variables.Find("inputs")->value == std::vector<float>{2.f, 3.f});
        variables.Reject(fresh);
        variables.Complete("inputs", fresh, {99.f});
        CHECK(*variables.Find("inputs")->value == std::vector<float>{2.f, 3.f});
        CHECK(variables.Unset("INPUTS"));
        variables.Complete("inputs", fresh, {4.f});
        CHECK(variables.Find("inputs") == nullptr);
    }

    TEST_CASE("Failed assignments and session clears cannot expose stale values") {
        Variables<int> variables;
        REQUIRE(variables.Reserve("value", Ticket{1, 0}));
        variables.Complete("value", Ticket{1, 0}, 42);
        REQUIRE(variables.Reserve("value", Ticket{2, 0}));
        CHECK_FALSE(variables.Find("value")->value);
        variables.Reject(Ticket{1, 0});
        CHECK(variables.Find("value")->pending);
        variables.Reject(Ticket{2, 0});
        CHECK_FALSE(variables.Find("value")->pending);
        CHECK_FALSE(variables.Find("value")->value);
        variables.Clear();
        REQUIRE(variables.Reserve("value", Ticket{3, 1}));
        variables.Complete("value", Ticket{2, 0}, 99);
        CHECK(variables.Find("value")->pending);
    }

    TEST_CASE("Variable capacity permits replacement and reclamation") {
        Variables<int> variables;
        for (int i = 0; i < 64; ++i)
            REQUIRE(variables.Reserve("v" + std::to_string(i), Ticket{1, 0}));
        CHECK_FALSE(variables.Reserve("overflow", Ticket{2, 0}));
        CHECK(variables.Reserve("v0", Ticket{2, 0}));
        CHECK(variables.Unset("v1"));
        CHECK(variables.Reserve("overflow", Ticket{2, 0}));
    }

    TEST_CASE("Global and instance commands have explicit receivers") {
        const auto global = RequireCommand("global Utility.RandomInt 1 10");
        CHECK(global.operation == Operation::Global);
        CHECK(global.script == "Utility");
        CHECK(global.function == "RandomInt");
        CHECK(ArgumentTexts(global) == std::vector<std::string>{"1", "10"});
        const auto method = RequireCommand("call @player Actor.GetLevel");
        CHECK(method.operation == Operation::Method);
        CHECK(method.target == "@player");
        CHECK(method.arguments.empty());
    }

    TEST_CASE("Quoted arguments preserve whitespace, empty strings and escapes") {
        auto command = RequireCommand(R"(global Debug.Trace "Hello world" 0)");
        CHECK(command.arguments[0].text == "Hello world");
        command = RequireCommand(R"(global Test.Run "" "a\nb\t\"c\\")");
        CHECK(command.arguments[0].text.empty());
        CHECK(command.arguments[1].text == "a\nb\t\"c\\");
    }

    TEST_CASE("Malformed or oversized commands are rejected") {
        for (const auto text : {"", "global", "global Actor", "global .GetLevel", "global Actor.Get.Level",
                                "call broken Actor.GetLevel", "global Debug.Trace \"unfinished",
                                "global Debug.Trace \"x\"y", "methods Actor extra", "get @player Actor.Level 1"})
            CHECK_MESSAGE(std::holds_alternative<std::string>(Parse(text)), text);
        CHECK(std::holds_alternative<std::string>(Parse(std::string(MaximumCommandLength + 1, 'a'))));
        std::string tooMany = "global Test.Run";
        for (std::size_t i = 0; i <= MaximumArguments; ++i)
            tooMany += " 1";
        CHECK(std::holds_alternative<std::string>(Parse(tooMany)));
    }

    TEST_CASE("Form IDs are runtime hexadecimal identifiers with a player shorthand") {
        CHECK(FormID("@player") == 0x14);
        CHECK(FormID("PLAYER") == 0x14);
        CHECK(FormID("@0xFF000ABC") == 0xFF000ABC);
        CHECK(FormID("00000042") == 0x00000042);
        for (const auto text : {"", "0", "GG1234", "123456789", "14garbage", "-14"})
            CHECK_FALSE(FormID(text));
    }

    TEST_CASE("Argument numbers require full, finite values within their type's range") {
        CHECK(NumberArgument<std::int32_t>("-42") == -42);
        CHECK(NumberArgument<float>("1.5") == doctest::Approx(1.5f));
        CHECK(NumberArgument<float>("1e2") == doctest::Approx(100));
        CHECK_FALSE(NumberArgument<std::int32_t>("2147483648"));
        CHECK_FALSE(NumberArgument<std::int32_t>("1.5"));
        CHECK_FALSE(NumberArgument<float>("1,5"));
        CHECK_FALSE(NumberArgument<float>("nan"));
        CHECK_FALSE(NumberArgument<float>("inf"));
        CHECK_FALSE(NumberArgument<float>("1.5extra"));
    }

    TEST_CASE("Metadata and property commands retain their distinct operations") {
        CHECK(RequireCommand("methods Actor").operation == Operation::Methods);
        const auto property = RequireCommand("get 00000043 FixtureController.Target");
        CHECK(property.operation == Operation::Property);
        CHECK(property.function == "Target");
        CHECK(RequireCommand("GLOBAL Game.GetPlayer").operation == Operation::Global);
    }

    TEST_CASE("A completion receipt is required before a submitted call is completed") {
        Requests requests;
        const auto ticket = requests.Begin("global Game.GetPlayer");
        REQUIRE(ticket);
        requests.Submitted(*ticket);
        CHECK(requests.Entries()[0].status == Status::Submitted);
        requests.Complete(*ticket, "Actor");
        CHECK(requests.Entries()[0].status == Status::Completed);
        requests.Reject(*ticket, "late dispatch rejection");
        CHECK(requests.Entries()[0].result == "Actor");
    }

    TEST_CASE("Completion can arrive synchronously without being overwritten by submitted status") {
        Requests requests;
        const auto ticket = requests.Begin("global Test.Immediate");
        REQUIRE(ticket);
        requests.Complete(*ticket, "42");
        requests.Submitted(*ticket);
        CHECK(requests.Entries()[0].status == Status::Completed);
        CHECK(requests.Entries()[0].result == "42");
    }

    TEST_CASE("Save changes invalidate queued calls and reject old completion receipts") {
        Requests requests;
        const auto old = requests.Begin("call @player Actor.GetLevel");
        REQUIRE(old);
        requests.Loaded();
        CHECK_FALSE(requests.Current(*old));
        CHECK(requests.Entries()[0].status == Status::Abandoned);
        const auto fresh = requests.Begin("global Game.GetPlayer");
        REQUIRE(fresh);
        CHECK(fresh->id > old->id);
        requests.Complete(*old, "late result");
        CHECK(requests.Entries()[1].status == Status::Queued);
        CHECK(requests.Entries()[0].status == Status::Abandoned);
    }

    TEST_CASE("Clearing results leaves pending requests available for completion") {
        Requests requests;
        const auto pending = requests.Begin("global Test.Wait");
        const auto done = requests.Begin("global Test.Done");
        REQUIRE(pending);
        REQUIRE(done);
        requests.Complete(*done, "true");
        requests.ClearFinished();
        REQUIRE(requests.Entries().size() == 1);
        CHECK(requests.Current(*pending));
        requests.Complete(*pending, "false");
        CHECK(requests.Entries()[0].status == Status::Completed);
    }

    TEST_CASE("Pending calls are bounded and completed history is trimmed") {
        Requests requests;
        for (int i = 0; i < 32; ++i)
            REQUIRE(requests.Begin("global Test.Wait"));
        CHECK_FALSE(requests.Begin("global Test.Overflow"));
        const auto pending = requests.Entries()[0].ticket;
        requests.Complete(pending, "None");
        REQUIRE(requests.Begin("global Test.Replacement"));
        requests.Loaded();
        for (int i = 0; i < 150; ++i) {
            const auto ticket = requests.Begin("global Test.Immediate");
            REQUIRE(ticket);
            requests.Complete(*ticket, "None");
        }
        CHECK(requests.Entries().size() <= 128);
    }
}
