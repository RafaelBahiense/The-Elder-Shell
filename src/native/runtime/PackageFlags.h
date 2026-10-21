#pragma once

namespace elder_shell {
    void RegisterPackageServices(RE::BSScript::IVirtualMachine* vm);
    void RestorePackageFlagOverrides();
}
