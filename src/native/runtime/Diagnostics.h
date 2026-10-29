#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace elder_shell::diagnostics {
    constexpr std::size_t Rows = 128;
    constexpr std::size_t TextLimit = 32768;
    bool Valid(RE::TESQuest* self);
    std::string Label(const RE::TESForm* form);
    std::string Conditions(const RE::TESCondition* conditions, RE::TESQuest* quest, RE::TESObjectREFR* subject,
                           RE::TESObjectREFR* target);
    std::string Lifecycle(RE::TESQuest* quest);
    std::string Stages(RE::TESQuest* quest);
    std::string Aliases(RE::TESQuest* quest);
    std::string Scene(RE::BGSScene* scene);
    std::string Instance(RE::TESForm* owner, const std::string& script);
    std::string AliasInstance(RE::BGSBaseAlias* alias, const std::string& script, bool summary = false);
    std::string Stacks(RE::TESForm* owner, const std::string& script = {});
    std::string Stack(std::uint32_t id, bool latentOnly);
    std::string QuestState(RE::TESQuest* quest);
    std::string Guard(RE::TESQuest* quest);
    bool HasOwnerExecution(RE::TESForm* owner);
    std::vector<RE::BSTSmartPointer<RE::BSScript::Object>> Attached(RE::TESForm* owner);
    std::string PropertySource(RE::TESForm* owner, const std::string& script, const std::string& property,
                               const std::string& expected, const std::string& replacement);
    bool CheckPropertyPlan(RE::TESQuest* self, RE::TESForm* owner, std::string script, std::string property,
                           std::string plan);
    void RegisterQuestDiagnostics(RE::BSScript::IVirtualMachine* vm);
    void RegisterScriptDiagnostics(RE::BSScript::IVirtualMachine* vm);
    void RegisterRecoveryServices(RE::BSScript::IVirtualMachine* vm);
    void RegisterStartRecovery(RE::BSScript::IVirtualMachine* vm);
    std::string RejectRepair(std::string error);
    std::string RetainRepair(std::string operation, std::string identity, std::string expected,
                             std::string description);
    std::string ConsumeRepair(std::string_view token, std::string_view operation, std::string_view identity,
                              std::string_view expected);
    void Clear();
}
