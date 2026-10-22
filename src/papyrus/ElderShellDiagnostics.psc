Scriptname ElderShellDiagnostics extends Quest Native Hidden

String Function CleanupPluginFactions(Actor target, String plugin, Faction registration, Bool apply = False) Native
String Function RestorePluginFactions(Actor target, String plugin, Faction registration) Native

String Function InspectPluginActor(Actor target, String plugin) Native
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
String Function InspectPluginScripts(Actor target, String plugin) Native
String Function RemovePluginScripts(Actor target, String plugin) Native
String Function InspectPluginPackages(Actor target, String plugin) Native
String Function RemovePluginPackages(Actor target, String plugin) Native
String Function InspectPluginQuests(Actor target, String plugin) Native
String Function InspectPluginAliases(Actor target, String plugin) Native

Quest[] Function CollectPluginActorQuests(Actor target, String plugin) Native
ReferenceAlias[] Function CollectPluginActorAliases(Actor target, String plugin) Native

String Function RemovePluginQuests(Actor target, String plugin)
    String before = InspectPluginQuests(target, plugin)
    Quest[] targets = CollectPluginActorQuests(target, plugin)
    Int i = 0
    While i < targets.Length
        targets[i].Stop()
        i += 1
    EndWhile
    Return before + "\nStop requested for " + i + " quests.\nAfter:\n" + InspectPluginQuests(target, plugin)
EndFunction

String Function RemovePluginAliases(Actor target, String plugin)
    String before = InspectPluginAliases(target, plugin)
    ReferenceAlias[] targets = CollectPluginActorAliases(target, plugin)
    Int i = 0
    While i < targets.Length
        targets[i].Clear()
        i += 1
    EndWhile
    Return before + "\nClear called for " + i + " aliases.\nAfter:\n" + InspectPluginAliases(target, plugin)
EndFunction

String Function CleanupPluginActor(Actor target, String plugin)
    String result = RemovePluginQuests(target, plugin)
    result += "\n" + RemovePluginAliases(target, plugin)
    result += "\n" + RemovePluginScripts(target, plugin)
    result += "\n" + RemovePluginPackages(target, plugin)
    result += "\n" + RemovePluginEquipment(target, plugin)
    result += "\n" + RemovePluginEffects(target, plugin)
    result += "\n" + RemovePluginSpells(target, plugin)
    result += "\n" + RemovePluginInventory(target, plugin)
    result += "\n" + RemovePluginFactions(target, plugin)
    Return result + "\nFinal inspection:\n" + InspectPluginActor(target, plugin)
EndFunction

String Function InspectQuest(Quest target) Native
String Function InspectActorDialogue(Actor target) Native

String Function InspectPackage(Package target) Native
String Function SetPackageInterruptFlag(Package target, String flag, Bool enabled) Native
String Function RestorePackageInterruptFlags(Package target) Native

; Returns an error or an empty string when it is safe to call Start.
; This native performs the explicitly requested alreadyRun change.
String Function PrepareRestart(Quest target, Bool clearAlreadyRun) Native

String Function RestartQuest(Quest target, Bool clearAlreadyRun = False)
    String before = InspectQuest(target)
    String error = PrepareRestart(target, clearAlreadyRun)
    If error != ""
        Return error + "\nBefore:\n" + before
    EndIf
    Bool started = target.Start()
    Return "Before:\n" + before + "\nQuest.Start() returned: " + started + "\nAfter:\n" + InspectQuest(target)
EndFunction
