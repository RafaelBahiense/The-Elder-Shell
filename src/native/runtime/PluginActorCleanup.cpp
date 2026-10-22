#include "PluginActorCleanup.h"
#include "Services.h"
#include "compiler/Compiler.h"
#include "shell/PluginActorCleanup.h"
#include "shell/Services.h"
#include "RE/E/ExtraAliasInstanceArray.h"
#include "RE/E/ExtraEnchantment.h"
#include "RE/E/ExtraFactionChanges.h"
#include "RE/E/ExtraLeveledCreature.h"
#include <fstream>
#include <map>

namespace elder_shell {
    namespace {
        using Category = plugin_cleanup::Category;
        using Object = RE::BSTSmartPointer<RE::BSScript::Object>;
        constexpr auto Limit = plugin_cleanup::Limit;

        struct Context {
            RE::Actor* actor;
            const RE::TESFile* plugin;
        };

        Context Resolve(RE::TESQuest* self, RE::Actor* actor, const std::string& name) {
            auto* data = RE::TESDataHandler::GetSingleton();
            const auto* plugin = data ? data->LookupModByName(name) : nullptr;
            if (!self || self != ServiceQuest() || !actor || !plugin)
                throw std::runtime_error(
                    "Provide an Actor and a loaded plugin filename through ElderShell.Diagnostics.");
            if (actor == RE::PlayerCharacter::GetSingleton())
                throw std::runtime_error("Player cleanup is not supported; provide an NPC actor.");
            return {actor, plugin};
        }

        bool Owned(const RE::TESForm* form, const Context& context) {
            return form && form->GetFile(0) == context.plugin;
        }

        bool BaseFactionMembership(RE::Actor* actor, RE::TESFaction* faction) {
            const auto contains = [faction](const RE::TESActorBase* base) {
                if (base)
                    for (const auto& entry : base->factions)
                        if (entry.faction == faction)
                            return true;
                return false;
            };
            if (contains(actor->GetActorBase()))
                return true;
            const auto* leveled = actor->extraList.GetByType<RE::ExtraLeveledCreature>();
            return leveled && (contains(leveled->originalBase) || contains(leveled->templateBase));
        }

        bool Related(const RE::MagicItem* magic, const Context& context) {
            if (!magic)
                return false;
            if (Owned(magic, context))
                return true;
            for (const auto* effect : magic->effects)
                if (effect && Owned(effect->baseEffect, context))
                    return true;
            return false;
        }

        std::string Label(const RE::TESForm* form) {
            if (!form)
                return "NONE";
            const auto* editor = form->GetFormEditorID();
            return std::format("{:08X} {}", form->GetFormID(), editor && *editor ? editor : form->GetName());
        }

        template <class T>
        struct Selection {
            std::vector<T> values;
            bool limited = false;

            void Add(T value) {
                if (values.size() >= Limit)
                    limited = true;
                else
                    values.push_back(std::move(value));
            }
        };

        struct Report {
            std::string text;
            bool remove;

            Report(const Context& context, std::string_view category, bool apply)
                : text(std::format("{}: actor {:08X}; plugin {}; {}\n", category, context.actor->GetFormID(),
                                   context.plugin->fileName, apply ? "REMOVE" : "INSPECT")),
                  remove(apply) {}

            void Row(std::string_view row) {
                text += std::string(row) + "\n";
            }

            bool Check(bool limited) {
                if (limited)
                    Row("LIMIT: snapshot exceeds 1024 entries; this category was not changed.");
                return !limited;
            }
        };

        struct Alias {
            RE::TESQuest* quest;
            RE::BGSRefAlias* alias;
        };

