#pragma once

#include <string>
#include <string_view>

namespace elder_shell {
    // Game thread only.
    RE::NiPointer<RE::TESObjectREFR> ConsoleSelection();
    std::string RunGameConsole(std::string_view command, RE::TESObjectREFR* target = nullptr);
}
