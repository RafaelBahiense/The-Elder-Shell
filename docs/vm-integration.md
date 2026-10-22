# VM integration

The game thread snapshots linked script signatures and selected instances.
The compiler receives ordinary source and declarations, without VM references.
Generated Papyrus runs through `DispatchStaticCall` and returns through a normal
VM completion callback. Property getters, setters and array access execute as
Papyrus instructions; there is no separate native accessor dispatcher.

## Named-state metadata

Named-state method tables use a packed count (low 9 bits) and a relative byte
offset (remaining 23 bits). The pinned CommonLib's offset bitfield keeps only
9 bits, so `GetFuncIter` can misread metadata on large scripts.

`src/native/vm/StateFunctions.h` decodes the full word and checks consecutive
function blocks. Additional metadata can appear after the state headers;
the function array begins at the first nonempty state's offset. Import generation
uses this reader and never reads function tables from unlinked types.

Metadata handling is verified on Skyrim **1.6.1170**. Other versions receive a
warning in the console and plugin log. Compatibility is unverified; there is no
version block. Engine dispatch and save-load interruption require in-game checks.

## Skyrim console commands

Skyrim console commands use a separate native dispatcher in
`src/native/runtime/GameConsole.cpp`. Its version-aware entry-point selection accounts
for the console compiler move in AE 1.6.1130; the pinned CommonLib wrapper uses
the older entry point. `ElderShell.Console.Execute` reaches it through a real
quest-attached Papyrus native function and returns immediate output as a String.
An optional reference applies to that dispatch only. Deferred engine work is not awaited.

## Built-in services

`TheElderShell.esp` contains one start-enabled, Run Once service quest (local ID
`0x800`) with `ElderShellRoot` and the `Console`, `Selection`, `Forms`, `Actors`,
`AI`, `Quests` and `Scripts` service scripts
attached. Root property getters cast their owning quest to the attached service
types. The shell supplies a retained root object as a normal evaluator input;
it never creates a service receiver or rewrites member syntax.

Service signatures come from the existing VM snapshot. Only source defaults and
native parameter names for these packaged scripts are supplemented by the shell.
Other scripts still require all arguments. Completion reads metadata without
executing getters. Reassignment, unset, instance naming and fragment shadowing
reject the reserved root name, case-insensitively.

Engine natives are registered with tasklet calls disabled. Package diagnostics
read and change `PACKAGE_DATA::foBehaviorFlags` only on the main thread. Named
interrupt flags have compile-time checks against CommonLib's masks. The runtime
tracks the original value and changed-bit mask per package; explicit restoration
and pre-load/new-game cleanup restore only those bits. No package change is
serialized or replayed. These are shared record edits, not actor package overrides.

`Quests.Inspect` reads
bounded runtime data without calling script getters. `PrepareRestart` validates a
stopped, non-event quest and performs only an explicitly requested `alreadyRun`
change, marking quest flags for saving. The Papyrus `Quests.Restart` body calls the
normal latent `Quest.Start()` and returns before/after reports with its Boolean
result. It neither fabricates alias bindings nor treats dispatch as completion.
Save loads retain the existing ticket/gate interruption contract; no call is replayed.

## Ownership

Variables retain their declared or inferred type even when a callback returns
`None`. Inference and completion can update only the binding owned by their ticket.
Save loads invalidate requests; late callbacks cannot restore removed variables
or release another evaluation's gate. Requests are never replayed automatically.

## Actor cleanup

Actor cleanup services run on the game thread. Native collectors copy alias and
script references under their respective locks, then release the locks before VM
calls. Papyrus wrappers stop quests and clear real ReferenceAlias objects through
normal engine APIs. Each category refuses mutation when its selection exceeds
1,024 entries; other categories can still proceed.

Ownership means the defining plugin (`GetFile(0)`), not the latest override or the
mod that caused a runtime change. Magic matches include the spell, base effect,
source item or enchantment. Inventory cleanup deletes matching base items, or only
matching extra-enchantment instances when the base item is unrelated.

Faction cleanup masks authored memberships on the selected actor without changing
shared NPC or template records. Package cleanup ends matching interrupt packages;
it does not rewrite base package lists. Engine spell removals can leave authored
spells present. Effects may remain until deferred teardown or be reapplied by a
controller. Inspect again to check the resulting state.

### Script declaration index

Generate an index using the plugin generator CLI:

```text
dotnet TheElderShell.Plugin.dll inspect-scripts <plugin file> <output directory>
```

Install the resulting `<plugin filename>.tsv` under
`SKSE/Plugins/TheElderShell/PluginScripts/`. The Mutagen reader collects script
names from the plugin's own records and quest aliases. The runtime checks the
plugin filename and SHA-256 before using exact, case-insensitive class matches.
Indexes must be regenerated after the plugin changes.

An index establishes a declaration match, not the origin of a PEX or the reason a
script was attached. Missing, invalid or stale indexes leave scripts unattributed
and retained. Unbinding removes direct actor bindings; it does not terminate
running or suspended calls, erase event registrations, or delete quest/alias script
instances. Cleanup does not edit external files, controller arrays or arbitrary
script properties. Such state can cause removed actor data to return.
