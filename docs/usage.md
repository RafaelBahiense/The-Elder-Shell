# Using Papyrus Console

Open **SKSE Menu Framework → Papyrus Console → Console**. Enter one statement
at a time and wait for it to complete before using its result.

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
arrays retain live VM references. Removing a variable does not cancel its
command, and late callbacks cannot replace newer values.

## Instances

Open **Papyrus Console → Instances**. Search by form/reference ID, script name,
or both. Searches run on request, examine at most 100,000 attached entries and
return up to 128 results. Derived scripts match parent filters.

Choose a variable name and **Use instance**, then access it through
`instance.Function()` or `instance.Property`. Selection retains an existing
instance without creating or attaching scripts. Refreshing replaces search
results; loading clears results and variables.

## Execution and output

Some VM calls need the menu closed before they proceed. Completion means the
VM returned; runtime Papyrus errors belong in the game's script log. Commands
can change game state and are never repeated automatically. See
[compiled evaluation](compiled-evaluation.md) for the execution contract.

Use **Up/Down** for history. Output supports mouse selection and **Ctrl+C**;
**Ctrl+A** selects the transcript when it has focus.

## Developer checks

```powershell
./build.ps1 -Target Test
./build.ps1 -Target Analyze
```

Tests use synthetic inputs; VM scheduling and save behavior require in-game
validation. Setup installs pinned dependencies into ignored `.tools/`, without
changing system settings. Rerun it after dependency or Caprica port changes.
Dependencies can be reused through `-CommonLib`, `-Vcpkg` and
`-InstalledDependencies`.
