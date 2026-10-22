# The Elder Shell

Run Papyrus statements inside Skyrim through an SKSE Menu Framework console.
Inspect live scripts, call functions, and read or change properties without
building a test mod or restarting the game.

## Why this exists

I wanted to run arbitrary Papyrus code while playing. Skyrim's console doesn't
let you do that, so I built one that does. After finishing it, I discovered I
didn't need it nearly as often as I thought. Naturally, the correct response
was to release it.

Useful for debugging a script, checking a mod's state, trying an API, or
answering “what happens if I call this?” with something more expensive than a
guess.

## Use it

Install the mod, then open **SKSE Menu Framework → The Elder Shell → Console**.
Enter one statement at a time:

```papyrus
var player = Game.GetPlayer()
player.GetLevel()
Debug.Notification("It works. Now find a reason to use it.")
```

Results appear in the transcript. `var` infers the type; ordinary Papyrus type
declarations work too. Variables last until you load a save or start a new game.

Use the **Instances** tab to find an existing script and store it in a variable,
then call its functions or inspect its properties. **Up/Down** selects suggestions;
**Tab/Enter** accepts one, **Escape** dismisses, and **Ctrl+Space** reopens the list.
With suggestions closed, single-line **Up/Down** recalls commands;
output can be selected and copied.

Enable **Multiline** for several statements, `If`/`While` blocks and comments.
Enter inserts a newline; **Ctrl+Enter** runs the fragment.

Use `ElderShell.Console.Execute("help")` for Skyrim console output and
`ElderShell.Quests.Inspect(quest)` for a quest report. `ElderShell` is a
read-only binding to real quest-attached service scripts, using ordinary Papyrus syntax.

For other installed scripts, supply all function arguments, including optional ones.
Commands run in the real Papyrus VM and can change
your game. There is no undo button hiding behind the sarcasm.

See the [usage guide](docs/usage.md) for properties, arrays, instance lookup,
session commands and execution limits.

## Requirements

| Requirement | What to install |
| --- | --- |
| [SKSE](https://skse.silverlock.org/) | The build matching your Skyrim executable version. Launch the game through SKSE. |
| [Address Library for SKSE Plugins](https://www.nexusmods.com/skyrimspecialedition/mods/32444) | The package covering your Skyrim executable version. |
| [SKSE Menu Framework](https://www.nexusmods.com/skyrimspecialedition/mods/120352) **3.9+** | The in-game interface used by the shell. Install the runtime mod, not the developer headers. |

Tested on Skyrim **1.6.1170**. Other versions can be tried, but compatibility is
unverified. Match dependencies to your executable version, rather than the
Special Edition / Anniversary Edition store label.

## Build

With Visual Studio 2022 C++ tools, Windows SDK, CMake, Ninja and .NET 10 installed:

```powershell
./setup.ps1
./build.ps1
```

Install the complete mod package and enable **TheElderShell.esp**. Its ESL-flagged
service quest and three scripts are included; keep them with the DLL.

Install `build/package` through your mod manager. Caprica is embedded in the DLL;
no separate compiler is needed in-game. Builds do not modify your game directory.

Developer details: [how evaluation works](docs/compiled-evaluation.md),
[architecture](docs/architecture.md),
[embedded compiler](docs/embedded-compiler.md),
[VM integration](docs/vm-integration.md), [builds and releases](docs/build-release.md).

## License

[GPL-3.0](LICENSE). Binary distributions require corresponding source and
dependency notices, including Caprica's MIT license.
