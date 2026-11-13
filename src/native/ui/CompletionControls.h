#pragma once
#include "Suggestions.h"
#include <algorithm>

namespace elder_shell {
    inline float CompletionPanelHeight(float available, float line, float padding, float gap) {
        const auto desired = line * static_cast<float>(Suggestions::VisibleRows + 3) + padding * 2;
        const auto budget = std::max(0.0f, available - line * 3 - gap);
        return budget < line * 4 + padding * 2 ? 0 : std::min(desired, budget);
    }
    enum class CompletionKey { Up, Down, Enter, Tab, Escape, Space };
    enum class CompletionAction { None, Previous, Next, Accept, Dismiss, Open, Submit };

    constexpr CompletionAction CompletionShortcut(CompletionKey key, bool visible, bool control = false,
                                                  bool shift = false, bool alt = false) {
        if (shift || alt)
            return CompletionAction::None;
        if (key == CompletionKey::Enter)
            return CompletionAction::Submit;
        if (control) {
            if (key == CompletionKey::Space)
                return CompletionAction::Open;
            return CompletionAction::None;
        }
        if (!visible) {
            if (key == CompletionKey::Tab)
                return CompletionAction::Open;
            return CompletionAction::None;
        }
        switch (key) {
            case CompletionKey::Up:
                return CompletionAction::Previous;
            case CompletionKey::Down:
                return CompletionAction::Next;
            case CompletionKey::Tab:
                return CompletionAction::Accept;
            case CompletionKey::Escape:
                return CompletionAction::Dismiss;
            default:
                return CompletionAction::None;
        }
    }
}
