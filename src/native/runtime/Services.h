#pragma once

namespace elder_shell {
    RE::TESQuest* ServiceQuest();
    RE::BSTSmartPointer<RE::BSScript::Object> ServiceRoot();
    bool RegisterServices(RE::BSScript::IVirtualMachine* vm);
}
