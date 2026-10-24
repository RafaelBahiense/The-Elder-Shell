#pragma once

#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace elder_shell::ui {
    inline constexpr ImGuiMCP::ImVec4 Accent{0.83f, 0.72f, 0.43f, 1};
    inline constexpr ImGuiMCP::ImVec4 Success{0.55f, 0.78f, 0.62f, 1};
    inline constexpr ImGuiMCP::ImVec4 Pending{0.55f, 0.73f, 0.88f, 1};
    inline constexpr ImGuiMCP::ImVec4 Failure{0.92f, 0.55f, 0.49f, 1};

    inline void Heading(const char* text) {
        ImGuiMCP::TextColored(Accent, "%s", text);
        ImGuiMCP::Spacing();
    }

    inline bool Wide(float width) {
        return width >= ImGuiMCP::GetFontSize() * 30;
    }

    inline float ListWidth(float width) {
        return std::clamp(width * 0.28f, ImGuiMCP::GetFontSize() * 10, ImGuiMCP::GetFontSize() * 16);
    }

    inline void NextControl(const char* label, bool checkbox = false) {
        ImGuiMCP::SameLine();
        ImGuiMCP::ImVec2 available, text;
        ImGuiMCP::GetContentRegionAvail(&available);
        ImGuiMCP::CalcTextSize(&text, label, nullptr, false, -1);
        const auto width =
            text.x + ImGuiMCP::GetStyle()->FramePadding.x * 2 + (checkbox ? ImGuiMCP::GetFrameHeight() : 0);
        if (available.x < width)
            ImGuiMCP::NewLine();
    }

    inline std::string Preview(std::string_view text, std::size_t limit = 48) {
        const auto end = std::min(text.find_first_of("\r\n"), text.size());
        if (end <= limit)
            return std::string(text.substr(0, end));
        // Do not split a UTF-8 code point when shortening a list label.
        auto count = limit;
        while (count && (static_cast<unsigned char>(text[count]) & 0xC0) == 0x80)
            --count;
        return std::string(text.substr(0, count)) + "...";
    }

    struct TextBuffer {
        std::string text;
        std::vector<char> bytes{'\0'};

        bool Update(std::string_view value) {
            if (text == value)
                return false;
            text = value;
            bytes.assign(text.begin(), text.end());
            bytes.push_back('\0');
            return true;
        }

        void Draw(const char* id, float maxLines = 12) {
            const auto lines = std::min(maxLines, 1.0f + static_cast<float>(std::ranges::count(text, '\n')));
            const auto height =
                lines * ImGuiMCP::GetTextLineHeightWithSpacing() + ImGuiMCP::GetStyle()->FramePadding.y * 2;
            ImGuiMCP::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg, {0, 0, 0, 0});
            ImGuiMCP::PushStyleVar(ImGuiMCP::ImGuiStyleVar_FrameBorderSize, 0);
            ImGuiMCP::ImVec2 available;
            ImGuiMCP::GetContentRegionAvail(&available);
            ImGuiMCP::InputTextMultiline(id, bytes.data(), bytes.size(), {available.x, height},
                                         ImGuiMCP::ImGuiInputTextFlags_ReadOnly, nullptr, nullptr);
            ImGuiMCP::PopStyleVar();
            ImGuiMCP::PopStyleColor();
        }
    };
}
