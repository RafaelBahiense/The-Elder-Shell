#pragma once

#include <windows.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <string>
#include <vector>

namespace ImGuiMCP {
    using ::ImGuiCol_Text;
    using ::ImGuiCond_Appearing;
    using ::ImGuiTableColumnFlags_WidthFixed;
    using ::ImGuiTableColumnFlags_WidthStretch;
    using ::ImGuiTableFlags_BordersInnerH;
    using ::ImGuiTableFlags_RowBg;
    using ::ImGuiTableFlags_SizingFixedFit;
    using ::ImGuiTableFlags_SizingStretchProp;
    using namespace ImGui;
    using ::ImGuiCol_FrameBg;
    using ::ImGuiID;
    using ::ImGuiInputFlags_LockUntilRelease;
    using ::ImGuiInputFlags_Repeat;
    using ::ImGuiInputTextCallbackData;
    using ::ImGuiInputTextFlags_AllowTabInput;
    using ::ImGuiInputTextFlags_CallbackAlways;
    using ::ImGuiInputTextFlags_CallbackEdit;
    using ::ImGuiInputTextFlags_CallbackHistory;
    using ::ImGuiInputTextFlags_EnterReturnsTrue;
    using ::ImGuiInputTextFlags_ReadOnly;
    using ::ImGuiKey;
    using ::ImGuiKey_DownArrow;
    using ::ImGuiKey_Enter;
    using ::ImGuiKey_Escape;
    using ::ImGuiKey_KeypadEnter;
    using ::ImGuiKey_Space;
    using ::ImGuiKey_Tab;
    using ::ImGuiKey_UpArrow;
    using ::ImGuiKeyData;
    using ::ImGuiStyleVar_FrameBorderSize;
    using ::ImVec2;
    using ::ImVec4;
    inline constexpr auto ImGuiChildFlags_Border = ::ImGuiChildFlags_Borders;

    inline ImGuiStyle* GetStyle() {
        return &ImGui::GetStyle();
    }

    inline ImGuiIO* GetIO() {
        return &ImGui::GetIO();
    }

    inline void GetContentRegionAvail(ImVec2* output) {
        *output = ImGui::GetContentRegionAvail();
    }

    inline void GetCursorPos(ImVec2* output) {
        *output = ImGui::GetCursorPos();
    }

    inline void CalcTextSize(ImVec2* output, const char* text, const char* end, bool hide, float wrap) {
        *output = ImGui::CalcTextSize(text, end, hide, wrap);
    }

    struct ImGuiInputTextCallbackDataManager {
        static void DeleteChars(ImGuiInputTextCallbackData* data, int position, int count) {
            data->DeleteChars(position, count);
        }

        static void InsertChars(ImGuiInputTextCallbackData* data, int position, const char* begin,
                                const char* end = nullptr) {
            data->InsertChars(position, begin, end);
        }
    };
}

namespace SKSEMenuFramework {
    using RenderFunction = void(__stdcall*)();

    struct Page {
        std::string name;
        RenderFunction render;
    };

    inline std::vector<Page> Pages;

    inline bool IsInstalled() {
        return true;
    }

    inline void SetSection(const char*) {}

    inline void AddSectionItem(const char* name, RenderFunction render) {
        Pages.push_back({name, render});
    }
}

namespace SKSE::log {
    template <class... Args>
    void error(const char*, Args&&...) {}
}
