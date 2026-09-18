Using Visual Studio to compile T3000
============================

We have setup CI using MsBuild and github actions. This document explains compilation running locally on your machine.

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

The repository declares one active submodule, `T3000Webview`. Clone with submodules, or initialise it afterwards:

```
git submodule update --init T3000Webview
```

`.gitmodules` also lists `T3000_CrossPlatform` and `PartsAndVendors`, but neither has a corresponding entry in the index, so git ignores them. This is harmless.

What to do if CI or local build fails with a build error
-----------------------------------------------------------
* If the above fails this is mostly due to:
   * Compilation errors in one or more CPP files
   * Developer _forgot_ to add new files to CMakeLists.txt

Common first-time failures and what they mean:

| Error | Cause | Fix |
| --- | --- | --- |
| `MSB8020` | v143 platform toolset not installed | Add the `MSVC v143` individual component |
| `MSB8041` | MFC not installed for the toolset in use | Add `C++ MFC for v143 build tools` |
| `MSB3644` | .NET Framework 4.5.2 targeting pack missing | Install the 4.5.2 Developer Pack |
| `MSB3073` | Built a `.vcxproj` directly instead of the `.sln` | Build `T3000 - VS2019.sln` |

### A note on the CI badge

The GitHub Actions build has been failing since around 2026-06-10, when the `windows-latest` runner image moved to Visual Studio 2026. The runner image does not carry `C++ MFC for v143` or the .NET Framework 4.5.2 targeting pack, so the workflow hits `MSB8041` and `MSB3644` before compiling anything. This is an environment gap in the workflow, not a defect in the source -- the same commit builds cleanly on a local machine that has both components installed.
