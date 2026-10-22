#include <doctest/doctest.h>
#include "shell/PluginActorCleanup.h"

using namespace elder_shell::plugin_cleanup;

namespace {
    const std::string Digest(64, 'a');

    std::string Index(std::string_view records = "script\tExampleController\n") {
        return "ElderShellScriptIndex\t1\nplugin\tExample.esm\nsha256\t" + Digest + "\n" + std::string(records);
    }
}

TEST_CASE("Plugin script attribution requires exact class names and a matching source digest") {
    const auto parsed = ParseIndex(Index(), "example.ESM", Digest);
    REQUIRE(parsed.error.empty());
    CHECK(parsed.Contains("EXAMPLECONTROLLER"));
    CHECK_FALSE(parsed.Contains("ExampleControllerExtra"));
    CHECK_FALSE(parsed.Contains("Example"));
    CHECK_FALSE(ParseIndex(Index(), "Other.esm", Digest).error.empty());
    CHECK_FALSE(ParseIndex(Index(), "Example.esm", std::string(64, 'b')).error.empty());
}

TEST_CASE("Malformed or excessive script indexes never authorize detachment") {
    for (const auto& text :
         {std::string{}, std::string("ElderShellScriptIndex\t1\n"), Index("other\tExampleController\n"),
          Index("script\t../ExampleController\n"), Index("script\t\n"), Index("script\tExample Controller\n")}) {
        const auto parsed = ParseIndex(text, "Example.esm", Digest);
        CHECK_FALSE(parsed.error.empty());
        CHECK_FALSE(parsed.Contains("ExampleController"));
    }
    CHECK_FALSE(ParseIndex(std::string(4 * 1024 * 1024 + 1, 'x'), "Example.esm", Digest).error.empty());
}

TEST_CASE("Script indexes accept CRLF and deduplicate case variants") {
    auto text = Index("script\tExampleController\nscript\texamplecontroller\n");
    std::string crlf;
    for (auto ch : text) {
        if (ch == '\n')
            crlf += '\r';
        crlf += ch;
    }
    const auto parsed = ParseIndex(crlf, "Example.esm", Digest);
    CHECK(parsed.error.empty());
    CHECK(parsed.names.size() == 1);
}

TEST_CASE("Full cleanup orders producers before effects and actor data") {
    CHECK(Categories.front() == Category::Quests);
    CHECK(Categories[1] == Category::Aliases);
    CHECK(Categories[2] == Category::Scripts);
    CHECK(Categories.back() == Category::Factions);
}
