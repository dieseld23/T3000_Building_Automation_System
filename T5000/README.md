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

As of 2026-09-28. **Nothing in T5000 has been run against a real controller
yet.** Everything below is checked against T3000's source and against
synthetic devices on loopback.

| Area | State |
| --- | --- |
| Scan | Done. Broadcasts T3000's discovery query on a chosen interface and lists what answers. |
| Serial ports | Scanning done; reading not yet. The Devices page lists the computer's COM ports, read from the registry without opening any, and a port picked there is scanned at 9600, 19200, 38400, 57600, 76800 and 115200 baud in turn, as T3000 scans one. At each rate it listens first, then finds each device by halving the id range, and it can send only the range query and a read of registers 0-9: nothing is written. A line that runs MS/TP, or where something else is talking, is left alone. The devices found are listed and saved with their port, rate and id. Tested on com0com's virtual pair only; no real adapter has been opened. Reading their points is still to do; see the migration plan. |
| Device list | Done. Identifies each product and flags problems the scan noticed, as repairs for someone to approve later. Nothing is written to a device. |
| Saved device list | Done. Every device with a serial number that answers a scan, or is found by Find, is saved in `T5000.db` and listed again the next time T5000 starts, with when it was last seen. (A device reporting no serial is listed but cannot be saved: there is nothing to know it by next time.) Each can be given a name, building, floor and room, and the list is grouped by them. A device can be forgotten. |
| Devices added by hand | Partly done. A device no scan has found - on another network, or not installed yet - can be added with its model and serial, named and placed. The models are T3000's Add virtual device list (a T3-OEM is a TSTAT10 set up as panel type T3_OEM), held to T3000's source by the conformance checks, with "Model not known" for each product and every other product as itself. It is saved and shown as "added by hand", and nothing is sent to it unless Find is clicked. Once its model is chosen (Edit; a CM5 needs none), its inputs can be configured on the Inputs page before it is found, every column T3000's grid lets be changed: Full Label, Label, Auto/Manual, Value, Range (from the ranges T3000's Range dialog offers each input of that model), and an analog input's Calibration, Sign, Filter and Signal Type, by T3000's rules, kept in `T5000.db`. Its inputs can also be imported from a `.prog` file T3000 saved from it: matched by its serial and model, asked first, and keeping only what the operator sets. It can be exported as a `.prog` file for T3000's Load File: its inputs, and T3000's defaults for a new panel for everything else, after a warning read from the file that says what Load File would put in place of a panel's. When a scan, or Find, finds its serial, that device takes its place, keeping the name, the location and those changes, which its Inputs page says are not written yet. Outputs and variables are still to do; see the migration plan. |
| Find at an address | Done. For a device a scan does not reach, such as one across a router: Find, on the Devices page, asks the address and port the operator gives for the panel's settings - one read, sent when Find is clicked - and the device is found if they give its serial. It is offered for a device with a serial that has not answered a scan since T5000 started, of the five private-data products. A device found is saved at that address, and every later read checks its serial again first. |
| Virtual devices | Done, for inputs. A virtual device is a configuration with no device behind it: made on the Devices page (Add virtual device) from one of the models T3000's Add virtual device list offers, and named and placed like any device. T5000 gives it a serial from 4278190080 (0xFF000000) up, which neither of T3000's serial repairs can produce, and keeps it apart from the real devices: a device that answers with the same serial is listed as another device, and neither takes the other's configuration. Nothing is ever sent for it: the Inputs, Outputs and Variables reads and Find refuse it. Its inputs are configured on the Inputs page as a device added by hand's are, and a `.prog` file saved from any device of its model can be imported into it, whatever the file's serial. Its model can be changed, to another model only. It is exported as a device added by hand is. This build raises `T5000.db` to schema 5, which an older T5000 refuses to open rather than hide the virtual devices in it. |
| Inputs | Done for the five BACnet private-data products (CM5, MiniPanel, MiniPanel ARM, ESP32 T3, TSTAT10) over BACnet/IP. The grid matches T3000's column by column, including the panel's own custom range names and its row count per model. The Panel and Type columns are not done. |
| Outputs | Done for the same five products over BACnet/IP. The grid matches T3000's column by column: the HOA Switch column and the rows it marks, each model's row count, the panel's custom digital range names, and outputs on T3 expansion modules. The Panel, Type and Product Name columns are not done. |
| Variables | Done for the same five products over BACnet/IP. The grid matches T3000's column by column: every variable the panel has, three decimals, times, the fixed and the panel's own state pairs, the panel's own units, and names from its multi-state tables. |
| Firmware | Checking only; nothing is sent. The Firmware page, last in the bar (T3000's Tools menu, Load firmware for a single device, which T3000 opens on Ctrl+R; the Ctrl+F2 its menu shows is T3000's factory reset), lists each device with its product, firmware, the path ISP would take to it, its bootloader's version and whether the last scan found it in its bootloader. A `.hex` or `.bin` picked from disk is read as ISP would read it on that path, up to 16 MiB, and checked against the device by ISP's rules and T5000's stricter ones, with the reasons. Read asks a panel for its settings, one request (sent once more if nothing answers), for its bootloader's version, which Find and the points pages keep too. Sending the file comes with the synthetic bootloaders; see [the firmware plan](../docs/t5000-firmware-plan.md). |
| Every other screen | Not started. The bar across the top of every page names them all in T3000's toolbar order, dimmed until they are built; Alt+I, Alt+O and Alt+V open Inputs, Outputs and Variables, as in T3000. See the migration plan's stages. |
| Writes | Started. A write of inputs can be encoded, and a panel's answer classified, byte for byte as T3000's BACnet stack does it, but nothing can send one yet. Writing one input's Filter, approved in the page and confirmed by reading it back, is next. See the migration plan's Writes. |
| Tstats and Modbus modules | Identified in the device list; not read. They need the register path, which does not exist yet. |
| Devices behind a controller | Refused, with the controller named. T3000 reaches them through the controller over Modbus; T5000 does not yet. |

