# Build and release

Identity and version: `mod.json`. CMake generates native constants from it.
The DLL, menu, log, cache, generated script prefix and archive names use that identity.

```powershell
./setup.ps1
./lint.ps1                       # C++ and C# checks
./lint.ps1 -Fix                  # Apply formatting; review the diff
./build.ps1 -Target Test
./lint.ps1 -NativeAnalysis       # Formatting + MSVC /analyze
./lint.ps1 -Language Cpp -IncludeAnalysis -IncludeScope src/native/terminal
./lint.ps1 -Language Cpp -IncludeAnalysis -IncludeScope src/native/terminal -Fix
./package.ps1                   # Build and create ZIPs
```

Requires Visual Studio 2022 C++ tools, Windows SDK, CMake, Ninja and .NET 10.
Setup keeps dependencies under ignored `.tools/`; rerun it after dependency changes.
Mutagen and test dependencies use checked-in NuGet lock files. Setup restores them;
normal builds use the restored packages without downloading dependencies.
Use `-CommonLib`, `-Vcpkg` and `-InstalledDependencies` to reuse local dependencies.
Lint uses Visual Studio's clang-format and `dotnet format`;
`-Language Cpp` or `-Language CSharp` limits it to one language.
`-ClangFormat` selects another formatting executable.
Analysis checks project code with separate objects and warnings as errors.
It does not change release flags or analyze dependencies.

`-IncludeAnalysis` checks unused and missing direct C++ includes with Clang-Tidy
and `build/compile_commands.json`. Run a build first to configure the database.
`-IncludeScope` accepts files or directories; omit it to check all project C++
sources and headers. Headers are parsed as main files. `-Fix` applies include
edits and formatting, then rechecks the include results. Review its diff.
`-ClangTidy` selects an existing executable with `misc-include-cleaner`.
Diagnostics are saved under `build/lint/`; parse failures fail lint.

## Outputs

| File under `build/dist/` | Purpose |
| --- | --- |
| `TheElderShell-<version>.zip` | Install through Mod Organizer or another mod manager |
| `TheElderShell-<version>-source.zip` | Matching project source, Caprica archive and port patches |
| `SHA256SUMS.txt` | Archive checksums |

The installable ZIP contains `SKSE/Plugins/TheElderShell.dll`, the ESL-flagged
`TheElderShell.esp`, service PEX files under `Scripts/`, the terminal client under
`Tools/TheElderShell/`, and license notices under `docs/TheElderShell/`, including
Caprica (MIT) and Boost (BSL). Enable the ESP when installing.
CMake's `service_assets` target generates the ESP from `src/plugin/` using Mutagen
and compiles `src/papyrus/` with embedded Caprica and signature-only vanilla imports.
No Bethesda sources are needed. The ESP contains only the service quest, with no
vanilla overrides. `build.ps1 -Target Test` includes native, compiler and plugin tests.
No compiler executable, evaluator cache, dynamically generated evaluation PEX,
game imports, PDB or local configuration is included. Project and Papyrus sources,
build tooling and lint configuration are in the corresponding-source ZIP.
ZIP timestamps are fixed; entries contain relative paths.
Packaging checks required runtime/source entries and rejects generated game data,
local settings and build directories in the wrong archive. Recheck existing ZIPs
with `tools/VerifyArchives.ps1 -Binary <runtime.zip> -Source <source.zip>`.

## GitHub Actions

Pushes and pull requests run formatting, tests, analysis and builds on Windows.
Changes limited to Markdown or `docs/` skip CI.
Dependency binaries and CommonLib objects are cached separately from project code.

For a release, commit the version change and push a matching `v<version>` tag.
After CI passes, open **Actions → Build release → Run workflow**. The workflow reads
`mod.json` from the selected ref and builds its existing `v<version>` tag.
This manual workflow creates both ZIPs, checksums and a draft GitHub release.
Review its notes and publish it. Pushing a tag does not create a release.
A mismatched tag fails packaging. Both ZIPs are also available as workflow artifacts.
No Bethesda files or game installation are needed on CI.