        Selection<Alias> Aliases(const Context& context) {
            Selection<Alias> result;
            auto* extra = context.actor->extraList.GetByType<RE::ExtraAliasInstanceArray>();
            if (!extra)
                return result;
            std::vector<std::pair<RE::TESQuest*, const RE::BGSBaseAlias*>> copied;
            {
                RE::BSReadLockGuard lock(extra->lock);
                for (const auto* instance : extra->aliases) {
                    if (!instance || !Owned(instance->quest, context))
                        continue;
                    if (copied.size() == Limit) {
                        result.limited = true;
                        break;
                    }
                    copied.emplace_back(instance->quest, instance->alias);
                }
            }
            for (auto [quest, alias] : copied) {
                // Find the live mutable alias through its owning quest, outside
                // the reference's alias lock. Never write an alias array directly.
                for (auto* candidate : quest->aliases)
                    if (candidate == alias)
                        if (auto* reference = skyrim_cast<RE::BGSRefAlias*>(candidate);
                            reference && reference->GetActorReference() == context.actor)
                            result.Add({quest, reference});
            }
            return result;
        }

        Selection<RE::TESQuest*> Quests(const Context& context) {
            const auto aliases = Aliases(context);
            Selection<RE::TESQuest*> result;
            result.limited = aliases.limited;
            std::set<RE::TESQuest*> seen;
            for (const auto& value : aliases.values)
                if (seen.insert(value.quest).second)
                    result.Add(value.quest);
            return result;
        }

        std::string InspectAliases(const Context& context) {
            Report report(context, "Aliases", false);
            const auto selected = Aliases(context);
            report.Check(selected.limited);
            for (const auto& value : selected.values)
                report.Row(std::format("{} alias={} {}; actor link can be cleared through ReferenceAlias.Clear()",
                                       Label(value.quest), value.alias->aliasID, value.alias->aliasName.c_str()));
            if (selected.values.empty())
                report.Row("No matching actor aliases.");
            return report.text;
        }

        std::string InspectQuests(const Context& context) {
            Report report(context, "Quests", false);
            const auto selected = Quests(context);
            report.Check(selected.limited);
            for (const auto* quest : selected.values)
                report.Row(std::format("{} running={} stopping={} stopped={}; stopping affects the entire quest",
                                       Label(quest), quest->IsRunning(), quest->IsStopping(), quest->IsStopped()));
            report.Row("Scope: plugin-defined quests currently binding this actor through an alias.");
            return report.text;
        }

        std::string ReadFile(const std::filesystem::path& path, std::uintmax_t maximum) {
            if (std::filesystem::file_size(path) > maximum)
                throw std::runtime_error("Index or plugin exceeds the inspection file-size limit.");
            std::ifstream input(path, std::ios::binary);
            if (!input)
                throw std::runtime_error("Cannot read script index/plugin file.");
            return {std::istreambuf_iterator<char>(input), {}};
        }

        plugin_cleanup::ScriptIndex ScriptIndex(const Context& context) {
            // File identity is checked again when either file changes. No engine
            // objects, VM instances or actor state are kept in this cache.
            struct Cached {
                std::filesystem::file_time_type pluginTime, indexTime;
                std::uintmax_t pluginSize, indexSize;
                plugin_cleanup::ScriptIndex index;
            };

            static std::map<std::string, Cached> cache;
            try {
                const auto pluginPath = std::filesystem::path("Data") / context.plugin->fileName;
                const auto indexPath = std::filesystem::path("Data/SKSE/Plugins/TheElderShell/PluginScripts") /
                                       (std::string(context.plugin->fileName) + ".tsv");
                const auto pluginTime = std::filesystem::last_write_time(pluginPath);
                const auto indexTime = std::filesystem::last_write_time(indexPath);
                const auto pluginSize = std::filesystem::file_size(pluginPath);
                const auto indexSize = std::filesystem::file_size(indexPath);
                const auto key = plugin_cleanup::Lower(context.plugin->fileName);
                if (const auto found = cache.find(key);
                    found != cache.end() && found->second.pluginTime == pluginTime &&
                    found->second.indexTime == indexTime && found->second.pluginSize == pluginSize &&
                    found->second.indexSize == indexSize)
                    return found->second.index;
                auto index = plugin_cleanup::ParseIndex(ReadFile(indexPath, 4 * 1024 * 1024), context.plugin->fileName,
                                                        ContentDigest(ReadFile(pluginPath, 256 * 1024 * 1024)));
                if (cache.size() >= 32)
                    cache.clear();
                cache.insert_or_assign(key, Cached{pluginTime, indexTime, pluginSize, indexSize, index});
                return index;
            } catch (const std::exception&) {
                return {
                    {},
                    "Script index unavailable. Generate a matching PluginScripts/<plugin>.tsv with inspect-scripts."};
            }
        }

