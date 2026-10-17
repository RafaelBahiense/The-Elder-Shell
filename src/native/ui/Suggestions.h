#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>

namespace elder_shell {
    class Suggestions {
    public:
        static constexpr std::size_t VisibleRows = 5;

        void Update(std::string_view query, std::size_t count) {
            if (query != query_) {
                query_ = query;
                selected_ = 0;
                revealSelection_ = true;
                dismissed_ = explicit_ = false;
            }
            count_ = count;
            const auto selected = count ? std::min(selected_, count - 1) : 0;
            revealSelection_ = revealSelection_ || selected != selected_;
            selected_ = selected;
        }

        bool Move(bool next) {
            if (!Visible())
                return false;
            selected_ = next ? (selected_ + 1) % count_ : (selected_ ? selected_ - 1 : count_ - 1);
            revealSelection_ = true;
            return true;
        }

        std::size_t Selected() const {
            return selected_;
        }

        bool Visible() const {
            return count_ != 0 && !dismissed_;
        }

        void Dismiss() {
            dismissed_ = true;
            explicit_ = false;
        }

        void Open() {
            dismissed_ = false;
            explicit_ = true;
            revealSelection_ = true;
        }

        bool ExplicitFor(std::string_view query) const {
            return explicit_ && query == query_;
        }

        bool TakeRevealRequest() {
            const bool reveal = revealSelection_;
            revealSelection_ = false;
            return reveal;
        }

    private:
        std::string query_;
        std::size_t count_ = 0;
        std::size_t selected_ = 0;
        bool revealSelection_ = true;
        bool dismissed_ = false, explicit_ = false;
    };
}
