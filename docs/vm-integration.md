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

`src/vm/StateFunctions.h` decodes the full word and checks consecutive
function blocks. Additional metadata can appear after the state headers;
the function array begins at the first nonempty state's offset. Import generation
uses this reader and never reads function tables from unlinked types.

Metadata handling is verified on Skyrim **1.6.1170**. Other versions receive a
warning in the console and plugin log. Compatibility is unverified; there is no
version block. Engine dispatch and save-load interruption require in-game checks.

## Ownership

Variables retain their declared or inferred type even when a callback returns
`None`. Inference and completion can update only the binding owned by their ticket.
Save loads invalidate requests; late callbacks cannot restore removed variables
or release another evaluation's gate. Requests are never replayed automatically.
