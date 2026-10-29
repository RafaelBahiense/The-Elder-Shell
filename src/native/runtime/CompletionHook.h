#pragma once

#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace elder_shell::completion_hook {
    inline std::optional<std::uintptr_t> CallTarget(std::uintptr_t site, std::span<const std::uint8_t> bytes) {
        if (bytes.size() < 5 || bytes[0] != 0xE8 || site > std::numeric_limits<std::uintptr_t>::max() - 5)
            return std::nullopt;
        std::int32_t displacement;
        std::memcpy(&displacement, bytes.data() + 1, sizeof(displacement));
        const auto next = site + 5;
        if (displacement >= 0) {
            const auto distance = static_cast<std::uintptr_t>(displacement);
            if (next > std::numeric_limits<std::uintptr_t>::max() - distance)
                return std::nullopt;
            return next + distance;
        }
        const auto distance = static_cast<std::uintptr_t>(-static_cast<std::int64_t>(displacement));
        return next >= distance ? std::optional{next - distance} : std::nullopt;
    }

    // CommonLib's five-byte call trampoline: jmp [rip+0], followed by its destination.
    inline std::optional<std::uintptr_t> TrampolineTarget(std::span<const std::uint8_t> bytes) {
        if (bytes.size() < 14 || bytes[0] != 0xFF || bytes[1] != 0x25 || bytes[2] || bytes[3] || bytes[4] || bytes[5])
            return std::nullopt;
        std::uint64_t target;
        std::memcpy(&target, bytes.data() + 6, sizeof(target));
        return static_cast<std::uintptr_t>(target);
    }
}
