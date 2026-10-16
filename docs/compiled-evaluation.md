# How evaluation works

Embedded Caprica compiles each statement to PEX (Papyrus bytecode). Skyrim's VM
executes it and returns the result through a callback.

## Pipeline

1. Parse the statement; identify its result type and session variables.
2. Snapshot linked VM signatures on the game thread, without executing type probes.
3. Generate signature-only imports; original PSC source is unnecessary.
4. Wrap the statement in a typed global `Run` function.
5. Compile on a worker, or reuse verified cached bytecode.
6. Dispatch `Run` through `DispatchStaticCall` on the game thread.
7. Store the callback result and display it.

For `var level = player.GetLevel()`, the generated function is equivalent to:

```papyrus
Scriptname TheElderShell_Eval_<hash> Hidden

Int Function Run(Actor player) Global
    Return player.GetLevel()
EndFunction
```

`<hash>` represents a hexadecimal hash. `player` is a typed parameter; the native
session stores the result as `level`. No quest or attached wrapper is created.

## Types and imports

- `var` infers a fixed type from signatures and literals; Caprica checks semantics.
- Properties and arrays execute as ordinary Papyrus instructions.
- Imports supply signatures, without replacing installed script bodies.
- Parameter defaults are unavailable; every argument must be supplied.
- Directly used types require linked VM metadata. Unavailable indirect dependencies
  receive type-only declarations; members with missing signature types are omitted.

## Cache

SHA256 covers source, imports, compiler identity and profile. Cached PEX must
match its recorded digest. Compiler timestamps, paths and machine identity are
normalized.

Compiler files live under `SKSE/Plugins/TheElderShell/cache/`. Loader scripts live
under `Scripts/TheElderShell_Eval_<hash>.pex`; they remain immutable and are never
evicted because saved stacks may need them. See [usage](usage.md#execution-and-output)
for cache rotation and usage counters.

## Execution contract

One evaluation runs at a time. Only its VM callback confirms completion;
acceptance does not. Arguments capture submitted values, while objects and arrays
remain live references.

Loads clear variables and abandon requests without undoing effects. Requests are
never replayed. A lost callback requires restarting Skyrim before further evaluation.
See [VM ownership](vm-integration.md#ownership) for stale-callback handling.

The [compiler deadline](embedded-compiler.md#sessions) does not limit VM execution.

Further detail: [compiler embedding](embedded-compiler.md),
[VM metadata](vm-integration.md), [source layout](architecture.md).
