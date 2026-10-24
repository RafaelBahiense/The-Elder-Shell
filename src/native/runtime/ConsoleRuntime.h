#pragma once

#include <array>
#include <cstdint>

namespace elder_shell {
    constexpr std::uint64_t ConsoleSelectedReferenceID(bool anniversary, std::array<std::uint16_t, 3> version) {
        if (!anniversary)
            return 519394;
        return version >= std::array<std::uint16_t, 3>{1, 6, 1130} ? 504099 : 405935;
    }

    constexpr std::uint64_t ConsoleCompileAndRunID(bool anniversary, std::array<std::uint16_t, 3> version) {
        if (!anniversary)
            return 21416;
        return version >= std::array<std::uint16_t, 3>{1, 6, 1130} ? 441582 : 21890;
    }
}
