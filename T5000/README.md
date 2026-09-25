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

As of 2026-09-24. **Nothing in T5000 has been run against a real controller
yet.** Everything below is checked against T3000's source and against
synthetic devices on loopback.

| Area | State |
| --- | --- |
| Scan | Done. Broadcasts T3000's discovery query on a chosen interface and lists what answers. |
| Device list | Done. Identifies each product and flags problems the scan noticed, as repairs for someone to approve later. Nothing is written. |
| Inputs | Done for the five BACnet private-data products (CM5, MiniPanel, MiniPanel ARM, ESP32 T3, TSTAT10) over BACnet/IP. The grid matches T3000's column by column, including the panel's own custom range names and its row count per model. The Panel and Type columns are not done. |
| Outputs, Variables | Not started. They use the same point-struct path as Inputs, and their structs are already guarded. |
| Every other screen | Not started. See the migration plan's stages. |
| Writes | Not started. They will come as a separate transport, with per-action approval and a read-back that confirms each one. |
| Tstats and Modbus modules | Identified in the device list; not read. They need the register path, which does not exist yet. |
| Devices behind a controller | Refused, with the controller named. T3000 reaches them through the controller over Modbus; T5000 does not yet. |

The Inputs page shows built-in fixture points when no device is selected,
labelled as such. The Connection dialog saves transport settings to
`T5000.connection.json` beside the exe. Nothing reads them yet: a scanned
device is read at the address its scan response came from.

## Safety rules

- **The scan and the read path cannot write.** Neither transport can express
  a write. The scan can only broadcast the discovery query and receive
  (`discovery/scanner.h`), and a read can only send a command on the
  `ReadCommand` whitelist (`bacnet/command.h`). A compile-time guard checks
  the whitelist against T3000's command codes and against a list of codes
  that must never be sent (`bacnet/command_guard.cpp`).
- **The UI is bound to loopback.** The server binds 127.0.0.1 only
  (`http/server.cpp`). Remote access is a later decision that will come with
  authentication.
- **Test against loopback synthetic devices, never real ones.** Selecting a
  scanned device and opening Inputs sends it real requests. Start T5000 with
  `--no-browser`, scan with interface 127.0.0.1, and select only devices you
  are serving yourself.

## Build, run, test

Build the solution as [`README_Build.md`](../README_Build.md) describes, or
just T5000:

```
& "C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe" "T3000 - VS2019.sln" -t:T5000 -p:Platform=x86 -p:Configuration=Release
```

The exe lands in `T3000 Output\release\T5000.exe`. Stop any running T5000
first, since a running server locks the exe.

| Command | Does |
| --- | --- |
| `T5000.exe` | Serves the UI on `http://127.0.0.1:8730` and opens a browser |
| `T5000.exe --no-browser` | The same, without the browser; for scripted runs |
| `T5000.exe --selftest` | Runs every self-test and exits non-zero if any fails |

The self-test also runs after every build (the post-build step passes
`--source-root`, so the tests find the T3000 source), and a failing check
fails the build. Some of the tests read T3000's own headers and fail if a
table or constant T5000 copied has drifted from them. One, the oracle, has
T3000's BACnet DLL encode a request and requires T5000's bytes to match.

`scripts/ci-local.ps1` builds a clean checkout the way CI does; see
[`README_Build.md`](../README_Build.md).

## Layout

| Folder | Holds |
| --- | --- |
| `app/` | What the routes serve: the Inputs read in page order, and the JSON the pages get |
| `bacnet/` | Private-transfer requests and replies, the command whitelist, and the oracle |
| `device/` | Product identity (`ProductClassId` and `MiniType`, kept as distinct types), the device registry, read-path choice, row limits, connection settings |
| `discovery/` | The scan: the query, parsing the responses, the scanner |
| `display/` | Ports of how T3000 turns a point into grid text, and the tables it uses |
| `http/`, `json/`, `net/` | A small loopback HTTP server, a JSON reader, local interfaces |
| `testing/` | The check macros and a scripted transport for the tests |
| `web/` | The two pages, embedded as strings |
| `wire/` | The struct layouts from `T3000/CM5/ud_str.h`, with compile-time guards on every offset T5000 uses |

New source files go in `T5000.vcxproj`. MSBuild puts every object file for
this project in one directory, so two `.cpp` files with the same name in
different folders overwrite each other's object; give each a unique name.
