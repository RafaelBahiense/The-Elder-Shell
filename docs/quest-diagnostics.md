# Quest and script diagnostics

Use `ElderShell.Quests` for quest state and recovery plans, and
`ElderShell.Scripts` for attached instances and live execution. Inspections do
not run property getters, advance stages or clear engine flags.

## Inspect a quest

```papyrus
var quest = Quest.GetQuest("ExampleQuest")
ElderShell.Quests.InspectLifecycle(quest)
ElderShell.Quests.InspectStages(quest)
ElderShell.Quests.InspectAliases(quest)
ElderShell.Scripts.InspectInstance(quest)
ElderShell.Scripts.InspectStacks(quest)
```

| Inspection | Contents |
| --- | --- |
| `Quests.InspectLifecycle(Quest)` | Enabled, stage-wait and promotion state, current stage, instance, completion and story event |
| `Quests.InspectStartWaiters(Quest)` | Registered startup stacks and stages on verified Skyrim SSE 1.6.1170 |
| `Quests.InspectStages(Quest)` | Recorded stages, waiting stages and objective states |
| `Quests.InspectAliases(Quest)` | Reference bindings, fill rules, restrictions, attached script names and supported condition checks |
| `Quests.InspectDialogue(Actor, Quest)` | Quest and topic INFO conditions for that speaker, said-once state and flags |
| `Quests.InspectStorySelection(Quest)` | Matching Story Manager nodes, ancestor conditions and competing quests |
| `Quests.InspectDependencies(Quest)` | External quest-alias links, owned scenes and direct script-storage references on quest owners |
| `Quests.InspectScene(Scene)` | Owner, playing state, actor bindings, phase conditions and action definitions |
| `Scripts.InspectInstance(Form, String script = "")` | Attached script state, storage fields and property metadata |
| `Scripts.InspectAliasInstance(Quest, Int aliasID, String script = "")` | Instance inspection for an alias owner |
| `Scripts.InspectStacks(Form owner = None, String script = "")` | Matching paused chains and stacks whose ownership cannot be verified |
| `Scripts.InspectStack(Int stackID)` | Paused frames, instruction offsets, arguments, locals and return types |
| `Scripts.InspectLatentCall(Int stackID)` | Waiting native call, return registration, script-result callback presence and cancellation visibility limits |

Stack IDs are hexadecimal in reports. For example:

```papyrus
ElderShell.Scripts.InspectStack(0x00123456)
ElderShell.Scripts.InspectLatentCall(0x00123456)
```

The `script` argument filters class names without case sensitivity. Stack scans
are bounded to 4,096 entries and 128 output rows. Frame contents are read only
while a stack is waiting on a latent function and is not freezing or frozen.
Other stacks remain visible as unverified; absence from a matching chain does
not prove that the owner has no execution.

### Interpreting results

- A disabled quest with stage work pending is **stopping**, even if its stage is
  completed. The flag alone does not identify which execution caused it.
- `PASS` and `FAIL` describe individual supported native conditions. OR groups,
  dialogue selection and scene gates still matter. Unsupported functions and
  missing alias, package or event context produce `UNKNOWN`.
- Instance fields are storage snapshots, not getter results. Property metadata
  identifies permissions without executing accessors. Arrays show type and
  length; use the Variables inspector or ordinary Papyrus for their contents.
- Runtime stage lists are not a complete catalogue of authored stages or
  fragments. Current scene phase/action completion, location-alias bindings,
  authored alias package/faction/spell lists and reservation history are not
  available in the mapped layout.
- Dependency reports search bounded quest-alias and direct script-storage links,
  not a complete reference graph. Reports state their scan limits.

## Compare snapshots

```papyrus
var before = ElderShell.Quests.Capture(quest)
```

Perform the action being investigated, then:

```papyrus
ElderShell.Quests.Compare(quest, before)
ElderShell.Quests.ReadSnapshot(before)
```

Snapshots capture lifecycle, aliases, recorded stages, script storage, stack
reports and owned scenes. Comparison reports added and removed lines; it does
not imply causation or provide rollback. Up to 32 snapshots are retained until
`Quests.ClearSnapshots()`, a save load or a new game. An empty token means no snapshot
was retained.

## Preview and apply repairs

Make a separate save before applying a repair. Previewing does not alter game
state. A nonempty plan token is required for checked operations:

```papyrus
var plan = ElderShell.Quests.PreviewRestart(quest)
ElderShell.Quests.InspectRepair(plan)
ElderShell.Quests.RestartChecked(quest, plan)
```

