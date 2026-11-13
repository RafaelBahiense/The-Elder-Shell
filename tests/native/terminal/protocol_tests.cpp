#include <doctest/doctest.h>
#include "terminal/Protocol.h"
#include "shell/Requests.h"
#include <string>

using namespace elder_shell;

TEST_CASE("Terminal completion validates UTF-8 byte cursors without normalizing source offsets") {
    const auto request = terminal::DecodeCompletion("complete\n8\nGame.Get");
    CHECK(request.error.empty());
    CHECK(request.cursor == 8);
    CHECK(request.source == "Game.Get");
    CHECK(terminal::DecodeCompletion("complete\n0\n").error.empty());
    CHECK_FALSE(terminal::DecodeCompletion("complete\n-1\nGame.Get").error.empty());
    CHECK_FALSE(terminal::DecodeCompletion("complete\n99\nGame.Get").error.empty());
    CHECK_FALSE(terminal::DecodeCompletion("complete\n2a\nGame.Get").error.empty());
    CHECK_FALSE(terminal::DecodeCompletion("complete\n\nGame.Get").error.empty());
    CHECK_FALSE(terminal::DecodeCompletion("complete\n0").error.empty());
    CHECK_FALSE(terminal::DecodeCompletion("complete\n2\nx\xC3\xA9").error.empty());
    CHECK(terminal::DecodeCompletion("complete\n3\nx\xC3\xA9").error.empty());
}

TEST_CASE("Terminal completions use shared insertion rules and bounded JSON-safe metadata") {
    CompletionResult result;
    result.context.begin = 5;
    result.context.end = 8;
    result.items.push_back({"GetPlayer", "Actor with \"quotes\"\n", 0});
    const auto encoded = terminal::EncodeCompletions("Game.Get", result);
    CHECK(encoded.find("\"begin\":5,\"end\":8") != std::string::npos);
    CHECK(encoded.find("\"text\":\"GetPlayer()\"") != std::string::npos);
    CHECK(encoded.find("\"cursor\":11") != std::string::npos);
    CHECK(encoded.find("\\\"quotes\\\"\\u000a") != std::string::npos);
    CHECK(terminal::JsonString("\t\\\"\r") == "\"\\u0009\\\\\\\"\\u000d\"");
    result.items.clear();
    for (int i = 0; i < 130; ++i)
        result.items.push_back({"Call" + std::to_string(i), "function", 1});
    const auto bounded = terminal::EncodeCompletions("Game.Get", result);
    CHECK(bounded.find("\"total\":130") != std::string::npos);
    CHECK(bounded.find("\"name\":\"Call127\"") != std::string::npos);
    CHECK(bounded.find("\"name\":\"Call128\"") == std::string::npos);
}

TEST_CASE("Terminal input uses shell continuation and rejects malformed or oversized requests") {
    CHECK(terminal::Decode("input\nIf True\n").command.more);
    CHECK(terminal::Decode("execute\nIf True\n").error == "Incomplete Papyrus input.");
    const auto input = terminal::Decode("execute\nvar value = 2\nvalue += 1");
    CHECK(input.error.empty());
    CHECK(input.command.fragment);
    CHECK(terminal::Decode("input\nGame.\\\nGetPlayer()").command.source == "Game.GetPlayer()");
    CHECK_FALSE(terminal::Decode("execute").error.empty());
    CHECK_FALSE(terminal::Decode("unknown\nTrue").error.empty());
    CHECK_FALSE(terminal::Decode("execute\n ").error.empty());
    CHECK_FALSE(terminal::Decode("execute\n" + std::string(terminal::MaximumCommand + 1, 'a')).error.empty());
    CHECK_FALSE(terminal::Decode(std::string("input\nx\0y", 11)).error.empty());
}

TEST_CASE("Terminal status distinguishes submission and session abandonment from successful results") {
    Entry entry{{4, 2}, "True", Status::Submitted, "Waiting"};
    CHECK(terminal::Encode(entry) == "pending\n4\n2\nSubmitted to VM\nWaiting");
    entry.status = Status::Completed;
    entry.result = "first\nsecond";
    CHECK(terminal::Encode(entry) == "result\n4\n2\nCompleted\nfirst\nsecond");
    CHECK_FALSE(terminal::Pending(Status::Rejected));
    CHECK_FALSE(terminal::Pending(Status::Abandoned));
}

TEST_CASE("Retained terminal results survive UI clearing trimming and save invalidation until release") {
    Requests requests;
    const auto retained = requests.Begin("terminal", true);
    REQUIRE(retained);
    requests.Complete(*retained, "result");
    requests.ClearFinished();
    REQUIRE(requests.Find(*retained));
    for (int i = 0; i < 150; ++i) {
        const auto ticket = requests.Begin("ui");
        REQUIRE(ticket);
        requests.Complete(*ticket, "done");
    }
    REQUIRE(requests.Find(*retained));
    CHECK(requests.Find(*retained)->result == "result");
    const auto latent = requests.Begin("latent", true);
    REQUIRE(latent);
    requests.Loaded();
    requests.ClearFinished();
    REQUIRE(requests.Find(*latent));
    CHECK(requests.Find(*latent)->status == Status::Abandoned);
    requests.Complete(*latent, "late");
    CHECK(requests.Find(*latent)->status == Status::Abandoned);
    requests.Release(*latent);
    requests.Release(*retained);
    requests.ClearFinished();
    CHECK_FALSE(requests.Find(*latent));
    CHECK_FALSE(requests.Find(*retained));
}
