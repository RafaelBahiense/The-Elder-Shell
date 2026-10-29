Scriptname ElderShellScripts extends Quest Native Hidden

String Function InspectPlugin(Actor target, String plugin) Native

String Function RemovePlugin(Actor target, String plugin) Native

String Function InspectStacks(Form owner = None, String script = "") Native
String Function InspectStack(Int stackID) Native
String Function InspectLatentCall(Int stackID) Native
String Function InspectInstance(Form owner, String script = "") Native
String Function InspectAliasInstance(Quest owner, Int aliasID, String script = "") Native

; Returns a reviewable Multiline block. Assignment uses normal Papyrus accessors.
String Function PreviewPropertyRestore(Form owner, String script, String propertyName, String expectedExpression, String replacementExpression) Native
Bool Function CheckPropertyRestore(Form owner, String script, String propertyName, String plan) Native
