#include "Diagnostics.h"
#include "Services.h"
#include "shell/Recovery.h"
#include "shell/Services.h"
#include "vm/ValueDisplay.h"
#include <set>

namespace elder_shell::diagnostics {
    namespace {
        using VM = RE::BSScript::Internal::VirtualMachine;
        using Object = RE::BSTSmartPointer<RE::BSScript::Object>;

        std::string Value(const RE::BSScript::Variable& value) {
            if (value.IsObject()) {
                const auto object = value.GetObject();
                if (object && !object->GetTypeInfo())
                    return "<untyped object>";
            }
            if (value.IsArray()) {
                const auto array = value.GetArray();
                return array ? std::format("{} [{} elements; contents not traversed]", value.GetType().TypeAsString(),
                                           array->size())
                             : "None array";
            }
            if (value.IsString() && value.GetString().size() > 2048)
                return Display(value) + " [Truncated]";
            return Display(value);
        }

        struct Frame {
            std::string script;
            std::string function;
            std::string returnType;
            std::string owner;
            std::string variables;
            std::uint32_t instruction = 0;
            bool native = false;
            bool matches = false;
        };

        struct Execution {
            std::uint32_t id = 0;
            std::uint32_t state = 0;
            std::uint32_t freeze = 0;
            bool readable = false;
            bool matches = false;
            bool resultCallback = false;
            bool latentRegistered = false;
            std::uint32_t nextStack = 0;
            std::vector<Frame> frames;
        };

        std::vector<Execution> Executions(RE::TESForm* owner, const std::string& script, std::uint32_t selected = 0,
                                          bool locals = false) {
            auto* vm = VM::GetSingleton();
            std::vector<Execution> result;
            if (!vm)
                return result;
            const auto* policy = vm->GetObjectHandlePolicy();
            const auto handle = owner && policy
                                    ? policy->GetHandleForObject(static_cast<RE::VMTypeID>(owner->GetFormType()), owner)
                                    : 0;
            // Collection and paused-frame snapshots share the VM's ownership lock.
            // Never dispatch VM calls, evaluate getters, or retain frame pointers here.
            RE::BSSpinLockGuard lock(vm->runningStacksLock);
            std::vector<std::pair<std::uint32_t, RE::BSTSmartPointer<RE::BSScript::Stack>>> candidates;
            bool limited = false;
            if (selected) {
                const auto found = vm->allRunningStacks.find(selected);
                if (found != vm->allRunningStacks.end())
                    candidates.emplace_back(found->first, found->second);
            } else {
                for (const auto& [id, stack] : vm->allRunningStacks) {
                    if (candidates.size() >= 4096) {
                        limited = true;
                        break;
                    }
                    candidates.emplace_back(id, stack);
                }
            }
            for (const auto& [id, stack] : candidates) {
                if (result.size() >= Rows) {
                    Execution incomplete;
                    incomplete.matches = true;
                    result.push_back(std::move(incomplete));
                    break;
                }
                if (!stack || (selected && selected != id))
                    continue;
                Execution entry;
                entry.id = id;
                entry.state = stack->state.underlying();
                entry.freeze = stack->freezeState.underlying();
                entry.resultCallback = static_cast<bool>(stack->callback);
                entry.nextStack = stack->nextStack ? stack->nextStack->stackID : 0;
                entry.latentRegistered = vm->waitingLatentReturns.find(id) != vm->waitingLatentReturns.end();
                entry.readable = stack->state == RE::BSScript::Stack::State::kWaitingOnLatentFunction &&
                                 stack->freezeState == RE::BSScript::Stack::FreezeState::kUnfrozen;
                entry.matches = !owner && script.empty();
                if (entry.readable) {
                    std::set<const RE::BSScript::StackFrame*> visited;
                    for (const auto* frame = stack->top; frame && visited.size() < Rows; frame = frame->previousFrame) {
                        if (!visited.insert(frame).second || frame->parent != stack.get())
                            break;
                        const auto function = frame->owningFunction;
                        if (!function)
                            break;
                        Frame row;
                        row.script = function->GetObjectTypeName().c_str();
                        row.function = function->GetName().c_str();
                        row.returnType = function->GetReturnType().TypeAsString();
                        row.native = function->GetIsNative();
                        row.instruction = frame->instructionPointer;
                        row.owner = Value(frame->self);
                        const auto object = frame->self.IsObject() ? frame->self.GetObject() : Object{};
                        bool ownerMatches = !owner || (object && object->GetHandle() == handle);
                        if (!ownerMatches && object && owner && owner->As<RE::TESQuest>())
                            for (const auto aliasType : {RE::BGSRefAlias::VMTYPEID, RE::BGSLocAlias::VMTYPEID})
                                if (const auto* alias = static_cast<RE::BGSBaseAlias*>(object->Resolve(aliasType));
                                    alias && alias->owningQuest == owner) {
                                    ownerMatches = true;
                                    break;
                                }
                        row.matches =
                            ownerMatches && (script.empty() || _stricmp(row.script.c_str(), script.c_str()) == 0);
                        entry.matches = entry.matches || row.matches;
                        if (locals) {
                            const auto count = std::min<std::uint32_t>(function->GetStackFrameSize(), 128);
                            const auto page = frame->GetPageForFrame();
                            for (std::uint32_t index = 0; index < count; ++index) {
                                RE::BSFixedString name;
                                function->GetVarNameForStackIndex(index, name);
                                row.variables += std::format("\n    {}: {}", name.empty() ? "<unnamed>" : name.c_str(),
                                                             Value(frame->GetStackFrameVariable(index, page)));
                                if (row.variables.size() >= 4096)
                                    break;
                            }
                        }
                        entry.frames.push_back(std::move(row));
                    }
                }
                // Unknown ownership is retained, not silently treated as unrelated.
                if (entry.matches || !entry.readable)
                    result.push_back(std::move(entry));
            }
            if (limited) {
                Execution incomplete;
                incomplete.matches = true;
                result.push_back(std::move(incomplete));
            }
            std::ranges::sort(result, {}, &Execution::id);
            return result;
        }