The Inputs, Outputs and Variables pages show built-in fixture points when no
device is selected, labelled as such. The Connection dialog saves transport settings to
`T5000.connection.json` beside the exe. Nothing reads them yet: a scanned
device is read at the address its scan response came from, and a device from
the saved list at the address it answered from last time. A device no scan has
found since T5000 started has nothing read beyond its settings unless they
give its saved serial, and the page says when it was last seen, or that Find
found it. A device added by hand that has not been found is sent nothing at
all, unless the operator clicks Find, which sends the address given one read
of its settings. A virtual device is sent nothing, ever.

## Safety rules

- **The scans and the read path cannot write.** None of their transports can
  express a write. The scan can only broadcast the discovery query and
  receive (`discovery/scanner.h`). The serial scan can only send the range
  query and the read of registers 0-9 (`serial/rtu.h`), and opens a port
  only when the operator picks it, and only one Windows lists as a serial
  port (`discovery/com_port_line.h`). A read can only send a command on the
  `ReadCommand` whitelist
  (`bacnet/command.h`). A compile-time guard checks
  the whitelist against T3000's command codes and against a list of codes
  that must never be sent (`conformance/command_guard.cpp`).
- **A write is another type, and nothing can send one yet.** Its command is a
  `WriteCommand` (`bacnet/write_command.h`), which no read function takes, and
  only the writes listed there can be encoded. Every other write code in
  T3000's header is on a held list or a never-write list, and
  `conformance/write_command_guard.cpp` checks the lists cover the header.
  `conformance/write_separation_guard.cpp` fails if the read path, or anything
  outside `bacnet/`, includes the write code.
- **Firmware is read and checked, never sent.** `firmware/` reads a file it
  is given and says whether ISP would take it for a device. The separation
  guard holds it to naming no socket, serial port or file, and holds the
  Firmware page's check (`app/firmware_page.*`) to reaching no transport
  through anything it includes. Only `/api/firmware/check` takes a request
  larger than 256 KB, up to 16 MiB, and only from T5000's own page or a
  local program that names no other page (`from_this_tool`).
