# T5000

A standalone configuration tool for Temco controllers, replacing T3000's MFC
screens one at a time. It is a separate process with a web UI, served on
`http://127.0.0.1:8730`. The goal is every screen T3000 has except the
graphics editor, for every product family T3000 supports. T3000's source is
the reference for what each screen shows and sends. It is not a foundation:
T5000 links no MFC and runs none of T3000's device code.

This file says where the project stands and how to work on it. The details
are in:

- [`READ_PATH.md`](READ_PATH.md): how a point is read. What goes on the wire,
  where it is sent, what is read before the inputs, and how each value is
  shown.
- [`docs/t5000-migration-plan.md`](../docs/t5000-migration-plan.md): the plan
  for bringing T3000's screens across, stage by stage, with what is done and
  what is next.
- [`README_Build.md`](../README_Build.md): building the solution, T5000
  included.

## Where it stands

As of 2026-09-25. **Nothing in T5000 has been run against a real controller
yet.** Everything below is checked against T3000's source and against
synthetic devices on loopback.

| Area | State |
| --- | --- |
| Scan | Done. Broadcasts T3000's discovery query on a chosen interface and lists what answers. |
| Device list | Done. Identifies each product and flags problems the scan noticed, as repairs for someone to approve later. Nothing is written to a device. |
| Saved device list | Done. Every device with a serial number that answers a scan is saved in `T5000.db` and listed again the next time T5000 starts, with when it was last seen. (A device reporting no serial is listed but cannot be saved: there is nothing to know it by next time.) Each can be given a name, building, floor and room, and the list is grouped by them. A device can be forgotten. |
| Virtual devices | Not started. Next; see the migration plan. |
| Inputs | Done for the five BACnet private-data products (CM5, MiniPanel, MiniPanel ARM, ESP32 T3, TSTAT10) over BACnet/IP. The grid matches T3000's column by column, including the panel's own custom range names and its row count per model. The Panel and Type columns are not done. |
| Outputs, Variables | Not started. They use the same point-struct path as Inputs, and their structs are already guarded. |
| Every other screen | Not started. See the migration plan's stages. |
| Writes | Not started. They will come as a separate transport, with per-action approval and a read-back that confirms each one. |
| Tstats and Modbus modules | Identified in the device list; not read. They need the register path, which does not exist yet. |
| Devices behind a controller | Refused, with the controller named. T3000 reaches them through the controller over Modbus; T5000 does not yet. |

The Inputs page shows built-in fixture points when no device is selected,
labelled as such. The Connection dialog saves transport settings to
`T5000.connection.json` beside the exe. Nothing reads them yet: a scanned
device is read at the address its scan response came from, and a device from
the saved list at the address it answered from last time.

## Safety rules

- **The scan and the read path cannot write.** Neither transport can express
  a write. The scan can only broadcast the discovery query and receive
  (`discovery/scanner.h`), and a read can only send a command on the
  `ReadCommand` whitelist (`bacnet/command.h`). A compile-time guard checks
  the whitelist against T3000's command codes and against a list of codes
  that must never be sent (`conformance/command_guard.cpp`).
- **The device list is a file on this machine.** Saving it, naming a device
  and forgetting one change `T5000.db` and nothing else. None of it is sent to
  a device. T5000 will not write to a database it did not create, such as one
  of T3000's.
- **The UI is bound to loopback, and answers only its own page.** The server
  binds 127.0.0.1 only (`http/server.cpp`). Loopback keeps other machines
  out but not other web pages, so a request whose `Origin` is not T5000's,
  or whose `Host` is not 127.0.0.1 or localhost, is refused before any route
  runs (`from_this_tool`, `http/server.h`). Remote access is a later decision
  that will come with authentication.
- **Test against loopback synthetic devices, never real ones.** Selecting a
  device and opening Inputs sends it real requests, and that includes a
  device from the saved list, at its saved address. Start T5000 with
  `--no-browser` and `--db` naming a scratch file, scan with interface
  127.0.0.1, and select only devices you are serving yourself.

## Build, run, test

T5000 has its own solution, `T5000.sln`, and builds with nothing else from
the repository: it compiles only what is in this folder and links only
Windows libraries. No MFC, no .NET, no T3000 project.

```
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "T5000\T5000.sln" -p:Platform=x86 -p:Configuration=Release
```

The exe lands in `T5000\bin\Release\T5000.exe`, and `T5000.db` and
`T5000.connection.json` are kept beside it. Stop any running T5000 first,
since a running server locks the exe.

| Command | Does |
| --- | --- |
| `T5000.exe` | Serves the UI on `http://127.0.0.1:8730` and opens a browser |
| `T5000.exe --no-browser` | The same, without the browser; for scripted runs |
| `T5000.exe --db <file>` | Keeps the device list in `<file>` instead of `T5000.db` beside the exe |
| `T5000.exe --selftest` | Runs every self-test and exits non-zero if any fails |

The self-test also runs after every build, and a failing check fails the
build.

**The checks against T3000 are in `conformance/`**, a separate project that
`T3000 - VS2019.sln` builds and runs, not T5000's own build. They hold
T5000's copies of T3000 to the originals:

- the point, settings and command layouts against `CM5/ud_str.h`;
- every product code against `ProductModel.h`;
- the display tables and copied constants against `global_define.h`;
- the oracle: T3000's BACnet DLL encodes a request, and T5000's bytes must
  match it.

A change to `wire/`, `bacnet/command.h`, `device/product.h` or
`display/tables.h` is not checked against T3000 until those run. Build
`-t:T5000Conformance` or run `scripts/ci-local.ps1` before pushing one.
[`README_Build.md`](../README_Build.md) has both.

## Layout

| Folder | Holds |
| --- | --- |
| `app/` | What the routes serve: the Inputs read in page order, the device list kept in step with its saved copy, and the JSON the pages get |
| `bacnet/` | Private-transfer requests and replies, and the command whitelist |
| `conformance/` | The checks against T3000: its headers, its tables and its BACnet stack. A separate project, `T5000Conformance.vcxproj`, built by `T3000 - VS2019.sln` |
| `device/` | Product identity (`ProductClassId` and `MiniType`, kept as distinct types), the device registry, read-path choice, row limits, connection settings |
| `discovery/` | The scan: the query, parsing the responses, the scanner |
| `display/` | Ports of how T3000 turns a point into grid text, and the tables it uses |
| `http/`, `json/`, `net/` | A small loopback HTTP server, a JSON reader, local interfaces |
| `store/` | The saved device list, on the SQLite that ships with Windows |
| `testing/` | The check macros, a scripted transport, and temporary files for the tests |
| `web/` | The two pages, embedded as strings |
| `wire/` | The struct layouts from `T3000/CM5/ud_str.h`, each offset T5000 uses guarded by `conformance/` |

New source files go in `T5000.vcxproj`, and must not include anything
outside this folder: T5000 builds on its own, and CI's `t5000` job checks
out only this folder to prove it. Only `conformance/` reaches into T3000. MSBuild puts every object file for
this project in one directory, so two `.cpp` files with the same name in
different folders overwrite each other's object; give each a unique name.
