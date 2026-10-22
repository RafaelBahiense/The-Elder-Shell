Scriptname ElderShellRoot extends Quest Hidden

ElderShellConsole Property Console
    ElderShellConsole Function Get()
        Return (Self as Quest) as ElderShellConsole
    EndFunction
EndProperty

ElderShellSelection Property Selection
    ElderShellSelection Function Get()
        Return (Self as Quest) as ElderShellSelection
    EndFunction
EndProperty

ElderShellForms Property Forms
    ElderShellForms Function Get()
        Return (Self as Quest) as ElderShellForms
    EndFunction
EndProperty

ElderShellActors Property Actors
    ElderShellActors Function Get()
        Return (Self as Quest) as ElderShellActors
    EndFunction
EndProperty

ElderShellAI Property AI
    ElderShellAI Function Get()
        Return (Self as Quest) as ElderShellAI
    EndFunction
EndProperty

ElderShellQuests Property Quests
    ElderShellQuests Function Get()
        Return (Self as Quest) as ElderShellQuests
    EndFunction
EndProperty

ElderShellScripts Property Scripts
    ElderShellScripts Function Get()
        Return (Self as Quest) as ElderShellScripts
    EndFunction
EndProperty
