Building T5000
==============

T5000 is the repository root; T3000 is under `T3000\`. This file covers
T5000: its build, its checks against T3000, and CI.
[`T3000/README_Build.md`](T3000/README_Build.md) covers building T3000.

Building T5000
--------------

T5000 has its own solution, `T5000.sln`, and builds with nothing of T3000's:

```
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "T5000.sln" -p:Platform=x86 -p:Configuration=Release
```

* It compiles only T5000's own files, none under `T3000\`, and links only Windows libraries, so it needs neither MFC nor the .NET targeting pack.
* Its self-test runs after every build, as `T5000.exe --selftest`, and a failing check fails the build.
* It builds to `bin\Release\T5000.exe` (`bin\Debug\` for Debug), with intermediates in `obj\`. A running T5000 locks the exe, so stop it before building.
* `T5000.exe` serves its UI on `http://127.0.0.1:8730` and opens a browser; `--no-browser` skips the browser; `--selftest` runs the tests by hand.
* It keeps its device list in `T5000.db` beside the exe, or in the file `--db <file>` names. The self-test uses a database in memory and temporary files, so a build leaves no `T5000.db` behind.
* It links `winsqlite3.lib`, the import library for the SQLite in Windows, which comes with the Windows SDK. There is nothing extra to install.

[`README.md`](README.md) covers what it does and how to work on it.

Checking T5000 against T3000
----------------------------

