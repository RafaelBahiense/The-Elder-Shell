#pragma once
#include "Server.h"

namespace elder_shell::terminal {
    struct SettingsStatus {
        bool enabled = false;
        ServerStatus server;
        std::string configurationError;
    };

    void Initialize();
    void SetEnabled(bool enabled);
    SettingsStatus SettingsSnapshot();
}
