#include "Diagnostics.h"
#include "Services.h"
#include "shell/Recovery.h"
#include "shell/Services.h"
#include <set>

namespace elder_shell::diagnostics {
    namespace {
        using Function = RE::FUNCTION_DATA::FunctionID;

        const char* FunctionName(Function function) {
            switch (function) {
                case Function::kGetStage:
                    return "GetStage";
                case Function::kGetStageDone:
                    return "GetStageDone";
                case Function::kGetQuestCompleted:
                    return "GetQuestCompleted";
                case Function::kGetVMQuestVariable:
                    return "GetVMQuestVariable";
                case Function::kGetGlobalValue:
                    return "GetGlobalValue";
                case Function::kGetInFaction:
                    return "GetInFaction";
                case Function::kGetFactionRank:
                    return "GetFactionRank";
                case Function::kGetDead:
                    return "GetDead";
                case Function::kGetDisabled:
                    return "GetDisabled";
                case Function::kGetIsID:
                    return "GetIsID";
                case Function::kGetIsRace:
                    return "GetIsRace";
                case Function::kGetIsReference:
                    return "GetIsReference";
                case Function::kGetItemCount:
                    return "GetItemCount";
                case Function::kGetRelationshipRank:
                    return "GetRelationshipRank";
                case Function::kGetPlayerTeammate:
                    return "GetPlayerTeammate";
                default:
                    return nullptr;
            }
        }

        bool ContextIndependent(Function function) {
            return function == Function::kGetStage || function == Function::kGetStageDone ||
                   function == Function::kGetQuestCompleted || function == Function::kGetVMQuestVariable ||
                   function == Function::kGetGlobalValue;
        }

        template <class T, class F>
        std::string QuestQuery(RE::TESQuest* self, T* target, F report) {
            return Valid(self) && target ? report(target) : "Provide a target through the Elder Shell service.";
        }

        std::string InspectLifecycle(RE::TESQuest* self, RE::TESQuest* quest) {
            return QuestQuery(self, quest, Lifecycle);
        }

        std::string InspectStages(RE::TESQuest* self, RE::TESQuest* quest) {
            return QuestQuery(self, quest, Stages);
        }

        std::string InspectAliases(RE::TESQuest* self, RE::TESQuest* quest) {
            return QuestQuery(self, quest, Aliases);
        }

        std::string InspectScene(RE::TESQuest* self, RE::BGSScene* scene) {
            return QuestQuery(self, scene, Scene);
        }

        std::string InspectDialogue(RE::TESQuest* self, RE::Actor* actor, RE::TESQuest* quest) {
            if (!Valid(self) || !actor || !quest)
                return "Provide an Actor and Quest.";
            std::string text = "Dialogue conditions: " + Label(quest) + "; speaker " + Label(actor);
            text += "\nQuest conditions:" +
                    Conditions(&quest->objConditions, quest, actor, RE::PlayerCharacter::GetSingleton());
            std::set<const RE::TESTopic*> visited;
            std::size_t rows = 0;
            for (const auto& topics : quest->topics) {
                for (const auto* topic : topics) {
                    if (!topic || !visited.insert(topic).second)
                        continue;
                    text += "\nTopic " + Label(topic);
                    for (std::uint32_t i = 0; topic->topicInfos && i < topic->numTopicInfos; ++i) {
                        if (++rows > Rows || text.size() >= TextLimit)
                            return text.substr(0, TextLimit) + "\n[Dialogue listing truncated]";
                        const auto* info = topic->topicInfos[i];
                        if (info) {
                            text += std::format("\n INFO {}; saidOnce {}; flags {:04X}", Label(info), info->saidOnce,
                                                info->data.flags.underlying());
                            text += Conditions(&info->objConditions, quest, actor, RE::PlayerCharacter::GetSingleton());
                        }
                    }
                }
            }
            return text +
                   "\nCondition checks are not a dialogue availability verdict: branch, scene, "
                   "cooldown, random selection and event context can impose additional gates.";
        }

