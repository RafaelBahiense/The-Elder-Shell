# Embedded compiler

Caprica is a static library linked through `Caprica::caprica`, without its CLI
entry point. The [vcpkg port](../cmake/ports/caprica/portfile.cmake) defines the
source revision and checksum. Upstream files remain outside version control.

## Dependency

`setup.ps1` installs dependencies into `.tools/vcpkg_installed/`, using
`.tools/vcpkg-cache/` for binaries. Normal builds link installed dependencies;
rerun setup after changing the port or manifest.

Tracked patches provide session cleanup, pool ownership, namespace reset,
diagnostics, deadlines and fmt compatibility. Port changes invalidate vcpkg's
binary cache. The installed compiler identity and adapter source hash also
invalidate affected PEX caches.

## Sessions

`EmbeddedCompiler` serializes sessions on the compiler worker. Each session owns
its imports, nodes and input buffers. Dependency jobs run synchronously; Caprica
starts no internal workers. Diagnostics use a worker-local sink.

A 30-second cooperative deadline is checked between jobs and while lexing.
It cannot interrupt arbitrary compiler code. The evaluation gate remains owned
until compilation returns; threads are never forcibly terminated.

## Validation

Synthetic compiler tests cover imports, properties, diagnostics, cache invalidation,
deadline recovery and memory growth across 1,000 mixed sessions. They require no
game files. VM dispatch requires in-game validation.

See [builds and releases](build-release.md) for commands, archives and notices.
