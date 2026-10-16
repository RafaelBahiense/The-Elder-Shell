# Build and release

Identity and version: `mod.json`. CMake generates native constants from it.
The DLL, menu, log, cache, generated script prefix and archive names use that identity.

```powershell
./setup.ps1
./lint.ps1                       # clang-format check
./lint.ps1 -Fix                  # Apply formatting; review the diff
./build.ps1 -Target Test
./lint.ps1 -NativeAnalysis       # Formatting + MSVC /analyze
./package.ps1                   # Build and create ZIPs
```

Requires Visual Studio 2022 C++ tools, Windows SDK, CMake and Ninja.
Setup keeps dependencies under ignored `.tools/`; rerun it after dependency changes.
Use `-CommonLib`, `-Vcpkg` and `-InstalledDependencies` to reuse local dependencies.
Lint uses Visual Studio's clang-format; `-ClangFormat` selects another executable.
Analysis checks project code with separate objects and warnings as errors.
It does not change release flags or analyze dependencies.

## Outputs

| File under `build/dist/` | Purpose |
| --- | --- |
| `TheElderShell-<version>.zip` | Install through Mod Organizer or another mod manager |
| `TheElderShell-<version>-source.zip` | Matching project source, Caprica archive and port patches |
| `SHA256SUMS.txt` | Archive checksums |

The installable ZIP has `SKSE/Plugins/TheElderShell.dll` at its root layout,
plus license notices under `docs/TheElderShell/`, including Caprica (MIT) and Boost (BSL). No compiler executable,
cache, generated PEX, game imports, PDB or local configuration is included.
ZIP timestamps are fixed; entries contain relative paths.

## GitHub Actions

Pushes and pull requests run formatting, tests, analysis and builds on Windows.
Dependency binaries and CommonLib objects are cached separately from project code.

For a release, commit the version change and push a matching `v<version>` tag.
After CI passes, open **Actions → Build release → Run workflow** and enter the tag.
This manual workflow creates both ZIPs, checksums and a draft GitHub release.
Review its notes and publish it. Pushing a tag does not create a release.
A mismatched tag fails packaging. Both ZIPs are also available as workflow artifacts.
No Bethesda files or game installation are needed on CI.
