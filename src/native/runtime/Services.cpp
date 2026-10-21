#include "Services.h"
#include "GameConsole.h"
#include "shell/Services.h"

namespace elder_shell {
    namespace {
        bool ValidService(RE::TESQuest* self) {
            return self && self == ServiceQuest();
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

        std::string InspectQuest(RE::TESQuest* self, RE::TESQuest* quest) {
            if (!ValidService(self) || !quest)
                return "Provide an existing Quest and the Elder Shell diagnostics service.";
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
                return "Provide an existing Quest and the Elder Shell diagnostics service.";
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
        vm->RegisterFunction("InspectQuest", services::DiagnosticsScript, InspectQuest);
        vm->RegisterFunction("PrepareRestart", services::DiagnosticsScript, PrepareRestart);
        return true;
    }
}
