#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace papyrus_console {
    class History {
    public:
        void Add(std::string_view command) {
            if (!command.empty() && (entries_.empty() || entries_.back() != command)) {
                entries_.emplace_back(command);
                if (entries_.size() > 128)
                    entries_.erase(entries_.begin());
            }
            ResetNavigation();
        }

        std::optional<std::string_view> Previous(std::string_view draft) {
            if (entries_.empty() || position_ == 0)
                return std::nullopt;
            if (position_ == entries_.size())
                draft_ = draft;
            return entries_[--position_];
        }

        std::optional<std::string_view> Next() {
            if (position_ == entries_.size())
                return std::nullopt;
            ++position_;
            return position_ == entries_.size() ? std::string_view(draft_) : std::string_view(entries_[position_]);
        }

        void ResetNavigation() {
            position_ = entries_.size();
            draft_.clear();
        }

    private:
        std::vector<std::string> entries_;
        std::size_t position_ = 0;
        std::string draft_;
    };
}
