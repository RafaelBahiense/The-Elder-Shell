#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "shell/Command.h"
#include "shell/Requests.h"
#include "shell/Variables.h"
#include "shell/EvaluationGate.h"

using namespace elder_shell;

namespace {
    Command RequireCommand(std::string_view text) {
        const auto result = Parse(text);
        REQUIRE(std::holds_alternative<Command>(result));
        return std::get<Command>(result);
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
        const auto ticket = requests.Begin("Int x = 1");
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

    TEST_CASE("Rejected fragments preserve captured values without reviving stale assignments") {
        Variables<int> variables;
        REQUIRE(variables.Reserve("count", Ticket{1, 0}, "Int"));
        variables.Complete("count", Ticket{1, 0}, 3);
        REQUIRE(variables.Reserve("count", Ticket{2, 0}, "Float", true));
        CHECK(variables.Find("count")->pending);
        variables.Reject(Ticket{2, 0});
        CHECK_FALSE(variables.Find("count")->pending);
        CHECK(variables.Find("count")->value == 3);
        CHECK(variables.Find("count")->declaredType == "Int");
        REQUIRE(variables.Reserve("count", Ticket{3, 0}, "Int", true));
        variables.Unset("count");
        variables.Complete("count", Ticket{3, 0}, 10);
        CHECK_FALSE(variables.Find("count"));
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

    TEST_CASE("A completion receipt is required before a submitted call is completed") {
        Requests requests;
        const auto ticket = requests.Begin("Game.GetPlayer()");
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
        const auto ticket = requests.Begin("Service.Immediate()");
        REQUIRE(ticket);
        requests.Complete(*ticket, "42");
        requests.Submitted(*ticket);
        CHECK(requests.Entries()[0].status == Status::Completed);
        CHECK(requests.Entries()[0].result == "42");
    }

    TEST_CASE("Save changes invalidate queued calls and reject old completion receipts") {
        Requests requests;
        const auto old = requests.Begin("player.GetLevel()");
        REQUIRE(old);
        requests.Loaded();
        CHECK_FALSE(requests.Current(*old));
        CHECK(requests.Entries()[0].status == Status::Abandoned);
        const auto fresh = requests.Begin("Game.GetPlayer()");
        REQUIRE(fresh);
        CHECK(fresh->id > old->id);
        requests.Complete(*old, "late result");
        CHECK(requests.Entries()[1].status == Status::Queued);
        CHECK(requests.Entries()[0].status == Status::Abandoned);
    }

    TEST_CASE("Clearing results leaves pending requests available for completion") {
        Requests requests;
        const auto pending = requests.Begin("Service.Wait()");
        const auto done = requests.Begin("Service.Done()");
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
            REQUIRE(requests.Begin("Service.Wait()"));
        CHECK_FALSE(requests.Begin("Service.Overflow()"));
        const auto pending = requests.Entries()[0].ticket;
        requests.Complete(pending, "None");
        REQUIRE(requests.Begin("Service.Replacement()"));
        requests.Loaded();
        for (int i = 0; i < 150; ++i) {
            const auto ticket = requests.Begin("Service.Immediate()");
            REQUIRE(ticket);
            requests.Complete(*ticket, "None");
        }
        CHECK(requests.Entries().size() <= 128);
    }
}