        std::string InspectStorySelection(RE::TESQuest* self, RE::TESQuest* quest) {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!Valid(self) || !quest || !data)
                return "Provide a Quest with game data available.";
            std::string text = "Story selection for " + Label(quest);
            std::size_t scanned = 0;
            for (const auto* node : data->GetFormArray<RE::BGSStoryManagerQuestNode>()) {
                if (++scanned > 8192)
                    return text + "\n[Node scan truncated]";
                if (!node || std::ranges::find(node->quests, quest) == node->quests.end())
                    continue;
                text += std::format("\nNode {}; node flags {:04X}; quest flags {:04X}; requested count {}", Label(node),
                                    node->flags.nodeFlags.underlying(), node->flags.questFags.underlying(),
                                    node->numQuestsToStart);
                std::set<const RE::BGSStoryManagerNodeBase*> seen;
                for (const RE::BGSStoryManagerNodeBase* parent = node; parent && seen.size() < 32;
                     parent = parent->parent) {
                    if (!seen.insert(parent).second)
                        break;
                    text += "\n Branch " + Label(parent) + Conditions(&parent->conditions, quest, nullptr, nullptr);
                }
                std::size_t count = 0;
                for (auto* candidate : node->quests) {
                    if (++count > Rows) {
                        text += "\n[Truncated] Row limit reached.";
                        break;
                    }
                    text += "\n Candidate " + Label(candidate);
                    if (candidate)
                        text += Conditions(&candidate->storyManagerConditions, candidate, nullptr, nullptr);
                }
                if (text.size() >= TextLimit)
                    return text.substr(0, TextLimit) + "\n[Truncated]";
            }
            return text +
                   "\nLive event payload, random draw, node cooldown timing and alias reservation decisions "
                   "are not reproduced. Event-dependent conditions remain unknown.";
        }

