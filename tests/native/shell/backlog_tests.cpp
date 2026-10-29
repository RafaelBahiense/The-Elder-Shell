#include "shell/Backlog.h"
#include <doctest/doctest.h>

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
