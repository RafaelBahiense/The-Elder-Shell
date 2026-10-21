#include <doctest/doctest.h>
#include "runtime/ConsoleRuntime.h"

using namespace elder_shell;

TEST_CASE("Console compilation uses the moved entry point on modern Anniversary runtimes") {
    CHECK(ConsoleCompileAndRunID(false, {1, 5, 97}) == 21416);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 640}) == 21890);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1129}) == 21890);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1130}) == 441582);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1170}) == 441582);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1179}) == 441582);
}
