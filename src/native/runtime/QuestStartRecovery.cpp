#include "Diagnostics.h"
#include "Services.h"
#include "shell/QuestStart.h"
#include "shell/Services.h"
#include <array>
#include <set>

namespace elder_shell::diagnostics {
    namespace {
        using VM = RE::BSScript::Internal::VirtualMachine;

        // Verified SSE 1.6.1170 layouts. These overlays never own or erase engine records.
        struct StartWaiter {
            bool needsStage;
            std::array<std::byte, 3> pad01;
            std::uint32_t stage;
            std::uint32_t stack;
            std::uint32_t pad0C;
            RE::BSTSmartPointer<RE::BSScript::IVirtualMachine> vm;
        };

        static_assert(sizeof(StartWaiter) == 0x18);
        static_assert(offsetof(StartWaiter, vm) == 0x10);

        struct PendingStart {
            std::uint32_t references;
            std::uint32_t pad04;
            RE::BSTArray<StartWaiter> calls;
        };

        static_assert(sizeof(PendingStart) == 0x20);
        static_assert(offsetof(PendingStart, calls) == 8);

        struct CallbackManager {
            std::array<std::uintptr_t, 3> vtables;
            std::uint32_t unknown18;
            RE::BSSpinLock startLock;
            std::uint32_t pad24;
            RE::BSTHashMap<RE::FormID, PendingStart*> starts;
            RE::BSSpinLock stageLock;
            RE::BSTHashMap<RE::FormID, void*> stages;
        };

        static_assert(offsetof(CallbackManager, startLock) == 0x1C);
        static_assert(offsetof(CallbackManager, starts) == 0x28);
        static_assert(offsetof(CallbackManager, stageLock) == 0x58);
        static_assert(offsetof(CallbackManager, stages) == 0x60);

        using CompleteStarts = void (*)(PendingStart*, RE::TESQuest*, bool, bool);
        constexpr auto Operation = "fail-pending-start";

        // Hash the entire verified routine, including its VM-return and reference cleanup paths.
        // RIP-relative operands are independent of ASLR. Modified/unrecognized code is rejected.
        bool VerifiedCode(std::uintptr_t address) {
            auto hash = std::uint64_t{14695981039346656037ull};
            const auto* bytes = reinterpret_cast<const unsigned char*>(address);
            for (std::size_t i = 0; i < 0x2CB; ++i)
                hash = (hash ^ bytes[i]) * 1099511628211ull;
            return hash == 0x0601407D48A54826ull;
        }

        std::string Resolve(CallbackManager*& manager, CompleteStarts& complete) {
            if (REL::Module::get().version() != REL::Version{1, 6, 1170, 0})
                return "Startup waiter mapping is available only on Skyrim SSE 1.6.1170.0.";
            const REL::Relocation<CallbackManager**> singleton{REL::ID{406735}};
            const REL::Relocation<CompleteStarts> routine{REL::ID{53713}};
            manager = *singleton;
            if (!manager)
                return "Quest callback manager unavailable.";
            for (std::size_t i = 0; i < manager->vtables.size(); ++i)
                if (manager->vtables[i] != RE::VTABLE_SkyrimScript__QuestCallbackMgr[i].address())
                    return "Quest callback manager layout verification failed.";
            if (!VerifiedCode(routine.address()))
                return "Startup completion routine differs from the verified engine code; recovery rejected.";
            complete = routine.get();
            return {};
        }

