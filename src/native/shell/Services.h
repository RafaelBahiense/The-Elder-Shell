#pragma once

#include "Command.h"
#include "compiler/ScriptDeclarations.h"
#include <array>

namespace elder_shell::services {
    inline constexpr auto Root = "ElderShell";
    inline constexpr auto RootScript = "ElderShellRoot";
    inline constexpr auto ConsoleScript = "ElderShellConsole";
    inline constexpr auto SelectionScript = "ElderShellSelection";
    inline constexpr auto FormsScript = "ElderShellForms";
    inline constexpr auto ActorsScript = "ElderShellActors";
    inline constexpr auto AIScript = "ElderShellAI";
    inline constexpr auto QuestsScript = "ElderShellQuests";
    inline constexpr auto ScriptsScript = "ElderShellScripts";
    inline constexpr std::array Scripts{RootScript,   ConsoleScript, SelectionScript, FormsScript,
                                        ActorsScript, AIScript,      QuestsScript,    ScriptsScript};
    inline constexpr auto Plugin = "TheElderShell.esp";
    inline constexpr unsigned QuestID = 0x800;

    // VM signatures do not expose source defaults. These defaults belong to
    // our packaged scripts; all other signature data still comes from the VM.
    inline void ApplyDefaults(std::string_view script, FunctionDeclaration& function) {
        const auto names = [&](std::initializer_list<std::string_view> parameters) {
            if (parameters.size() != function.parameters.size())
                return;
            std::size_t i = 0;
            for (const auto name : parameters)
                function.parameters[i++].name = name;
        };
        if (Equal(script, ScriptsScript)) {
            if (Equal(function.name, "InspectAliasInstance") && function.parameters.size() == 3) {
                names({"owner", "aliasID", "script"});
                function.parameters[2].defaultValue = "\"\"";
            }
            if ((Equal(function.name, "InspectStacks") || Equal(function.name, "InspectInstance")) &&
                function.parameters.size() == 2) {
                function.parameters[0].name = "owner";
                function.parameters[1].name = "script";
                function.parameters[1].defaultValue = "\"\"";
                if (Equal(function.name, "InspectStacks"))
                    function.parameters[0].defaultValue = "None";
            }
            if ((Equal(function.name, "InspectStack") || Equal(function.name, "InspectLatentCall")) &&
                function.parameters.size() == 1)
                function.parameters[0].name = "stackID";
            if (Equal(function.name, "PreviewPropertyRestore"))
                names({"owner", "script", "propertyName", "expectedExpression", "replacementExpression"});
            if (Equal(function.name, "CheckPropertyRestore"))
                names({"owner", "script", "propertyName", "plan"});
        }
        if (Equal(script, QuestsScript)) {
            for (const auto method : {"InspectLifecycle", "InspectStages", "InspectAliases", "InspectStorySelection",
                                      "InspectDependencies", "InspectScene", "InspectStartWaiters", "Capture"})
                if (Equal(function.name, method))
                    names({"target"});
            if (Equal(function.name, "InspectDialogue"))
                names({"speaker", "target"});
            if (Equal(function.name, "Compare"))
                names({"target", "snapshot"});
            if (Equal(function.name, "ReadSnapshot"))
                names({"snapshot"});
            if (Equal(function.name, "InspectRepair"))
                names({"plan"});
            if (Equal(function.name, "PreviewRestart"))
                names({"target", "resetStages", "clearAlreadyRun"});
            if (Equal(function.name, "PreviewFailPendingStart"))
                names({"target", "stackID"});
            if (Equal(function.name, "FailPendingStart"))
                names({"target", "stackID", "plan"});
            if (Equal(function.name, "PreviewRebindAlias"))
                names({"target", "aliasID", "replacement"});
            if (Equal(function.name, "PreviewScene"))
                names({"target", "start"});
            if ((Equal(function.name, "PreviewRestart") || Equal(function.name, "RestartChecked")) &&
                function.parameters.size() >= 3) {
                const auto first = Equal(function.name, "PreviewRestart") ? 1u : 2u;
                for (auto i = first; i < function.parameters.size(); ++i)
                    if (Equal(function.parameters[i].type, "Bool"))
                        function.parameters[i].defaultValue = "False";
            }
        }
        if ((Equal(script, ActorsScript) || Equal(script, AIScript) || Equal(script, QuestsScript) ||
             Equal(script, ScriptsScript)) &&
            function.parameters.size() == 2 && Equal(function.parameters[0].type, "Actor") &&
            Equal(function.parameters[1].type, "String")) {
            function.parameters[0].name = "target";
            function.parameters[1].name = "plugin";
        }
        if (Equal(script, ActorsScript) && Equal(function.name, "CleanupPluginFactions") &&
            function.parameters.size() == 4 && Equal(function.parameters[3].type, "Bool"))
            function.parameters[3].defaultValue = "False";
        if (Equal(script, ConsoleScript) && Equal(function.name, "Execute") && function.parameters.size() == 2 &&
            Equal(function.parameters[0].type, "String") && Equal(function.parameters[1].type, "ObjectReference")) {
            function.parameters[0].name = "command";
            function.parameters[1].name = "target";
            function.parameters[1].defaultValue = "None";
        }
        if (Equal(script, ActorsScript) && Equal(function.name, "InspectDialogue") && function.parameters.size() == 1 &&
            Equal(function.parameters[0].type, "Actor"))
            function.parameters[0].name = "target";
        if (Equal(script, FormsScript) && function.parameters.size() == 1 && Equal(function.parameters[0].type, "Form"))
            function.parameters[0].name = "target";
        if (Equal(script, QuestsScript) && Equal(function.name, "Inspect") && function.parameters.size() == 1 &&
            Equal(function.parameters[0].type, "Quest"))
            function.parameters[0].name = "target";
        if (Equal(script, QuestsScript) && Equal(function.name, "Restart") && function.parameters.size() == 2 &&
            Equal(function.parameters[1].type, "Bool"))
            function.parameters[1].defaultValue = "False";
        if (Equal(script, AIScript) &&
            (Equal(function.name, "InspectPackage") || Equal(function.name, "RestorePackageInterruptFlags")) &&
            function.parameters.size() == 1 && Equal(function.parameters[0].type, "Package"))
            function.parameters[0].name = "target";
        if (Equal(script, AIScript) && Equal(function.name, "SetPackageInterruptFlag") &&
            function.parameters.size() == 3 && Equal(function.parameters[0].type, "Package") &&
            Equal(function.parameters[1].type, "String") && Equal(function.parameters[2].type, "Bool")) {
            function.parameters[0].name = "target";
            function.parameters[1].name = "flag";
            function.parameters[2].name = "enabled";
        }
    }