        std::string ExecutionText(const Execution& entry, bool latent) {
            if (!entry.id)
                return "[Truncated] Execution scan exceeded its limit; ownership is incomplete.";
            std::string text = std::format("Stack {:08X}; state {}; freeze {}", entry.id, entry.state, entry.freeze);
            text += std::format("\nScript-result callback present {}; latent-return registration {}; next stack {:08X}",
                                entry.resultCallback, entry.latentRegistered, entry.nextStack);
            if (!entry.readable)
                return text + "\nFrames not read: stack is executing, transitioning or frozen; ownership unverified.";
            for (const auto& frame : entry.frames) {
                text +=
                    std::format("\n  {}.{} -> {}; native {}; instruction {}; owner {}{}", frame.script, frame.function,
                                frame.returnType, frame.native, frame.instruction, frame.owner, frame.variables);
                if (text.size() > TextLimit)
                    return text.substr(0, TextLimit) + "\n[Truncated]";
            }
            if (latent) {
                const bool native = !entry.frames.empty() && entry.frames.front().native;
                text += "\nPending engine operation/callback: not exposed by the supported VM interface.\n";
                text += recovery::LatentRecoveryError(entry.state == 3, native, false);
                if (native && entry.frames.front().script == "Quest" &&
                    entry.frames.front().function == "SetCurrentStageID")
                    text +=
                        "\nFor startup waits, inspect Quests.InspectStartWaiters(ownerQuest). "
                        "Its operation-specific recovery has separate runtime and ownership checks.";
            }
            return text;
        }

        std::string InspectStacks(RE::TESQuest* self, RE::TESForm* owner, std::string script) {
            return Valid(self) ? Stacks(owner, script) : "Elder Shell service receiver unavailable.";
        }

        std::string InspectStack(RE::TESQuest* self, std::int32_t id) {
            return Valid(self) ? Stack(static_cast<std::uint32_t>(id), false) : "Service receiver unavailable.";
        }

        std::string InspectLatentCall(RE::TESQuest* self, std::int32_t id) {
            return Valid(self) ? Stack(static_cast<std::uint32_t>(id), true) : "Service receiver unavailable.";
        }

        std::string InspectInstance(RE::TESQuest* self, RE::TESForm* owner, std::string script) {
            return Valid(self) ? Instance(owner, script) : "Service receiver unavailable.";
        }

        std::string InspectAliasInstance(RE::TESQuest* self, RE::TESQuest* quest, std::int32_t aliasID,
                                         std::string script) {
            if (Valid(self) && quest && aliasID >= 0)
                for (auto* alias : quest->aliases)
                    if (alias && alias->aliasID == static_cast<std::uint32_t>(aliasID))
                        return AliasInstance(alias, script);
            return "Provide an existing Quest and alias ID.";
        }

        std::string PreviewPropertyRestore(RE::TESQuest* self, RE::TESForm* owner, std::string script,
                                           std::string property, std::string expected, std::string replacement) {
            return Valid(self) ? PropertySource(owner, script, property, expected, replacement)
                               : "Service receiver unavailable.";
        }
    }

    namespace {
        std::vector<Object> Attachments(RE::VMTypeID type, const void* owner) {
            auto* vm = VM::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            std::vector<Object> result;
            if (!owner || !policy)
                return result;
            const auto handle = policy->GetHandleForObject(type, owner);
            {
                RE::BSSpinLockGuard lock(vm->attachedScriptsLock);
                const auto found = vm->attachedScripts.find(handle);
                if (found != vm->attachedScripts.end())
                    for (const auto& object : found->second) {
                        if (result.size() >= Rows)
                            break;
                        if (object && object->GetTypeInfo())
                            result.emplace_back(object.get());
                    }
            }
            std::ranges::sort(result, [](const auto& a, const auto& b) {
                return std::string_view(a->GetTypeInfo()->GetName()) < b->GetTypeInfo()->GetName();
            });
            return result;
        }

