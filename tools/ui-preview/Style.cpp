#include "Style.h"
#include <windows.h>
#include <imgui.h>
#include <filesystem>
#include <string_view>


namespace preview {
    namespace {
        void SetColor(ImGuiStyle& style, std::string_view name, unsigned int rgba) {
            // Framework 3 themes use names from an older ImGui version.
            if (name == "TabActive")
                name = "TabSelected";
            if (name == "TabUnfocused")
                name = "TabDimmed";
            if (name == "TabUnfocusedActive")
                name = "TabDimmedSelected";
            if (name == "NavHighlight")
                name = "NavCursor";
            for (int index = 0; index < ImGuiCol_COUNT; ++index) {
                if (name == ImGui::GetStyleColorName(index)) {
                    style.Colors[index] = {static_cast<float>((rgba >> 24) & 0xFF) / 255.0f,
                                           static_cast<float>((rgba >> 16) & 0xFF) / 255.0f,
                                           static_cast<float>((rgba >> 8) & 0xFF) / 255.0f,
                                           static_cast<float>(rgba & 0xFF) / 255.0f};
                    break;
                }
            }
        }
    }
}

#include "ThemeData.h"

bool preview::ApplyStyle() {
    ImGui::StyleColorsDark();
    auto& style = ImGui::GetStyle();
    // SKSE Menu Framework's baseline before its theme JSON is applied.
    style.WindowRounding = style.FrameRounding = style.GrabRounding = style.ScrollbarRounding = style.ChildRounding =
        style.PopupRounding = 0;
    style.WindowBorderSize = style.ChildBorderSize = style.PopupBorderSize = 1;
    style.FrameBorderSize = style.TabBarBorderSize = style.TabBorderSize = 0;
    style.FramePadding = {4, 3};
    ApplyThemeValues(style);
    auto& io = ImGui::GetIO();
    if constexpr (HasFont) {
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        if (!length || length >= std::size(executable))
            return false;
        const auto font = std::filesystem::path(executable).parent_path() / "preview-font.ttf";
        io.FontDefault = io.Fonts->AddFontFromFileTTF(font.string().c_str(), FontSize);
    } else {
        ImFontConfig config;
        config.SizePixels = FontSize;
        io.FontDefault = io.Fonts->AddFontDefault(&config);
    }
    return io.FontDefault != nullptr;
}
