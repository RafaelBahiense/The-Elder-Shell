Scriptname ElderShellActors extends Quest Native Hidden

String Function CleanupPluginFactions(Actor target, String plugin, Faction registration, Bool apply = False) Native

String Function RestorePluginFactions(Actor target, String plugin, Faction registration) Native

String Function InspectPlugin(Actor target, String plugin) Native

String Function InspectPluginFactions(Actor target, String plugin) Native

String Function RemovePluginFactions(Actor target, String plugin) Native

String Function InspectPluginSpells(Actor target, String plugin) Native

String Function RemovePluginSpells(Actor target, String plugin) Native

String Function InspectPluginEffects(Actor target, String plugin) Native

String Function RemovePluginEffects(Actor target, String plugin) Native

String Function InspectPluginEquipment(Actor target, String plugin) Native

String Function RemovePluginEquipment(Actor target, String plugin) Native

String Function InspectPluginInventory(Actor target, String plugin) Native

String Function RemovePluginInventory(Actor target, String plugin) Native

String Function CleanupPlugin(Actor target, String plugin)
    String result = ((Self as Quest) as ElderShellQuests).RemovePlugin(target, plugin)
    result += "\n" + ((Self as Quest) as ElderShellQuests).RemovePluginAliases(target, plugin)
    result += "\n" + ((Self as Quest) as ElderShellScripts).RemovePlugin(target, plugin)
    result += "\n" + ((Self as Quest) as ElderShellAI).RemovePluginPackages(target, plugin)
    result += "\n" + RemovePluginEquipment(target, plugin)
    result += "\n" + RemovePluginEffects(target, plugin)
    result += "\n" + RemovePluginSpells(target, plugin)
    result += "\n" + RemovePluginInventory(target, plugin)
    result += "\n" + RemovePluginFactions(target, plugin)
    Return result + "\nFinal inspection:\n" + InspectPlugin(target, plugin)
EndFunction

String Function InspectDialogue(Actor target) Native
