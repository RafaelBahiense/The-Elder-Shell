Scriptname ElderShellQuests extends Quest Native Hidden

String Function InspectPlugin(Actor target, String plugin) Native

String Function InspectPluginAliases(Actor target, String plugin) Native

Quest[] Function CollectPluginQuests(Actor target, String plugin) Native

ReferenceAlias[] Function CollectPluginAliases(Actor target, String plugin) Native

String Function RemovePlugin(Actor target, String plugin)
    String before = InspectPlugin(target, plugin)
    Quest[] targets = CollectPluginQuests(target, plugin)
    Int i = 0
    While i < targets.Length
        targets[i].Stop()
        i += 1
    EndWhile
    Return before + "\nStop requested for " + i + " quests.\nAfter:\n" + InspectPlugin(target, plugin)
EndFunction

String Function RemovePluginAliases(Actor target, String plugin)
    String before = InspectPluginAliases(target, plugin)
    ReferenceAlias[] targets = CollectPluginAliases(target, plugin)
    Int i = 0
    While i < targets.Length
        targets[i].Clear()
        i += 1
    EndWhile
    Return before + "\nClear called for " + i + " aliases.\nAfter:\n" + InspectPluginAliases(target, plugin)
EndFunction

String Function Inspect(Quest target) Native

String Function PrepareRestart(Quest target, Bool clearAlreadyRun) Native

String Function Restart(Quest target, Bool clearAlreadyRun = False)
    String before = Inspect(target)
    String error = PrepareRestart(target, clearAlreadyRun)
    If error != ""
        Return error + "\nBefore:\n" + before
    EndIf
    Bool started = target.Start()
    Return "Before:\n" + before + "\nQuest.Start() returned: " + started + "\nAfter:\n" + Inspect(target)
EndFunction
