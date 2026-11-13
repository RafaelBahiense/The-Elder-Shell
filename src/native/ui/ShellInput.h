#pragma once

#include <algorithm>
#include "shell/Input.h"
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elder_shell {
    struct ShellSelection {
        std::size_t cursor = 0, start = 0, end = 0;
    };

    class ShellDocument {
    public:
        static constexpr std::size_t MaximumDraft = 4096;

        const std::string& Draft() const {
            return draft_;
        }

        std::size_t Boundary() const {
            return prefix_.size();
        }

        std::string Text() const {
            return prefix_ + draft_;
        }

        void ConstrainCaret(ShellSelection& selection, bool mouseSelecting = false) const {
            if (mouseSelecting || selection.start != selection.end)
                return;
            const auto cursor = std::clamp(selection.cursor, Boundary(), Boundary() + draft_.size());
            selection = {cursor, cursor, cursor};
        }

        void SetDraft(std::string_view draft, ShellSelection& selection) {
            draft_ = draft.substr(0, MaximumDraft);
            selection = {Boundary() + draft_.size(), Boundary() + draft_.size(), Boundary() + draft_.size()};
        }

        void Rebase(std::string prefix, ShellSelection& selection) {
            if (prefix_ == prefix)
                return;
            const auto common = CommonPrefix(prefix_, prefix);
            auto oldEnd = prefix_.size(), newEnd = prefix.size();
            while (oldEnd > common && newEnd > common && prefix_[oldEnd - 1] == prefix[newEnd - 1]) {
                --oldEnd;
                --newEnd;
            }
            const auto map = [&](std::size_t position) {
                if (position >= Boundary())
                    return prefix.size() + std::min(position - Boundary(), draft_.size());
                if (position <= common)
                    return position;
                if (position >= oldEnd)
                    return newEnd + position - oldEnd;
                return common + std::min(position - common, newEnd - common);
            };
            selection = {map(selection.cursor), map(selection.start), map(selection.end)};
            prefix_ = std::move(prefix);
        }

        // Accept edits to the draft, preserving transcript bytes even for a
        // cut or paste whose selection crosses the prompt boundary.
        bool Edit(std::string_view edited, ShellSelection& selection) {
            const auto original = Text();
            if (edited == original)
                return true;
            const auto begin = CommonPrefix(original, edited);
            auto oldEnd = original.size(), newEnd = edited.size();
            while (oldEnd > begin && newEnd > begin && original[oldEnd - 1] == edited[newEnd - 1]) {
                --oldEnd;
                --newEnd;
            }
            const auto inserted = edited.substr(begin, newEnd - begin);
            auto draft = draft_;
            if (begin >= Boundary())
                draft.replace(begin - Boundary(), oldEnd - begin, inserted);
            else if (oldEnd > Boundary()) {
                draft.replace(0, oldEnd - Boundary(), inserted);
                selection = {Boundary() + inserted.size(), Boundary() + inserted.size(), Boundary() + inserted.size()};
            } else {
                draft += inserted;
                const auto cursor = Boundary() + (inserted.empty() ? 0 : draft.size());
                selection = {cursor, cursor, cursor};
            }
            if (draft.size() > MaximumDraft || draft.find('\0') != std::string::npos) {
                SetDraft(draft_, selection);
                return false;
            }
            draft_ = std::move(draft);
            return true;
        }

    private:
        static std::size_t CommonPrefix(std::string_view first, std::string_view second) {
            std::size_t i = 0;
            while (i < first.size() && i < second.size() && first[i] == second[i])
                ++i;
            return i;
        }

        std::string prefix_, draft_;
    };
}
