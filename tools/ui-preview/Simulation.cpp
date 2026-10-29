#include "Simulation.h"
#include "runtime/Compatibility.h"
#include "runtime/CompletionInput.h"
#include "runtime/PapyrusBacklog.h"
#include "shell/Command.h"
#include <chrono>
#include <format>

namespace {
    using namespace elder_shell;
    Requests requests;
    VariableInspection variables;
    InstanceExplorer instances;
    EvaluationUsage usage;
    EvaluationEnvironment metadata;
    PapyrusBacklog backlog;
    bool ready = true;
    std::vector<std::pair<Ticket, std::chrono::steady_clock::time_point>> pending;

    void Record(std::string command, std::string result, bool rejected = false) {
        if (const auto ticket = requests.Begin(std::move(command))) {
            if (rejected)
                requests.Reject(*ticket, std::move(result));
            else
                requests.Complete(*ticket, std::move(result));
        }
    }

    void AddActor() {
        const auto found =
            std::ranges::find_if(variables.variables, [](const auto& row) { return row.name == "player"; });
        if (found != variables.variables.end())
            return;
        InspectedValue actor;
        actor.expression = "player";
        actor.type = "Actor";
        actor.summary = "Sample player (00000014)";

        variables.variables.push_back({"player", {1, 0}, actor});
        metadata.variables.push_back({"Actor", "player"});
    }
}

void preview::Reset(Scenario scenario) {
    elder_shell::ResetPapyrusBacklog(scenario != Scenario::Empty);
    if (scenario != Scenario::Empty)
        elder_shell::RefreshPapyrusBacklog();
    requests = {};
    variables = {};
    variables.status = "Preview variables";
    instances = {};
    usage = {};
    pending.clear();
    ready = true;
    elder_shell::previewTestedRuntime = scenario != Scenario::Untested;
    metadata = {};
    metadata.scripts = {
        {"Form", "", {{"GetName", "String", {}, false}}, {}},
        {"Actor",
         "Form",
         {{"GetLevel", "Int", {}, false},
          {"IsDead", "Bool", {}, false},
          {"GetActorValue", "Float", {{"String", "name"}}, false}},
         {}},
        {"Game", "", {{"GetPlayer", "Actor", {}, true}}, {}},
        {"Quest", "Form", {{"GetStage", "Int", {}, false}}, {}},
        {"ExampleController", "Quest", {{"GetStage", "Int", {}, false}}, {{"Enabled", "Bool", true, true}}}};
    elder_shell::RestoreMenuSession({});
    if (scenario == Scenario::Empty)
        return;
    AddActor();
    InspectedValue array;
    array.expression = "numbers";
    array.type = "Int[]";
    array.summary = "[10, 20, 30]";
    array.array = true;
    array.length = 3;
    array.elements = {{"numbers[0]", "Int", "10"}, {"numbers[1]", "Int", "20"}, {"numbers[2]", "Int", "30"}};
    variables.variables.push_back({"numbers", {3, 0}, array});
    InspectedValue controller;
    controller.expression = "controller";
    controller.type = "ExampleController";
    controller.summary = "Preview quest controller";
    controller.properties = {{"Enabled", "Bool", true}, {"WriteOnly", "Int", false}};
    variables.variables.push_back({"controller", {4, 0}, controller});
    metadata.variables.push_back({"ExampleController", "controller"});
    metadata.variables.push_back({"Int[]", "numbers"});
    Record("var player = Game.GetPlayer()", "Actor (00000014)");
    Record("player.GetLevel()", "24");
    Record("var numbers = new Int[3]", "Int[3]");
    instances.rows = {{1, "ExampleController", "Following", "0000000000010200", true},
                      {2, "ExampleController", "Waiting", "0000000000010201", true},
                      {3, "ExampleAlias", "", "0000000000010202", false}};
    instances.status = "Preview: 3 attached instances";
    usage = {8, 3, 6};
    if (scenario == Scenario::Pending) {
        const auto ticket = requests.Begin("player.GetLevel()");
        requests.Submitted(*ticket);
    } else if (scenario == Scenario::Rejected) {
        Record("player.UnknownMethod()", "Member not found: Actor.UnknownMethod", true);
    } else if (scenario == Scenario::Large) {
        usage = {150, 40, 300};
        for (int i = 0; i < 60; ++i) {
            Record("player.GetActorValue(\"health\")", std::to_string(100 + i));
            variables.variables.push_back({"value" + std::to_string(i),
                                           {static_cast<std::uint64_t>(i + 4), 0},
                                           {"value" + std::to_string(i), "String", "Synthetic inspection value"}});
        }
    }
}

void preview::Tick() {
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(pending, [now](const auto& item) {
        if (now < item.second)
            return false;
        const auto found = std::ranges::find_if(requests.Entries(),
                                                [&](const auto& entry) { return entry.ticket.id == item.first.id; });
        if (found == requests.Entries().end())
            return true;
        const auto command = found->command;
        if (command == "player.GetLevel()")
            requests.Complete(item.first, "24 (preview sample)");
        else if (command == "var player = Game.GetPlayer()") {
            AddActor();
            requests.Complete(item.first, "Actor (00000014), preview sample");
        } else if (command == "vars")
            requests.Complete(item.first,
                              "player : Actor\nnumbers : Int[]\ncontroller : ExampleController\nPreview variables are "
                              "available on the Variables tab.");
        else
            requests.Reject(
                item.first,
                "Preview only: Papyrus is not executed. Try player.GetLevel() or var player = Game.GetPlayer().");
        return true;
    });
}

