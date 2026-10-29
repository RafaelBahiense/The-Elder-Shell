#include "Diagnostics.h"
#include "Services.h"
#include "shell/Command.h"
#include "shell/Recovery.h"
#include "shell/Services.h"
#include <map>
#include <set>
#include <Windows.h>
#include <bcrypt.h>
#include <array>

namespace elder_shell::diagnostics {
    namespace {
        recovery::Plans plans{""};
        recovery::Plans snapshots{""};
        std::string lastPreviewError;

        bool EnsureScope() {
            static std::string epoch;
            if (!epoch.empty())
                return true;
            std::array<unsigned char, 16> random{};
            if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()),
                                BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0)
                return false;
            for (const auto byte : random)
                epoch += std::format("{:02X}", byte);
            plans.SetScope(epoch + ":repair");
            snapshots.SetScope(epoch + ":snapshot");
            return true;
        }

        std::string Rejected(std::string error) {
            lastPreviewError = std::move(error);
            return {};
        }

        std::string Identity(const RE::TESForm* owner) {
            return owner ? std::to_string(owner->GetFormID()) : "0";
        }

        bool Complete(const std::string& report) {
            return report.find("[Truncated]") == std::string::npos && report.find("truncated") == std::string::npos &&
                   report.find("contents not traversed") == std::string::npos;
        }

        std::string TransitionError(RE::TESQuest* quest) {
            if (!quest || quest == ServiceQuest())
                return "Provide a quest other than the Elder Shell service quest.";
            if (quest->promoteTask || quest->data.flags.any(RE::QuestFlag::kStageWait))
                return "Quest has pending lifecycle/stage work; no repair authorized.";
            return {};
        }

        std::string AddPlan(std::string operation, std::string identity, std::string expected,
                            std::string description) {
            if (!EnsureScope())
                return Rejected("Session token generation failed; no repair plan was created.");
            if (!Complete(expected)) {
                return Rejected(
                    "State snapshot is truncated or contains untracked array contents; "
                    "no repair plan was authorized.");
            }
            const auto id =
                plans.Add({std::move(operation), std::move(identity), std::move(expected), std::move(description)});
            lastPreviewError =
                !id.empty() ? "" : "The 32-plan limit was reached. Inspect existing plans or call ClearRepairs.";
            return id;
        }

        std::string Capture(RE::TESQuest* self, RE::TESQuest* quest) {
            if (!Valid(self) || !quest || !EnsureScope())
                return {};
            return snapshots.Add({"quest", Identity(quest), QuestState(quest), "Read-only quest snapshot"});
        }

        std::string Compare(RE::TESQuest* self, RE::TESQuest* quest, std::string snapshot) {
            if (!Valid(self) || !quest)
                return "Provide a Quest.";
            const auto* before = snapshots.Find(snapshot);
            if (!before || before->identity != Identity(quest))
                return "Snapshot missing, belongs to another quest, or was invalidated by a save load.";
            return recovery::CompareReports(before->expected, QuestState(quest));
        }

        std::string ReadSnapshot(RE::TESQuest* self, std::string snapshot) {
            const auto* value = Valid(self) ? snapshots.Find(snapshot) : nullptr;
            return value ? value->expected : "Snapshot unavailable.";
        }

        std::string ClearSnapshots(RE::TESQuest* self) {
            if (!Valid(self))
                return "Service receiver unavailable.";
            snapshots.Clear();
            return "Read-only snapshots cleared. Repair plans are unchanged.";
        }

        std::string ClearRepairs(RE::TESQuest* self) {
            if (!Valid(self))
                return "Service receiver unavailable.";
            plans.Clear();
            return "Repair plans discarded; no game state changed.";
        }

        std::string InspectRepair(RE::TESQuest* self, std::string id) {
            const auto* plan = Valid(self) ? plans.Find(id) : nullptr;
            if (Valid(self) && id.empty() && !lastPreviewError.empty())
                return "Preview rejected: " + lastPreviewError;
            return plan ? std::format(
                              "Plan {}: {}\n{}\nSingle use; expected state is rechecked before dispatch. "
                              "Does not provide rollback.",
                              id, plan->operation, plan->description)
                        : "Plan unavailable. An empty token means preview rejected or the 32-plan limit was reached. "
                          "Inspect quest lifecycle and stacks; no mutation occurred.";
        }

        std::string RestartOperation(bool reset, bool clearAlreadyRun) {
            return std::format("restart:{}:{}", reset, clearAlreadyRun);
        }

        std::string RestartError(RE::TESQuest* quest, bool clearAlreadyRun) {
            if (const auto error = TransitionError(quest); !error.empty())
                return error;
            return services::RestartError(
                !quest->IsEnabled(), quest->eventID != RE::QuestEvent::kNone, quest == ServiceQuest(),
                quest->alreadyRun && quest->data.flags.any(RE::QuestFlag::kRunOnce), clearAlreadyRun);
        }

        std::string PreviewRestart(RE::TESQuest* self, RE::TESQuest* quest, bool reset, bool clearAlreadyRun) {
            if (!Valid(self))
                return Rejected("Service receiver unavailable.");
            if (const auto error = RestartError(quest, clearAlreadyRun); !error.empty())
                return Rejected(error);
            if (HasOwnerExecution(quest))
                return Rejected("An owned paused execution exists, or the execution scan was truncated.");
            return AddPlan(RestartOperation(reset, clearAlreadyRun), Identity(quest), Guard(quest),
                           std::format("Target {}. Reset stages: {}. Clear alreadyRun: {}. "
                                       "Start through normal Papyrus Quest.Start; authored flags remain intact.",
                                       Label(quest), reset, clearAlreadyRun));
        }

        std::string PrepareRestartChecked(RE::TESQuest* self, RE::TESQuest* quest, std::string id, bool reset,
                                          bool clearAlreadyRun) {
            if (!Valid(self))
                return "Service receiver unavailable.";
            if (const auto error =
                    plans.Consume(id, RestartOperation(reset, clearAlreadyRun), Identity(quest), Guard(quest));
                !error.empty())
                return error;
            if (const auto error = RestartError(quest, clearAlreadyRun); !error.empty())
                return error;
            if (HasOwnerExecution(quest))
                return "An owned paused execution still exists; no restart dispatched.";
            if (clearAlreadyRun && quest->alreadyRun) {
                quest->alreadyRun = false;
                quest->AddChange(RE::TESQuest::ChangeFlags::kQuestFlags | RE::TESQuest::ChangeFlags::kQuestAlreadyRun);
            }
            SKSE::log::info("Quest restart plan {} accepted for {:08X}; reset {}", id, quest->GetFormID(), reset);
            return {};
        }

        RE::BGSRefAlias* FindAlias(RE::TESQuest* quest, std::int32_t id) {
            if (quest && id >= 0)
                for (auto* alias : quest->aliases)
                    if (alias && alias->aliasID == static_cast<std::uint32_t>(id))
                        return skyrim_cast<RE::BGSRefAlias*>(alias);
            return nullptr;
        }

        std::string AliasError(RE::TESQuest* quest, RE::BGSRefAlias* alias, RE::TESObjectREFR* replacement) {
            if (const auto error = TransitionError(quest); !error.empty())
                return error;
            if (!quest->IsEnabled() || !alias || !replacement)
                return "Provide a running quest, ReferenceAlias ID and replacement reference.";
            auto* actor = replacement->As<RE::Actor>();
            using Flags = RE::BGSBaseAlias::FLAGS;
            if (alias->flags.any(Flags::kActorsOnly) && !actor)
                return "Alias requires an Actor.";
            if (actor && actor->IsDead() && !alias->flags.any(Flags::kAllowDead))
                return "Alias does not allow dead actors.";
            if (replacement->IsDisabled() && !alias->flags.any(Flags::kAllowDisabled))
                return "Alias does not allow disabled references.";
            if (!replacement->Is3DLoaded() && alias->flags.any(Flags::kLoadedOnly))
                return "Alias requires a loaded reference.";
            if (alias->flags.any(Flags::kExternalLink) || alias->fillType == RE::BGSBaseAlias::FILL_TYPE::kFromExternal)
                return "External alias ownership must be repaired on the source quest.";
            if (alias->fillType == RE::BGSBaseAlias::FILL_TYPE::kUniqueActor &&
                (!actor || actor->GetActorBase() != alias->fillData.uniqueActor.uniqueActor))
                return "Replacement does not match the authored unique actor.";
            if (alias->conditions && alias->conditions->head)
                return "Conditional alias repair requires search/reservation context not available here.";
            if (alias->fillType != RE::BGSBaseAlias::FILL_TYPE::kForced &&
                alias->fillType != RE::BGSBaseAlias::FILL_TYPE::kUniqueActor)
                return "Only forced-reference and unique-actor bindings support this repair.";
            if (HasOwnerExecution(quest))
                return "An owned paused execution still exists; alias repair rejected.";
            for (const auto* scene : quest->scenes)
                if (scene && scene->isPlaying)
                    return "A quest scene is playing; alias repair rejected.";
            return {};
        }

        std::string AliasIdentity(RE::TESQuest* quest, std::int32_t alias, RE::TESObjectREFR* replacement) {
            return std::format("{}:{}:{}", Identity(quest), alias, Identity(replacement));
        }

        std::string PreviewRebindAlias(RE::TESQuest* self, RE::TESQuest* quest, std::int32_t alias,
                                       RE::TESObjectREFR* replacement) {
            auto* binding = FindAlias(quest, alias);
            if (!Valid(self))
                return Rejected("Service receiver unavailable.");
            if (const auto error = AliasError(quest, binding, replacement); !error.empty())
                return Rejected(error);
            return AddPlan("alias", AliasIdentity(quest, alias, replacement), Guard(quest),
                           std::format("{} / alias {}: {} -> {}. ForceRefTo applies normal engine alias effects. "
                                       "Authored packages, factions and spells may be applied or removed; their "
                                       "lists are not mapped. No other alias or quest stage is reset.",
                                       Label(quest), alias, Label(binding->GetReference()), Label(replacement)));
        }

        std::string PrepareRebindAlias(RE::TESQuest* self, RE::TESQuest* quest, std::int32_t alias,
                                       RE::TESObjectREFR* replacement, std::string id) {
            if (!Valid(self))
                return "Service receiver unavailable.";
            if (const auto error = plans.Consume(id, "alias", AliasIdentity(quest, alias, replacement), Guard(quest));
                !error.empty())
                return error;
            if (const auto error = AliasError(quest, FindAlias(quest, alias), replacement); !error.empty())
                return error;
            quest->AddChange(RE::TESQuest::ChangeFlags::kQuestRuntimeData);
            SKSE::log::info("Alias repair plan {} accepted for {:08X}/{}", id, quest->GetFormID(), alias);
            return {};
        }

        std::string SceneError(RE::BGSScene* scene, bool start) {
            if (!scene || !scene->parentQuest)
                return "Provide a quest-owned scene.";
            if (const auto error = TransitionError(scene->parentQuest); !error.empty())
                return error;
            if (start && (!scene->parentQuest->IsEnabled() || scene->isPlaying))
                return "Starting requires a running owner quest and a stopped scene.";
            if (!start && !scene->isPlaying)
                return "Scene is already stopped.";
            return {};
        }

        std::string PreviewScene(RE::TESQuest* self, RE::BGSScene* scene, bool start) {
            if (!Valid(self))
                return Rejected("Service receiver unavailable.");
            if (const auto error = SceneError(scene, start); !error.empty())
                return Rejected(error);
            return AddPlan(
                start ? "scene-start" : "scene-stop", Identity(scene), Guard(scene->parentQuest) + Scene(scene),
                std::format("{} {}; owner {}. Scene fragments and quest side effects are not rolled back.\n{}",
                            start ? "Start" : "Stop", Label(scene), Label(scene->parentQuest), Scene(scene)));
        }

        std::string PrepareScene(RE::TESQuest* self, RE::BGSScene* scene, bool start, std::string id) {
            if (!Valid(self))
                return "Service receiver unavailable.";
            const auto current = scene && scene->parentQuest ? Guard(scene->parentQuest) + Scene(scene) : "INVALID";
            if (const auto error = plans.Consume(id, start ? "scene-start" : "scene-stop", Identity(scene), current);
                !error.empty())
                return error;
            if (const auto error = SceneError(scene, start); !error.empty())
                return error;
            return {};
        }

        bool Expression(std::string_view text) {
            if (text.empty() || text.size() > 512 || text.find_first_of("\r\n;\0", 0, 4) != std::string_view::npos)
                return false;
            const auto parsed = Parse(std::string(text));
            const auto* command = std::get_if<Command>(&parsed);
            return command && command->assignment.empty() && command->operation == Operation::Evaluate;
        }
    }

    std::string PropertySource(RE::TESForm* owner, const std::string& script, const std::string& property,
                               const std::string& expected, const std::string& replacement) {
        if (!owner || owner == ServiceQuest() || !Identifier(script) || script.size() > 128 || !Identifier(property) ||
            property.size() > 128 || !Expression(expected) || !Expression(replacement))
            return "Provide an owner, script/property identifiers and single expected/replacement expressions.";
        bool writable = false;
        std::size_t matches = 0;
        for (const auto& object : Attached(owner)) {
            if (_stricmp(object->GetTypeInfo()->GetName(), script.c_str()) != 0)
                continue;
            if (!object->IsInitialized() || ++matches > 1)
                return "Property repair requires exactly one initialized script instance.";
            std::set<const RE::BSScript::ObjectTypeInfo*> visited;
            bool found = false;
            for (auto* type = object->GetTypeInfo(); type && visited.size() < 32 && !found; type = type->GetParent()) {
                if (!visited.insert(type).second ||
                    type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid)
                    break;
                const auto* props = type->GetPropertyIter();
                for (std::uint32_t i = 0; props && i < type->GetNumProperties(); ++i)
                    if (_stricmp(props[i].name.c_str(), property.c_str()) == 0) {
                        writable = (std::to_underlying(*props[i].info.permissions) & 3u) == 3u;
                        found = true;
                        break;
                    }
            }
        }
        if (!writable)
            return "No attached instance with that readable, writable property was found.";
        const auto identity = Identity(owner) + ":" + script + ":" + property;
        const auto id = AddPlan("property", identity, Instance(owner, script),
                                std::format("{} / {}.{}: expected ({}) -> ({}). Apply through compiled Papyrus only.",
                                            Label(owner), script, property, expected, replacement));
        if (id.empty())
            return "Property preview could not retain a complete plan.";
        return recovery::PropertyRepairSource(id, owner->GetFormID(), script, property, expected, replacement);
    }

    bool CheckPropertyPlan(RE::TESQuest* self, RE::TESForm* owner, std::string script, std::string property,
                           std::string id) {
        if (!Valid(self) || !owner || owner == ServiceQuest())
            return false;
        const auto error =
            plans.Consume(id, "property", Identity(owner) + ":" + script + ":" + property, Instance(owner, script));
        if (!error.empty())
            SKSE::log::warn("Property repair plan {} rejected: {}", id, error);
        return error.empty();
    }

    std::string RejectRepair(std::string error) {
        return Rejected(std::move(error));
    }

    std::string RetainRepair(std::string operation, std::string identity, std::string expected,
                             std::string description) {
        return AddPlan(std::move(operation), std::move(identity), std::move(expected), std::move(description));
    }

    std::string ConsumeRepair(std::string_view token, std::string_view operation, std::string_view identity,
                              std::string_view expected) {
        return plans.Consume(token, operation, identity, expected);
    }

    void Clear() {
        plans.Clear();
        snapshots.Clear();
        lastPreviewError.clear();
    }

    void RegisterRecoveryServices(RE::BSScript::IVirtualMachine* vm) {
        RegisterStartRecovery(vm);
        vm->RegisterFunction("Capture", services::QuestsScript, Capture);
        vm->RegisterFunction("Compare", services::QuestsScript, Compare);
        vm->RegisterFunction("ReadSnapshot", services::QuestsScript, ReadSnapshot);
        vm->RegisterFunction("ClearSnapshots", services::QuestsScript, ClearSnapshots);
        vm->RegisterFunction("ClearRepairs", services::QuestsScript, ClearRepairs);
        vm->RegisterFunction("InspectRepair", services::QuestsScript, InspectRepair);
        vm->RegisterFunction("PreviewRestart", services::QuestsScript, PreviewRestart);
        vm->RegisterFunction("PrepareRestartChecked", services::QuestsScript, PrepareRestartChecked);
        vm->RegisterFunction("PreviewRebindAlias", services::QuestsScript, PreviewRebindAlias);
        vm->RegisterFunction("PrepareRebindAlias", services::QuestsScript, PrepareRebindAlias);
        vm->RegisterFunction("PreviewScene", services::QuestsScript, PreviewScene);
        vm->RegisterFunction("PrepareScene", services::QuestsScript, PrepareScene);
    }
}
