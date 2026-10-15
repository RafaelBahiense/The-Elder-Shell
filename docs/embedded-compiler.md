# Embedded compiler

Caprica is linked into the DLL. Evaluation compiles on the existing worker;
no compiler process is launched. VM signature snapshots and dispatch remain on
the game thread.

## Dependency and sessions

The custom vcpkg port lives in `cmake/ports/caprica/`. `setup.ps1` installs the
manifest dependencies into ignored `.tools/vcpkg_installed/`, with a local binary
cache in `.tools/vcpkg-cache/`. Normal builds only find and link `Caprica::caprica`;
rerun setup after changing the port or dependency manifest.

The [port configuration](../cmake/ports/caprica/portfile.cmake) defines the source
revision and checksum. Upstream files stay in vcpkg's download/build directories,
outside version control.
Tracked `patches/sessions.patch` and `patches/fmt.patch` provide pool ownership,
error cleanup, namespace reset, diagnostic capture, deadlines and fmt compatibility
as ordinary C++ diffs. The port builds a static library without the CLI entry point.

Vcpkg invalidates its binary cache when the port changes. The installed package
also exposes its build identity; together with our adapter's source hash, this
invalidates PEX caches when compiler behavior changes.

`EmbeddedCompiler` serializes sessions and sets the Skyrim profile. Each session
owns its imports, nodes and input buffers. Caprica does not start internal workers;
dependency jobs execute synchronously on the console's compiler worker. Diagnostics
use a worker-local sink, without redirecting process streams or debugger breaks.

A 30-second cooperative deadline is checked between jobs and while lexing. It
cannot interrupt arbitrary compiler code immediately. The evaluation gate stays
owned until compilation returns; threads are never forcibly terminated.

## Validation and packaging

Compiler tests exercise arithmetic, custom imports, properties, errors, changed
imports, deadline recovery and 1,000 mixed sessions with a memory-growth bound.
These tests use synthetic declarations, not game files. VM dispatch still needs
in-game validation.

Normal packages contain the DLL and dependency notices, without `Caprica.exe`.
Caprica's MIT and Boost's BSL notices are retained. When publishing binaries,
include the pinned Caprica archive and this repository's patches in matching
source. Vcpkg retains the downloaded archive under its `downloads/` directory
and patched source under `buildtrees/caprica/src/`.

