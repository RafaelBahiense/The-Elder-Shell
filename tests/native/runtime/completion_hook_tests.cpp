#include <doctest/doctest.h>
#include "runtime/CompletionHook.h"
#include <array>

using namespace elder_shell::completion_hook;

TEST_CASE("Completion hook requires a relative call and resolves signed displacements") {
    CHECK(CallTarget(0x1000, std::array<std::uint8_t, 5>{0xE8, 0x20, 0, 0, 0}) == 0x1025);
    CHECK(CallTarget(0x1000, std::array<std::uint8_t, 5>{0xE8, 0xF0, 0xFF, 0xFF, 0xFF}) == 0x0FF5);
    CHECK_FALSE(CallTarget(0x1000, std::array<std::uint8_t, 5>{0xE9, 0, 0, 0, 0}));
    CHECK_FALSE(CallTarget(0x1000, std::array<std::uint8_t, 2>{0xFF, 0x15}));
    CHECK_FALSE(CallTarget(0, std::array<std::uint8_t, 5>{0xE8, 0, 0, 0, 0x80}));
    CHECK_FALSE(
        CallTarget(std::numeric_limits<std::uintptr_t>::max() - 3, std::array<std::uint8_t, 5>{0xE8, 0, 0, 0, 0}));
    CHECK_FALSE(
        CallTarget(std::numeric_limits<std::uintptr_t>::max() - 5, std::array<std::uint8_t, 5>{0xE8, 1, 0, 0, 0}));
}

TEST_CASE("Completion hook recognizes only the supported CommonLib trampoline layout") {
    std::array<std::uint8_t, 14> bytes{0xFF, 0x25, 0, 0, 0, 0, 0x78, 0x56, 0x34, 0x12};
    CHECK(TrampolineTarget(bytes) == 0x12345678);
    CHECK_FALSE(TrampolineTarget(std::span{bytes}.first(13)));
    bytes[2] = 1;
    CHECK_FALSE(TrampolineTarget(bytes));
    bytes[2] = 0;
    bytes[1] = 0x15;
    CHECK_FALSE(TrampolineTarget(bytes));
}
