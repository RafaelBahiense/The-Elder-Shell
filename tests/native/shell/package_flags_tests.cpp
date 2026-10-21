#include <doctest/doctest.h>
#include "shell/PackageFlags.h"

using namespace elder_shell::package_flags;

TEST_CASE("Package interrupt edits preserve unrelated and unknown bits") {
    Override change{0x1001};
    auto flags = change.Set(change.original, Find("RandomConversations"), true);
    CHECK(flags == 0x1003);
    flags = change.Set(flags, Find("AllowIdleChatter"), true);
    CHECK(flags == 0x1083);
    // Another mod changes unrelated bits after our edit.
    flags = static_cast<std::uint16_t>((flags & ~0x1000) | 0x2000);
    CHECK(change.Restore(flags) == 0x2001);
}

TEST_CASE("Repeated package edits retain the original baseline for restoration") {
    Override change{0x0082};
    auto flags = change.Set(change.original, Find("RandomConversations"), false);
    flags = change.Set(flags, Find("AllowIdleChatter"), false);
    flags = change.Set(flags, Find("RandomConversations"), true);
    CHECK(flags == 0x0002);
    CHECK(change.Restore(flags) == change.original);
}

TEST_CASE("An unchanged flag is not claimed for restoration") {
    Override change{0x0002};
    const auto flags = change.Set(change.original, Find("RandomConversations"), true);
    CHECK(flags == change.original);
    CHECK(change.changed == 0);
    CHECK(change.Restore(0) == 0);
}

TEST_CASE("Package flags accept case-insensitive names and reject unknown names") {
    CHECK(Find("randomconversations") == Find("RandomConversations"));
    CHECK(Find("AllowIdleChatter") == 0x80);
    CHECK(Find("2") == 0);
    CHECK(Find("") == 0);
    CHECK(Find("UnknownFlag") == 0);
}
