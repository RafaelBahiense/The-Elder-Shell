# Architecture

| Path | Responsibility |
| --- | --- |
| `mod.json`, `cmake/ModInfo.h.in` | Mod identity and generated native constants |
| `src/native/shell/` | Input, expression types, values and request ownership |
| `src/native/compiler/` | Signature declarations, embedded Caprica sessions and PEX integrity |
| `src/native/vm/` | Skyrim integration, metadata snapshots and VM dispatch |
| `src/native/ui/` | Menu, transcript, instance explorer and command history |
| `src/native/runtime/` | SKSE entry point and game lifecycle events |
| `src/native/PCH.h` | Skyrim/SKSE precompiled headers |
| `tests/native/` | Subsystem tests with synthetic inputs |
| `cmake/`, build/setup scripts | Dependencies, validation and packaging |

Private headers stay beside their implementations. Cross-subsystem includes use
paths such as `shell/Command.h`, relative to `src/native/`.

Shell policy uses ordinary values and signature snapshots without Skyrim or the
PCH. VM integration reads engine state on the game thread, passes snapshots to
that policy, and applies validated results. The compiler worker uses plain source
and declarations; it never queries the VM. The UI submits requests and reads
synchronized results.

Tests mirror these boundaries. Parser, UI history and VM metadata rules run without
Skyrim; compiler tests run Caprica with synthetic declarations. In-game dispatch
and save-load behavior still require manual validation.

CMake discovers native `.cpp` files and subsystem tests with `CONFIGURE_DEPENDS`.
Release and analysis targets share the source list. Compiler tests remain separate
because they require Windows and Caprica. Adding a file within these groups does
not require editing a source list.

See [compiled evaluation](compiled-evaluation.md) and
[VM integration](vm-integration.md) for execution contracts.
See [compiler embedding](embedded-compiler.md) for dependency and session details.