void preview::InspectFixture(std::string_view variable) {
    Reset(Scenario::Ready);
    std::erase_if(variables.variables, [variable](const auto& row) { return row.name != variable; });
}

void preview::SaveSession(Session& session) {
    session.valid = true;
    session.menu = elder_shell::CaptureMenuSession();
    session.requests = requests;
    session.requests.Loaded();
    session.variables = variables;
    session.instances = instances;
    session.usage = usage;
    session.metadata = metadata;
    session.ready = ready;
}

void preview::RestoreSession(const Session& session) {
    if (!session.valid)
        return;
    elder_shell::RestoreMenuSession(session.menu);
    requests = session.requests;
    variables = session.variables;
    instances = session.instances;
    usage = session.usage;
    metadata = session.metadata;
    ready = session.ready;
    elder_shell::previewTestedRuntime = session.scenario != static_cast<int>(Scenario::Untested);
    pending.clear();
}

namespace elder_shell {
    void RefreshPapyrusBacklog(bool full) {
        backlog = {};
        backlog.captured = true;
        backlog.capturedAt = std::chrono::steady_clock::now();
        backlog.status = "Preview: synthetic VM snapshot.";
        backlog.full = full;
        backlog.registeredStacks = 24000;
        backlog.latentReturns = 24;
        backlog.suspendedOverflow = 23500;
        backlog.functionOverflow = 16;
        backlog.sampledStacks = 4096;
        backlog.sampledCalls = 1040;
        backlog.calls = {{"ExampleController.OnLocationChange", 1000}, {"ExampleAlias.OnUpdate", 40}};
        backlog.states = {
            {"Running / scheduled", 4000}, {"Waiting on latent function", 24}, {"Waiting on another stack's call", 72}};
        if (full) {
            backlog.sampledStacks = backlog.registeredStacks;
            backlog.sampledCalls = backlog.suspendedOverflow + backlog.functionOverflow;
            backlog.calls.front().count = backlog.sampledCalls - backlog.calls.back().count;
            backlog.states.front().count = backlog.registeredStacks - 96;
        }
    }

    PapyrusBacklog PapyrusBacklogSnapshot() {
        return backlog;
    }

    void ResetPapyrusBacklog(bool) {
        backlog = {};
    }

    void RefreshCompletionMetadata(std::string) {}

    std::shared_ptr<const EvaluationEnvironment> CompletionMetadataSnapshot() {
        return std::make_shared<EvaluationEnvironment>(metadata);
    }

    void RefreshVariableInspection() {
        variables.status = "Preview: values refreshed from sample data";
    }

    VariableInspection VariableInspectionSnapshot() {
        return variables;
    }

    InspectionRead ReadInspectedProperty(PropertyInspection inspection) {
        if (!ValidPropertyInspection(inspection))
            return {{}, "Invalid property expression"};
        const auto ticket = requests.Begin(inspection.expression);
        if (!ticket)
            return {{}, "Too many preview requests"};
        requests.Complete(*ticket, "Preview property value (getter was not executed)");
        return {*ticket, {}};
    }

    std::string ExploreInstances(std::string, std::string script) {
        instances.rows = {{1, script.empty() ? "ExampleController" : script, "Following", "0000000000010200", true},
                          {2, script.empty() ? "ExampleController" : script, "", "0000000000010201", false}};
        instances.status = "Preview search: 2 synthetic instances";
        return {};
    }

    InstanceExplorer InstanceSnapshot() {
        return instances;
    }

    std::string UseInstance(std::uint64_t id, std::string variable) {
        if (!Identifier(variable))
            return "Use a valid variable name";
        const auto found =
            std::ranges::find_if(instances.rows, [id](const auto& row) { return row.id == id && row.initialized; });
        if (found == instances.rows.end())
            return "Instance unavailable";
        std::erase_if(variables.variables, [&](const auto& row) { return Equal(row.name, variable); });
        std::erase_if(metadata.variables, [&](const auto& row) { return Equal(row.name, variable); });
        variables.variables.push_back({variable, {id, 0}, {variable, found->script, "Preview attached instance"}});
        metadata.variables.push_back({found->script, variable});
        return {};
    }

    std::string Submit(std::string command, bool multiline) {
        if (!ready)
            return "No preview session";
        const auto parsed = Parse(command, multiline);
        if (const auto* error = std::get_if<std::string>(&parsed))
            return *error;
        const auto ticket = requests.Begin(std::move(command));
        if (!ticket)
            return "Too many preview requests";
        requests.Compiling(*ticket);
        pending.push_back({*ticket, std::chrono::steady_clock::now() + std::chrono::milliseconds(500)});
        return {};
    }

    std::vector<Entry> Snapshot() {
        return requests.Entries();
    }

    EvaluationUsage EvaluationUsageSnapshot() {
        return usage;
    }

    void ClearFinished() {
        requests.ClearFinished();
    }

    void SessionChanged(bool value) {
        ready = value;
        requests.Loaded();
    }

    void RegisterCompletionInput() {}

    void PublishCompletionInput(bool) {}

    bool TakeCompletionDismissal() {
        return false;
    }
}
