#include "terminal/Settings.h"
#include "terminal/Server.h"
#include "terminal/Protocol.h"
#include "vm/VM.h"
#include "SKSE/API.h"
#include "SKSE/Interfaces.h"
#include <Windows.h>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>

namespace elder_shell::terminal {
    namespace {
        struct Queued {
            std::mutex mutex;
            TrackedSubmission submission;
            bool submitted = false, released = false;
        };

        Operation Queue(std::string source, bool fragment) {
            auto state = std::make_shared<Queued>();
            const auto generation = RequestGeneration();
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                state->submitted = true;
                state->submission.error = "SKSE task interface unavailable.";
            } else {
                tasks->AddTask([state, source = std::move(source), fragment, generation] {
                    std::scoped_lock lock(state->mutex);
                    if (state->released)
                        return;
                    try {
                        state->submission = SubmitTracked(source, fragment, generation);
                    } catch (const std::exception& error) {
                        state->submission.error = error.what();
                    }
                    state->submitted = true;
                });
            }
            return {[state] {
                        std::scoped_lock lock(state->mutex);
                        if (!state->submitted)
                            return Entry{{}, {}, Status::Queued, "Waiting for the game thread."};
                        if (!state->submission.error.empty())
                            return Entry{state->submission.ticket, {}, Status::Rejected, state->submission.error};
                        const auto entry = RequestSnapshot(state->submission.ticket);
                        return entry.value_or(Entry{state->submission.ticket,
                                                    {},
                                                    Status::Abandoned,
                                                    "Request is no longer retained; it will not be replayed."});
                    },
                    [state] {
                        std::scoped_lock lock(state->mutex);
                        state->released = true;
                        if (state->submission.ticket.id)
                            ReleaseRequest(state->submission.ticket);
                    }};
        }

        Operation QueueCompletion(std::string source, std::size_t cursor) {
            struct Pending {
                std::mutex mutex;
                Entry response{{}, {}, Status::Queued, "Waiting for completion metadata on the game thread."};
                bool released = false;
            };

            auto state = std::make_shared<Pending>();
            const auto generation = RequestGeneration();
            auto* tasks = SKSE::GetTaskInterface();
            if (!tasks) {
                state->response.status = Status::Rejected;
                state->response.result = "SKSE task interface unavailable.";
            } else {
                tasks->AddTask([state, source = std::move(source), cursor, generation] {
                    std::scoped_lock lock(state->mutex);
                    if (state->released)
                        return;
                    try {
                        const auto completion = CompleteInput(source, cursor, generation);
                        state->response.result = EncodeCompletions(source, completion);
                        state->response.status = Status::Completed;
                    } catch (const std::exception& error) {
                        state->response.result = error.what();
                        state->response.status = Status::Rejected;
                    }
                });
            }
            return {[state, generation] {
                        std::scoped_lock lock(state->mutex);
                        if (RequestGeneration() != generation)
                            return Entry{{}, {}, Status::Abandoned, "Game session changed; completion was discarded."};
                        return state->response;
                    },
                    [state] {
                        std::scoped_lock lock(state->mutex);
                        state->released = true;
                    }};
        }

        Server server{Queue, QueueCompletion};
        std::mutex settingsMutex;
        bool enabled = false;
        std::string configurationError;

        std::filesystem::path Configuration() {
            return std::filesystem::absolute("Data/SKSE/Plugins/TheElderShell.ini");
        }
    }

    void Initialize() {
        const auto path = Configuration();
        if (!std::filesystem::exists(path))
            WritePrivateProfileStringW(L"Terminal", L"Enabled", L"false", path.c_str());
        const auto value = GetPrivateProfileIntW(L"Terminal", L"Enabled", 0, path.c_str());
        // Windows profile integer readers accept 0/1; also support readable true/false values.
        wchar_t text[16]{};
        GetPrivateProfileStringW(L"Terminal", L"Enabled", L"false", text, 16, path.c_str());
        enabled = value != 0 || _wcsicmp(text, L"true") == 0;
        if (enabled)
            server.Start();
    }

    void SetEnabled(bool value) {
        std::scoped_lock lock(settingsMutex);
        configurationError.clear();
        if (!WritePrivateProfileStringW(L"Terminal", L"Enabled", value ? L"true" : L"false", Configuration().c_str()))
            configurationError = "Could not save the terminal setting; the change applies to this process only.";
        enabled = value;
        if (enabled)
            server.Start();
        else
            server.Stop();
    }

    SettingsStatus SettingsSnapshot() {
        std::scoped_lock lock(settingsMutex);
        return {enabled, server.Snapshot(), configurationError};
    }
}
