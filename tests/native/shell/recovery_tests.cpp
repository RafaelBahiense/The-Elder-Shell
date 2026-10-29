#include "shell/Recovery.h"
#include "shell/Services.h"
#include <doctest/doctest.h>

using namespace elder_shell::recovery;

TEST_CASE("Repair plans reject changed state and cannot be replayed") {
    Plans plans;
    const auto id = plans.Add({"alias", "example:7", "before", "replace binding"});
    REQUIRE(!id.empty());
    CHECK_FALSE(plans.Consume(id, "alias", "example:7", "after").empty());
    CHECK_FALSE(plans.Consume(id, "alias", "example:7", "before").empty());
    const auto fresh = plans.Add({"alias", "example:7", "before", "replace binding"});
    CHECK(plans.Consume(fresh, "alias", "example:7", "before").empty());
    CHECK_FALSE(plans.Consume(fresh, "alias", "example:7", "before").empty());
}

TEST_CASE("Plans are scoped to target and operation and invalidated on load") {
    Plans plans;
    const auto id = plans.Add({"restart", "quest-a", "before", "start"});
    CHECK_FALSE(plans.Consume(id, "restart", "quest-b", "before").empty());
    const auto second = plans.Add({"restart", "quest-a", "before", "start"});
    CHECK_FALSE(plans.Consume(second, "alias", "quest-a", "before").empty());
    const auto previous = plans.Add({"restart", "quest-a", "before", "start"});
    plans.Clear();
    CHECK(plans.Find(previous) == nullptr);
    const auto next = plans.Add({"restart", "quest-a", "before", "start"});
    CHECK(next > previous);
}

TEST_CASE("Plans have a bounded session allocation") {
    Plans plans;
    for (int i = 0; i < 32; ++i)
        CHECK(plans.Add({"read", "target", "state", "snapshot"}).empty() == false);
    CHECK(plans.Add({"read", "target", "state", "snapshot"}).empty());
    plans.Clear();
    CHECK(plans.Add({"read", "target", "state", "snapshot"}).empty() == false);
}

TEST_CASE("Lifecycle diagnostics identify disabled stage work as stopping") {
    CHECK(Lifecycle(false, false, false) == "Stopped");
    CHECK(Lifecycle(false, true, false) == "Stopping; stage work pending");
    CHECK(Lifecycle(true, true, false) == "Running; stage work pending");
    CHECK(Lifecycle(true, false, true) == "Starting");
    CHECK(Lifecycle(false, false, true) == "Transition pending");
}

TEST_CASE("Saved tokens cannot select another process session or store") {
    Plans previous("previous:repair");
    Plans current("current:repair");
    Plans snapshots("current:snapshot");
    const auto stale = previous.Add({"alias", "quest:7", "before", "replace"});
    const auto live = current.Add({"alias", "quest:7", "before", "replace"});
    const auto read = snapshots.Add({"alias", "quest:7", "before", "replace"});
    CHECK(stale != live);
    CHECK(read != live);
    CHECK_FALSE(current.Consume(stale, "alias", "quest:7", "before").empty());
    CHECK_FALSE(current.Consume(read, "alias", "quest:7", "before").empty());
    CHECK(current.Consume(live, "alias", "quest:7", "before").empty());
}

TEST_CASE("Unverified native cancellation never authorizes a synthetic return") {
    CHECK_FALSE(LatentRecoveryError(false, true, true).empty());
    CHECK_FALSE(LatentRecoveryError(true, false, true).empty());
    CHECK_FALSE(LatentRecoveryError(true, true, false).empty());
    CHECK(LatentRecoveryError(true, true, true).empty());
}

TEST_CASE("Report comparisons preserve duplicate counts and ignore row ordering") {
    CHECK(CompareReports("stage 1\nalias 2", "alias 2\nstage 1") == "No changes in captured fields.");
    const auto difference = CompareReports("stage 1\nalias 2\nalias 2", "stage 10\nalias 2");
    CHECK(difference.find("- stage 1") != std::string::npos);
    CHECK(difference.find("+ stage 10") != std::string::npos);
    CHECK(difference.find("- alias 2") != std::string::npos);
}

TEST_CASE("Diagnostic service defaults are limited to the built-in service scripts") {
    elder_shell::FunctionDeclaration stacks{"InspectStacks", "String", {{"Form", "arg1"}, {"String", "arg2"}}};
    elder_shell::services::ApplyDefaults(elder_shell::services::ScriptsScript, stacks);
    CHECK(stacks.parameters[0].name == "owner");
    CHECK(stacks.parameters[0].defaultValue == "None");
    CHECK(stacks.parameters[1].defaultValue == "\"\"");
    elder_shell::FunctionDeclaration external{"InspectStacks", "String", {{"Form", "arg1"}, {"String", "arg2"}}};
    elder_shell::services::ApplyDefaults("ExampleController", external);
    CHECK(external.parameters[0].defaultValue.empty());
    elder_shell::FunctionDeclaration restart{
        "RestartChecked",
        "String",
        {{"Quest", "target"}, {"String", "plan"}, {"Bool", "resetStages"}, {"Bool", "clearAlreadyRun"}}};
    elder_shell::services::ApplyDefaults(elder_shell::services::QuestsScript, restart);
    CHECK(restart.parameters[1].defaultValue.empty());
    CHECK(restart.parameters[2].defaultValue == "False");
    CHECK(restart.parameters[3].defaultValue == "False");
}
