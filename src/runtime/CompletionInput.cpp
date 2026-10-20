#include "CompletionInput.h"
#include "ui/CompletionEscape.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>

namespace elder_shell {
    namespace {
        CompletionEscape escape;
        std::atomic<SKSEMenuFramework::Model::WindowInterface*> mainWindow{nullptr};
        using GetMainWindow = SKSEMenuFramework::Model::WindowInterface* (*)();
        GetMainWindow getMainWindow = nullptr;
        int lastConsoleFrame = -1;

        void __stdcall ObserveVisibility() {
            mainWindow = getMainWindow ? getMainWindow() : nullptr;
            if (lastConsoleFrame != ImGuiMCP::GetFrameCount() - 1)
                escape.Publish(false);
        }

        struct InputHook {
            static void Thunk(RE::BSTEventSource<RE::InputEvent*>* dispatcher, RE::InputEvent* const* events) {
                auto* head = events ? *events : nullptr;
                RE::InputEvent* previous = nullptr;
                std::vector<std::pair<RE::InputEvent*, RE::InputEvent*>> changedLinks;
                for (auto* event = head; event; event = event->next) {
                    bool consume = false;
                    if (auto* button = event->AsButtonEvent();
                        button && button->GetDevice() == RE::INPUT_DEVICE::kKeyboard &&
                        button->GetIDCode() == RE::BSWin32KeyboardDevice::Key::kEscape) {
                        const auto* window = mainWindow.load();
                        consume = escape.Consume(button->IsDown(), button->IsUp(), window && window->IsOpen);
                    }
                    if (!consume) {
                        previous = event;
                        continue;
                    }
                    if (previous) {
                        changedLinks.emplace_back(previous, previous->next);
                        previous->next = event->next;
                    } else {
                        head = event->next;
                    }
                }
                original(dispatcher, &head);
                for (auto it = changedLinks.rbegin(); it != changedLinks.rend(); ++it)
                    it->first->next = it->second;
            }

            static inline REL::Relocation<decltype(Thunk)> original;
        };
    }

    void RegisterCompletionInput() {
        getMainWindow = SKSEMenuFramework::Model::Internal::GetFunction<GetMainWindow>("GetMainWindow");
        if (!getMainWindow) {
            SKSE::log::warn("Completion Escape interception requires the menu framework GetMainWindow export");
            return;
        }
        SKSEMenuFramework::AddHudElement(ObserveVisibility);
        // Wrap the same dispatcher call as the framework, preserving its existing hook.
        // It handles menu-close keys before its public input callbacks:
        // https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/master/src/Hooks.cpp
        SKSE::AllocTrampoline(32);
        InputHook::original = SKSE::GetTrampoline().write_call<5>(
            REL::RelocationID(67315, 68617).address() + REL::Relocate(0x7B, 0x7B, 0x81), InputHook::Thunk);
    }

    void PublishCompletionInput(bool visible) {
        lastConsoleFrame = ImGuiMCP::GetFrameCount();
        escape.Publish(visible);
    }

    bool TakeCompletionDismissal() {
        return escape.TakeDismissal();
    }
}
