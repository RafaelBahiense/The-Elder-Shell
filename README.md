# The Elder Shell

An in-game Papyrus shell for investigating Skyrim and its mods. Inspect live
objects, call installed script APIs, and make targeted changes without creating
a separate test mod or restarting Skyrim for each check.

## What you can do

| Task | Tools |
| --- | --- |
| Inspect a mod's live state | Find attached script instances, read properties and call their functions |
| Examine saved collections | Follow actor references and inspect arrays or data exposed by installed mod APIs |
| Identify an NPC or object | Get the console selection or crosshair target; inspect FormIDs, base forms and defining plugins |
| Investigate dialogue and AI | Inspect dialogue permissions, actor state, current packages and interruption flags |
| Diagnose and recover quests | Inspect lifecycle, aliases, conditions and paused script stacks; compare snapshots and preview targeted repairs |
| Clean up NPC data | Inspect and remove supported plugin-related factions, spells, effects, items, aliases and script bindings |
| Use Skyrim's console from Papyrus | Execute console commands and store their immediate output in variables |

Use it to test an API while developing a mod, find which reference a controller
tracks, or inspect a quest before choosing a recovery action. It works with
existing objects and script instances in the running game.

## Get started

Open **SKSE Menu Framework → The Elder Shell → Console** and run one statement
at a time:

```papyrus
var player = Game.GetPlayer()
player.GetLevel()
player.GetActorValue("Health")
player.IsInCombat()
```

Results appear in the transcript. `var` infers the type; ordinary Papyrus type
declarations work too. Variables last until you load a save or start a new game.

Resolve known forms with `Game.GetFormFromFile()` or runtime references with
`Game.GetFormEx()`, then cast them to their Papyrus type to call methods.

The **Instances** tab finds existing attached scripts and stores them in variables.
The **Variables** tab shows values and arrays, with explicit controls for running
property getters. Member completion and parameter hints help explore available APIs.

For installed scripts, supply every function argument, including optional ones.

Use **Up/Down** to select a suggestion, **Tab/Enter** to accept it, **Escape** to
dismiss the list and **Ctrl+Space** to reopen it. With suggestions closed,
single-line **Up/Down** recalls commands. Transcript output can be selected and copied.

Enable **Multiline** for several statements, `If`/`While` blocks and comments.
**Enter** inserts a newline; **Ctrl+Enter** runs the fragment. Store results in
variables to inspect them afterward.

```papyrus
String combatState = "Out of combat"
If player.IsInCombat()
    combatState = "In combat"
EndIf
```

After the fragment completes, enter `combatState` in single-line mode to read it.

## Built-in services

`ElderShell` groups services under `Selection`, `Forms`, `Actors`, `AI`, `Quests`,
`Scripts` and `Console`. Inspection is separate from explicitly named actions such
as `Set`, `Remove`, `Cleanup`, `Restore` and `Restart`.

To investigate an NPC, select it in Skyrim's console, then return to Elder Shell:

```papyrus
var npc = ElderShell.Selection.GetConsoleActor()
ElderShell.Forms.Inspect(npc)
ElderShell.Forms.Inspect(npc.GetActorBase())
ElderShell.Actors.InspectDialogue(npc)
```

Check that `npc` contains an actor before calling its methods. The packaged
services expose their optional argument defaults.

See [quest and script diagnostics](docs/quest-diagnostics.md) for live stack
inspection, snapshot comparisons and checked recovery operations.

Capture Skyrim console output in a variable:

```papyrus
var output = ElderShell.Console.Execute("player.getavinfo health")
output
```

Console execution captures immediate output; it does not wait for work scheduled
by the command.

Calls execute in the real Papyrus VM. Property getters can run script code, and
changes can persist in your save. Use a separate save before recovery or cleanup;
there is no general undo. Plugin cleanup covers specific actor data, not a complete
mod uninstall.

See the [usage guide](docs/usage.md) for service functions, instance lookup,
properties, arrays, session commands and execution limits.

## Requirements

| Requirement | What to install |
| --- | --- |
| [SKSE](https://skse.silverlock.org/) | The build matching your Skyrim executable version. Launch the game through SKSE. |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | The package covering your Skyrim executable version. |
| [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) **3.9+** | The in-game interface used by the shell. Install the runtime mod, not the developer headers. |

Tested on Skyrim **1.6.1170**. Other versions can be tried, but compatibility is
unverified. Match dependencies to your executable version, rather than the
Special Edition / Anniversary Edition store label.

Install the complete release ZIP through your mod manager and enable
**TheElderShell.esp**. Keep its service scripts with the DLL. Caprica is embedded;
no separate compiler is needed in-game.

## Build

With Visual Studio 2022 C++ tools, Windows SDK, CMake, Ninja and .NET 10 installed:

```powershell
./setup.ps1
./build.ps1
```

Install the resulting `build/package` folder through your mod manager.
Builds do not modify your game directory.

For UI work, run `./tools/ui-preview/build.ps1` to open the standalone preview
with hot reload. See [UI preview](tools/ui-preview/README.md) for scenarios and styling.

Developer details: [how evaluation works](docs/compiled-evaluation.md),
[architecture](docs/architecture.md),
[embedded compiler](docs/embedded-compiler.md),
[VM integration](docs/vm-integration.md), [builds and releases](docs/build-release.md).

## License

[GPL-3.0](LICENSE). Binary distributions require corresponding source and
dependency notices, including Caprica's MIT license.
