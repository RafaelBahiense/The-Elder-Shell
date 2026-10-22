#include <doctest/doctest.h>
#include "shell/FactionCleanup.h"
#include <array>

using namespace elder_shell::faction_cleanup;

TEST_CASE("Faction cleanup includes negative stored ranks and protects authored memberships") {
    for (auto rank : std::array<std::int8_t, 5>{-128, -20, -1, 0, 127}) {
        CHECK(Removable({1, rank, false, true}));
        CHECK_FALSE(Removable({1, rank, true, true}));
        CHECK_FALSE(Removable({1, rank, true, false}));
        CHECK_FALSE(Removable({1, rank, false, false}));
    }
}

TEST_CASE("Cleanup rejects active registrations, plugin aliases, scenes and the player") {
    CHECK(Error(false, false, false, false, 10).empty());
    CHECK_FALSE(Error(true, false, false, false, 10).empty());
    CHECK_FALSE(Error(false, true, false, false, 10).empty());
    CHECK_FALSE(Error(false, false, true, false, 10).empty());
    CHECK_FALSE(Error(false, false, false, true, 10).empty());
    CHECK_FALSE(Error(false, false, false, false, MaximumEntries + 1).empty());
}

TEST_CASE("Cleanup retains authored faction ranks and removes every runtime-only data entry") {
    const std::vector<Entry> before{
        {1, 100, false, true}, {2, -99, false, true}, {3, -1, false, true}, {4, 12, true, false}, {5, 7, true, true}};
    const std::vector<Entry> expected{{4, 12, true, false}, {5, 7, true, true}};
    CHECK(Retained(before) == expected);
    CHECK(Retained(expected) == expected);
    CHECK(Retained({}).empty());
}

TEST_CASE("Cleanup and undo require the exact captured faction data and registration guard") {
    const std::vector<Entry> before{{1, -25, false, true}, {2, 5, true, true}};
    CHECK(Matches(before, before, 9, 9));
    CHECK_FALSE(Matches(before, before, 9, 10));
    auto changed = before;
    changed[0].rank = -24;
    CHECK_FALSE(Matches(before, changed, 9, 9));
    changed = before;
    changed[0].authored = true;
    CHECK_FALSE(Matches(before, changed, 9, 9));
    changed = before;
    changed.push_back({3, 0, false, true});
    CHECK_FALSE(Matches(before, changed, 9, 9));
    changed = before;
    changed.erase(changed.begin());
    CHECK_FALSE(Matches(before, changed, 9, 9));
}
