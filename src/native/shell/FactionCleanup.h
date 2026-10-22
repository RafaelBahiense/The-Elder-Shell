#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace elder_shell::faction_cleanup {
    inline constexpr std::size_t MaximumEntries = 1024;

    struct Entry {
        std::uint32_t faction = 0;
        std::int8_t rank = -1;
        bool authored = false;
        bool changed = false;
        auto operator<=>(const Entry&) const = default;
    };

    inline bool Removable(const Entry& entry) {
        // Negative ranks may store data too. Do not treat them as an empty row.
        return entry.changed && !entry.authored;
    }

    inline std::vector<Entry> Retained(const std::vector<Entry>& entries) {
        std::vector<Entry> result;
        for (const auto& entry : entries)
            if (!Removable(entry))
                result.push_back(entry);
        return result;
    }

    inline std::string Error(bool player, bool registered, bool alias, bool scene, std::size_t count) {
        if (player)
            return "Player faction cleanup is not supported.";
        if (registered)
            return "Actor is in the registration faction. Use the owning controller's release process first.";
        if (alias)
            return "Actor occupies an alias owned by the selected plugin; cleanup is blocked.";
        if (scene)
            return "Actor is in a scene; finish it before cleanup.";
        if (count > MaximumEntries)
            return "Faction snapshot exceeds the cleanup limit; no changes made.";
        return {};
    }

    inline bool Matches(const std::vector<Entry>& preview, const std::vector<Entry>& current,
                        std::uint32_t expectedGuard, std::uint32_t guard) {
        return expectedGuard == guard && preview == current;
    }
}
