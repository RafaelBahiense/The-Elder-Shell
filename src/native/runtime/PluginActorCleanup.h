#pragma once

namespace RE::BSScript {
    class IVirtualMachine;
}

namespace elder_shell {
    void RegisterPluginActorCleanupServices(RE::BSScript::IVirtualMachine* vm);
}