        std::string StackError(VM* vm, RE::TESQuest* quest, std::uint32_t id, std::uint32_t& stage) {
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!policy || !id)
                return "VM or selected stack unavailable.";
            const auto handle = policy->GetHandleForObject(static_cast<RE::VMTypeID>(RE::FormType::Quest), quest);
            RE::BSSpinLockGuard lock(vm->runningStacksLock);
            const auto found = vm->allRunningStacks.find(id);
            const auto latent = vm->waitingLatentReturns.find(id);
            if (found == vm->allRunningStacks.end() || latent == vm->waitingLatentReturns.end() || !found->second ||
                found->second != latent->second)
                return "Selected stack has no matching live latent-return registration.";
            const auto& stack = found->second;
            if (stack->stackID != id || stack->state != RE::BSScript::Stack::State::kWaitingOnLatentFunction ||
                stack->freezeState != RE::BSScript::Stack::FreezeState::kUnfrozen || !stack->callback ||
                stack->nextStack || !stack->top)
                return "Selected stack is not an unfrozen, independent latent call with a result callback.";
            const auto function = stack->top->owningFunction;
            if (!function || !function->GetIsNative() || function->GetIsStatic() ||
                _stricmp(function->GetObjectTypeName().c_str(), "Quest") != 0 ||
                _stricmp(function->GetName().c_str(), "SetCurrentStageID") != 0 ||
                !function->GetReturnType().IsBool() || function->GetParamCount() != 1)
                return "Selected native call is not Quest.SetCurrentStageID(Int) returning Bool.";
            RE::BSFixedString parameter;
            RE::BSScript::TypeInfo type;
            function->GetParam(0, parameter, type);
            if (!type.IsInt() || function->GetStackFrameSize() < 1 || stack->top->parent != stack.get())
                return "Native stage parameter metadata is invalid.";
            const auto& argument = stack->top->GetStackFrameVariable(0, stack->top->GetPageForFrame());
            if (!argument.IsInt() || argument.GetSInt() <= 0 || argument.GetSInt() > 65535)
                return "Native stage argument is invalid.";
            stage = static_cast<std::uint32_t>(argument.GetSInt());
            std::set<const RE::BSScript::StackFrame*> visited;
            for (const auto* frame = stack->top; frame; frame = frame->previousFrame) {
                if (visited.size() >= 32 || !visited.insert(frame).second || frame->parent != stack.get() ||
                    !frame->owningFunction || !frame->self.IsObject())
                    return "Stack chain ownership is incomplete.";
                const auto object = frame->self.GetObject();
                if (!object || object->GetHandle() != handle)
                    return "Stack chain contains another owner; recovery rejected.";
            }
            return {};
        }

        // Caller holds startLock. VM lock is released before invoking any engine completion.
        std::string Validate(CallbackManager* manager, RE::TESQuest* quest, std::uint32_t id, PendingStart*& pending,
                             std::string& state) {
            if (!quest || quest == ServiceQuest())
                return "Provide a quest other than the Elder Shell service quest.";
            if (manager->starts.size() > 4096)
                return "Startup waiter map exceeds the inspection limit.";
            const auto found = manager->starts.find(quest->GetFormID());
            if (found == manager->starts.end() || !found->second)
                return "No startup waiter registration for this quest.";
            pending = found->second;
            recovery::StartWait data;
            data.enabled = quest->IsEnabled();
            data.stageWait = quest->data.flags.any(RE::QuestFlag::kStageWait);
            data.promoting = quest->promoteTask != nullptr;
            data.currentStage = quest->currentStage;
            data.waiters = pending->calls.size();
            {
                RE::BSSpinLockGuard lock(manager->stageLock);
                if (manager->stages.size() > 4096)
                    return "Stage waiter map exceeds the inspection limit.";
                data.stageCompletion = manager->stages.find(quest->GetFormID()) != manager->stages.end();
            }
            if (data.waiters != 1 || !pending->calls.data() || pending->calls.capacity() < data.waiters)
                return "Requires exactly one startup waiter.";
            const auto& call = pending->calls.front();
            auto* vm = VM::GetSingleton();
            data.needsStage = call.needsStage;
            data.sameVM = vm && call.vm.get() == static_cast<RE::BSScript::IVirtualMachine*>(vm);
            data.sameStack = call.stack == id;
            data.requestedStage = call.stage;
            if (!data.sameVM || !data.sameStack)
                return "Registered startup waiter belongs to another VM or stack.";
            if (const auto error = StackError(vm, quest, id, data.argumentStage); !error.empty())
                return error;
            if (const auto error = recovery::StartWaitError(data); !error.empty())
                return error;
            state = std::format("{}\nStack {:08X}; startup stage {}; record {:016X}; VM {:016X}", Lifecycle(quest), id,
                                call.stage, reinterpret_cast<std::uintptr_t>(pending),
                                reinterpret_cast<std::uintptr_t>(vm));
            return {};
        }

        std::string Identity(RE::TESQuest* quest, std::uint32_t stack) {
            return std::format("{}:{}", quest ? quest->GetFormID() : 0, stack);
        }

