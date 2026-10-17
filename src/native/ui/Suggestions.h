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
                selected_ = first_ = 0;
            }
            count_ = count;
            selected_ = count ? std::min(selected_, count - 1) : 0;
            RevealSelection();
        }

        bool Move(bool next) {
            if (!count_)
                return false;
            selected_ = next ? (selected_ + 1) % count_ : (selected_ ? selected_ - 1 : count_ - 1);
            RevealSelection();
            return true;
        }

        std::size_t Selected() const {
            return selected_;
        }

        std::size_t FirstVisible() const {
            return first_;
        }

    private:
        void RevealSelection() {
            if (selected_ < first_)
                first_ = selected_;
            if (selected_ >= first_ + VisibleRows)
                first_ = selected_ - VisibleRows + 1;
        }

        std::string query_;
        std::size_t count_ = 0;
        std::size_t selected_ = 0;
        std::size_t first_ = 0;
    };
}
