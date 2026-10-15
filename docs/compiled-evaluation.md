# How evaluation works

Input becomes a temporary Papyrus script. Embedded Caprica compiles it to PEX
(Skyrim's script bytecode). Skyrim's VM executes it.

```text
Statement → type snapshot → generated source → Caprica → PEX → VM → result
```

## Pipeline

1. Parse one statement. Identify its result type and referenced variables.
2. Snapshot linked VM signatures on the game thread. Do not execute type probes.
3. Generate signature-only imports. Original PSC source is unnecessary.
4. Wrap the statement in a typed global `Run` function.
5. Compile on a worker, or reuse verified cached bytecode.
6. Dispatch `Run` through Skyrim's `DispatchStaticCall` on the game thread.
7. Store the callback result. Display it in the console.

For `var level = player.GetLevel()`, the generated function is equivalent to:

```papyrus
Scriptname PapyrusConsole_Eval_<hash> Hidden

Int Function Run(Actor player) Global
    Return player.GetLevel()
EndFunction
```

The script name above is schematic. The real name contains a hexadecimal hash.
`player` arrives as a typed parameter. The native session stores the returned
value as `level`. No quest or attached wrapper instance is created.

## Types and imports

- `var` infers a fixed type from signatures and literals.
- Caprica validates Papyrus semantics and argument types.
- Properties and arrays execute as ordinary Papyrus instructions.
- Imports describe signatures; their bodies do not replace installed scripts.
- Parameter defaults are unavailable. Every argument must be supplied.
- Directly used script types require linked VM metadata.
- Unavailable indirect dependencies receive type-only declarations.
- Members with lost property or parameter types are omitted.

## Cache and lifetime

- SHA256 covers source, imports, compiler identity and profile.
- Cached PEX must match its recorded digest.
- Compiler timestamps, paths and machine identity are normalized.
- Cache: `SKSE/Plugins/PapyrusConsole/cache/`.
- Loader input: `Scripts/PapyrusConsole_Eval_<hash>.pex`.
- Generated files are immutable. Saved stacks may still need them.

## Execution contract

- Tested runtime: Skyrim 1.6.1170. Other versions receive a warning, not a version block.
- One compiled evaluation runs at a time.
- Completion requires a VM callback. Acceptance is insufficient.
- Arguments capture submitted values; objects and arrays remain live references.
- Ticket ownership rejects stale assignment callbacks.
- Loads clear console variables and abandon requests. Effects are not undone.
- Requests are never replayed automatically.
- A lost completion callback requires restarting Skyrim before further evaluation.
- Limits: 128 generated types per process, 32 input variables, 256 imported types.
- Compilation deadline: 30 seconds, cooperative. It is not a VM execution timeout.

Further detail: [compiler embedding](embedded-compiler.md),
[VM metadata](vm-integration.md), [source layout](architecture.md).
