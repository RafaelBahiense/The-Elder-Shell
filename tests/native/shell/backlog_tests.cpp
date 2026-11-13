#include "shell/Backlog.h"
#include <doctest/doctest.h>
#include <limits>
#include <sstream>

using namespace elder_shell;

TEST_CASE("Backlog groups rank sample counts with stable ties") {
    const auto rows = BacklogRows({{"B.Tick", 5}, {"A.Tick", 5}, {"C.Load", 12}});
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].name == "C.Load");
    CHECK(rows[0].count == 12);
    CHECK(rows[1].name == "A.Tick");
    CHECK(rows[2].name == "B.Tick");
}

TEST_CASE("Backlog group display is bounded without changing sample counts") {
    std::map<std::string, std::uint32_t> counts;
    for (std::uint32_t i = 0; i < 100; ++i)
        counts[std::to_string(i)] = i + 1;
    const auto rows = BacklogRows(counts);
    REQUIRE(rows.size() == 32);
    CHECK(rows.front().count == 100);
    CHECK(rows.back().count == 69);
    CHECK(counts.size() == 100);
    CHECK(BacklogRows({}).empty());
    const auto full = BacklogRows(counts, counts.size());
    CHECK(full.size() == 100);
    std::uint32_t total = 0;
    for (const auto& row : full)
        total += row.count;
    CHECK(total == 5050);
}

TEST_CASE("Backlog states distinguish native waits from memory and stack dependencies") {
    CHECK(BacklogStateName(2) == "Waiting on memory");
    CHECK(BacklogStateName(3) == "Waiting on latent function");
    CHECK(BacklogStateName(4) != BacklogStateName(3));
    CHECK(BacklogStateName(99) == "Unknown state 99");
}

TEST_CASE("Uncaptured backlog does not invent zero counts and pending reads identify older data") {
    PapyrusBacklog snapshot;
    const auto empty = DescribeBacklog(snapshot);
    CHECK(empty.find("Refresh to capture") != std::string::npos);
    CHECK(empty.find("Registered stacks:") == std::string::npos);
    snapshot.pending = snapshot.captured = true;
    snapshot.registeredStacks = 12;
    const auto pending = DescribeBacklog(snapshot);
    CHECK(pending.find("previous snapshot") != std::string::npos);
    CHECK(pending.find("Registered stacks: 12") != std::string::npos);
}

TEST_CASE("Backlog reports preserve counts coverage and receiver names without summing overlapping queues") {
    PapyrusBacklog snapshot;
    snapshot.captured = true;
    snapshot.full = true;
    snapshot.capturedAt = std::chrono::steady_clock::now();
    snapshot.registeredStacks = 10;
    snapshot.sampledStacks = 10;
    snapshot.latentReturns = 2;
    snapshot.suspendedOverflow = 174308;
    snapshot.functionOverflow = 10080;
    snapshot.sampledCalls = 184388;
    snapshot.calls = {{"ExampleReceiver.OnLocationChange", 174308}, {"ExampleInventory.OnItemRemoved", 10080}};
    snapshot.states = {{"Waiting on latent function", 2}, {"Running / scheduled", 8}};
    snapshot.reportPath = "Papyrus-report.txt";
    const auto text = DescribeBacklog(snapshot, snapshot.capturedAt + std::chrono::seconds(7));
    CHECK(text.find("Captured 7 seconds ago") != std::string::npos);
    CHECK(text.find("Suspended overflow entries: 174308") != std::string::npos);
    CHECK(text.find("Function-message overflow entries: 10080") != std::string::npos);
    CHECK(text.find("Overflow coverage complete: 1") != std::string::npos);
    CHECK(text.find("Stack coverage complete: 1") != std::string::npos);
    CHECK(text.find("174308\tExampleReceiver.OnLocationChange") != std::string::npos);
    CHECK(text.find("Report: Papyrus-report.txt") != std::string::npos);
    CHECK(text.find("Counts overlap; do not add them") != std::string::npos);
    CHECK(text.find("Total:") == std::string::npos);
    snapshot.sampledCalls = 3072;
    snapshot.sampledStacks = 3;
    const auto partial = DescribeBacklog(snapshot);
    CHECK(partial.find("Overflow coverage complete: 0") != std::string::npos);
    CHECK(partial.find("Stack coverage complete: 0") != std::string::npos);
}

TEST_CASE("Shell backlog view is bounded while the exported report preserves every retained group") {
    PapyrusBacklog snapshot;
    snapshot.captured = snapshot.full = true;
    for (int i = 0; i < 50; ++i)
        snapshot.calls.push_back({"Receiver.Call_" + std::to_string(i), 1});
    const auto bounded = DescribeBacklog(snapshot);
    CHECK(bounded.find("Receiver.Call_31") != std::string::npos);
    CHECK(bounded.find("Receiver.Call_32") == std::string::npos);
    CHECK(bounded.find("18 additional groups omitted") != std::string::npos);
    std::ostringstream file;
    WriteBacklogReport(file, snapshot, std::numeric_limits<std::size_t>::max());
    CHECK(file.str().find("Receiver.Call_49") != std::string::npos);
    CHECK(file.str().find("additional groups omitted") == std::string::npos);
}