- **The device list is a file on this machine.** Saving it, naming a device,
  adding one by hand, configuring one offline and forgetting one change
  `T5000.db` and nothing else.
  None of it is sent to a device. T5000 will not write to a database it did not create, such as one
  of T3000's.
- **The UI is bound to loopback, and answers only its own page.** The server
  binds 127.0.0.1 only (`http/server.cpp`). Loopback keeps other machines
  out but not other web pages, so a request whose `Origin` is not T5000's,
  or whose `Host` is not 127.0.0.1 or localhost, is refused before any route
  runs (`from_this_tool`, `http/server.h`). Remote access is a later decision
  that will come with authentication.
- **Test against loopback synthetic devices, never real ones.** Selecting a
  device and opening Inputs, Outputs or Variables sends it real requests, and that includes a
  device from the saved list, at its saved address: at least the settings
  read, which is how T5000 checks it is still the same panel. Find sends the
  same read to whatever address is typed into it. Start T5000 with
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
- what the ports copy that no constant names - the models, the Variables
  grid, and the rules an offline change to an input follows, the Range
  dialog's among them - against T3000's source and resources, as text;
- the write lists against every write code in `CM5/ud_str.h`, and the read
  path kept apart from the write code;
- the firmware checks against ISP's source: its name tables and aliases,
  the file's bootloader flags and the bootloader rules, run over every
  name, product and version, and which serial thread checks which file;
- the oracle: T3000's BACnet DLL encodes a read or a write, and T5000's bytes
  must match it.

A change to `wire/`, `bacnet/command.h`, `bacnet/write_command.h`,
`bacnet/private_transfer.cpp`, `bacnet/private_write.cpp`,
`device/product.h`, `display/tables.h`, `offline/` or `firmware/` is not checked against
T3000 until those run. Build
`-t:T5000Conformance` or run `scripts/ci-local.ps1` before pushing one.
[`README_Build.md`](../README_Build.md) has both.

## Layout

| Folder | Holds |
| --- | --- |
| `app/` | What the routes serve: the Inputs, Outputs and Variables reads in page order, the device list kept in step with its saved copy, the Firmware page's check and its one read, and the JSON the pages get |
| `bacnet/` | Private-transfer requests and replies, the read command whitelist, and the write lists and encoder |
| `conformance/` | The checks against T3000: its headers, its tables and its BACnet stack. A separate project, `T5000Conformance.vcxproj`, built by `T3000 - VS2019.sln` |
| `device/` | Product identity (`ProductClassId` and `MiniType`, kept as distinct types), the device registry, read-path choice, row limits, connection settings |
| `discovery/` | The scan: the query, parsing the responses, the scanner |
| `display/` | Ports of how T3000 turns a point into grid text, and the tables it uses |
| `firmware/` | Reading a firmware file as ISP does on each path, and whether ISP would take it for a device. Nothing here sends, or opens a file |
| `http/`, `json/`, `net/` | A small loopback HTTP server, a JSON reader, local interfaces |
| `offline/` | The rules a change to an input of a device configured offline follows, ported from T3000's Inputs grid. No transport: nothing here can reach a device |
| `store/` | The saved device list and the offline configurations, on the SQLite that ships with Windows |
| `testing/` | The check macros, a scripted transport, and temporary files for the tests |
| `web/` | The pages, embedded as strings |
| `wire/` | The struct layouts from `T3000/CM5/ud_str.h`, each offset T5000 uses guarded by `conformance/` |

New source files go in `T5000.vcxproj`, and must not include anything
outside this folder: T5000 builds on its own, and CI's `t5000` job checks
out only this folder to prove it. Only `conformance/` reaches into T3000. MSBuild puts every object file for
this project in one directory, so two `.cpp` files with the same name in
different folders overwrite each other's object; give each a unique name.
