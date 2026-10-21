#pragma once

#include "Command.h"
#include <array>
#include <cstdint>

namespace elder_shell::package_flags {
    struct Flag {
        std::string_view name;
        std::uint16_t mask;
    };

    inline constexpr std::array<Flag, 9> Flags{{{"HellosToPlayer", 1 << 0},
                                                {"RandomConversations", 1 << 1},
                                                {"ObserveCombatBehaviour", 1 << 2},
                                                {"GreetCorpseBehaviour", 1 << 3},
                                                {"ReactionToPlayerActions", 1 << 4},
                                                {"FriendlyFireComments", 1 << 5},
                                                {"AggroRadiusBehavior", 1 << 6},
                                                {"AllowIdleChatter", 1 << 7},
                                                {"WorldInteractions", 1 << 9}}};

    inline std::uint16_t Find(std::string_view name) {
        for (const auto& flag : Flags)
            if (Equal(name, flag.name))
                return flag.mask;
        return 0;
    }

    struct Override {
        std::uint16_t original = 0;
        std::uint16_t changed = 0;

        std::uint16_t Set(std::uint16_t current, std::uint16_t mask, bool enabled) {
            const auto next = static_cast<std::uint16_t>(enabled ? current | mask : current & ~mask);
            changed |= current ^ next;
            return next;
        }

        std::uint16_t Restore(std::uint16_t current) const {
            return static_cast<std::uint16_t>((current & ~changed) | (original & changed));
        }
    };
}