`PreviewRestart` and `RestartChecked` accept optional `resetStages` and
`clearAlreadyRun` booleans, both defaulting to `False`. Supply the same values
to both calls. Restart requires a fully stopped, non-event quest without
identified owned paused execution. It preserves authored flags and calls normal
Papyrus `Quest.Start()`; the result includes its actual Boolean return.

Other repair pairs are:

```papyrus
var aliasPlan = ElderShell.Quests.PreviewRebindAlias(quest, 7, replacement)
ElderShell.Quests.InspectRepair(aliasPlan)
ElderShell.Quests.RebindAlias(quest, 7, replacement, aliasPlan)

var scenePlan = ElderShell.Quests.PreviewScene(scene, False)
ElderShell.Quests.InspectRepair(scenePlan)
ElderShell.Quests.ChangeScene(scene, False, scenePlan)
```

Alias repair supports forced-reference and unique-actor aliases without search
conditions. It checks actor/dead/disabled/loaded restrictions, rejects external
ownership and playing owner scenes, and uses normal `ForceRefTo`. Alias effects
may add/remove packages, factions or spells. Those authored lists are not mapped,
so inspect the actor before and after. Quest runtime data is marked for saving.

Scene plans request start (`True`) or stop (`False`) through ordinary Papyrus.
They reject a transitioning owner quest. Starting requires an enabled owner and
a stopped scene. Requests may trigger fragments and deferred effects; the report
does not claim those effects have finished.

If preview returns an empty string, `InspectRepair("")` gives the latest preview rejection.
Up to 32 plans are retained; `ClearRepairs()` discards them. Tokens have a random
process scope and are single-use and target-specific. A save load or new game
invalidates every plan, including
when the load fails. Changed captured state rejects application. Truncated
state and untracked array contents cannot authorize a plan. Checks cover mapped
state, not every external dependency or running worker.

### Restore a script property

```papyrus
ElderShell.Scripts.PreviewPropertyRestore(quest, "ExampleController", "ReturnRace", "expectedRace", "replacementRace")
```

The result is a reviewable Multiline block. Execute it with the referenced session
variables defined. It checks the expected getter result and retained instance
snapshot, then performs a typed Papyrus assignment. Getters, setters and supplied
expressions execute normally; the compiler checks their types. The service never
writes native property storage or invokes a separate accessor dispatcher.

Exactly one initialized attached instance with a readable, writable property is
required. Expected/replacement expressions are limited to 512 characters each
and cannot contain newlines or semicolons. After executing, inspect
`propertyRepairReport`, then verify the property and its gameplay effects. The
report confirms that assignment returned, not that all setter effects completed.

## Pending native calls

`InspectLatentCall` does not authorize cancellation or manufacture a result.
The supported VM interface exposes waiting stacks and return types, but does
not expose ownership/cancellation contracts for arbitrary engine work. Returning
a synthetic result could leave an original callback able to complete later.

### Fail a blocked startup stage request

On Skyrim SSE **1.6.1170.0**, a disabled quest stuck at stage 0 may retain a
startup waiter for `Quest.SetCurrentStageID`. Inspect the registration and its
stack before previewing recovery:

```papyrus
ElderShell.Quests.InspectStartWaiters(quest)
ElderShell.Scripts.InspectLatentCall(0x00123456)
var startPlan = ElderShell.Quests.PreviewFailPendingStart(quest, 0x00123456)
ElderShell.Quests.InspectRepair(startPlan)
ElderShell.Quests.FailPendingStart(quest, 0x00123456, startPlan)
```

Application invokes the engine's failed-start completion handler, which returns
`False` and clears the registered waiter. The suspended Papyrus chain then runs
normally; it may ignore that result or perform further work. Recheck lifecycle
and stacks after it finishes before attempting a restart. Completion dispatch
does not prove that the quest has finished stopping.

Recovery requires one matching startup waiter, no separate stage-completion
registration or promotion task, and an unfrozen latent stack whose entire chain
belongs to the quest. The native argument, VM registration, runtime, manager
vtables and complete handler code are verified again during application.
Unrecognized code, including a modified handler, rejects recovery. The operation
does not clear quest flags, reset stages, unbind aliases or terminate threads.

Other native waits have no generic latent-result repair. Restart and alias plans
reject pending stage work rather than bypassing it. Save editing and automatic
script-thread termination are not performed.
