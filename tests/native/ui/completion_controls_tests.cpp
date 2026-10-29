#include <doctest/doctest.h>
#include <initializer_list>
#include "ui/CompletionControls.h"

using namespace elder_shell;

TEST_CASE("Visible completions own navigation and acceptance") {
    CHECK(CompletionShortcut(CompletionKey::Up, true) == CompletionAction::Previous);
    CHECK(CompletionShortcut(CompletionKey::Down, true) == CompletionAction::Next);
    CHECK(CompletionShortcut(CompletionKey::Tab, true) == CompletionAction::Accept);
    CHECK(CompletionShortcut(CompletionKey::Enter, true) == CompletionAction::Submit);
    CHECK(CompletionShortcut(CompletionKey::Escape, true) == CompletionAction::Dismiss);
}

TEST_CASE("Closed completions preserve editor keys and explicit shortcuts work independently") {
    for (const auto key : {CompletionKey::Up, CompletionKey::Down, CompletionKey::Escape})
        CHECK(CompletionShortcut(key, false) == CompletionAction::None);
    CHECK(CompletionShortcut(CompletionKey::Space, false, true) == CompletionAction::Open);
    CHECK(CompletionShortcut(CompletionKey::Space, true, true) == CompletionAction::Open);
    CHECK(CompletionShortcut(CompletionKey::Up, true, false, true) == CompletionAction::None);
    CHECK(CompletionShortcut(CompletionKey::Down, true, true) == CompletionAction::None);
    CHECK(CompletionShortcut(CompletionKey::Enter, true, true) == CompletionAction::Submit);
    CHECK(CompletionShortcut(CompletionKey::Enter, false, true) == CompletionAction::Submit);
    CHECK(CompletionShortcut(CompletionKey::Enter, false) == CompletionAction::Submit);
    CHECK(CompletionShortcut(CompletionKey::Tab, false) == CompletionAction::Open);
}
