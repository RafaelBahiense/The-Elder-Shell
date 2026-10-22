#include "Services.h"
#include "GameConsole.h"
#include "PackageFlags.h"
#include "FactionCleanup.h"
#include "PluginActorCleanup.h"
#include "shell/Services.h"
#include "RE/E/ExtraCanTalkToPlayer.h"
#include "RE/C/CrosshairPickData.h"

namespace elder_shell {
    namespace {
        bool ValidService(RE::TESQuest* self) {
            return self && self == ServiceQuest();
        }

        RE::TESObjectREFR* GetConsoleReference(RE::TESQuest* self) {
            return ValidService(self) ? RE::Console::GetSelectedRef().get() : nullptr;
        }

        RE::Actor* GetConsoleActor(RE::TESQuest* self) {
            auto* reference = GetConsoleReference(self);
            return reference ? reference->As<RE::Actor>() : nullptr;
        }

        RE::TESObjectREFR* GetCrosshairReference(RE::TESQuest* self) {
            const auto* pick = ValidService(self) ? RE::CrosshairPickData::GetSingleton() : nullptr;
            return pick ? pick->target.get().get() : nullptr;
        }

        RE::Actor* GetCrosshairActor(RE::TESQuest* self) {
            auto* reference = GetCrosshairReference(self);
            return reference ? reference->As<RE::Actor>() : nullptr;
        }

        std::string GetPlugin(RE::TESQuest* self, RE::TESForm* form) {
            const auto* file = ValidService(self) && form ? form->GetFile(0) : nullptr;
            return file ? file->fileName : "";
        }

        std::string InspectForm(RE::TESQuest* self, RE::TESForm* form) {
            if (!ValidService(self) || !form)
                return "Provide an existing Form and the Elder Shell service.";

            const auto* file = form->GetFile(0);
            const auto* editor = form->GetFormEditorID();
            std::string report =
                std::format("Form: {:08X} {}\nType: {}; editor ID: {}\nDefining plugin: {}", form->GetFormID(),
                            form->GetName(), static_cast<unsigned>(form->GetFormType()),
                            editor && *editor ? editor : "<unavailable>", file ? file->fileName : "<runtime>");
            if (file)
                report += std::format("\nLocal ID: {:06X}", form->GetLocalFormID());

            report += "\nSource files (engine order):";
            const auto* sources = form->sourceFiles.array;
            if (!sources || sources->empty())
                return report + " NONE";
            std::size_t count = 0;
            for (const auto* source : *sources) {
                if (++count > 256) {
                    report += "\n[Source listing truncated]";
                    break;
                }
                if (source)
                    report += std::format("\n{}", source->fileName);
            }
            return report;
        }

        std::string ExecuteConsole(RE::TESQuest* self, std::string command, RE::TESObjectREFR* target) {
            if (!ValidService(self))
                return "Elder Shell service receiver unavailable.";
            if (const auto error = services::ConsoleError(command); !error.empty())
                return error;
            try {
                return RunGameConsole(command, target);
            } catch (const std::exception& error) {
                return error.what();
            }
        }

        std::string InspectActorDialogue(RE::TESQuest* self, RE::Actor* actor) {
            if (!ValidService(self) || !actor)
                return "Provide an existing Actor and the Elder Shell service.";

            const auto* permission = actor->extraList.GetByType<RE::ExtraCanTalkToPlayer>();
            const auto* race = actor->GetRace();
            const auto describe = [](const RE::TESForm* form) {
                if (!form)
                    return std::string("NONE");
                const auto* file = form->GetFile(0);
                return std::format("{:08X} {} [{}]", form->GetFormID(), form->GetName(),
                                   file ? file->fileName : "runtime");
            };
            return std::format(
                "Actor: {}\nCan talk to player: {}; reference override: {}\n"
                "Race: {}; allows player dialogue: {}\nActivation blocked: {}\n"
                "Dead: {}; disabled: {}; loaded: {}; in combat: {}; teammate: {}\n"
                "Unconscious: {}; mounted: {}; sit/sleep state: {}\nScene: {}\nPackage: {}\n"
                "Read-only snapshot. Dialogue topic, quest and alias conditions can still prevent conversation.",
                describe(actor), actor->CanTalkToPlayer(),
                permission ? (permission->talk ? "true" : "false") : "absent (race default)", describe(race),
                race && race->AllowsPCDialogue(), actor->IsActivationBlocked(), actor->IsDead(), actor->IsDisabled(),
                actor->Is3DLoaded(), actor->IsInCombat(), actor->IsPlayerTeammate(),
                actor->AsActorState()->IsUnconscious(), actor->IsOnMount(),
                static_cast<unsigned>(actor->AsActorState()->GetSitSleepState()), describe(actor->GetCurrentScene()),
                describe(actor->GetCurrentPackage()));
        }

