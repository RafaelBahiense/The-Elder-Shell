# Changelog

## 1.2.0

### Added

- External terminal connection with a bundled PowerShell client. Enter Papyrus
  interactively, run script files or retrieve command results as JSON.
- Terminal command history, multiline editing, Tab completion, function
  signatures and `clear` / Ctrl+L screen clearing.
- **Settings → Enable local terminal connection**. Connections are local to the
  same Windows user and share variables with the in-game shell.
- `ElderShell.Scripts.InspectBacklog(full)` captures a quick or full bounded
  Papyrus backlog report. `ReadBacklog()` reads the latest capture.
- Scoped C++ include analysis and fixes through `lint.ps1 -IncludeAnalysis`.

### Changed

- Installable archives contain runtime files and terminal tools. Papyrus source
  files are provided in the matching source ZIP.

### Fixed

- Completion suggestions occupy a stable panel without moving the console
  text. The suggestion list fills the panel, reserving space for signatures
  only when present.
- The editing caret stays after the prompt while transcript text remains
  selectable for copying.

### Updating

- Install the complete package to keep the DLL and Papyrus services matched.
- The terminal client is included under `Tools/TheElderShell/`. Enable the
  terminal setting in Skyrim, then run `Connect-ElderShell.ps1` from PowerShell.
  The connection is disabled by default.
- Disconnecting or timing out does not cancel submitted commands. Save loads
  invalidate pending requests; commands are never replayed automatically.

## 1.1.0

### Added

- Built-in `ElderShell` services for console selection, form identity, actor
  inspection, AI packages, quests, scripts and Skyrim console execution.
- Quest lifecycle, alias, condition and paused-stack inspection, snapshot
  comparisons, repair previews and validated recovery operations.
- Actor dialogue permission reports and runtime package interruption controls.
- Plugin-based actor inspection and targeted cleanup of supported factions,
  spells, effects, inventory, equipment, aliases and script bindings.
- Papyrus backlog tab with queue sizes, grouped call samples and exported full
  overflow reports.
- Standalone UI preview with hot reload for development.

### Changed

- Reworked Console into one selectable transcript with an editable prompt,
  automatic continuation for unfinished input and backslash line continuation.
- Enter submits complete commands; Tab accepts completion suggestions.
- Redesigned Console, Variables and Instances with clearer navigation and detail
  panels.
- Organized shell services by domain, with selection helpers and form inspection.
- Expanded documentation with workflows and screenshots.

### Fixed

- Corrected console execution and selected-reference mappings for modern
  Anniversary runtimes.
- Escape dismisses completion suggestions before closing the menu. Hook
  installation now checks the call instruction, memory permissions and framework
  destination, skipping unrecognized code.
- Release workflow resolves the release tag from the selected version.
- Preview builds reuse the compiler environment and deduplicate tool paths.

### Updating

- Install the complete release and enable **TheElderShell.esp**. Keep the DLL
  and packaged service scripts together; 1.0.0 did not require an ESP.
- Replace the `console ...` shortcut with `ElderShell.Console.Execute(...)`.
- The separate Multiline toggle is replaced by continued input in Console.
- Requires matching SKSE, Address Library and SKSE Menu Framework **3.9+**.
  Tested on Skyrim **1.6.1170**; other runtimes remain unverified. Quest startup
  failure recovery is restricted to 1.6.1170.
- Recovery and cleanup modify real game state. Keep a separate save first.
