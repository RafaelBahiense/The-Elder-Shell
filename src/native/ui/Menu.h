#pragma once

#include "History.h"
#include <array>
#include <string>

namespace elder_shell {
    struct MenuSession {
        std::array<char, 4097> input{};
        History history;
        std::array<char, 32> explorerForm{};
        std::array<char, 128> explorerScript{};
        std::array<char, 128> instanceVariable{'i', 'n', 's', 't', 'a', 'n', 'c', 'e', '\0'};
    };

    MenuSession CaptureMenuSession();
    void RestoreMenuSession(const MenuSession& state);
    void RegisterMenu();
    void __stdcall RenderVariables();
    void __stdcall RenderPapyrus();
}
