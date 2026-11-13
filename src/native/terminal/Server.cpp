#include "Server.h"
#include "Protocol.h"
#include <Windows.h>
#include <sddl.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace elder_shell::terminal {
    namespace {
        class Handle {
        public:
            explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}

            ~Handle() {
                if (value_ && value_ != INVALID_HANDLE_VALUE)
                    CloseHandle(value_);
            }

            Handle(const Handle&) = delete;
            Handle& operator=(const Handle&) = delete;

            HANDLE Get() const {
                return value_;
            }

        private:
            HANDLE value_;
        };

        class LocalMemory {
        public:
            ~LocalMemory() {
                if (value)
                    LocalFree(value);
            }

            void* value = nullptr;
        };

        std::wstring UserSecurity() {
            HANDLE raw = nullptr;
            if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &raw))
                throw std::runtime_error("Cannot read the pipe owner's token.");
            Handle token(raw);
            DWORD size = 0;
            GetTokenInformation(token.Get(), TokenUser, nullptr, 0, &size);
            if (!size)
                throw std::runtime_error("Cannot size the pipe owner's token.");
            std::vector<std::uint8_t> bytes(size);
            if (!GetTokenInformation(token.Get(), TokenUser, bytes.data(), size, &size))
                throw std::runtime_error("Cannot read the pipe owner's identity.");
            wchar_t* sid = nullptr;
            if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid))
                throw std::runtime_error("Cannot format the pipe owner's identity.");
            LocalMemory memory;
            memory.value = sid;
            return L"D:P(A;;GA;;;" + std::wstring(sid) + L")";
        }

        bool Finish(HANDLE pipe, OVERLAPPED& operation, std::stop_token stop, DWORD& transferred) {
            while (!stop.stop_requested()) {
                const auto wait = WaitForSingleObject(operation.hEvent, 50);
                if (wait == WAIT_OBJECT_0)
                    return GetOverlappedResult(pipe, &operation, &transferred, FALSE) != FALSE;
                if (wait != WAIT_TIMEOUT)
                    break;
            }
            CancelIoEx(pipe, &operation);
            GetOverlappedResult(pipe, &operation, &transferred, TRUE);
            return false;
        }

        bool Transfer(HANDLE pipe, void* bytes, DWORD size, bool writing, std::stop_token stop) {
            auto* cursor = static_cast<std::uint8_t*>(bytes);
            while (size && !stop.stop_requested()) {
                Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                if (!event.Get())
                    return false;
                OVERLAPPED operation{};
                operation.hEvent = event.Get();
                DWORD count = 0;
                const bool done = writing ? WriteFile(pipe, cursor, size, &count, &operation) != FALSE
                                          : ReadFile(pipe, cursor, size, &count, &operation) != FALSE;
                if (!done && (GetLastError() != ERROR_IO_PENDING || !Finish(pipe, operation, stop, count)))
                    return false;
                if (!count || count > size)
                    return false;
                cursor += count;
                size -= count;
            }
            return size == 0;
        }

        bool Write(HANDLE pipe, std::string text, std::stop_token stop) {
            if (text.size() > MaximumFrame)
                text = "error\nResponse exceeds the 8 MiB transport limit; write the report to a file instead.";
            auto size = static_cast<std::uint32_t>(text.size());
            return Transfer(pipe, &size, sizeof(size), true, stop) && Transfer(pipe, text.data(), size, true, stop);
        }

        std::optional<std::string> Read(HANDLE pipe, std::stop_token stop) {
            std::uint32_t size = 0;
            if (!Transfer(pipe, &size, sizeof(size), false, stop) || !size || size > MaximumCommand + 32)
                return std::nullopt;
            std::string frame(size, '\0');
            return Transfer(pipe, frame.data(), size, false, stop) ? std::optional{std::move(frame)} : std::nullopt;
        }

        struct ReleaseOperation {
            Operation& operation;

            ~ReleaseOperation() {
                if (operation.release)
                    operation.release();
            }
        };
    }

    struct Server::State {
        explicit State(Submit value, CompleteRequest completion)
            : submit(std::move(value)), complete(std::move(completion)) {
            status.pipe = "TheElderShell-" + std::to_string(GetCurrentProcessId());
        }

        Submit submit;
        CompleteRequest complete;
        mutable std::mutex mutex;
        ServerStatus status;
        std::jthread worker;

        bool Serve(HANDLE pipe, std::stop_token stop) {
            if (!Write(pipe, "hello\n" + std::to_string(ProtocolVersion) + '\n' + status.pipe, stop))
                return false;
            while (!stop.stop_requested()) {
                const auto frame = Read(pipe, stop);
                if (!frame)
                    return false;
                if (*frame == "ping") {
                    if (!Write(pipe, "pong", stop))
                        return false;
                    continue;
                }
                Operation operation;
                if (frame->starts_with("complete\n")) {
                    auto input = DecodeCompletion(*frame);
                    if (input.error.empty() && !complete)
                        input.error = "Completion backend unavailable.";
                    if (!input.error.empty()) {
                        if (!Write(pipe, "error\n" + input.error, stop))
                            return false;
                        continue;
                    }
                    operation = complete(std::move(input.source), input.cursor);
                } else {
                    auto input = Decode(*frame);
                    if (!input.error.empty()) {
                        if (!Write(pipe, "error\n" + input.error, stop))
                            return false;
                        continue;
                    }
                    if (input.command.more) {
                        if (!Write(pipe, "continue", stop))
                            return false;
                        continue;
                    }
                    operation = submit(std::move(input.command.source), input.command.fragment);
                }
                ReleaseOperation release{operation};
                if (!operation.poll) {
                    if (!Write(pipe, "error\nExecution backend unavailable.", stop))
                        return false;
                    continue;
                }
                std::string previous;
                while (!stop.stop_requested()) {
                    const auto entry = operation.poll();
                    auto response = Encode(entry);
                    const bool final = !Pending(entry.status);
                    // Heartbeats also detect a disconnected client during latent work.
                    if (response != previous || final || heartbeat++ % 10 == 0) {
                        previous = response;
                        if (!Write(pipe, std::move(response), stop))
                            return false;
                    }
                    if (final)
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                }
            }
            return true;
        }

        unsigned heartbeat = 0;

        void Run(std::stop_token stop) {
            try {
                const auto sddl = UserSecurity();
                LocalMemory descriptor;
                if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                        sddl.c_str(), SDDL_REVISION_1, reinterpret_cast<PSECURITY_DESCRIPTOR*>(&descriptor.value),
                        nullptr))
                    throw std::runtime_error("Cannot create pipe security descriptor.");
                SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), descriptor.value, FALSE};
                const std::wstring path = L"\\\\.\\pipe\\" + std::wstring(status.pipe.begin(), status.pipe.end());
                while (!stop.stop_requested()) {
                    Handle pipe(CreateNamedPipeW(
                        path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED | FILE_FLAG_FIRST_PIPE_INSTANCE,
                        PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1, 65536, 65536,
                        0, &security));
                    if (pipe.Get() == INVALID_HANDLE_VALUE)
                        throw std::runtime_error("Cannot create named pipe (Windows error " +
                                                 std::to_string(GetLastError()) + ").");
                    {
                        std::scoped_lock lock(mutex);
                        status.listening = true;
                    }
                    Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
                    if (!event.Get())
                        throw std::runtime_error("Cannot create pipe connection event.");
                    OVERLAPPED operation{};
                    operation.hEvent = event.Get();
                    const bool connected = ConnectNamedPipe(pipe.Get(), &operation) != FALSE;
                    const auto error = connected ? ERROR_SUCCESS : GetLastError();
                    DWORD transferred = 0;
                    if (!connected && error != ERROR_PIPE_CONNECTED &&
                        (error != ERROR_IO_PENDING || !Finish(pipe.Get(), operation, stop, transferred)))
                        break;
                    {
                        std::scoped_lock lock(mutex);
                        status.connected = true;
                    }
                    Serve(pipe.Get(), stop);
                    DisconnectNamedPipe(pipe.Get());
                    {
                        std::scoped_lock lock(mutex);
                        status.connected = false;
                    }
                }
            } catch (const std::exception& error) {
                std::scoped_lock lock(mutex);
                status.error = error.what();
            }
            std::scoped_lock lock(mutex);
            status.listening = status.connected = false;
        }
    };

    Server::Server(Submit submit, CompleteRequest complete)
        : state_(std::make_unique<State>(std::move(submit), std::move(complete))) {}

    Server::~Server() {
        Stop();
    }

    void Server::Start() {
        Stop();
        {
            std::scoped_lock lock(state_->mutex);
            state_->status.error.clear();
        }
        state_->worker = std::jthread([this](std::stop_token stop) { state_->Run(stop); });
    }

    void Server::Stop() {
        if (state_->worker.joinable()) {
            state_->worker.request_stop();
            state_->worker.join();
        }
    }

    ServerStatus Server::Snapshot() const {
        std::scoped_lock lock(state_->mutex);
        return state_->status;
    }
}