    inline bool WritesRoot(const Command& command) {
        if ((command.operation == Operation::Unset && Equal(command.target, Root)) || Equal(command.assignment, Root))
            return true;
        if (!command.evaluation.multiline)
            return false;
        for (const auto& line : FragmentLines(command.evaluation.expression))
            if (Equal(line.name, Root))
                return true;
        return std::ranges::any_of(FragmentWrites(command.evaluation.expression),
                                   [](const auto& name) { return Equal(name, Root); });
    }

    inline std::string ConsoleError(std::string_view command) {
        if (command.empty() || command.size() > MaximumCommandLength ||
            command.find_first_not_of(" \t") == std::string_view::npos ||
            command.find_first_of("\r\n\0", 0, 3) != std::string_view::npos)
            return "Provide one console command, at most 4096 characters, without newlines or NUL bytes.";
        return {};
    }

    inline std::string RestartError(bool stopped, bool eventDriven, bool serviceQuest, bool alreadyRun,
                                    bool clearAlreadyRun) {
        if (serviceQuest)
            return "The Elder Shell service quest cannot be restarted.";
        if (!stopped)
            return "Quest must be fully stopped before Quests.Restart; stop it explicitly and inspect it first.";
        if (eventDriven)
            return "Event-driven quests must start through their normal story event.";
        if (alreadyRun && !clearAlreadyRun)
            return "alreadyRun is set. Inspect the quest and explicitly pass True to clear it before starting.";
        return {};
    }
}
