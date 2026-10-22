#include "FactionCleanup.h"
#include "Services.h"
#include "shell/FactionCleanup.h"
#include "shell/Services.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/E/ExtraFactionChanges.h"
#include "RE/E/ExtraLeveledCreature.h"
#include <map>
#include <set>

namespace elder_shell {
    namespace {
        using faction_cleanup::Entry;

        struct Snapshot {
            RE::ActorHandle actor;
            RE::FormID guard = 0;
            std::vector<Entry> before, after;
            bool applied = false;
        };

        using Key = std::pair<RE::FormID, std::string>;
        std::map<Key, Snapshot> snapshots;
        constexpr std::size_t MaximumSnapshots = 32;

        bool Owned(const RE::TESForm* form, const RE::TESFile* plugin) {
            return form && form->GetFile(0) == plugin;
        }

        bool HasAlias(RE::Actor* actor, const RE::TESFile* plugin) {
            auto* instances = actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (!instances)
                return false;
            RE::BSReadLockGuard lock(instances->lock);
            for (const auto* instance : instances->aliases)
                if (instance && Owned(instance->quest, plugin))
                    return true;
            return false;
        }

        std::vector<Entry> Read(RE::Actor* actor, const RE::TESFile* plugin) {
            std::map<RE::FormID, Entry> entries;
            auto protect = [&](const RE::TESActorBase* base) {
                if (base)
                    for (const auto& value : base->factions)
                        if (Owned(value.faction, plugin))
                            entries.try_emplace(value.faction->GetFormID(),
                                                Entry{value.faction->GetFormID(), value.rank, true, false});
            };
            protect(actor->GetActorBase());
            if (const auto* leveled = actor->extraList.GetByType<RE::ExtraLeveledCreature>()) {
                protect(leveled->originalBase);
                protect(leveled->templateBase);
            }
            if (const auto* extra = actor->extraList.GetByType<RE::ExtraFactionChanges>())
                for (const auto& value : extra->factionChanges) {
                    if (!Owned(value.faction, plugin))
                        continue;
                    auto& row = entries[value.faction->GetFormID()];
                    row.faction = value.faction->GetFormID();
                    row.rank = value.rank;
                    row.changed = true;
                }
            std::vector<Entry> result;
            for (const auto& [id, entry] : entries)
                result.push_back(entry);
            return result;
        }

        std::string Validate(RE::TESQuest* self, RE::Actor* actor, const RE::TESFile* plugin,
                             RE::TESFaction* registration, std::size_t count) {
            if (!self || self != ServiceQuest() || !actor || !plugin || !registration || !Owned(registration, plugin))
                return "Provide an Actor, a loaded plugin filename and its registration Faction through "
                       "ElderShell.Actors.";
            if (!actor->GetActorBase())
                return "Actor base is unavailable; no changes made.";
            if (const auto* extra = actor->extraList.GetByType<RE::ExtraFactionChanges>()) {
                std::set<RE::FormID> seen;
                for (const auto& value : extra->factionChanges)
                    if (Owned(value.faction, plugin) && !seen.insert(value.faction->GetFormID()).second)
                        return "Duplicate faction entries require separate investigation; no changes made.";
            }
            return faction_cleanup::Error(actor == RE::PlayerCharacter::GetSingleton(),
                                          actor->IsInFaction(registration), HasAlias(actor, plugin),
                                          actor->GetCurrentScene() != nullptr, count);
        }

        std::string Describe(RE::Actor* actor, const RE::TESFile* plugin, const std::vector<Entry>& entries) {
            std::size_t remove = 0, keep = 0;
            for (const auto& entry : entries)
                faction_cleanup::Removable(entry) ? ++remove : ++keep;
            std::string text =
                std::format("Actor {:08X}; source: {}\nClear {} runtime entries; preserve {} base-record entries.\n",
                            actor->GetFormID(), plugin->fileName, remove, keep);
            for (const auto& entry : entries) {
                const auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.faction);
                text += std::format(
                    "{} {:08X} {} rank={}\n", faction_cleanup::Removable(entry) ? "CLEAR" : "KEEP", entry.faction,
                    faction && faction->GetFormEditorID() ? faction->GetFormEditorID() : "<no editor ID>",
                    static_cast<int>(entry.rank));
            }
            return text;
        }

        const RE::TESFile* FindPlugin(const std::string& name) {
            auto* data = RE::TESDataHandler::GetSingleton();
            return data ? data->LookupModByName(name) : nullptr;
        }

