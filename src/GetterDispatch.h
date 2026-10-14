#pragma once

#include <RE/B/BSTSmartPointer.h>
#include <string>

namespace RE::BSScript {
    class Object;
    class IStackCallbackFunctor;
    class Variable;

    namespace Internal {
        class VirtualMachine;
    }
}

namespace papyrus_console {
    // Empty means accepted by the VM; the callback still determines completion.
    std::string DispatchGetter(RE::BSScript::Internal::VirtualMachine* vm,
                               const RE::BSTSmartPointer<RE::BSScript::Object>& object, const std::string& property,
                               RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& completion);
    std::string DispatchSetter(RE::BSScript::Internal::VirtualMachine* vm,
                               const RE::BSTSmartPointer<RE::BSScript::Object>& object, const std::string& property,
                               RE::BSScript::Variable value,
                               RE::BSTSmartPointer<RE::BSScript::IStackCallbackFunctor>& completion);
}
