#include "PapyrusBacklog.h"
#include <mutex>
#include <fstream>
#include <limits>

namespace elder_shell {
    namespace {
        using VM = RE::BSScript::Internal::VirtualMachine;
        std::mutex backlogMutex;
        PapyrusBacklog backlog;
        std::uint64_t generation = 0;
        bool sessionReady = false;
        constexpr std::uint32_t StackLimit = 4096;
        constexpr std::uint32_t QueueLimit = 1024;
        constexpr std::uint32_t FullLimit = 1000000;

        std::string CallName(const RE::BSTSmartPointer<RE::BSScript::Internal::IFuncCallQuery>& query) {
            // Read immutable call metadata only. Do not invoke GetFunctionCallInfo:
            // query implementations may transfer their arguments when consumed.
            const auto* raw = query ? skyrim_cast<RE::BSScript::Internal::RawFuncCallQuery*>(query.get()) : nullptr;
            if (!raw)
                return "<call metadata unavailable>";
            if (!raw->objType)
                return "<unknown script>." + std::string(raw->name.c_str());
            return std::string(raw->objType->GetName()) + "." + raw->name.c_str();
        }

        void Export(PapyrusBacklog& result) {
            const auto directory = SKSE::log::log_directory();
            if (!directory) {
                result.status += " Report directory unavailable.";
                return;
            }
            const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   std::chrono::system_clock::now().time_since_epoch())
                                   .count();
            const auto path = *directory / std::format("TheElderShell-Papyrus-{}.txt", stamp);
            std::ofstream report(path);
            report << "Papyrus backlog: full overflow report\n"
                   << "Registered stacks: " << result.registeredStacks << '\n'
                   << "Latent return registrations: " << result.latentReturns << '\n'
                   << "Suspended overflow entries: " << result.suspendedOverflow << '\n'
                   << "Function-message overflow entries: " << result.functionOverflow << '\n'
                   << "Calls inspected: " << result.sampledCalls << '\n'
                   << "Stacks inspected: " << result.sampledStacks << '\n'
                   << "Overflow coverage complete: "
                   << (result.sampledCalls ==
                       static_cast<std::uint64_t>(result.suspendedOverflow) + result.functionOverflow)
                   << "\n\nCount\tReceiver / function\n";
            for (const auto& row : result.calls)
                report << row.count << '\t' << row.name << '\n';
            report << "\nCount\tStack state\n";
            for (const auto& row : result.states)
                report << row.count << '\t' << row.name << '\n';
            report << "\nCounts overlap. Fixed queues are not inspected. Each collection is read under its own lock; "
                      "this is not one atomic snapshot of the whole VM. Unknown metadata remains counted. "
                      "Counts identify receivers, not producers, and are not CPU samples or wait durations. "
                      "Full scans stop at one million entries per collection. No engine work was modified.\n";
            report.close();
            if (report)
                result.reportPath = path.string();
            else
                result.status += " Report could not be written.";
        }

        PapyrusBacklog Capture(bool full) {
            PapyrusBacklog result;
            result.full = full;
            auto* vm = VM::GetSingleton();
            if (!vm) {
                result.status = "Papyrus VM unavailable.";
                return result;
            }
            std::map<std::string, std::uint32_t> states, calls;
            {
                RE::BSSpinLockGuard lock(vm->runningStacksLock);
                result.registeredStacks = vm->allRunningStacks.size();
                result.latentReturns = vm->waitingLatentReturns.size();
                for (const auto& [id, stack] : vm->allRunningStacks) {
                    (void)id;
                    if (result.sampledStacks >= (full ? FullLimit : StackLimit))
                        break;
                    ++result.sampledStacks;
                    ++states[stack ? BacklogStateName(stack->state.underlying()) : "<missing stack>"];
                }
            }
            {
                RE::BSSpinLockGuard lock(vm->suspendQueueLock);
                result.suspendedOverflow = vm->overflowSuspendArray1.size() + vm->overflowSuspendArray2.size();
                for (const auto* queue : {&vm->overflowSuspendArray1, &vm->overflowSuspendArray2}) {
                    const auto count = std::min<std::uint32_t>(queue->size(), full ? FullLimit : QueueLimit);
                    for (std::uint32_t index = 0; index < count; ++index) {
                        ++calls[CallName((*queue)[index].funcCallQuery)];
                        ++result.sampledCalls;
                    }
                }
            }
            {
                RE::BSSpinLockGuard lock(vm->funcQueueLock);
                result.functionOverflow = vm->overflowFuncMsgs.size();
                const auto count = std::min<std::uint32_t>(vm->overflowFuncMsgs.size(), full ? FullLimit : QueueLimit);
                for (std::uint32_t index = 0; index < count; ++index) {
                    ++calls[CallName(vm->overflowFuncMsgs[index].funcQuery)];
                    ++result.sampledCalls;
                }
            }
            const auto groups = full ? std::numeric_limits<std::size_t>::max() : 32;
            result.states = BacklogRows(states, groups);
            result.calls = BacklogRows(calls, groups);
            result.captured = true;
            result.capturedAt = std::chrono::steady_clock::now();
            result.status = "Snapshot captured. Refresh to compare current queue sizes.";
            if (full)
                Export(result);
            return result;
        }
    }

    void RefreshPapyrusBacklog(bool full) {
        auto* tasks = SKSE::GetTaskInterface();
        std::uint64_t expected;
        {
            std::scoped_lock lock(backlogMutex);
            if (backlog.pending)
                return;
            if (!sessionReady || !tasks) {
                backlog.status = "Load a save or start a new game first.";
                return;
            }
            expected = generation;
            backlog.pending = true;
            backlog.status = "Snapshot queued on the game thread.";
        }
        tasks->AddTask([expected, full] {
            {
                std::scoped_lock lock(backlogMutex);
                if (expected != generation || !sessionReady)
                    return;
            }
            auto* ui = RE::UI::GetSingleton();
            auto result = ui && ui->IsMenuOpen("Main Menu") ? PapyrusBacklog{} : Capture(full);
            if (!result.captured && ui && ui->IsMenuOpen("Main Menu"))
                result.status = "Load a save or start a new game first.";
            std::scoped_lock lock(backlogMutex);
            if (expected == generation && sessionReady)
                backlog = std::move(result);
        });
    }

    PapyrusBacklog PapyrusBacklogSnapshot() {
        std::scoped_lock lock(backlogMutex);
        return backlog;
    }

    void ResetPapyrusBacklog(bool ready) {
        std::scoped_lock lock(backlogMutex);
        ++generation;
        sessionReady = ready;
        backlog = {};
    }
}
