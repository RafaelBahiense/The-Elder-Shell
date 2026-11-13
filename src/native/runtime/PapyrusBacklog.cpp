#include "PapyrusBacklog.h"
#include <mutex>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <utility>

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
            WriteBacklogReport(report, result, std::numeric_limits<std::size_t>::max());
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

        std::optional<std::uint64_t> BeginCapture(std::string& error) {
            std::scoped_lock lock(backlogMutex);
            if (backlog.pending) {
                error = "Backlog capture is already pending; wait for it to finish.";
                return std::nullopt;
            }
            if (!sessionReady) {
                backlog.status = error = "Load a save or start a new game first.";
                return std::nullopt;
            }
            backlog.pending = true;
            backlog.status = "Snapshot queued on the game thread.";
            return generation;
        }

        PapyrusBacklog CaptureAndPublish(std::uint64_t expected, bool full) {
            {
                std::scoped_lock lock(backlogMutex);
                if (expected != generation || !sessionReady)
                    return {.status = "Game session changed; backlog capture was discarded."};
            }
            PapyrusBacklog result;
            try {
                auto* ui = RE::UI::GetSingleton();
                if (ui && ui->IsMenuOpen("Main Menu"))
                    result.status = "Load a save or start a new game first.";
                else
                    result = Capture(full);
            } catch (const std::exception& error) {
                result.status = "Backlog capture failed: " + std::string(error.what());
            }
            std::scoped_lock lock(backlogMutex);
            if (expected != generation || !sessionReady)
                return {.status = "Game session changed; backlog capture was discarded."};
            backlog = result;
            return result;
        }
    }

    void RefreshPapyrusBacklog(bool full) {
        auto* tasks = SKSE::GetTaskInterface();
        if (!tasks) {
            std::scoped_lock lock(backlogMutex);
            backlog.status = "SKSE task interface unavailable.";
            return;
        }
        std::string error;
        if (const auto expected = BeginCapture(error))
            tasks->AddTask([expected = *expected, full] { CaptureAndPublish(expected, full); });
    }

    PapyrusBacklog CapturePapyrusBacklog(bool full) {
        std::string error;
        if (const auto expected = BeginCapture(error))
            return CaptureAndPublish(*expected, full);
        return {.status = std::move(error)};
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
