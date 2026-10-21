#include <doctest/doctest.h>
#include "ui/CompletionEscape.h"

using namespace elder_shell;

TEST_CASE("Completion dismissal consumes the entire Escape press before menu handling") {
    CompletionEscape escape;
    escape.Publish(true);
    CHECK(escape.Consume(true, false, true));
    CHECK(escape.TakeDismissal());
    CHECK_FALSE(escape.TakeDismissal());
    escape.Publish(false);
    CHECK(escape.Consume(false, false, true));
    CHECK(escape.Consume(false, true, true));
    CHECK_FALSE(escape.Consume(true, false, true));
    CHECK_FALSE(escape.Consume(false, true, true));
}

TEST_CASE("Hidden suggestions and closed menus preserve normal Escape handling") {
    CompletionEscape escape;
    CHECK_FALSE(escape.Consume(true, false, true));
    escape.Publish(true);
    CHECK_FALSE(escape.Consume(true, false, false));
    CHECK_FALSE(escape.TakeDismissal());
    escape.Publish(false);
    CHECK_FALSE(escape.Consume(true, false, true));
}