        std::string DescribeInstances(std::string label, const std::vector<Object>& objects,
                                      const std::string& script) {
            std::string text = "Script instances on " + label;
            if (objects.size() >= Rows)
                text += "\n[Truncated] Attachment limit reached.";
            for (const auto& object : objects) {
                if (!script.empty() && _stricmp(object->GetTypeInfo()->GetName(), script.c_str()) != 0)
                    continue;
                text += std::format("\n{}; handle {:016X}; initialized {}; state {}", object->GetTypeInfo()->GetName(),
                                    object->GetHandle(), object->IsInitialized(), object->currentState.c_str());
                if (!object->IsInitialized())
                    continue;
                std::set<const RE::BSScript::ObjectTypeInfo*> visited;
                for (auto* type = object->GetTypeInfo(); type && visited.size() < 32; type = type->GetParent()) {
                    if (!visited.insert(type).second ||
                        type->linkedValid != RE::BSScript::ObjectTypeInfo::LinkValidState::kLinkedValid)
                        break;
                    const auto* fields = type->GetVariableIter();
                    if (type->GetNumVariables() > 256 || type->GetNumProperties() > 256)
                        text += "\n[Truncated] Script metadata limit reached.";
                    for (std::uint32_t i = 0; fields && i < std::min<std::uint32_t>(type->GetNumVariables(), 256); ++i)
                        if (const auto* value = object->GetVariable(fields[i].name))
                            text += std::format("\n  field {} {} = {}", fields[i].type.TypeAsString(),
                                                fields[i].name.c_str(), Value(*value));
                    const auto* properties = type->GetPropertyIter();
                    for (std::uint32_t i = 0; properties && i < std::min<std::uint32_t>(type->GetNumProperties(), 256);
                         ++i)
                        text += std::format("\n  property {} {}; permissions {} (getter not executed)",
                                            properties[i].info.type.TypeAsString(), properties[i].name.c_str(),
                                            std::to_underlying(*properties[i].info.permissions));
                    if (text.size() >= TextLimit)
                        return text.substr(0, TextLimit) + "\n[Truncated]";
                }
            }
            return text + "\nFields are debug storage, not property getter results; arrays are not traversed.";
        }
    }

    std::vector<Object> Attached(RE::TESForm* owner) {
        return owner ? Attachments(static_cast<RE::VMTypeID>(owner->GetFormType()), owner) : std::vector<Object>{};
    }

    std::string Instance(RE::TESForm* owner, const std::string& script) {
        return owner ? DescribeInstances(Label(owner), Attached(owner), script) : "Provide a Form owner.";
    }

    std::string AliasInstance(RE::BGSBaseAlias* alias, const std::string& script, bool summary) {
        if (!alias)
            return "Provide an alias.";
        const auto objects = Attachments(alias->GetVMTypeID(), alias);
        const auto label = std::format("{} / alias {}", Label(alias->owningQuest), alias->aliasID);
        if (!summary)
            return DescribeInstances(label, objects, script);
        std::string text = "\n Attached scripts:";
        for (const auto& object : objects)
            text += " " + std::string(object->GetTypeInfo()->GetName());
        return objects.empty() ? text + " NONE" : text;
    }

    std::string Stacks(RE::TESForm* owner, const std::string& script) {
        const auto executions = Executions(owner, script);
        std::string text = "Live stacks: " + Label(owner) + "; script " + (script.empty() ? "any" : script);
        for (const auto& entry : executions) {
            text += "\n" + ExecutionText(entry, false);
            if (text.size() >= TextLimit)
                return text.substr(0, TextLimit) + "\n[Truncated]";
        }
        return text +
               "\nBounded to 4096 inspected stacks / 128 rows. Non-latent ownership is unverified; "
               "absence from this report is not proof of no execution.";
    }

    std::string Stack(std::uint32_t id, bool latentOnly) {
        if (!id)
            return "Provide a nonzero stack ID.";
        const auto executions = Executions(nullptr, {}, id, true);
        return executions.empty() ? "Stack not found within the bounded scan; it may have completed."
                                  : ExecutionText(executions.front(), latentOnly);
    }

    bool HasOwnerExecution(RE::TESForm* owner) {
        const auto executions = Executions(owner, {});
        return std::ranges::any_of(executions, [](const auto& execution) { return execution.matches; });
    }

    void RegisterScriptDiagnostics(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("InspectStacks", services::ScriptsScript, InspectStacks);
        vm->RegisterFunction("InspectStack", services::ScriptsScript, InspectStack);
        vm->RegisterFunction("InspectLatentCall", services::ScriptsScript, InspectLatentCall);
        vm->RegisterFunction("InspectInstance", services::ScriptsScript, InspectInstance);
        vm->RegisterFunction("InspectAliasInstance", services::ScriptsScript, InspectAliasInstance);
        vm->RegisterFunction("PreviewPropertyRestore", services::ScriptsScript, PreviewPropertyRestore);
        vm->RegisterFunction("CheckPropertyRestore", services::ScriptsScript, CheckPropertyPlan);
    }
}