        Selection<Object> ActorScripts(const Context& context) {
            Selection<Object> result;
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            auto* policy = vm ? vm->GetObjectHandlePolicy() : nullptr;
            if (!policy)
                throw std::runtime_error("VM handle policy unavailable.");
            const auto handle = policy->GetHandleForObject(RE::FormType::ActorCharacter, context.actor);
            {
                RE::BSSpinLockGuard lock(vm->attachedScriptsLock);
                const auto found = vm->attachedScripts.find(handle);
                if (found != vm->attachedScripts.end())
                    for (const auto& object : found->second)
                        if (object)
                            result.Add(Object{object.get()});
            }
            return result;
        }

        std::string Scripts(const Context& context, bool remove) {
            Report report(context, "Scripts", remove);
            const auto index = ScriptIndex(context);
            const auto selected = ActorScripts(context);
            if (!report.Check(selected.limited))
                return report.text;
            if (!index.error.empty())
                report.Row(index.error);
            auto* vm = RE::BSScript::Internal::VirtualMachine::GetSingleton();
            for (const auto& object : selected.values) {
                const auto* type = object->GetTypeInfo();
                if (!type)
                    continue;
                const std::string name = type->GetName();
                if (!index.Contains(name)) {
                    report.Row("UNATTRIBUTED " + name + "; retained");
                    continue;
                }
                if (remove) {
                    vm->UnbindObject(object);
                    Object remaining;
                    const auto handle =
                        vm->GetObjectHandlePolicy()->GetHandleForObject(RE::FormType::ActorCharacter, context.actor);
                    const bool bound = vm->FindBoundObject(handle, name.c_str(), remaining) && remaining;
                    report.Row(std::format("{} {}; indexed declaration match", bound ? "REMAINS" : "UNBOUND", name));
                } else {
                    report.Row("MATCH " + name + "; declared on a record owned by the selected plugin");
                }
            }
            report.Row(
                "Scope: direct actor script bindings. Unbinding does not terminate running/suspended calls or remove "
                "external event registrations.");
            return report.text;
        }

        Selection<RE::SpellItem*> Spells(const Context& context) {
            Selection<RE::SpellItem*> result;

            struct Visitor final : RE::Actor::ForEachSpellVisitor {
                const Context& context;
                Selection<RE::SpellItem*>& result;
                std::set<RE::SpellItem*> seen;

                Visitor(const Context& value, Selection<RE::SpellItem*>& output) : context(value), result(output) {}

                RE::BSContainer::ForEachResult Visit(RE::SpellItem* spell) override {
                    if (Related(spell, context) && seen.insert(spell).second)
                        result.Add(spell);
                    return result.limited ? RE::BSContainer::ForEachResult::kStop
                                          : RE::BSContainer::ForEachResult::kContinue;
                }
            } visitor(context, result);

            context.actor->VisitSpells(visitor);
            return result;
        }

        std::string SpellReport(const Context& context, bool remove) {
            Report report(context, "Spells", remove);
            const auto selected = Spells(context);
            if (!report.Check(selected.limited))
                return report.text;
            for (auto* spell : selected.values) {
                const auto label = Label(spell);
                if (remove) {
                    context.actor->DeselectSpell(spell);
                    context.actor->RemoveSpell(spell);
                    report.Row(std::string(context.actor->HasSpell(spell) ? "REMAINS " : "REMOVED ") + label);
                } else {
                    report.Row("MATCH " + label);
                }
            }
            if (selected.values.empty())
                report.Row("No matching spells.");
            report.Row("Uses Actor.RemoveSpell; no direct edits to shared race/NPC spell lists.");
            return report.text;
        }

