#pragma once

#include <atomic>

namespace elder_shell {
    
    class CompletionEscape {
    public:
        void Publish(bool visible) {
            visible_ = visible;
        }

        bool Consume(bool down, bool up, bool menuOpen) {
            if (down && menuOpen && visible_.exchange(false)) {
                swallowed_ = true;
                dismiss_ = true;
            }
            const bool consumed = swallowed_;
            if (up)
                swallowed_ = false;
            return consumed;
        }

        bool TakeDismissal() {
            return dismiss_.exchange(false);
        }

    private:
        std::atomic_bool visible_{false}, dismiss_{false};
        bool swallowed_ = false;
    };
}