        std::string CleanupPluginFactions(RE::TESQuest* self, RE::Actor* actor, std::string name,
                                          RE::TESFaction* registration, bool apply) {
            auto* plugin = FindPlugin(name);
            if (const auto error = Validate(self, actor, plugin, registration, 0); !error.empty())
                return error;
            const auto current = Read(actor, plugin);
            if (current.size() > faction_cleanup::MaximumEntries)
                return "Too many faction entries; no changes made.";
            const Key key{actor->GetFormID(), plugin->fileName};
            const auto found = snapshots.find(key);
            if (!apply) {
                if (found != snapshots.end() && found->second.applied)
                    return "An undo snapshot already exists. Restore it or load a save before another cleanup.\n" +
                           Describe(actor, plugin, current);
                if (found == snapshots.end() && snapshots.size() >= MaximumSnapshots)
                    return "Cleanup snapshot limit reached (32 actors/plugins).";
                snapshots[key] = Snapshot{actor->GetHandle(), registration->GetFormID(), current, {}, false};
                return "PREVIEW: no changes made. Pass True as the fourth argument to apply this exact snapshot.\n" +
                       Describe(actor, plugin, current) +
                       "Only actor faction data is covered. External files and other controller data are unchanged.";
            }
            if (found == snapshots.end() || found->second.applied || found->second.actor.get().get() != actor ||
                !faction_cleanup::Matches(found->second.before, current, found->second.guard,
                                          registration->GetFormID()))
                return "A matching, unchanged preview is required. Run CleanupPluginFactions without True first.";
            auto& snapshot = found->second;
            const auto report = Describe(actor, plugin, current);
            for (const auto& entry : current) {
                if (!faction_cleanup::Removable(entry))
                    continue;
                auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.faction);
                // Use the engine removal path for notifications/save dirtiness,
                // then discard its -1 override: this entry had no authored base membership.
                // A negative data rank might otherwise make removal a no-op.
                if (entry.rank < 0)
                    actor->AddToFaction(faction, 0);
                actor->RemoveFromFaction(faction);
                if (auto* extra = actor->extraList.GetByType<RE::ExtraFactionChanges>())
                    for (auto it = extra->factionChanges.begin(); it != extra->factionChanges.end();)
                        if (it->faction == faction)
                            it = extra->factionChanges.erase(it);
                        else
                            ++it;
            }
            snapshot.after = Read(actor, plugin);
            snapshot.applied = true;
            actor->EvaluatePackage(true, false);
            SKSE::log::info("Explicit faction cleanup; saved ranks for session undo:\n{}", report);
            const std::string outcome =
                snapshot.after == faction_cleanup::Retained(current)
                    ? "Cleanup applied and faction data verified."
                    : "Cleanup verification failed. Inspect the actor and use session undo or the earlier save.";
            return outcome + " Undo is available only until the next save load or game restart.\n" + report +
                   "\nSave to retain the cleanup. The plugin may generate or import data again on a later interaction.";
        }

        std::string RestorePluginFactions(RE::TESQuest* self, RE::Actor* actor, std::string name,
                                          RE::TESFaction* registration) {
            auto* plugin = FindPlugin(name);
            if (const auto error = Validate(self, actor, plugin, registration, 0); !error.empty())
                return error;
            const Key key{actor->GetFormID(), plugin->fileName};
            const auto found = snapshots.find(key);
            if (found == snapshots.end() || !found->second.applied || found->second.actor.get().get() != actor)
                return "No applied cleanup snapshot for this actor/plugin in the current session.";
            const auto& snapshot = found->second;
            if (!faction_cleanup::Matches(snapshot.after, Read(actor, plugin), snapshot.guard,
                                          registration->GetFormID()))
                return "Faction state changed since cleanup. Restore refused to preserve newer changes; use the "
                       "earlier save.";
            for (const auto& entry : snapshot.before) {
                if (!faction_cleanup::Removable(entry))
                    continue;
                auto* faction = RE::TESForm::LookupByID<RE::TESFaction>(entry.faction);
                // Allocate/mark through the engine, then restore signed data
                // ranks exactly, including -1 tombstones and negative statistics.
                actor->AddToFaction(faction, entry.rank < 0 ? 0 : entry.rank);
                if (auto* extra = actor->extraList.GetByType<RE::ExtraFactionChanges>())
                    for (auto& value : extra->factionChanges)
                        if (value.faction == faction)
                            value.rank = entry.rank;
            }
            const auto restored = Read(actor, plugin);
            const auto report = Describe(actor, plugin, restored);
            if (restored != snapshot.before) {
                SKSE::log::error("Faction cleanup restore verification failed:\n{}", report);
                return "Restore verification failed; use the earlier save.\n" + report;
            }
            snapshots.erase(found);
            actor->EvaluatePackage(true, false);
            SKSE::log::info("Restored faction cleanup snapshot:\n{}", report);
            return "Restored captured faction ranks.\n" + report;
        }
    }

    void ClearFactionCleanupSnapshots() {
        // Cleanup is a saved actor edit, not a runtime package flag override.
        // A load discards undo state; it must never replay or revert actor edits.
        snapshots.clear();
    }

    void RegisterFactionCleanupServices(RE::BSScript::IVirtualMachine* vm) {
        vm->RegisterFunction("CleanupPluginFactions", services::ActorsScript, CleanupPluginFactions);
        vm->RegisterFunction("RestorePluginFactions", services::ActorsScript, RestorePluginFactions);
    }
}