        std::string Effects(const Context& context, bool remove) {
            Report report(context, "Effects", remove);
            Selection<RE::ActiveEffect*> selected;
            auto* list = context.actor->AsMagicTarget()->GetActiveEffectList();
            if (list)
                for (auto* effect : *list)
                    if (effect && (Owned(effect->GetBaseObject(), context) || Owned(effect->spell, context) ||
                                   Owned(effect->source, context)))
                        selected.Add(effect);
            if (!report.Check(selected.limited))
                return report.text;
            // Dispel can invalidate effect instances. Never read an instance
            // after its own Dispel call; snapshot descriptions before mutations.
            std::vector<std::pair<RE::ActiveEffect*, std::string>> descriptions;
            for (auto* effect : selected.values) {
                const auto label = std::format("{}; spell {}; source {}", Label(effect->GetBaseObject()),
                                               Label(effect->spell), Label(effect->source));
                descriptions.emplace_back(effect, label);
            }
            for (const auto& [effect, label] : descriptions) {
                if (remove) {
                    auto* current = context.actor->AsMagicTarget()->GetActiveEffectList();
                    if (!current || std::ranges::find(*current, effect) == current->end()) {
                        report.Row("ALREADY GONE " + label);
                        continue;
                    }
                    effect->Dispel(true);
                }
                report.Row((remove ? "DISPEL REQUESTED " : "MATCH ") + label);
            }
            if (selected.values.empty())
                report.Row("No matching active effects.");
            report.Row(
                "Effect teardown can complete later or be reapplied by another producer; inspect again afterward.");
            return report.text;
        }

        bool BaseItemRelated(RE::TESBoundObject* item, const Context& context) {
            if (Owned(item, context))
                return true;
            auto* enchantable = skyrim_cast<RE::TESEnchantableForm*>(item);
            return enchantable && Related(enchantable->formEnchanting, context);
        }

        bool InstanceRelated(RE::ExtraDataList* extra, const Context& context) {
            const auto* enchantment = extra ? extra->GetByType<RE::ExtraEnchantment>() : nullptr;
            return enchantment && Related(enchantment->enchantment, context);
        }

        struct Item {
            RE::TESBoundObject* object;
            RE::ExtraDataList* extra;
            std::int32_t count;
            bool worn;
        };

        Selection<Item> Items(const Context& context, bool equipment) {
            Selection<Item> result;
            for (const auto& [object, value] : context.actor->GetInventory([](auto&) { return true; }, true)) {
                const auto& [count, entry] = value;
                if (!object || !entry || count <= 0)
                    continue;
                const bool whole = BaseItemRelated(object, context);
                if (whole && !equipment) {
                    result.Add({object, nullptr, count, entry->IsWorn()});
                    continue;
                }
                if (entry->extraLists)
                    for (auto* extra : *entry->extraLists) {
                        if (!extra || (!whole && !InstanceRelated(extra, context)))
                            continue;
                        const bool worn =
                            extra->HasType(RE::ExtraDataType::kWorn) || extra->HasType(RE::ExtraDataType::kWornLeft);
                        if (!equipment || worn)
                            result.Add({object, extra, std::min(count, extra->GetCount()), worn});
                    }
            }
            return result;
        }

        std::int32_t ItemCount(RE::Actor* actor, RE::TESBoundObject* object) {
            const auto inventory = actor->GetInventoryCounts();
            const auto found = inventory.find(object);
            return found == inventory.end() ? 0 : found->second;
        }

