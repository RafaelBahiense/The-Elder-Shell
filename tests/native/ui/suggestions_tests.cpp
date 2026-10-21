#include <doctest/doctest.h>
#include "ui/Suggestions.h"

using namespace elder_shell;

TEST_CASE("Suggestion navigation wraps and reveals matches beyond the visible rows") {
    Suggestions suggestions;
    suggestions.Update("actor.", 12);
    CHECK(suggestions.Selected() == 0);
    REQUIRE(suggestions.Move(false));
    CHECK(suggestions.Selected() == 11);
    CHECK(suggestions.TakeRevealRequest());
    REQUIRE(suggestions.Move(true));
    CHECK(suggestions.Selected() == 0);
    CHECK(suggestions.TakeRevealRequest());
    for (int i = 0; i < 6; ++i)
        suggestions.Move(true);
    CHECK(suggestions.Selected() == 6);
    CHECK(suggestions.TakeRevealRequest());
}

TEST_CASE("Edits reset suggestions while metadata refreshes preserve a valid selection") {
    Suggestions suggestions;
    suggestions.Update("actor.", 12);
    suggestions.Move(false);
    suggestions.Update("actor.", 12);
    CHECK(suggestions.Selected() == 11);
    suggestions.Update("actor.", 3);
    CHECK(suggestions.Selected() == 2);
    CHECK(suggestions.TakeRevealRequest());
    suggestions.Update("actor.Get", 2);
    CHECK(suggestions.Selected() == 0);
    CHECK(suggestions.TakeRevealRequest());
    suggestions.Update("actor.Get", 0);
    CHECK_FALSE(suggestions.Move(true));
    CHECK_FALSE(suggestions.Move(false));
}

TEST_CASE("Mouse scrolling is not overridden by unchanged rendering or metadata refreshes") {
    Suggestions suggestions;
    suggestions.Update("actor.", 12);
    REQUIRE(suggestions.TakeRevealRequest());
    suggestions.Update("actor.", 12);
    CHECK_FALSE(suggestions.TakeRevealRequest());
    CHECK_FALSE(suggestions.TakeRevealRequest());
    suggestions.Move(true);
    CHECK(suggestions.TakeRevealRequest());
    CHECK_FALSE(suggestions.TakeRevealRequest());
    suggestions.Update("actor.Get", 3);
    CHECK(suggestions.TakeRevealRequest());
}

TEST_CASE("Dismissal survives metadata refreshes until editing or explicitly reopening") {
    Suggestions suggestions;
    suggestions.Update("actor.", 12);
    REQUIRE(suggestions.Visible());
    suggestions.Dismiss();
    suggestions.Update("actor.", 12);
    CHECK_FALSE(suggestions.Visible());
    CHECK_FALSE(suggestions.Move(true));
    suggestions.Open();
    CHECK(suggestions.Visible());
    CHECK(suggestions.ExplicitFor("actor."));
    suggestions.Dismiss();
    suggestions.Update("actor.Get", 3);
    CHECK(suggestions.Visible());
    CHECK_FALSE(suggestions.ExplicitFor("actor.Get"));
}
