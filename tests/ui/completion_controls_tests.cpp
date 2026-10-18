#include <doctest/doctest.h>
#include <initializer_list>
#include "ui/CompletionControls.h"

using namespace elder_shell;

TEST_CASE("Visible completions own navigation and acceptance in both input modes") {
    for (const bool multiline : {false, true}) {
        CHECK(CompletionShortcut(CompletionKey::Up, true, multiline) == CompletionAction::Previous);
        CHECK(CompletionShortcut(CompletionKey::Down, true, multiline) == CompletionAction::Next);
        CHECK(CompletionShortcut(CompletionKey::Tab, true, multiline) == CompletionAction::Accept);
        CHECK(CompletionShortcut(CompletionKey::Enter, true, multiline) == CompletionAction::Accept);
        CHECK(CompletionShortcut(CompletionKey::Escape, true, multiline) == CompletionAction::Dismiss);
    }
}

TEST_CASE("Closed completions preserve editor keys and explicit shortcuts work independently") {
    for (const bool multiline : {false, true}) {
        for (const auto key :
             {CompletionKey::Up, CompletionKey::Down, CompletionKey::Tab, CompletionKey::Enter, CompletionKey::Escape})
            CHECK(CompletionShortcut(key, false, multiline) == CompletionAction::None);
        CHECK(CompletionShortcut(CompletionKey::Space, false, multiline, true) == CompletionAction::Open);
        CHECK(CompletionShortcut(CompletionKey::Space, true, multiline, true) == CompletionAction::Open);
        CHECK(CompletionShortcut(CompletionKey::Up, true, multiline, false, true) == CompletionAction::None);
        CHECK(CompletionShortcut(CompletionKey::Down, true, multiline, true) == CompletionAction::None);
    }
    CHECK(CompletionShortcut(CompletionKey::Enter, true, true, true) == CompletionAction::Submit);
    CHECK(CompletionShortcut(CompletionKey::Enter, false, true, true) == CompletionAction::Submit);
}
