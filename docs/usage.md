# Using The Elder Shell

Open **SKSE Menu Framework → The Elder Shell → Console**. Enter statements at
the bottom `>` prompt. Wait for completion before using a result.
Expand **Help** for selectable syntax examples and shortcuts.

Suggestions appear as you type variable names or members after `.`. Press **Tab**
to insert the first match, or click another suggestion. Function calls show their
parameter types and current argument. Suggestions read metadata without running code.

## Statements and values

```papyrus
var player = Game.GetPlayer()
var level = player.GetLevel()
level += 1
player.GetLevel()
Debug.Notification("Hello from Papyrus")
```

`var` infers a fixed type from the initializer. Explicit declarations such as
`Actor player = Game.GetPlayer()` also work. Reassignment retains that type;
bare `None` needs an explicit declared type. Names are case-insensitive.
Expressions display results; functions without a result display **No return
value**. Supply every argument, including optional ones.

Properties and arrays use Papyrus syntax. Here, `instance` is an existing script
selected through the Instances tab, and `PropertyName` stands for one of its
properties:

```papyrus
var value = instance.PropertyName
instance.PropertyName = value
var weights = new Float[3]
weights[0] = 0.66
weights.Length
```

Only single statements are supported, without code blocks or comments.
Use `Game.GetFormEx(0x...)` for full runtime FormIDs, including dynamic `FF…`
references. Cast forms to their script type before accessing custom members.

## Session helpers

```text
vars
unset level
clearvars
methods Actor
```

Up to 64 typed variables are retained until a save load or new game. Pending
and failed assignments cannot be used. Object types survive `None` results;
arrays retain live VM references. Removing a variable does not cancel its command.

## Instances

Open **The Elder Shell → Instances**. Search by form/reference ID, script name,
or both. Searches run on request, examine at most 100,000 attached entries and
return up to 128 results. Derived scripts match parent filters.

Choose a variable name and **Use instance**, then access it through
`instance.Function()` or `instance.Property`. Selection retains an existing
instance without creating or attaching scripts. Refreshing replaces search
results; loading clears results and variables.

## Variable inspector

Open **Variables** to see stored types and values. **Refresh variables** updates
the snapshot; expand arrays to inspect elements. Object properties show their
types without evaluating getters. **Read (runs getter)** explicitly evaluates a
property and shows its result here and in Console. Reads can change game state.
Values may change between refresh and read; reassigned variables require a refresh.
Large snapshots are truncated; assign an array element to another variable for detail.

## Execution and output

Some VM calls need the menu closed before they proceed. Completion means the
VM returned; runtime Papyrus errors belong in the game's script log. Commands
can change game state and are never repeated automatically. See
[compiled evaluation](compiled-evaluation.md) for the execution contract.

The console counts distinct evaluation types loaded since Skyrim started.
At 128 it shows a warning, without blocking further evaluations. This counts
types, not memory. Restarting Skyrim resets the counter; loading a save does not.
Input-variable and import counts describe the last prepared evaluation. At 32
inputs or 256 imports, advisory warnings appear without blocking evaluation.
The compiler cache retains 30 folders, evicting the least recently used as needed.
Generated scripts under `Scripts/` are retained for saves.

Use **Up/Down** for history. Output supports mouse selection and **Ctrl+C**;
**Ctrl+A** selects the transcript when it has focus.
