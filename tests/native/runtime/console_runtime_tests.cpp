#include <doctest/doctest.h>
#include "runtime/ConsoleRuntime.h"

using namespace elder_shell;

TEST_CASE("Console selection follows the handle relocation at Anniversary 1.6.1130") {
    CHECK(ConsoleSelectedReferenceID(false, {1, 5, 97}) == 519394);
    CHECK(ConsoleSelectedReferenceID(true, {1, 6, 640}) == 405935);
    CHECK(ConsoleSelectedReferenceID(true, {1, 6, 1129}) == 405935);
    CHECK(ConsoleSelectedReferenceID(true, {1, 6, 1130}) == 504099);
    CHECK(ConsoleSelectedReferenceID(true, {1, 6, 1170}) == 504099);
    CHECK(ConsoleSelectedReferenceID(true, {1, 6, 1179}) == 504099);
}

TEST_CASE("Console compilation uses the moved entry point on modern Anniversary runtimes") {
    CHECK(ConsoleCompileAndRunID(false, {1, 5, 97}) == 21416);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 640}) == 21890);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1129}) == 21890);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1130}) == 441582);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1170}) == 441582);
    CHECK(ConsoleCompileAndRunID(true, {1, 6, 1179}) == 441582);
}
