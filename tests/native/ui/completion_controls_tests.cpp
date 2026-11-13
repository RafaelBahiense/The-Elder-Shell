#include <doctest/doctest.h>
#include <initializer_list>
#include "ui/CompletionControls.h"

using namespace elder_shell;

TEST_CASE("Completion reserves a stable panel while retaining a usable editor in short windows") {
    CHECK(CompletionPanelHeight(500, 20, 8, 4) == doctest::Approx(176));
    CHECK(CompletionPanelHeight(200, 20, 8, 4) == doctest::Approx(136));
    CHECK(CompletionPanelHeight(150, 20, 8, 4) == 0);
    CHECK(CompletionPanelHeight(0, 20, 8, 4) == 0);
}

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
