#include "Menu.h"
#include "Layout.h"
#include "terminal/Settings.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>

namespace elder_shell {
    void __stdcall RenderSettings() {
        ui::Heading("EXTERNAL TERMINAL");
        const auto status = terminal::SettingsSnapshot();
        auto enabled = status.enabled;
        if (ImGuiMCP::Checkbox("Enable local terminal connection", &enabled))
            terminal::SetEnabled(enabled);
        ImGuiMCP::TextWrapped(
            "Connect from PowerShell using Connect-ElderShell.ps1. Commands share this shell's "
            "variables and transcript. One client can connect at a time.");
        if (!status.server.error.empty())
            ImGuiMCP::TextWrapped("%s", status.server.error.c_str());
        else if (status.enabled)
            ImGuiMCP::Text("%s", status.server.connected   ? "Connected"
                                 : status.server.listening ? "Listening"
                                                           : "Starting");
        else
            ImGuiMCP::TextUnformatted("Disabled");
        if (status.enabled)
            ImGuiMCP::Text("Pipe: %s", status.server.pipe.c_str());
        if (!status.configurationError.empty())
            ImGuiMCP::TextWrapped("%s", status.configurationError.c_str());
    }
}
