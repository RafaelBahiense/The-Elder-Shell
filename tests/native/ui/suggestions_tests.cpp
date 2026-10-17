#include <doctest/doctest.h>
#include "ui/Suggestions.h"

using namespace elder_shell;

TEST_CASE("Suggestion navigation wraps and reveals matches beyond the visible rows") {
    Suggestions suggestions;
    suggestions.Update("actor.", 12);
    CHECK(suggestions.Selected() == 0);
    REQUIRE(suggestions.Move(false));
    CHECK(suggestions.Selected() == 11);
    CHECK(suggestions.FirstVisible() == 7);
    REQUIRE(suggestions.Move(true));
    CHECK(suggestions.Selected() == 0);
    CHECK(suggestions.FirstVisible() == 0);
    for (int i = 0; i < 6; ++i)
        suggestions.Move(true);
    CHECK(suggestions.Selected() == 6);
    CHECK(suggestions.FirstVisible() == 2);
}

TEST_CASE("Edits reset suggestions while metadata refreshes preserve a valid selection") {
    Suggestions suggestions;
    suggestions.Update("actor.", 12);
    suggestions.Move(false);
    suggestions.Update("actor.", 12);
    CHECK(suggestions.Selected() == 11);
    suggestions.Update("actor.", 3);
    CHECK(suggestions.Selected() == 2);
    CHECK(suggestions.FirstVisible() <= suggestions.Selected());
    suggestions.Update("actor.Get", 2);
    CHECK(suggestions.Selected() == 0);
    CHECK(suggestions.FirstVisible() == 0);
    suggestions.Update("actor.Get", 0);
    CHECK_FALSE(suggestions.Move(true));
    CHECK_FALSE(suggestions.Move(false));
}