        std::string InspectDependencies(RE::TESQuest* self, RE::TESQuest* quest) {
            auto* data = RE::TESDataHandler::GetSingleton();
            if (!Valid(self) || !quest || !data)
                return "Provide a Quest with game data available.";
            std::string text = "Runtime references to " + Label(quest);
            std::size_t scanned = 0;
            for (const auto* owner : data->GetFormArray<RE::TESQuest>()) {
                if (++scanned > 8192)
                    break;
                if (!owner)
                    continue;
                std::size_t aliases = 0;
                for (const auto* alias : owner->aliases) {
                    if (++aliases > Rows)
                        break;
                    const auto* reference = alias ? skyrim_cast<const RE::BGSRefAlias*>(alias) : nullptr;
                    if (reference && reference->fillType == RE::BGSBaseAlias::FILL_TYPE::kFromExternal &&
                        reference->fillData.fromExternal.externalQuest == quest)
                        text += std::format("\nExternal alias: {} / {} -> alias {}", Label(owner), alias->aliasID,
                                            reference->fillData.fromExternal.externalAlias);
                }
                if (text.size() >= TextLimit)
                    return text.substr(0, TextLimit) + "\n[Truncated]";
            }
            for (const auto* scene : quest->scenes)
                text += "\nOwned scene: " + Label(scene);
            const auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            const auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            const auto targetHandle =
                policy ? policy->GetHandleForObject(static_cast<RE::VMTypeID>(RE::FormType::Quest), quest) : 0;
            std::size_t objects = 0;
            for (auto* owner : data->GetFormArray<RE::TESQuest>()) {
                if (++objects > 1024)
                    break;
                for (const auto& object : Attached(owner)) {
                    const auto* type = object->GetTypeInfo();
                    if (!object->IsInitialized() ||
                        type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid)
                        continue;
                    const auto* fields = type->GetVariableIter();
                    for (std::uint32_t i = 0; fields && i < std::min<std::uint32_t>(type->GetNumVariables(), 256);
                         ++i) {
                        const auto* value = object->GetVariable(fields[i].name);
                        const auto linked = value && value->IsObject() ? value->GetObject()
                                                                       : RE::BSTSmartPointer<RE::BSScript::Object>{};
                        if (linked && linked->GetHandle() == targetHandle)
                            text += std::format("\nScript storage reference: {} / {}.{}", Label(owner), type->GetName(),
                                                fields[i].name.c_str());
                    }
                    if (text.size() >= TextLimit)
                        return text.substr(0, TextLimit) + "\n[Truncated]";
                }
            }
            return text +
                   "\nScope: up to 8192 quests / 128 aliases each; direct script storage on first 1024 quests "
                   "/ 256 fields each. Arrays, computed getters and unbound instances are not traversed. "
                   "Use InspectStorySelection for Story Manager links.";
        }
    }

    bool Valid(RE::TESQuest* self) {
        return self && self == ServiceQuest();
    }

    std::string Label(const RE::TESForm* form) {
        if (!form)
            return "NONE";
        const auto* editor = form->GetFormEditorID();
        return std::format("{:08X} {}", form->GetFormID(), editor && *editor ? editor : form->GetName());
    }

    std::string Conditions(const RE::TESCondition* conditions, RE::TESQuest* quest, RE::TESObjectREFR* subject,
                           RE::TESObjectREFR* target) {
        if (!conditions || !conditions->head)
            return "\n  No conditions.";
        std::string text;
        std::set<const RE::TESConditionItem*> seen;
        for (const auto* item = conditions->head; item && seen.size() < Rows; item = item->next) {
            if (!seen.insert(item).second)
                break;
            const auto function = *item->data.functionData.function;
            const auto* name = FunctionName(function);
            const bool context = !item->data.flags.usesAliases && !item->data.flags.usePackData &&
                                 (item->data.object == RE::CONDITIONITEMOBJECT::kSelf ||
                                  (subject && item->data.object == RE::CONDITIONITEMOBJECT::kTarget && target)) &&
                                 (subject || ContextIndependent(function));
            RE::ConditionCheckParams params(subject, target);
            params.quest = quest;
            const std::string result = name && context ? (item->IsTrue(params) ? "PASS" : "FAIL") : "UNKNOWN";
            text += std::format("\n  {} function {} ({}); run-on {}; operator {}; {} {}; {} next", result,
                                static_cast<unsigned>(function), name ? name : "not evaluated",
                                item->data.object.underlying(), static_cast<unsigned>(item->data.flags.opCode),
                                item->data.flags.global ? "global" : "constant",
                                item->data.flags.global ? Label(item->data.comparisonValue.g)
                                                        : std::format("{}", item->data.comparisonValue.f),
                                item->data.flags.isOR ? "OR" : "AND");
            if (name && !item->data.flags.usesAliases && !item->data.flags.usePackData &&
                function != Function::kGetDead && function != Function::kGetDisabled &&
                function != Function::kGetPlayerTeammate)
                text += "\n    Form parameter " + Label(static_cast<RE::TESForm*>(item->data.functionData.params[0]));
            if (function == Function::kGetStageDone)
                text += std::format("; stage {}", reinterpret_cast<std::uintptr_t>(item->data.functionData.params[1]));
            if (function == Function::kGetVMQuestVariable)
                text += "; variable-name encoding unavailable; correlate with Scripts.InspectInstance";
        }
        if (seen.size() >= Rows)
            text += "\n[Truncated] Condition limit reached.";
        return text + "\n  Per-condition results; OR groups are not individually mandatory. UNKNOWN is not FAIL.";
    }

    std::string Lifecycle(RE::TESQuest* quest) {
        if (!quest)
            return "Provide a Quest.";
        const bool wait = quest->data.flags.any(RE::QuestFlag::kStageWait);
        return std::format(
            "{}\nLifecycle: {}\nFlags {:04X}; enabled {}; stage wait {}; promote task {}; "
            "completed {}; alreadyRun {}; event {:08X}\nCurrent stage {}; instance {}\n"
            "StageWait does not identify its cause; correlate with Scripts.InspectStacks. "
            "Engine flags are not changed by inspection.",
            Label(quest), recovery::Lifecycle(quest->IsEnabled(), wait, static_cast<bool>(quest->promoteTask)),
            quest->data.flags.underlying(), quest->IsEnabled(), wait, static_cast<bool>(quest->promoteTask),
            quest->IsCompleted(), quest->alreadyRun, std::to_underlying(quest->eventID), quest->currentStage,
            quest->currentInstanceID);
    }

    std::string Stages(RE::TESQuest* quest) {
        if (!quest)
            return "Provide a Quest.";
        std::string text = "Stages for " + Label(quest) + "; current " + std::to_string(quest->currentStage);
        std::size_t count = 0;
        if (quest->executedStages)
            for (const auto& stage : *quest->executedStages) {
                if (++count > Rows) {
                    text += "\n[Truncated] Row limit reached.";
                    break;
                }
                text +=
                    std::format("\nRecorded stage {}; flags {:02X}", stage.data.index, stage.data.flags.underlying());
            }
        count = 0;
        if (quest->waitingStages)
            for (const auto* stage : *quest->waitingStages) {
                if (++count > Rows) {
                    text += "\n[Truncated] Row limit reached.";
                    break;
                }
                if (stage)
                    text += std::format("\nWaiting stage {}; flags {:02X}", stage->data.index,
                                        stage->data.flags.underlying());
            }
        count = 0;
        for (const auto* objective : quest->objectives) {
            if (++count > Rows) {
                text += "\n[Truncated] Row limit reached.";
                break;
            }
            if (objective)
                text += std::format("\nObjective {}; state {}; {}", objective->index, objective->state.underlying(),
                                    objective->displayText.c_str());
        }
        return text +
               "\nEach list bounded to 128. Complete authored stage/fragment definitions are not retained "
               "in the mapped runtime layout; recorded stages are not a complete stage catalogue.";
    }

    std::string Aliases(RE::TESQuest* quest) {
        if (!quest)
            return "Provide a Quest.";
        std::string text = "Aliases for " + Label(quest);
        std::size_t count = 0;
        for (const auto* alias : quest->aliases) {
            if (++count > Rows) {
                text += "\n[Truncated] Row limit reached.";
                break;
            }
            if (!alias)
                continue;
            text += std::format(
                "\n{} {}; type {}; fill {}; flags {:08X}; optional {}; reserves {}; "
                "allow dead {}; allow disabled {}; loaded only {}",
                alias->aliasID, alias->aliasName.c_str(), alias->GetTypeString().c_str(), alias->fillType.underlying(),
                alias->flags.underlying(), alias->flags.any(RE::BGSBaseAlias::FLAGS::kOptional),
                alias->flags.any(RE::BGSBaseAlias::FLAGS::kReserves),
                alias->flags.any(RE::BGSBaseAlias::FLAGS::kAllowDead),
                alias->flags.any(RE::BGSBaseAlias::FLAGS::kAllowDisabled),
                alias->flags.any(RE::BGSBaseAlias::FLAGS::kLoadedOnly));
            text += AliasInstance(const_cast<RE::BGSBaseAlias*>(alias), {}, true);
            const auto* reference = skyrim_cast<const RE::BGSRefAlias*>(alias);
            if (!reference) {
                text += "\n Location binding unavailable in mapped layout.";
                continue;
            }
            auto* bound = reference->GetReference();
            text += "\n Binding " + Label(bound);
            switch (*reference->fillType) {
                case RE::BGSBaseAlias::FILL_TYPE::kForced:
                    text += "\n Authored reference " + Label(reference->fillData.forced.forcedRef.get().get());
                    break;
                case RE::BGSBaseAlias::FILL_TYPE::kUniqueActor:
                    text += "\n Unique actor " + Label(reference->fillData.uniqueActor.uniqueActor);
                    break;
                case RE::BGSBaseAlias::FILL_TYPE::kFromExternal:
                    text += std::format("\n External quest {}; alias {}",
                                        Label(reference->fillData.fromExternal.externalQuest),
                                        reference->fillData.fromExternal.externalAlias);
                    break;
                case RE::BGSBaseAlias::FILL_TYPE::kCreated:
                    text += "\n Created base " + Label(reference->fillData.created.object);
                    break;
                default:
                    break;
            }
            text += Conditions(reference->conditions, quest, bound, RE::PlayerCharacter::GetSingleton());
            if (!bound)
                text += "\n Unfilled: event payload, search candidates and reservation history are not available.";
            if (text.size() >= TextLimit)
                return text.substr(0, TextLimit) + "\n[Truncated]";
        }
        return text +
               "\nAuthored alias package/faction/spell lists are not mapped; inspect the bound actor separately. "
               "Quest/alias scripts can impose additional restrictions.";
    }

    std::string Scene(RE::BGSScene* scene) {
        if (!scene)
            return "Provide a Scene.";
        std::string text = std::format("{}\nOwner {}; playing {}; flags {:08X}", Label(scene),
                                       Label(scene->parentQuest), scene->isPlaying, scene->flags.underlying());
        text += Conditions(&scene->conditions, scene->parentQuest, nullptr, nullptr);
        std::size_t count = 0;
        for (const auto aliasID : scene->actors) {
            if (++count > Rows) {
                text += "\n[Truncated] Row limit reached.";
                break;
            }
            if (scene->parentQuest)
                for (const auto* alias : scene->parentQuest->aliases)
                    if (alias && alias->aliasID == aliasID) {
                        const auto* reference = skyrim_cast<const RE::BGSRefAlias*>(alias);
                        text += std::format("\nActor alias {} {} -> {}", aliasID, alias->aliasName.c_str(),
                                            reference ? Label(reference->GetReference()) : "binding unavailable");
                        break;
                    }
        }
        count = 0;
        for (const auto* phase : scene->phases) {
            if (++count > Rows) {
                text += "\n[Truncated] Row limit reached.";
                break;
            }
            if (phase)
                text += std::format("\nPhase {} start:{}\nCompletion:{}", count - 1,
                                    Conditions(&phase->startConditions, scene->parentQuest, nullptr, nullptr),
                                    Conditions(&phase->completionConditions, scene->parentQuest, nullptr, nullptr));
            if (text.size() >= TextLimit)
                return text.substr(0, TextLimit) + "\n[Truncated]";
        }
        count = 0;
        for (const auto* action : scene->actions) {
            if (++count > Rows) {
                text += "\n[Truncated] Row limit reached.";
                break;
            }
            if (action)
                text += std::format("\nAction {}; type {}; actor alias {}; phase {}-{}; flags {:08X}", action->index,
                                    static_cast<unsigned>(action->GetType()), action->actorID, action->startPhase,
                                    action->endPhase, action->flags.underlying());
            if (text.size() >= TextLimit)
                return text.substr(0, TextLimit) + "\n[Truncated]";
        }
        return text + "\nCurrent phase and individual action completion state are not mapped; no wait cause inferred.";
    }

    std::string Guard(RE::TESQuest* quest) {
        return Lifecycle(quest) + "\n" + Stages(quest) + "\n" + Aliases(quest) + "\n" + Instance(quest, {});
    }

    std::string QuestState(RE::TESQuest* quest) {
        std::string text = Guard(quest) + "\n" + Stacks(quest);
        if (quest)
            for (auto* scene : quest->scenes) {
                if (text.size() >= 262144)
                    return text.substr(0, 262144) + "\n[Snapshot truncated; cannot authorize repairs]";
                text += "\n" + Scene(scene);
            }
        return text;
    }

    void RegisterQuestDiagnostics(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("InspectLifecycle", services::QuestsScript, InspectLifecycle);
        vm->RegisterFunction("InspectStages", services::QuestsScript, InspectStages);
        vm->RegisterFunction("InspectAliases", services::QuestsScript, InspectAliases);
        vm->RegisterFunction("InspectDialogue", services::QuestsScript, InspectDialogue);
        vm->RegisterFunction("InspectStorySelection", services::QuestsScript, InspectStorySelection);
        vm->RegisterFunction("InspectDependencies", services::QuestsScript, InspectDependencies);
        vm->RegisterFunction("InspectScene", services::QuestsScript, InspectScene);
    }
}
