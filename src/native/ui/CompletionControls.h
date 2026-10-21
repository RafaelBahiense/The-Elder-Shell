#pragma once

namespace elder_shell {
    enum class CompletionKey { Up, Down, Enter, Tab, Escape, Space };
    enum class CompletionAction { None, Previous, Next, Accept, Dismiss, Open, Submit };

    constexpr CompletionAction CompletionShortcut(CompletionKey key, bool visible, bool multiline, bool control = false,
                                                  bool shift = false, bool alt = false) {
        if (shift || alt)
            return CompletionAction::None;
        if (control) {
            if (key == CompletionKey::Space)
                return CompletionAction::Open;
            if (multiline && key == CompletionKey::Enter)
                return CompletionAction::Submit;
            return CompletionAction::None;
        }
        if (!visible)
            return CompletionAction::None;
        switch (key) {
            case CompletionKey::Up:
                return CompletionAction::Previous;
            case CompletionKey::Down:
                return CompletionAction::Next;
            case CompletionKey::Enter:
            case CompletionKey::Tab:
                return CompletionAction::Accept;
            case CompletionKey::Escape:
                return CompletionAction::Dismiss;
            default:
                return CompletionAction::None;
        }
    }
}