        std::string Inventory(const Context& context, bool remove, bool equipment) {
            Report report(context, equipment ? "Equipment" : "Inventory", remove);
            const auto selected = Items(context, equipment);
            if (!report.Check(selected.limited))
                return report.text;
            auto* manager = RE::ActorEquipManager::GetSingleton();
            for (const auto& item : selected.values) {
                const auto label = std::format("{} count={} worn={}{}", Label(item.object), item.count, item.worn,
                                               item.extra ? "; matching instance" : "; matching base item");
                if (!remove) {
                    report.Row("MATCH " + label);
                } else if (equipment) {
                    const bool accepted =
                        manager && manager->UnequipObject(context.actor, item.object, item.extra, item.count, nullptr,
                                                          false, true, false, true);
                    report.Row(std::string(accepted ? "UNEQUIP REQUESTED " : "UNEQUIP REJECTED ") + label);
                } else {
                    const auto before = ItemCount(context.actor, item.object);
                    context.actor->RemoveItem(item.object, item.count, RE::ITEM_REMOVE_REASON::kRemove, item.extra,
                                              nullptr);
                    const auto after = ItemCount(context.actor, item.object);
                    report.Row(std::format("REMOVED {} of {}: {}", std::max(0, before - after), item.count, label));
                }
            }
            if (selected.values.empty())
                report.Row("No matching items.");
            if (equipment) {
                // Deselect matching hand spells too. Inventory removal is separate.
                std::set<RE::SpellItem*> spells;
                for (auto* magic : context.actor->GetActorRuntimeData().selectedSpells)
                    if (Related(magic, context))
                        if (auto* spell = magic->As<RE::SpellItem>())
                            spells.insert(spell);
                for (auto* spell : spells) {
                    const auto label = Label(spell);
                    if (remove)
                        context.actor->DeselectSpell(spell);
                    report.Row((remove ? "DESELECT REQUESTED " : "EQUIPPED SPELL ") + label);
                }
            } else {
                report.Row(
                    "Removed inventory is deleted, not transferred. Extra-enchantment matches select only their own "
                    "instances.");
            }
            return report.text;
        }

        std::string Factions(const Context& context, bool remove) {
            Report report(context, "Factions", remove);
            std::set<RE::TESFaction*> factions;
            context.actor->VisitFactions([&](auto* faction, auto) {
                if (Owned(faction, context))
                    factions.insert(faction);
                return factions.size() > Limit;
            });
            if (!report.Check(factions.size() > Limit))
                return report.text;
            for (auto* faction : factions) {
                const bool authored = BaseFactionMembership(context.actor, faction);
                const auto rank = context.actor->GetFactionRank(faction, false);
                if (!remove) {
                    report.Row(std::format("MATCH {} rank={} baseMembership={}", Label(faction), rank, authored));
                    continue;
                }
                if (rank < 0)
                    context.actor->AddToFaction(faction, 0);
                context.actor->RemoveFromFaction(faction);
                if (!authored)
                    if (auto* extra = context.actor->extraList.GetByType<RE::ExtraFactionChanges>())
                        for (auto it = extra->factionChanges.begin(); it != extra->factionChanges.end();)
                            if (it->faction == faction)
                                it = extra->factionChanges.erase(it);
                            else
                                ++it;
                report.Row(std::format("{} {}{}", context.actor->IsInFaction(faction) ? "REMAINS" : "REMOVED",
                                       Label(faction),
                                       authored ? "; base record unchanged, membership masked on actor" : ""));
            }
            if (remove)
                context.actor->EvaluatePackage(true, false);
            if (factions.empty())
                report.Row("No matching factions.");
            return report.text;
        }

        std::string Packages(const Context& context, bool remove) {
            Report report(context, "Packages", remove);
            const auto* process = context.actor->GetActorRuntimeData().currentProcess;
            auto* interrupt = process && process->middleHigh ? process->middleHigh->runOncePackage.package : nullptr;
            if (Owned(interrupt, context)) {
                const auto label = Label(interrupt);
                if (remove)
                    context.actor->EndInterruptPackage(true);
                report.Row((remove ? "INTERRUPT END REQUESTED " : "INTERRUPT ") + label);
            }
            auto* current = context.actor->GetCurrentPackage();
            if (Owned(current, context))
                report.Row("CURRENT " + Label(current) + "; may be supplied by an alias or authored base list");
            if (remove)
                context.actor->EvaluatePackage(true, false);
            report.Row(
                "Authored base package lists are not edited. Clear matching aliases and stop their quests to remove "
                "those producers.");
            return report.text;
        }

        std::string Run(Category category, const Context& context, bool remove) {
            switch (category) {
                case Category::Quests:
                    return InspectQuests(context);
                case Category::Aliases:
                    return InspectAliases(context);
                case Category::Scripts:
                    return Scripts(context, remove);
                case Category::Packages:
                    return Packages(context, remove);
                case Category::Equipment:
                    return Inventory(context, remove, true);
                case Category::Effects:
                    return Effects(context, remove);
                case Category::Spells:
                    return SpellReport(context, remove);
                case Category::Inventory:
                    return Inventory(context, remove, false);
                case Category::Factions:
                    return Factions(context, remove);
            }
            return {};
        }

