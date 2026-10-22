#pragma once

namespace RE::BSScript {
    class IVirtualMachine;
}

namespace elder_shell {
    void RegisterFactionCleanupServices(RE::BSScript::IVirtualMachine* vm);
    void ClearFactionCleanupSnapshots();
}
