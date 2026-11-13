#pragma once
#include "shell/Requests.h"
#include <functional>
#include <cstddef>
#include <memory>
#include <string>

namespace elder_shell::terminal {
    struct Operation {
        std::function<Entry()> poll;
        std::function<void()> release;
    };

    using Submit = std::function<Operation(std::string, bool)>;
    using CompleteRequest = std::function<Operation(std::string, std::size_t)>;

    struct ServerStatus {
        bool listening = false, connected = false;
        std::string pipe, error;
    };

    // Transport worker handles bytes and plain request snapshots, never engine objects.
    class Server {
    public:
        explicit Server(Submit submit, CompleteRequest complete = {});
        ~Server();
        void Start();
        void Stop();
        ServerStatus Snapshot() const;

    private:
        struct State;
        std::unique_ptr<State> state_;
    };
}
