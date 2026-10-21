#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace elder_shell {
    inline std::string NewConsoleOutput(std::string_view before, std::string_view after) {
        // The engine may retain the whole buffer or discard its oldest text.
        if (after.empty())
            return {};
        // Prefix matching keeps rollover detection linear even for repetitive output.
        std::vector<std::size_t> prefix(after.size());
        for (std::size_t i = 1, matched = 0; i < after.size(); ++i) {
            while (matched && after[i] != after[matched])
                matched = prefix[matched - 1];
            if (after[i] == after[matched])
                ++matched;
            prefix[i] = matched;
        }
        std::size_t overlap = 0;
        for (const char character : before.substr(before.size() - std::min(before.size(), after.size()))) {
            while (overlap && (overlap == after.size() || character != after[overlap]))
                overlap = prefix[overlap - 1];
            if (character == after[overlap])
                ++overlap;
        }
        std::string output(after.substr(overlap));
        constexpr std::size_t limit = 32768;
        if (output.size() > limit) {
            output.resize(limit);
            output += "\n[Console output truncated]";
        }
        return output;
    }
}
