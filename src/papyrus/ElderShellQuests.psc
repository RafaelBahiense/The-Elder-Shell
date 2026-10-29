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

String Function InspectLifecycle(Quest target) Native
String Function InspectStartWaiters(Quest target) Native
String Function InspectStages(Quest target) Native
String Function InspectAliases(Quest target) Native
String Function InspectDialogue(Actor speaker, Quest target) Native
String Function InspectStorySelection(Quest target) Native
String Function InspectDependencies(Quest target) Native
String Function InspectScene(Scene target) Native

String Function Capture(Quest target) Native
String Function ReadSnapshot(String snapshot) Native
String Function Compare(Quest target, String snapshot) Native
String Function ClearSnapshots() Native

String Function InspectRepair(String plan) Native
String Function ClearRepairs() Native
String Function PreviewFailPendingStart(Quest target, Int stackID) Native
String Function FailPendingStart(Quest target, Int stackID, String plan) Native
String Function PreviewRestart(Quest target, Bool resetStages = False, Bool clearAlreadyRun = False) Native
String Function PrepareRestartChecked(Quest target, String plan, Bool resetStages, Bool clearAlreadyRun) Native

String Function RestartChecked(Quest target, String plan, Bool resetStages = False, Bool clearAlreadyRun = False)
    String before = InspectLifecycle(target)
    String error = PrepareRestartChecked(target, plan, resetStages, clearAlreadyRun)
    If error != ""
        Return error + "\nBefore:\n" + before
    EndIf
    If resetStages
        target.Reset()
    EndIf
    Bool started = target.Start()
    Return "Before:\n" + before + "\nQuest.Start() returned: " + started + "\nAfter:\n" + InspectLifecycle(target)
EndFunction

String Function PreviewRebindAlias(Quest target, Int aliasID, ObjectReference replacement) Native
String Function PrepareRebindAlias(Quest target, Int aliasID, ObjectReference replacement, String plan) Native

String Function RebindAlias(Quest target, Int aliasID, ObjectReference replacement, String plan)
    String before = InspectAliases(target)
    String error = PrepareRebindAlias(target, aliasID, replacement, plan)
    If error != ""
        Return error + "\nBefore:\n" + before
    EndIf
    ReferenceAlias binding = target.GetAlias(aliasID) as ReferenceAlias
    If binding == None
        Return "Alias disappeared before dispatch; no binding requested."
    EndIf
    binding.ForceRefTo(replacement)
    Return "Before:\n" + before + "\nForceRefTo returned; verify the binding below.\nAfter:\n" + InspectAliases(target)
EndFunction

String Function PreviewScene(Scene target, Bool start) Native
String Function PrepareScene(Scene target, Bool start, String plan) Native

String Function ChangeScene(Scene target, Bool start, String plan)
    String before = InspectScene(target)
    String error = PrepareScene(target, start, plan)
    If error != ""
        Return error + "\nBefore:\n" + before
    EndIf
    If start
        target.Start()
    Else
        target.Stop()
    EndIf
    Return "Before:\n" + before + "\nScene request returned; deferred effects may remain.\nAfter:\n" + InspectScene(target)
EndFunction

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
