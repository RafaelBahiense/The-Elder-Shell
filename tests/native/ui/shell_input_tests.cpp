#include <doctest/doctest.h>
#include "ui/ShellInput.h"

using namespace elder_shell;

TEST_CASE("Shell continuation joins escaped lines without changing Papyrus string escapes") {
    CHECK(ReadShellInput("player.\\\nGetLevel()").source == "player.GetLevel()");
    CHECK(ReadShellInput("player.\\\r\nGetLevel()").source == "player.GetLevel()");
    CHECK(ReadShellInput("var path = \"C:\\\\Temp\"").source == "var path = \"C:\\\\Temp\"");
    CHECK(ReadShellInput("player.GetLevel() \\").more);
    CHECK(ReadShellInput("player.\\\n").more);
    CHECK_FALSE(ReadShellInput("player.GetLevel() ; comment \\").more);
    CHECK_FALSE(ReadShellInput("\"\\\\\"").more);
}

TEST_CASE("Shell collects blocks and delimited expressions without interpreting commands") {
    CHECK(ReadShellInput("If True\n    player.GetLevel()").more);
    CHECK(ReadShellInput("While True\nIf False\nEndIf").more);
    const auto block = ReadShellInput("If True\nplayer.GetLevel()\nEndIf");
    CHECK_FALSE(block.more);
    CHECK(block.fragment);
    const auto call = ReadShellInput("player.GetActorValue(\n; a comment\n\"Health\")");
    CHECK_FALSE(call.more);
    CHECK_FALSE(call.fragment);
    CHECK(call.source.find("; a comment") == std::string::npos);
    CHECK(call.source.find('\n') == std::string::npos);
    CHECK(ReadShellInput("player.GetActorValue(").more);
    CHECK(ReadShellInput("var values = new Int[").more);
    CHECK(ReadShellInput("\"unfinished").more);
    CHECK_FALSE(ReadShellInput("If True\nEndWhile").more);
    CHECK_FALSE(ReadShellInput("player.GetLevel())").more);
    CHECK(ReadShellInput("var x = 1\nvar y = 2").fragment);
    CHECK_FALSE(ReadShellInput("player.GetLevel()\n").fragment);
    CHECK(ReadShellInput("var x = 1 ;/ comment\n/; If True").more);
}

TEST_CASE("Shell transcript remains immutable across edits and overlapping selections") {
    ShellDocument document;
    ShellSelection selection;
    document.Rebase("> old\nresult\n\n> ", selection);
    document.SetDraft("draft", selection);
    const auto prefix = document.Text().substr(0, document.Boundary());
    CHECK(document.Edit(prefix + "new draft", selection));
    CHECK(document.Draft() == "new draft");
    CHECK(document.Edit("replacement", selection));
    CHECK(document.Text() == prefix + "replacement");
    CHECK(document.Edit("", selection));
    CHECK(document.Text() == prefix);
    document.SetDraft("keep", selection);
    auto changed = document.Text();
    changed.replace(2, 3, "edit");
    CHECK(document.Edit(changed, selection));
    CHECK(document.Text() == prefix + "keepedit");
}

TEST_CASE("Async output preserves the draft and its cursor while selection spans older results") {
    ShellDocument document;
    ShellSelection selection;
    document.Rebase("> old\n\n> ", selection);
    document.SetDraft("player.Get", selection);
    const auto relative = selection.cursor - document.Boundary();
    document.Rebase("> old\n42\n\n> ", selection);
    CHECK(document.Draft() == "player.Get");
    CHECK(selection.cursor - document.Boundary() == relative);
    selection = {2, 0, 2};
    document.Rebase("> old\n42\n\n> other\n7\n\n> ", selection);
    CHECK(selection.start == 0);
    CHECK(selection.end == 2);
    CHECK_FALSE(document.Edit(document.Text() + std::string(4097, 'x'), selection));
    CHECK(document.Draft() == "player.Get");
}

TEST_CASE("Selections after a completed result track the same text") {
    ShellDocument document;
    ShellSelection selection;
    document.Rebase("> first\n\n> second\n42\n\n> ", selection);
    const auto start = document.Text().find("second");
    selection = {start + 6, start, start + 6};
    document.Rebase("> first\n1000\n\n> second\n42\n\n> ", selection);
    CHECK(document.Text().substr(selection.start, selection.end - selection.start) == "second");
}
