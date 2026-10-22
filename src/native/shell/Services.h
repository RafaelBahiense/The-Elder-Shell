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