        std::string InspectStartWaiters(RE::TESQuest* self, RE::TESQuest* quest) {
            if (!Valid(self) || !quest)
                return "Provide a Quest and valid service receiver.";
            CallbackManager* manager = nullptr;
            CompleteStarts complete = nullptr;
            if (const auto error = Resolve(manager, complete); !error.empty())
                return error;
            RE::BSSpinLockGuard lock(manager->startLock);
            if (manager->starts.size() > 4096)
                return "Startup waiter map exceeds the inspection limit.";
            std::string text = "Startup waiters: " + Label(quest);
            const auto found = manager->starts.find(quest->GetFormID());
            if (found == manager->starts.end() || !found->second)
                return text + "\nNo startup registration.";
            const auto& calls = found->second->calls;
            text += std::format("\nWaiters: {}", calls.size());
            if (calls.size() > Rows || calls.size() > calls.capacity() || (calls.size() && !calls.data()))
                return text + "\n[Truncated]";
            for (const auto& call : calls)
                text += std::format("\nStack {:08X}; stage {}; needs stage {}; current VM {}", call.stack, call.stage,
                                    call.needsStage, call.vm.get() == VM::GetSingleton());
            RE::BSSpinLockGuard stageLock(manager->stageLock);
            if (manager->stages.size() > 4096)
                return text + "\nStage waiter map exceeds the inspection limit.";
            text += std::format("\nSeparate stage-completion registration: {}",
                                manager->stages.find(quest->GetFormID()) != manager->stages.end());
            return text;
        }

        std::string PreviewFailPendingStart(RE::TESQuest* self, RE::TESQuest* quest, std::int32_t stack) {
            if (!Valid(self))
                return RejectRepair("Service receiver unavailable.");
            CallbackManager* manager = nullptr;
            CompleteStarts complete = nullptr;
            if (const auto error = Resolve(manager, complete); !error.empty())
                return RejectRepair(error);
            const auto id = static_cast<std::uint32_t>(stack);
            std::string state;
            PendingStart* pending = nullptr;
            RE::BSSpinLockGuard lock(manager->startLock);
            if (const auto error = Validate(manager, quest, id, pending, state); !error.empty())
                return RejectRepair(error);
            return RetainRepair(Operation, Identity(quest, id), state,
                                std::format("{}: fail startup stage {} for stack {:08X} through the engine's "
                                            "completion handler. Clears its waiter and resumes Papyrus with False. "
                                            "Remaining script code executes normally; no flags, aliases or stages "
                                            "are reset. Verify lifecycle after the stack finishes.",
                                            Label(quest), pending->calls.front().stage, id));
        }

        std::string FailPendingStart(RE::TESQuest* self, RE::TESQuest* quest, std::int32_t stack, std::string plan) {
            if (!Valid(self))
                return "Service receiver unavailable.";
            const auto id = static_cast<std::uint32_t>(stack);
            CallbackManager* manager = nullptr;
            CompleteStarts complete = nullptr;
            auto error = Resolve(manager, complete);
            if (!error.empty()) {
                ConsumeRepair(plan, Operation, Identity(quest, id), "unavailable");
                return error;
            }
            const auto before = Lifecycle(quest);
            {
                RE::BSSpinLockGuard lock(manager->startLock);
                PendingStart* pending = nullptr;
                std::string state;
                error = Validate(manager, quest, id, pending, state);
                const auto planError = ConsumeRepair(plan, Operation, Identity(quest, id), state);
                if (!error.empty() || !planError.empty())
                    return !error.empty() ? error : planError;
                auto* tls = RE::GetStaticTLSData();
                if (!tls)
                    return "Engine thread-local context unavailable.";

                // Mirrors the failed-start event's completion branch. Keep the manager lock,
                // but never hold runningStacksLock: the engine completes the registered VM return.
                // The normal event caller runs this handler in engine task context 0x14.
                struct TaskContext {
                    RE::TLSData* tls;
                    std::uint32_t previous;

                    ~TaskContext() {
                        tls->taskFlag = previous;
                    }
                } context{tls, tls->taskFlag};

                tls->taskFlag = 0x14;
                complete(pending, quest, false, true);
                if (!pending->calls.empty())
                    return "Engine handler returned with waiters remaining; inspect again. Do not replay.";
            }
            SKSE::log::warn("Failed pending quest startup {:08X} stack {:08X}; plan {}", quest->GetFormID(), id, plan);
            return "Engine failure completion dispatched; startup waiter cleared. Papyrus continuation is pending."
                   "\nBefore:\n" +
                   before + "\nAfter:\n" + Lifecycle(quest);
        }
    }

    void RegisterStartRecovery(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("InspectStartWaiters", services::QuestsScript, InspectStartWaiters);
        vm->RegisterFunction("PreviewFailPendingStart", services::QuestsScript, PreviewFailPendingStart);
        vm->RegisterFunction("FailPendingStart", services::QuestsScript, FailPendingStart);
    }
}
