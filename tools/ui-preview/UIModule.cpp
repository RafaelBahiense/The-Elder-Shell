#include "UIAPI.h"
#include "Simulation.h"
#include "Style.h"
#include "ui/Menu.h"
#include "shell/Completion.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <imgui_impl_win32.h>
#include <imgui_impl_dx11.h>
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

namespace {
    preview::Session* session = nullptr;
    bool win32Ready = false, dxReady = false;

    void Detach() {
        if (!ImGui::GetCurrentContext())
            return;
        if (session)
            preview::SaveSession(*session);
        SKSEMenuFramework::Pages.clear();
        if (dxReady)
            ImGui_ImplDX11_Shutdown();
        if (win32Ready)
            ImGui_ImplWin32_Shutdown();
        dxReady = win32Ready = false;
        ImGui::DestroyContext();
        session = nullptr;
    }

    bool Attach(HWND window, ID3D11Device* device, ID3D11DeviceContext* context, preview::Session* state) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::GetIO().IniFilename = nullptr;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        if (!preview::ApplyStyle()) {
            Detach();
            return false;
        }
        preview::Reset(preview::Scenario::Ready);
        preview::RestoreSession(*state);
        ImGui::GetIO().FontGlobalScale = state->scale;
        elder_shell::RegisterMenu();
        if (window) {
            win32Ready = ImGui_ImplWin32_Init(window);
            dxReady = win32Ready && ImGui_ImplDX11_Init(device, context);
            if (!dxReady) {
                Detach();
                return false;
            }
        }
        session = state;
        return true;
    }

    void Draw() {
        preview::Tick();
        const auto* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::Begin("Elder Shell preview", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
        constexpr const char* scenarios[] = {"Ready",         "Empty session",   "Pending request", "Rejected request",
                                             "Large session", "Untested runtime"};
        ImGui::SetNextItemWidth(180);
        if (ImGui::Combo("Scenario", &session->scenario, scenarios, IM_ARRAYSIZE(scenarios)))
            preview::Reset(static_cast<preview::Scenario>(session->scenario));
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            preview::Reset(static_cast<preview::Scenario>(session->scenario));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(110);
        if (ImGui::SliderFloat("Scale", &session->scale, 0.75f, 1.75f, "%.2f"))
            ImGui::GetIO().FontGlobalScale = session->scale;
        ImGui::TextDisabled("Standalone preview | Synthetic data | No Skyrim or Papyrus execution");
        ImGui::TextWrapped("%s", session->reloadStatus.c_str());
        if (ImGui::BeginTabBar("Pages")) {
            for (const auto& page : SKSEMenuFramework::Pages) {
                if (ImGui::BeginTabItem(page.name.c_str())) {
                    ImGui::BeginChild("Page content");
                    page.render();
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    int SmokeTest() {
        using namespace elder_shell;
        if (SKSEMenuFramework::Pages.size() != 3)
            return 10;
        if (session->valid && (CaptureMenuSession().input != session->menu.input ||
                               Snapshot().size() != session->requests.Entries().size()))
            return 11;
        unsigned char* pixels = nullptr;
        int width = 0, height = 0;
        auto& io = ImGui::GetIO();
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        if (!pixels || width <= 0 || height <= 0)
            return 12;
        for (const float size : {640.0f, 1000.0f, 1600.0f}) {
            io.DisplaySize = {size, 720};
            io.DeltaTime = 1.0f / 60;
            for (int scenario = 0; scenario < 6; ++scenario) {
                preview::Reset(static_cast<preview::Scenario>(scenario));
                for (const auto& page : SKSEMenuFramework::Pages) {
                    for (int frame = 0; frame < 3; ++frame) {
                        ImGui::NewFrame();
                        ImGui::SetNextWindowSize(io.DisplaySize);
                        ImGui::Begin("Smoke test");
                        ImGui::SetNextItemOpen(true, ImGuiCond_Always);
                        page.render();
                        ImGui::End();
                        ImGui::Render();
                        if (!ImGui::GetDrawData()->Valid || ImGui::GetDrawData()->TotalVtxCount <= 0)
                            return 13;
                    }
                }
            }
        }
        const auto variablesPage =
            std::ranges::find_if(SKSEMenuFramework::Pages, [](const auto& page) { return page.name == "Variables"; });
        for (const auto name : {"numbers", "controller"}) {
            preview::InspectFixture(name);
            for (int frame = 0; frame < 3; ++frame) {
                ImGui::NewFrame();
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Smoke test");
                variablesPage->render();
                ImGui::End();
                ImGui::Render();
            }
        }
        const auto consolePage =
            std::ranges::find_if(SKSEMenuFramework::Pages, [](const auto& page) { return page.name == "Console"; });
        for (const bool multiline : {false, true}) {
            MenuSession editing;
            constexpr char expression[] = "player.Get";
            std::copy(std::begin(expression), std::end(expression), editing.input.begin());
            editing.multiline = multiline;
            RestoreMenuSession(editing);
            for (int frame = 0; frame < 3; ++frame) {
                ImGui::NewFrame();
                ImGui::SetNextWindowSize(io.DisplaySize);
                ImGui::Begin("Smoke test");
                if (frame == 1)
                    ImGui::OpenPopup(multiline ? "Session details##popup" : "Console help");
                consolePage->render();
                ImGui::End();
                ImGui::Render();
            }
            ImGui::ClosePopupsOverWindow(nullptr, false);
        }
        preview::Reset(preview::Scenario::Ready);
        if (Complete("player.Get", 10, *CompletionMetadataSnapshot(), true).items.empty())
            return 14;
        if (!UseInstance(1, "sample").empty() || VariableInspectionSnapshot().variables.size() != 4)
            return 15;
        MenuSession draft;
        constexpr char command[] = "player.GetLevel()";
        std::copy(std::begin(command), std::end(command), draft.input.begin());
        draft.history.Add(command);
        RestoreMenuSession(draft);
        return 0;
    }

    void Frame() {
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Draw();
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    LRESULT Message(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
        return ImGui_ImplWin32_WndProcHandler(window, message, wParam, lParam);
    }
}

extern "C" __declspec(dllexport) const preview::UIAPI* Preview_GetUI() {
    static const preview::UIAPI api{
        1, sizeof(preview::Session), sizeof(elder_shell::MenuSession), Attach, Detach, Frame, Message, SmokeTest};
    return &api;
}