T5000 copies what it needs from T3000: wire layouts, command codes, product codes and display tables. `conformance\` checks each copy against the T3000 header it came from, and checks T5000's BACnet encoder against T3000's stack.

That project, `T5000Conformance`, is in T3000's solution, `T3000\T3000 - VS2019.sln`, as `..\conformance\T5000Conformance.vcxproj`. The solution builds it after `BACnet_Stack_Library` and runs it, so a change on either side that breaks the other fails the solution build. To build and run only those checks:

```
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "T3000\T3000 - VS2019.sln" -t:T5000Conformance -p:Platform=x86 -p:Configuration=Release
```

This builds the stack too, which needs MFC, and nothing else of T3000's. The checks run as `T5000Conformance.exe --source-root <repository>\T3000`: they read T3000's source from T3000's tree, so the paths they name are T3000's own, as in T3000's repository (`T3000\global_define.h`, `ISP\ComWriter.cpp`). T5000's code and docs cite T3000's source the same way.

T5000's own build does not run these checks; CI's `conformance` job does, on every push or pull request that touches T5000 or a T3000 file they read. Run them, or `scripts/ci-local.ps1`, before pushing a change to any of these files:

* `wire\`
* `bacnet\command.h`
* `bacnet\private_transfer.cpp`, which the oracle checks byte for byte against T3000's stack
* `bacnet\write_command.h`
* `bacnet\private_write.cpp`, which the oracle also checks against T3000's stack
* `device\product.h`
* `display\tables.h`
* `offline\` and `firmware\`
* the constants the table guard lists

Building a clean checkout, as CI does
-------------------------------------

`scripts/ci-local.ps1` builds a throwaway git worktree of a ref with the same commands CI runs, so a file that was never committed fails here rather than in CI. Run it with PowerShell 7; under Windows PowerShell 5, git's progress output on stderr is treated as an error.

By default it runs both jobs of `T5000.yml`:

* **`selftest`** copies the checkout, less `T3000\` and `.git`, somewhere else and builds `T5000.sln` there, as CI checks out everything but `T3000/`.
* **`conformance`** builds `T3000\T3000 - VS2019.sln` with `-t:T5000Conformance`: the conformance checks and the BACnet stack they link.

It fails if either job fails, or if either produces no test summary. `-Only T3000` instead builds all of `T3000\T3000 - VS2019.sln`, as `T3000.yml` does when it is run by hand.

```
pwsh -NoProfile -File scripts/ci-local.ps1
pwsh -NoProfile -File scripts/ci-local.ps1 -Only T5000
pwsh -NoProfile -File scripts/ci-local.ps1 -Only Conformance
pwsh -NoProfile -File scripts/ci-local.ps1 -Only T3000
pwsh -NoProfile -File scripts/ci-local.ps1 -Ref origin/master -Parallel
```

| Parameter | Default | Does |
| --- | --- | --- |
| `-Ref` | `HEAD` | What to build: a branch, tag or SHA |
| `-Only` | `All` | `All` is both T5000 jobs. `T5000` or `Conformance` runs just that job; `T3000` builds the whole T3000 solution |
| `-WorktreePath` | `C:\t3000-ci` | Where the worktree goes. Keep it short; deep MFC paths hit `MAX_PATH` |
| `-T5000Path` | `C:\t5000-ci` | Where the checkout, less `T3000\`, is copied to be built on its own |
| `-Parallel` | off | Adds `/m`. Faster, but no longer the exact CI command |
| `-Keep` | off | Leaves the checkouts in place afterwards |

Before the conformance job it checks for MFC for v143, and before the full T3000 build for the .NET 4.5.2 reference assemblies too. It stops with the reason if one is missing.

When a build fails
------------------

* A compile error, or a new file not added to `T5000.vcxproj`. The build uses MSBuild and the project files.
* A self-test failure, or a conformance failure: a T5000 copy that no longer matches T3000. The build log lists each failed check. A layout, command-code or product-code mismatch is a `static_assert`, so it shows as a compile error in `conformance\`.

CI
--

The checks on a push or pull request are T5000's, and T3000 is no longer built on each one.

| Workflow | Runs | Builds |
| --- | --- | --- |
| `.github/workflows/T5000.yml` | On a push to, or pull request against, `master` that touches anything outside `T3000/`, or a part of `T3000/` the conformance checks build or read: T3000's solution and `AppConfig.props`, `T3000/T3000/`, `T3000/ISP/`, `T3000/BacNetDllforVc/` and `T3000/ModbusDllforVc/ModbusDllforVc/`. Also by hand. | Two jobs, below |
| `.github/workflows/T3000.yml` | By hand only: in the Actions tab, "T3000 (run by hand)", then Run workflow | All of `T3000\T3000 - VS2019.sln` |
| `.github/workflows/Build.yml`, `Release.yml` | By hand, or on a published release, and only in `temcocontrols/T3000_Building_Automation_System` | Temco's signed T3000 installer and release. They need Temco's SignPath project and secrets, so they are skipped in a fork, and their paths under `T3000\` have not been run |

`T5000.yml`'s jobs:

* **`selftest`** checks out everything but `T3000/`, fails if `T3000/` is there anyway, and builds `T5000.sln`, which runs T5000's self-test. It installs nothing, because T5000 needs neither MFC nor .NET. Any path T5000 reaches into `T3000/` is absent there, so this job is what keeps the two separate.
* **`conformance`** builds `T3000\T3000 - VS2019.sln` with `-t:T5000Conformance`, which builds the conformance checks and the BACnet stack they link, and runs the checks. Nothing else of T3000's is built. The stack needs MFC for v143, so the job runs on `windows-2022`, whose Visual Studio 2022 carries it; its step that installs MFC only confirms it there, and is kept for an image without it. It needs no .NET.

`T3000.yml` installs MFC for v143 and the .NET Framework 4.5.2 reference assemblies before building, since `windows-latest` lacks both; without them the build stops at `MSB8041` and `MSB3644`.

Taking T3000's changes from upstream
------------------------------------

This repository is a fork of `temcocontrols/T3000_Building_Automation_System`, whose T3000 is at its root. Merge it into `T3000\` with git's subtree option, which maps upstream's root onto `T3000/`:

```
git fetch upstream
git merge -X subtree=T3000 upstream/master
```

A plain `git merge upstream/master` leaves git to guess each file's move from its content, and conflicts wherever upstream renamed or regenerated a file, such as the hashed webview assets. With `-X subtree=T3000`, a trial merge of upstream's two newest commits (422 files) on 2026-09-30 had no conflicts, and every file landed under `T3000/`.
