# UI preview

From the repository root:

```powershell
./tools/ui-preview/build.ps1
```

Opens a resizable 900 × 720 window with the production Console, Variables and
Instances pages. Save UI sources to rebuild and reload the menu. A failed build
keeps the current UI running; see `build/ui-preview/hot-reload.log` for details.
Close the preview to stop watching.

Scenarios cover ready, empty, pending, rejected, large and untested-runtime
sessions. Use Scale and window resizing to check layout. The preview uses synthetic
data; Papyrus compilation, VM dispatch and game effects require Skyrim.
`player.GetLevel()`, `var player = Game.GetPlayer()` and `vars` have sample responses.
Instance searches, variable inspection and property reads use fixture data.

Hot reload preserves drafts, history, variables, transcript, instance results,
scenario and scale. Pending preview requests are abandoned when the module unloads.
Completion selection and expanded sections reset. Host or shared session changes
require closing the preview and rerunning the launcher.

## Build and validation

Uses the existing Visual Studio C++ tools, Windows SDK and repository-configured
vcpkg. Dear ImGui is installed into the separate preview build directory.

```powershell
./tools/ui-preview/build.ps1 -BuildOnly
./tools/ui-preview/build.ps1 -SmokeTest
```

The smoke test renders every page across scenarios and window sizes, checks
array/property details, completion, help, protected console editing, continued commands,
session retention, and rejects a missing replacement
module while keeping the existing one. It opens no window.

## Styling

The bundled Skyrim theme provides the framework baseline. To use your installed
framework theme and font:

```powershell
./tools/ui-preview/build.ps1 -FrameworkDirectory '<SKSE Menu Framework mod>/SKSE/Plugins'
```

Optional `-FrameworkIni` and `-Font` override their selected files. Defaults can
also be stored as `FrameworkDirectory`, `FrameworkIni` and `Font` in ignored
`.tools/ui-preview.json`. Theme JSON edits reload automatically; changing font or
INI selection requires rerunning the launcher.

The preview host and reload mechanism are adapted from YLIWF's GPL-3.0 preview.
Theme data and the style baseline originate from
[SKSE Menu Framework 3](https://github.com/QTR-Modding/SKSE-Menu-Framework-3),
also under GPL-3.0. The preview backend is isolated from the Skyrim DLL.
