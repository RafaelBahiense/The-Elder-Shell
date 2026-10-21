Scriptname ElderShellDiagnostics extends Quest Native Hidden

String Function InspectQuest(Quest target) Native

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
