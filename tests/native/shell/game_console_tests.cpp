#include <doctest/doctest.h>
#include "shell/Command.h"
#include "shell/ConsoleOutput.h"

using namespace elder_shell;

TEST_CASE("Console output excludes older text even when the engine buffer rolls over") {
    CHECK(NewConsoleOutput("old\n", "old\nHealth: 50\n") == "Health: 50\n");
    CHECK(NewConsoleOutput("discarded\nretained\n", "retained\nnew\n") == "new\n");
    CHECK(NewConsoleOutput("old", "replaced") == "replaced");
    CHECK(NewConsoleOutput("unchanged", "unchanged").empty());
    CHECK(NewConsoleOutput("old", "").empty());
    CHECK(NewConsoleOutput("", "first") == "first");
    const auto large = NewConsoleOutput("", std::string(40000, 'x'));
    CHECK(large.starts_with(std::string(32768, 'x')));
    CHECK(large.ends_with("[Console output truncated]"));
}
