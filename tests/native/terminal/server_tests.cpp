#ifdef _WIN32
    #include <doctest/doctest.h>
    #include "terminal/Server.h"
    #include "terminal/Protocol.h"
    #include <Windows.h>
    #include <atomic>
    #include <chrono>
    #include <cstdint>
    #include <functional>
    #include <memory>
    #include <stdexcept>
    #include <string>
    #include <thread>

using namespace elder_shell;

namespace {
    bool Wait(const std::function<bool()>& condition) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (std::chrono::steady_clock::now() < deadline) {
            if (condition())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    class Client {
    public:
        explicit Client(terminal::Server& server) {
            if (!Wait([&] { return server.Snapshot().listening || !server.Snapshot().error.empty(); }))
                throw std::runtime_error("Server did not start.");
            const auto status = server.Snapshot();
            const auto path = "\\\\.\\pipe\\" + status.pipe;
            if (!Wait([&] {
                    handle =
                        CreateFileA(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                    return handle != INVALID_HANDLE_VALUE;
                }))
                throw std::runtime_error("Pipe connection failed: " + status.error);
            DWORD mode = PIPE_READMODE_BYTE | PIPE_NOWAIT;
            if (!SetNamedPipeHandleState(handle, &mode, nullptr, nullptr)) {
                CloseHandle(handle);
                throw std::runtime_error("Client mode failed.");
            }
        }

        ~Client() {
            CloseHandle(handle);
        }

        void Write(std::string value) {
            const auto size = static_cast<std::uint32_t>(value.size());
            DWORD count = 0;
            REQUIRE(WriteFile(handle, &size, 4, &count, nullptr));
            REQUIRE(count == 4);
            REQUIRE(WriteFile(handle, value.data(), size, &count, nullptr));
            REQUIRE(count == size);
        }

        std::string Read() {
            std::uint32_t size = 0;
            ReadBytes(&size, 4);
            if (!size || size > terminal::MaximumFrame)
                throw std::runtime_error("Invalid frame.");
            std::string result(size, '\0');
            ReadBytes(result.data(), size);
            return result;
        }

    private:
        void ReadBytes(void* output, DWORD size) {
            auto* cursor = static_cast<std::uint8_t*>(output);
            while (size) {
                DWORD count = 0;
                if (!Wait([&] {
                        if (ReadFile(handle, cursor, size, &count, nullptr))
                            return count > 0;
                        if (GetLastError() != ERROR_NO_DATA)
                            throw std::runtime_error("Pipe disconnected.");
                        return false;
                    }))
                    throw std::runtime_error("Read timed out.");
                cursor += count;
                size -= count;
            }
        }

        HANDLE handle = INVALID_HANDLE_VALUE;
    };
}

TEST_CASE("Local terminal connects submits multiline source and observes completion before reconnecting") {
    std::atomic_uint calls = 0, releases = 0;
    terminal::Server server([&](std::string source, bool fragment) {
        CHECK(source == "var value = 1\nvalue += 1");
        CHECK(fragment);
        ++calls;
        auto polls = std::make_shared<unsigned>(0);
        return terminal::Operation{
            [polls] { return Entry{{5, 3}, {}, ++*polls == 1 ? Status::Compiling : Status::Completed, "2"}; },
            [&] { ++releases; }};
    });
    server.Start();
    {
        Client client(server);
        CHECK(client.Read().starts_with("hello\n1\nTheElderShell-"));
        client.Write("input\nIf True");
        CHECK(client.Read() == "continue");
        CHECK(calls == 0);
        client.Write("execute\nvar value = 1\nvalue += 1");
        CHECK(client.Read() == "pending\n5\n3\nCompiling\n2");
        CHECK(client.Read() == "result\n5\n3\nCompleted\n2");
        REQUIRE(Wait([&] { return releases == 1; }));
    }
    {
        Client client(server);
        CHECK(client.Read().starts_with("hello\n1\n"));
        client.Write("ping");
        CHECK(client.Read() == "pong");
    }
    server.Stop();
    CHECK(calls == 1);
    CHECK_FALSE(server.Snapshot().listening);
}

TEST_CASE("Stopping a terminal during latent work releases its retained request without replay") {
    std::atomic_uint calls = 0, releases = 0;
    terminal::Server server([&](std::string, bool) {
        ++calls;
        return terminal::Operation{[] { return Entry{{1, 1}, {}, Status::Submitted, "latent"}; }, [&] { ++releases; }};
    });
    server.Start();
    Client client(server);
    client.Read();
    client.Write("execute\nTrue");
    CHECK(client.Read() == "pending\n1\n1\nSubmitted to VM\nlatent");
    const auto before = std::chrono::steady_clock::now();
    server.Stop();
    CHECK(std::chrono::steady_clock::now() - before < std::chrono::seconds(2));
    CHECK(calls == 1);
    CHECK(releases == 1);
}

TEST_CASE("Session abandonment is returned as failure and malformed commands never reach the backend") {
    std::atomic_uint calls = 0;
    terminal::Server server([&](std::string, bool) {
        ++calls;
        return terminal::Operation{[] { return Entry{{8, 9}, {}, Status::Abandoned, "Session changed"}; }, [] {}};
    });
    server.Start();
    Client client(server);
    client.Read();
    client.Write("execute\nIf True");
    CHECK(client.Read().starts_with("error\n"));
    CHECK(calls == 0);
    client.Write("execute\nTrue");
    CHECK(client.Read() == "result\n8\n9\nAbandoned after load\nSession changed");
    CHECK(calls == 1);
}

TEST_CASE("A disconnected latent client releases retention and permits a fresh connection without replay") {
    std::atomic_uint calls = 0, releases = 0;
    terminal::Server server([&](std::string, bool) {
        ++calls;
        return terminal::Operation{[] { return Entry{{1, 1}, {}, Status::Submitted, "latent"}; }, [&] { ++releases; }};
    });
    server.Start();
    {
        Client client(server);
        client.Read();
        client.Write("execute\nTrue");
        CHECK(client.Read().starts_with("pending\n"));
    }
    REQUIRE(Wait([&] { return releases == 1; }));
    Client next(server);
    CHECK(next.Read().starts_with("hello\n1\n"));
    next.Write("ping");
    CHECK(next.Read() == "pong");
    CHECK(calls == 1);
}

TEST_CASE("A listening terminal can stop without a client") {
    terminal::Server server([](std::string, bool) { return terminal::Operation{}; });
    server.Start();
    REQUIRE(Wait([&] { return server.Snapshot().listening; }));
    const auto start = std::chrono::steady_clock::now();
    server.Stop();
    CHECK(std::chrono::steady_clock::now() - start < std::chrono::seconds(2));
    CHECK_FALSE(server.Snapshot().listening);
}

TEST_CASE("Completion requests are routed separately and never submit Papyrus execution") {
    std::atomic_uint executions = 0, completions = 0, releases = 0;
    terminal::Server server(
        [&](std::string, bool) {
            ++executions;
            return terminal::Operation{};
        },
        [&](std::string source, std::size_t cursor) {
            CHECK(source == "Game.Get");
            CHECK(cursor == 8);
            ++completions;
            return terminal::Operation{[] { return Entry{{}, {}, Status::Completed, "{\"items\":[]}"}; },
                                       [&] { ++releases; }};
        });
    server.Start();
    Client client(server);
    client.Read();
    client.Write("complete\n99\nGame.Get");
    CHECK(client.Read().starts_with("error\n"));
    CHECK(completions == 0);
    client.Write("complete\n8\nGame.Get");
    CHECK(client.Read() == "result\n0\n0\nCompleted\n{\"items\":[]}");
    REQUIRE(Wait([&] { return releases == 1; }));
    CHECK(completions == 1);
    CHECK(executions == 0);
    client.Write("ping");
    CHECK(client.Read() == "pong");
}
#endif
