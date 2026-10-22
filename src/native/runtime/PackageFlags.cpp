#include "PackageFlags.h"
#include "Services.h"
#include "shell/PackageFlags.h"
#include "shell/Services.h"
#include <unordered_map>

namespace elder_shell {
    namespace {
        // Accessed only by main-thread VM natives and SKSE lifecycle messages.
        std::unordered_map<RE::FormID, package_flags::Override> overrides;
        constexpr std::size_t MaximumOverrides = 128;
        using EngineFlag = RE::PACKAGE_DATA::InterruptFlag;
        constexpr std::array EngineFlags{EngineFlag::kHellosToPlayer,          EngineFlag::kRandomConversations,
                                         EngineFlag::kObserveCombatBehaviour,  EngineFlag::kGreetCorpseBehaviour,
                                         EngineFlag::kReactionToPlayerActions, EngineFlag::kFriendlyFireComments,
                                         EngineFlag::kAggroRadiusBehavior,     EngineFlag::kAllowIdleChatter,
                                         EngineFlag::kWorldInteractions};
        static_assert([] {
            for (std::size_t index = 0; index < EngineFlags.size(); ++index)
                if (std::to_underlying(EngineFlags[index]) != package_flags::Flags[index].mask)
                    return false;
            return true;
        }());

        std::uint16_t ReadFlags(const RE::TESPackage* package) {
            return package->packData.foBehaviorFlags.underlying();
        }

        void WriteFlags(RE::TESPackage* package, std::uint16_t flags) {
            package->packData.foBehaviorFlags = static_cast<EngineFlag>(flags);
        }

        bool Valid(RE::TESQuest* self, RE::TESPackage* package) {
            return self && self == ServiceQuest() && package;
        }

        std::string InspectPackage(RE::TESQuest* self, RE::TESPackage* package) {
            if (!Valid(self, package))
                return "Provide an existing Package and the Elder Shell service.";
            const auto flags = ReadFlags(package);
            const auto found = overrides.find(package->GetFormID());
            std::string result = std::format(
                "Package {:08X}\nInterrupt flags: 0x{:04X}; Elder Shell changed bits: 0x{:04X}\n"
                "Scope: every actor using this Package. Changes are runtime-only.\n",
                package->GetFormID(), flags, found == overrides.end() ? 0 : found->second.changed);
            for (const auto& flag : package_flags::Flags)
                result += std::format("{}: {}\n", flag.name, (flags & flag.mask) != 0);
            return result;
        }

        std::string SetPackageInterruptFlag(RE::TESQuest* self, RE::TESPackage* package, std::string name,
                                            bool enabled) {
            if (!Valid(self, package))
                return "Provide an existing Package and the Elder Shell service.";
            const auto mask = package_flags::Find(name);
            if (!mask)
                return "Unknown interrupt flag. InspectPackage lists the supported flag names.";
            const auto current = ReadFlags(package);
            if (((current & mask) != 0) == enabled)
                return "Flag already has the requested value; no change.\n" + InspectPackage(self, package);
            const auto id = package->GetFormID();
            if (!overrides.contains(id) && overrides.size() >= MaximumOverrides)
                return "Package override limit reached (128). Restore previous overrides first.";
            auto& change = overrides.try_emplace(id, package_flags::Override{current}).first->second;
            const auto next = change.Set(current, mask, enabled);
            WriteFlags(package, next);
            SKSE::log::info("Package {:08X} interrupt flags {:04X} -> {:04X}: {}={}", id, current, next, name, enabled);
            return "Flag changed. Reevaluate affected actors explicitly.\n" + InspectPackage(self, package);
        }

        std::string RestorePackageInterruptFlags(RE::TESQuest* self, RE::TESPackage* package) {
            if (!Valid(self, package))
                return "Provide an existing Package and the Elder Shell service.";
            const auto found = overrides.find(package->GetFormID());
            if (found == overrides.end())
                return "No Elder Shell package override to restore.\n" + InspectPackage(self, package);
            WriteFlags(package, found->second.Restore(ReadFlags(package)));
            overrides.erase(found);
            return "Restored bits changed by Elder Shell. Reevaluate affected actors explicitly.\n" +
                   InspectPackage(self, package);
        }
    }

    void RestorePackageFlagOverrides() {
        for (const auto& [id, change] : overrides)
            if (auto* package = RE::TESForm::LookupByID<RE::TESPackage>(id))
                WriteFlags(package, change.Restore(ReadFlags(package)));
        overrides.clear();
    }

    void RegisterPackageServices(RE::BSScript::IVirtualMachine* vm) {
        // These touch shared engine records; tasklet dispatch must stay disabled.
        vm->RegisterFunction("InspectPackage", services::AIScript, InspectPackage);
        vm->RegisterFunction("SetPackageInterruptFlag", services::AIScript, SetPackageInterruptFlag);
        vm->RegisterFunction("RestorePackageInterruptFlags", services::AIScript, RestorePackageInterruptFlags);
    }
}
