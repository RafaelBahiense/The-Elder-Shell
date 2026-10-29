#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace elder_shell {
    struct ShellInput {
        std::string source, completion;
        bool more = false, fragment = false;
    };

    // Decide when to request evaluation; Papyrus remains the syntax authority.
    inline ShellInput ReadShellInput(std::string_view text) {
        ShellInput result;
        result.completion = text;
        bool quoted = false, lineComment = false, blockComment = false, firstWord = true, invalid = false;
        int parentheses = 0, brackets = 0;
        std::vector<std::string> blocks;
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (lineComment && c != '\n') {
                result.source += ' ';
                continue;
            }
            if (blockComment) {
                if (c == '/' && i + 1 < text.size() && text[i + 1] == ';') {
                    result.source += "  ";
                    ++i;
                    blockComment = false;
                } else {
                    result.source += c == '\n' && !parentheses && !brackets ? '\n' : ' ';
                    if (c == '\n' && !parentheses && !brackets)
                        firstWord = true;
                }
                continue;
            }
            if (c == '\\' && i + 1 == text.size()) {
                result.more = true;
                result.source += c;
                continue;
            }
            if (c == '\\' && i + 1 < text.size()) {
                auto next = i + 1;
                if (text[next] == '\r' && next + 1 < text.size())
                    ++next;
                if (text[next] == '\n') {
                    for (auto p = i; p <= next; ++p)
                        result.completion[p] = ' ';
                    i = next;
                    if (i + 1 == text.size())
                        result.more = true;
                    continue;
                }
                if (quoted || text[i + 1] == '\\') {
                    result.source += c;
                    result.source += text[++i];
                    continue;
                }
            }
            if (quoted) {
                result.source += c;
                if (c == '"')
                    quoted = false;
                continue;
            }
            if (c == ';') {
                blockComment = i + 1 < text.size() && text[i + 1] == '/';
                lineComment = !blockComment;
                result.source += ' ';
                if (blockComment) {
                    ++i;
                    result.source += ' ';
                }
                continue;
            }
            if (c == '"')
                quoted = true;
            if (c == '\n') {
                lineComment = false;
                if (parentheses > 0 || brackets > 0)
                    result.source += ' ';
                else {
                    result.source += '\n';
                    firstWord = true;
                }
                continue;
            }
            if (firstWord && (std::isalpha(static_cast<unsigned char>(c)) || c == '_')) {
                auto end = i;
                while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_'))
                    ++end;
                std::string word(text.substr(i, end - i));
                std::ranges::transform(word, word.begin(),
                                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
                if (word == "if" || word == "while") {
                    blocks.push_back(word);
                    result.fragment = true;
                } else if (word == "endif" || word == "endwhile") {
                    if (blocks.empty() || blocks.back() != (word == "endif" ? "if" : "while"))
                        invalid = true;
                    else
                        blocks.pop_back();
                }
                firstWord = false;
            } else if (!std::isspace(static_cast<unsigned char>(c)))
                firstWord = false;
            if (c == '(')
                ++parentheses;
            else if (c == ')') {
                if (--parentheses < 0)
                    invalid = true;
            } else if (c == '[')
                ++brackets;
            else if (c == ']') {
                if (--brackets < 0)
                    invalid = true;
            }
            result.source += c;
        }
        result.more =
            !invalid && (result.more || quoted || blockComment || parentheses > 0 || brackets > 0 || !blocks.empty());
        const auto first = result.source.find_first_not_of(" \t\r\n");
        const auto last = result.source.find_last_not_of(" \t\r\n");
        if (first != std::string::npos)
            result.fragment =
                result.fragment || result.source.substr(first, last - first + 1).find('\n') != std::string::npos;
        return result;
    }

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