        template <Category Kind, bool Remove>
        std::string CategoryService(RE::TESQuest* self, RE::Actor* actor, std::string plugin) {
            try {
                const auto text = Run(Kind, Resolve(self, actor, plugin), Remove);
                if constexpr (Remove)
                    SKSE::log::info("Explicit actor/plugin cleanup:\n{}", text);
                return text;
            } catch (const std::exception& error) {
                return std::string("ERROR: ") + error.what();
            }
        }

        std::string InspectPluginActor(RE::TESQuest* self, RE::Actor* actor, std::string plugin) {
            try {
                const auto context = Resolve(self, actor, plugin);
                std::string text;
                for (auto kind : plugin_cleanup::Categories)
                    text += Run(kind, context, false) + "\n";
                return text;
            } catch (const std::exception& error) {
                return std::string("ERROR: ") + error.what();
            }
        }

        std::vector<RE::TESQuest*> CollectQuests(RE::TESQuest* self, RE::Actor* actor, std::string plugin) {
            try {
                const auto selected = Quests(Resolve(self, actor, plugin));
                return selected.limited ? std::vector<RE::TESQuest*>{} : selected.values;
            } catch (const std::exception&) {
                return {};
            }
        }

        std::vector<RE::BGSRefAlias*> CollectAliases(RE::TESQuest* self, RE::Actor* actor, std::string plugin) {
            std::vector<RE::BGSRefAlias*> result;
            try {
                const auto selected = Aliases(Resolve(self, actor, plugin));
                if (!selected.limited)
                    for (const auto& value : selected.values)
                        result.push_back(value.alias);
            } catch (const std::exception&) {
            }
            return result;
        }
    }

    void RegisterPluginActorCleanupServices(RE::BSScript::IVirtualMachine* vm) {
        constexpr auto script = services::DiagnosticsScript;
        vm->RegisterFunction("InspectPluginActor", script, InspectPluginActor);
        vm->RegisterFunction("InspectPluginQuests", script, CategoryService<Category::Quests, false>);
        vm->RegisterFunction("InspectPluginAliases", script, CategoryService<Category::Aliases, false>);
        vm->RegisterFunction("CollectPluginActorQuests", script, CollectQuests);
        vm->RegisterFunction("CollectPluginActorAliases", script, CollectAliases);
        vm->RegisterFunction("InspectPluginScripts", script, CategoryService<Category::Scripts, false>);
        vm->RegisterFunction("RemovePluginScripts", script, CategoryService<Category::Scripts, true>);
        vm->RegisterFunction("InspectPluginPackages", script, CategoryService<Category::Packages, false>);
        vm->RegisterFunction("RemovePluginPackages", script, CategoryService<Category::Packages, true>);
        vm->RegisterFunction("InspectPluginEquipment", script, CategoryService<Category::Equipment, false>);
        vm->RegisterFunction("RemovePluginEquipment", script, CategoryService<Category::Equipment, true>);
        vm->RegisterFunction("InspectPluginEffects", script, CategoryService<Category::Effects, false>);
        vm->RegisterFunction("RemovePluginEffects", script, CategoryService<Category::Effects, true>);
        vm->RegisterFunction("InspectPluginSpells", script, CategoryService<Category::Spells, false>);
        vm->RegisterFunction("RemovePluginSpells", script, CategoryService<Category::Spells, true>);
        vm->RegisterFunction("InspectPluginInventory", script, CategoryService<Category::Inventory, false>);
        vm->RegisterFunction("RemovePluginInventory", script, CategoryService<Category::Inventory, true>);
        vm->RegisterFunction("InspectPluginFactions", script, CategoryService<Category::Factions, false>);
        vm->RegisterFunction("RemovePluginFactions", script, CategoryService<Category::Factions, true>);
    }
}
