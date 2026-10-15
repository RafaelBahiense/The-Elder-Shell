#include <doctest/doctest.h>
#include "ui/History.h"

using namespace papyrus_console;

namespace {
    TEST_CASE("Command history restores the draft at the end and stops at its boundaries") {
        History history;
        CHECK_FALSE(history.Previous("draft"));
        CHECK_FALSE(history.Next());
        history.Add("first");
        history.Add("second");
        CHECK(history.Previous("unfinished") == "second");
        CHECK(history.Previous("second") == "first");
        CHECK_FALSE(history.Previous("first"));
        CHECK(history.Next() == "second");
        CHECK(history.Next() == "unfinished");
        CHECK_FALSE(history.Next());
    }

    TEST_CASE("History bounds entries, skips consecutive duplicates and resets after edits") {
        History history;
        history.Add("same");
        history.Add("same");
        CHECK(history.Previous("") == "same");
        CHECK_FALSE(history.Previous("same"));
        history.ResetNavigation();
        CHECK(history.Previous("edited") == "same");
        CHECK(history.Next() == "edited");
        for (int i = 0; i < 130; ++i)
            history.Add(std::to_string(i));
        int count = 0;
        while (history.Previous(""))
            ++count;
        CHECK(count == 128);
    }

}
