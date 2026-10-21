Scriptname ElderShellConsole extends Quest Native Hidden

; Returns output produced during dispatch. Deferred work is not awaited.
; None uses the current Skyrim console selection.
String Function Execute(String command, ObjectReference target = None) Native
