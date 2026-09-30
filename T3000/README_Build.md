Using Visual Studio to compile T3000
============================

We have setup CI using MsBuild and github actions. This document explains compilation running locally on your machine.

> **In this repository T3000 is under `T3000\`**, and the repository root is T5000, the configuration tool replacing T3000's screens. Every path in this file is relative to `T3000\`, this file's folder, and every command is run from there. [`../README_Build.md`](../README_Build.md) covers building T5000, its checks against T3000, CI, and taking T3000's changes from upstream.

> **Note on the solution name.** The active solution is still called `T3000 - VS2019.sln`, but the name is historical. Its projects were migrated to the **v143** platform toolset (the Visual Studio 2022 toolset), so Visual Studio 2019 can no longer build it -- VS2019 ships v142 and cannot install v143. Use Visual Studio 2022 or 2026.

Prerequisites
-------------
T3000 uses MFC, so make sure you have MFC installed within Visual Studio.

### Visual Studio 2026

In the Visual Studio Installer, select the **Desktop development with C++** workload, then add these under **Individual components**:

* `MSVC v143 - VS 2022 C++ x64/x86 build tools`
* `C++ MFC for v143 build tools (x86 & x64)`

Both are required. Installing the v143 *compiler* alone is not enough: VS2026 defaults to the v145 toolset and only registers a platform toolset it has the MSBuild integration for, so without the full v143 component the build stops at `MSB8020`. Without the MFC component it gets one step further and stops at `MSB8041`.

You do **not** need to install Visual Studio 2022 alongside VS2026. VS2026 can host the v143 toolset itself.

### Visual Studio 2022

Select **Desktop development with C++** and `C++ MFC for latest v143 build tools (x86 & x64)`.

### .NET Framework

The solution contains two C# projects (`T3000Controls` and `TemcoStandardBacnetTool`) that target **.NET Framework 4.5.2**. Its targeting pack is no longer bundled with current Visual Studio versions. If it is missing the build fails with `MSB3644`; install the 4.5.2 Developer Pack from https://aka.ms/msbuild/developerpacks.

Launching a Build
-----------------------------------------------------------

### From the IDE

1. Open **`T3000 - VS2019.sln`**.
2. Set the Solution Configuration to **Release** and the Solution Platform to **Win32**.
3. Build > Build Solution (Ctrl+Shift+B).

Output lands in `T3000 Output\release\`, and the main executable is `T3000.exe`.

### From the command line

`msbuild` is not on the PATH by default. Either open a **Developer PowerShell for VS** and run:

```
msbuild "T3000 - VS2019.sln" -p:Platform=x86 -p:Configuration=Release
```

or call MSBuild by its full path from an ordinary shell:

```
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "T3000 - VS2019.sln" -p:Platform=x86 -p:Configuration=Release
```

`Platform=x86` is the solution-level name for the projects' `Win32` configuration; the two refer to the same build.

> Always build the **solution**, never an individual project file. The T3000 project's post-build step copies files using `$(SolutionDir)`, which falls back to the project's own directory when you build `T3000\T3000_VS2019.vcxproj` directly, and the copy then fails with `MSB3073`.

> Run the `-p:` switches from PowerShell or cmd. Git Bash rewrites arguments that look like paths and mangles them.

What you do not need
-----------------------------------------------------------

Several dependencies look like they are required but are committed to the repository already:

* **Rust / cargo** -- only needed to *modify* the webview API. The compiled `t3_webview_api.dll` and its import library are committed under `DLLs\T3WebviewApi\`.
* **Node / npm** -- only needed to *modify* the webview UI. The built assets are committed under `T3000\ResourceFile\webview\`.
* **NuGet restore** -- the `packages\` directory is committed, so WebView2 and the Windows Implementation Library resolve without a restore step.

A clean checkout therefore builds with nothing but Visual Studio installed.

Submodules
-----------------------------------------------------------

The repository declares one active submodule, `T3000Webview`, the source of the webview UI. The solution does not need it: no project in `T3000 - VS2019.sln` lives under it or references it, and the built UI is committed (see above). Initialise it only to work on that UI:

```
git submodule update --init T3000Webview
```

T5000's CI and `..\scripts\ci-local.ps1` skip them, and `ci-local.ps1` checks on each run that the solution still does not reference one. Only `T3000.yml`, the full T3000 build run by hand, checks them out.

`.gitmodules` also lists `T3000_CrossPlatform` and `PartsAndVendors`, but neither has a corresponding entry in the index, so git ignores them. This is harmless.

What to do if CI or local build fails with a build error
-----------------------------------------------------------
* If the above fails this is mostly due to:
   * Compilation errors in one or more CPP files
   * Developer _forgot_ to add new files to the project: the `.vcxproj` the file belongs to, such as `T3000\T3000_VS2019.vcxproj`. The build uses MSBuild and the project files; the `CMakeLists.txt` files in the tree are not part of it.
   * A conformance failure: the solution builds T5000's checks against T3000, `..\conformance\T5000Conformance.vcxproj`, and a change to a T3000 file they read can fail them. [`../README_Build.md`](../README_Build.md) says what they check.

Common first-time failures and what they mean:

| Error | Cause | Fix |
| --- | --- | --- |
| `MSB8020` | v143 platform toolset not installed | Add the `MSVC v143` individual component |
| `MSB8041` | MFC not installed for the toolset in use | Add `C++ MFC for v143 build tools` |
| `MSB3644` | .NET Framework 4.5.2 targeting pack missing | Install the 4.5.2 Developer Pack |
| `MSB3073` | Built a `.vcxproj` directly instead of the `.sln` | Build `T3000 - VS2019.sln` |

### CI

T3000 is built by hand, from the Actions tab: `T3000.yml`, "T3000 (run by hand)", builds all of `T3000 - VS2019.sln`. The checks on each push or pull request are T5000's; [`../README_Build.md`](../README_Build.md) lists the workflows.
