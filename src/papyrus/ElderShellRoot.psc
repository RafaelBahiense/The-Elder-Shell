Scriptname ElderShellRoot extends Quest Hidden

ElderShellConsole Property Console
    ElderShellConsole Function Get()
        Return (Self as Quest) as ElderShellConsole
    EndFunction
EndProperty

ElderShellDiagnostics Property Diagnostics
    ElderShellDiagnostics Function Get()
        Return (Self as Quest) as ElderShellDiagnostics
    EndFunction
EndProperty
