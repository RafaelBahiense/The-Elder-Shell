#pragma once

#include "Command.h"
#include <array>
#include <set>
#include <sstream>

namespace elder_shell::plugin_cleanup {
    inline constexpr std::size_t Limit = 1024;
    enum class Category { Quests, Aliases, Scripts, Packages, Equipment, Effects, Spells, Inventory, Factions };
    inline constexpr std::array Categories{Category::Quests,   Category::Aliases,   Category::Scripts,
                                           Category::Packages, Category::Equipment, Category::Effects,
                                           Category::Spells,   Category::Inventory, Category::Factions};

    inline std::string Lower(std::string_view value) {
        std::string result(value);
        for (auto& ch : result)
            if (ch >= 'A' && ch <= 'Z')
                ch += 'a' - 'A';
        return result;
    }

    struct ScriptIndex {
        std::set<std::string> names;
        std::string error;

        bool Contains(std::string_view name) const {
            return error.empty() && names.contains(Lower(name));
        }
    };

    inline ScriptIndex ParseIndex(std::string_view text, std::string_view plugin, std::string_view digest) {
        ScriptIndex result;
        auto invalid = [&] {
            result.names.clear();
            result.error = "Script index is missing, malformed or does not match the plugin's SHA256.";
            return result;
        };
        if (text.size() > 4 * 1024 * 1024 || digest.size() != 64)
            return invalid();
        std::istringstream stream{std::string(text)};
        std::string line;
        std::size_t row = 0;
        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (row == 0 && line != "ElderShellScriptIndex\t1")
                return invalid();
            if (row == 1 && Lower(line) != "plugin\t" + Lower(plugin))
                return invalid();
            if (row == 2 && Lower(line) != "sha256\t" + Lower(digest))
                return invalid();
            if (row >= 3) {
                if (!line.starts_with("script\t") || line.size() <= 7 || line.size() > 263 || row >= 20003)
                    return invalid();
                const auto name = line.substr(7);
                for (const auto ch : name)
                    if (static_cast<unsigned char>(ch) <= 32 || ch == '/' || ch == '\\')
                        return invalid();
                result.names.insert(Lower(name));
            }
            ++row;
        }
        return row >= 3 ? result : invalid();
    }
}
