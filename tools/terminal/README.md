# External terminal

Enable **The Elder Shell → Settings → Enable local terminal connection** in
Skyrim, then open PowerShell in the directory containing this script:

```powershell
.\Connect-ElderShell.ps1
```

Enter Papyrus directly. Variables and the transcript are shared with the in-game
shell. Blocks, unfinished expressions and backslash continuation use the same
input rules. Type `exit` to return to PowerShell.

Connecting clears the display and starts at the top of the terminal. Type
`clear` to clear it again, or press **Ctrl+L** to clear while keeping your current
input. Variables, command history and the in-game transcript are preserved.

Use **↑/↓** to recall commands and restore your current draft. History keeps the
last 256 commands for this connection, including complete multiline blocks.
**←/→**, **Home/End**, **Backspace** and **Delete** edit the recalled command;
**Shift+Enter** inserts a newline and **Ctrl+C** clears unfinished input.
Line breaks appear as **↵** while editing; long commands scroll horizontally.
History stays in memory and is separate from PowerShell's command history.

Press **Tab** to complete names using Skyrim's live script metadata and shell
variables. **Tab/Shift+Tab** or **↓/↑** cycle matches; **Escape** dismisses them
without clearing the input. **Ctrl+Space** refreshes suggestions at the cursor.
Function completions insert parentheses and position the cursor for arguments.
Suggestions include signatures where available and do not execute Papyrus.
Tab on an empty or whitespace-only line inserts indentation.

```papyrus
var player = Game.GetPlayer()
player.GetLevel()
ElderShell.Actors.InspectDialogue(player)
```

For automation:

```powershell
.\Connect-ElderShell.ps1 -Command 'Game.GetPlayer().GetLevel()'
.\Connect-ElderShell.ps1 -FilePath .\inspect.psc
.\Connect-ElderShell.ps1 -Command 'Game.GetPlayer().GetLevel()' -AsJson
.\Connect-ElderShell.ps1 -Command 'ElderShell.Scripts.InspectBacklog(False)' -AsJson
.\Connect-ElderShell.ps1 -Complete 'Game.GetP' -AsJson
```

`-AsJson` returns `Ticket`, `Generation`, `Status` and `Result`. Check `Status`
for `Completed`; failures retain their actual status. Without `-AsJson`, an
unsuccessful command raises an error.

`-Complete` returns suggestions without executing the source. `-Cursor` selects
a UTF-16 cursor position; it defaults to the end of the source.

Use `-GameProcessId <id>` when multiple Skyrim instances are running.
`-TimeoutSeconds` sets the command wait limit (default 60 seconds). Timing out or
disconnecting does not cancel an already submitted command. Inspect its entry
in the in-game transcript before deciding whether to submit another call.
Requests are never replayed automatically.

Skyrim must remain responsive and loaded into a game for Papyrus evaluation.
Some latent calls need gameplay to be unpaused. The menu need not be open.
One client can connect at a time; the pipe accepts only local connections under
the same Windows user as Skyrim.

The setting is saved in `Data/SKSE/Plugins/TheElderShell.ini`:

```ini
[Terminal]
Enabled=true
```

The terminal is disabled by default. Menu changes take effect immediately;
manual INI edits take effect on the next Skyrim launch.
