#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
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

}