        std::string InspectQuest(RE::TESQuest* self, RE::TESQuest* quest) {
            if (!ValidService(self) || !quest)
                return "Provide an existing Quest and the Elder Shell service.";
            const auto state = quest->IsStarting()   ? "Starting"
                               : quest->IsStopping() ? "Stopping"
                               : quest->IsStopped()  ? "Stopped"
                                                     : "Running";
            std::string text = std::format(
                "{} ({:08X})\nState: {}; stage: {}; enabled: {}; completed: {}\n"
                "Flags: 0x{:04X}; Run Once: {}; alreadyRun: {}; event: 0x{:08X}\nAliases: {}",
                quest->GetFormEditorID() ? quest->GetFormEditorID() : "<no editor ID>", quest->GetFormID(), state,
                quest->currentStage, quest->IsEnabled(), quest->IsCompleted(), quest->data.flags.underlying(),
                quest->data.flags.any(RE::QuestFlag::kRunOnce), quest->alreadyRun, std::to_underlying(quest->eventID),
                quest->aliases.size());
            std::size_t count = 0;
            for (auto* alias : quest->aliases) {
                if (++count > 128) {
                    text += "\n[Alias listing truncated]";
                    break;
                }
                if (!alias)
                    continue;
                text += std::format("\n{}: {} [{}]; fill={}; flags=0x{:08X}", alias->aliasID, alias->aliasName.c_str(),
                                    alias->GetTypeString().c_str(), alias->fillType.underlying(),
                                    alias->flags.underlying());
                if (const auto* reference = skyrim_cast<RE::BGSRefAlias*>(alias)) {
                    const auto* bound = reference->GetReference();
                    text += bound ? std::format(" -> {:08X}", bound->GetFormID()) : " -> NONE";
                } else {
                    text += " -> binding inspection unavailable for this alias type";
                }
                if (text.size() > 32768) {
                    text.resize(32768);
                    text += "\n[Quest report truncated]";
                    break;
                }
            }
            return text;
        }

        std::string PrepareRestart(RE::TESQuest* self, RE::TESQuest* quest, bool clearAlreadyRun) {
            if (!ValidService(self) || !quest)
                return "Provide an existing Quest and the Elder Shell service.";
            const auto error = services::RestartError(
                quest->IsStopped() && !quest->IsStarting() && !quest->IsStopping() && !quest->promoteTask,
                quest->eventID != RE::QuestEvent::kNone, quest == ServiceQuest(),
                quest->alreadyRun && quest->data.flags.any(RE::QuestFlag::kRunOnce), clearAlreadyRun);
            if (!error.empty())
                return error;
            if (clearAlreadyRun && quest->alreadyRun) {
                quest->alreadyRun = false;
                quest->AddChange(RE::TESQuest::ChangeFlags::kQuestFlags);
                SKSE::log::info("Explicit quest recovery: cleared alreadyRun on {:08X}", quest->GetFormID());
            }
            return {};
        }
    }

    RE::TESQuest* ServiceQuest() {
        auto* data = RE::TESDataHandler::GetSingleton();
        return data ? data->LookupForm<RE::TESQuest>(services::QuestID, services::Plugin) : nullptr;
    }

    RE::BSTSmartPointer<RE::BSScript::Object> ServiceRoot() {
        auto* quest = ServiceQuest();
        auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
        const auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
        RE::BSTSmartPointer<RE::BSScript::Object> object;
        if (!quest || !policy ||
            !vm->FindBoundObject(policy->GetHandleForObject(static_cast<RE::VMTypeID>(quest->GetFormType()), quest),
                                 services::RootScript, object) ||
            !object || !object->IsInitialized())
            throw std::runtime_error(
                "ElderShell services unavailable. Enable TheElderShell.esp and install its Scripts folder.");
        return object;
    }

    bool RegisterServices(RE::BSScript::IVirtualMachine* vm) {
        if (!vm)
            return false;
        // Ordinary native functions run at the VM's main-thread safe point.
        // Never mark these engine operations callable from tasklets.
        vm->RegisterFunction("Execute", services::ConsoleScript, ExecuteConsole);
        vm->RegisterFunction("GetConsoleReference", services::SelectionScript, GetConsoleReference);
        vm->RegisterFunction("GetConsoleActor", services::SelectionScript, GetConsoleActor);
        vm->RegisterFunction("GetCrosshairReference", services::SelectionScript, GetCrosshairReference);
        vm->RegisterFunction("GetCrosshairActor", services::SelectionScript, GetCrosshairActor);
        vm->RegisterFunction("Inspect", services::FormsScript, InspectForm);
        vm->RegisterFunction("GetPlugin", services::FormsScript, GetPlugin);
        vm->RegisterFunction("Inspect", services::QuestsScript, InspectQuest);
        vm->RegisterFunction("InspectDialogue", services::ActorsScript, InspectActorDialogue);
        vm->RegisterFunction("PrepareRestart", services::QuestsScript, PrepareRestart);
        RegisterPackageServices(vm);
        RegisterFactionCleanupServices(vm);
        RegisterPluginActorCleanupServices(vm);
        return true;
    }
}
