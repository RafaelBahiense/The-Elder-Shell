#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>

namespace elder_shell {
    struct StateFunctionLayout {
        std::uint32_t count, offset;

        static constexpr StateFunctionLayout Decode(std::uint32_t packed) {
            return {packed & 0x1FFu, packed >> 9};
        }

        constexpr std::optional<std::size_t> Position(std::size_t stateIndex, std::size_t states, std::size_t functions,
                                                      std::optional<std::size_t> functionStart = std::nullopt) const {
            constexpr std::size_t recordSize = 16, functionSize = 8;
            if (stateIndex >= states)
                return std::nullopt;
            const auto start = stateIndex * recordSize + offset;
            const auto headers = states * recordSize;
            const auto begin = functionStart.value_or(headers);
            const auto end = begin + functions * functionSize;
            if (begin < headers || start < begin || start > end || (start - begin) % functionSize != 0 ||
                count > (end - start) / functionSize)
                return std::nullopt;
            return start;
        }
    };
}
