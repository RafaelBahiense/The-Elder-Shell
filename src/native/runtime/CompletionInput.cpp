#include "CompletionInput.h"
#include "CompletionHook.h"
#include "ui/CompletionEscape.h"
#include <SKSEMCP/SKSEMenuFramework.hpp>
#include <array>

namespace elder_shell {
    namespace {
        CompletionEscape escape;
        std::atomic<SKSEMenuFramework::Model::WindowInterface*> mainWindow{nullptr};
        using GetMainWindow = SKSEMenuFramework::Model::WindowInterface* (*)();
        GetMainWindow getMainWindow = nullptr;
        int lastConsoleFrame = -1;

        bool Accessible(std::uintptr_t address, std::size_t size, bool executable, HMODULE owner = nullptr) {
            MEMORY_BASIC_INFORMATION memory{};
            if (!address || !VirtualQuery(reinterpret_cast<const void*>(address), &memory, sizeof(memory)) ||
                memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
                (owner && memory.AllocationBase != owner))
                return false;
            const auto protection = memory.Protect & 0xFF;
            const bool canExecute = protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
                                    protection == PAGE_EXECUTE_WRITECOPY;
            const bool canRead = canExecute || protection == PAGE_READONLY || protection == PAGE_READWRITE ||
                                 protection == PAGE_WRITECOPY;
            const auto offset = address - reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
            return canRead && (!executable || canExecute) && offset < memory.RegionSize &&
                   size <= memory.RegionSize - offset;
        }

        std::string VerifyInputCall(std::uintptr_t site) {
            if (!Accessible(site, 5, true, GetModuleHandleW(nullptr)))
                return "call site is outside readable executable game memory";
            std::array<std::uint8_t, 5> call;
            std::memcpy(call.data(), reinterpret_cast<const void*>(site), call.size());
            const auto target = completion_hook::CallTarget(site, call);
            if (!target)
                return "expected a five-byte relative CALL instruction";
            const auto framework = GetModuleHandleW(L"SKSEMenuFramework.dll");
            if (!framework)
                return "menu framework module is unavailable";
            if (Accessible(*target, 1, true, framework))
                return {};
            if (!Accessible(*target, 14, true))
                return "call target is not a readable executable trampoline";
            std::array<std::uint8_t, 14> trampoline;
            std::memcpy(trampoline.data(), reinterpret_cast<const void*>(*target), trampoline.size());
            const auto destination = completion_hook::TrampolineTarget(trampoline);
            if (!destination || !Accessible(*destination, 1, true, framework))
                return "call does not resolve to the menu framework through a recognized trampoline";
            return {};
        }

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
        // Wrap the same dispatcher call as the framework, preserving its existing hook.
        // It handles menu-close keys before its public input callbacks:
        // https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/master/src/Hooks.cpp
        const auto site = REL::RelocationID(67315, 68617).address() + REL::Relocate(0x7B, 0x7B, 0x81);
        if (const auto error = VerifyInputCall(site); !error.empty()) {
            SKSE::log::warn("Completion Escape interception skipped on {} at {:X}: {}",
                            REL::Module::get().version().string(), site, error);
            return;
        }
        SKSE::AllocTrampoline(32);
        InputHook::original = SKSE::GetTrampoline().write_call<5>(site, InputHook::Thunk);
        SKSEMenuFramework::AddHudElement(ObserveVisibility);
        SKSE::log::info("Completion Escape interception installed at {:X}", site);
    }

    void PublishCompletionInput(bool visible) {
        lastConsoleFrame = ImGuiMCP::GetFrameCount();
        escape.Publish(visible);
    }

    bool TakeCompletionDismissal() {
        return escape.TakeDismissal();
    }
}
