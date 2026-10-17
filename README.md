# The Elder Shell

Run Papyrus statements inside Skyrim through an SKSE Menu Framework console.
Inspect live scripts, call functions, and read or change properties without
building a test mod or restarting the game. No ESP required.

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

Results appear below the input. `var` infers the type; ordinary Papyrus type
declarations work too. Variables last until you load a save or start a new game.

Use the **Instances** tab to find an existing script and store it in a variable,
then call its functions or inspect its properties. **Tab/Shift+Tab** selects suggestions;
**Enter** accepts one, then executes on the next press. **Up/Down** recalls commands;
output can be selected and copied.

Single statements only; no blocks or comments. Supply all function arguments,
including optional ones. Commands run in the real Papyrus VM and can change
your game. There is no undo button hiding behind the sarcasm.

See the [usage guide](docs/usage.md) for properties, arrays, instance lookup,
session commands and execution limits.

## Requirements

- Skyrim Special Edition / Anniversary Edition on Windows.
- Matching SKSE and Address Library.
- SKSE Menu Framework **3.9+**.

Tested on **1.6.1170**. Other versions can be tried, but compatibility is unverified.

## Build

With Visual Studio 2022 C++ tools, Windows SDK, CMake and Ninja installed:

```powershell
./setup.ps1
./build.ps1
```

Install `build/package` through your mod manager. Caprica is embedded in the DLL;
no separate compiler is needed in-game. Builds do not modify your game directory.

Developer details: [how evaluation works](docs/compiled-evaluation.md),
[architecture](docs/architecture.md),
[embedded compiler](docs/embedded-compiler.md),
[VM integration](docs/vm-integration.md), [builds and releases](docs/build-release.md).

## License

[GPL-3.0](LICENSE). Binary distributions require corresponding source and
dependency notices, including Caprica's MIT license.
