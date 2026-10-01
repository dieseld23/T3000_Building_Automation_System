# Bringing T3000's functionality into T5000

How T3000's screens map onto the new standalone tool, what genuinely blocks
what, and what is not worth bringing across.

**Scope, settled:** all 15 `WINDOW_*` screens except `WINDOW_SCREEN` (the
graphics editor), across all product families.

Derived by reading the source — two rounds of parallel surveys, each claim then
put through an adversarial pass. 15 port-class claims and 5 product-scope
claims were refuted and revised. Several findings below are corrections to
those surveys, made by opening the files.

**Nothing in T5000 has yet touched a live controller.** Every claim here is
source-against-source, except the struct sizes, which the compiler asserts.

**Where it stands, 2026-09-30:** Stage 0 is done, and the device list is
saved between runs, as T3000's building database is ([The device
list](#the-device-list-and-virtual-devices)). A device added by hand can have
its inputs configured before a scan finds it, kept in the device list and sent
nowhere, and so can a virtual device, a configuration with no device behind
it, and their inputs are imported from and exported to T3000's `.prog`
files. A device no scan reaches can be found at an address the operator
gives ([part 2](#the-device-list-and-virtual-devices)). In Stage 1, Inputs,
Outputs and Variables are read and shown as T3000 shows them, after each
panel's settings and custom range names (#17). The Panel and Type columns are
still to do. Stage 2 has started: a write of inputs can be encoded, and a
panel's answer classified, but nothing can send one yet ([Writes](#writes)).
The Firmware page checks a file against a device and sends it nowhere, and
the synthetic bootloaders a flash will be tested against are built ([the
firmware plan](t5000-firmware-plan.md)).
The stage table below has each stage's state, and [Next](#next) is the list of
what comes after. [`README.md`](../README.md) describes the tool as
it is today.

T5000 is the repository root, and T3000 is under `T3000/`. T5000 builds on
its own, from `T5000.sln`: it compiles nothing of T3000's, links only
Windows libraries, and CI builds it from a checkout of everything but
`T3000/`. What it copies from T3000 - layouts, codes, tables - is checked
against the originals by `conformance/`, which T3000's solution,
`T3000/T3000 - VS2019.sln`, builds with T3000's BACnet stack and runs.

T3000's source is cited as T3000's own repository names it, relative to
`T3000/` here: `ISP/ComWriter.cpp` is `T3000/ISP/ComWriter.cpp`, and
`T3000/global_define.h` is `T3000/T3000/global_define.h`. T5000's files
are named from the repository root.

---

## The headline: additive, not multiplicative

The worry was that supporting every product multiplies the wire-format work.
It does not.

**There is one point-struct layout.** Every product that uses the struct path
uses the same `Str_in_point` / `Str_out_point` / `Str_variable_point`. All
three are copied in `wire/` and guarded field-by-field against
`CM5/ud_str.h` by `conformance/`, compiler-enforced when
`T3000/T3000 - VS2019.sln` builds:

| Struct | Size | Guarded |
|---|---|---|
| `InputPoint` | 46 | ✓ every field |
| `OutputPoint` | 45 | ✓ every field |
| `VariablePoint` | 39 | ✓ every field |

The panel's settings block and its two custom-range tables, which the Inputs
read asks for first, are guarded as well (`conformance/panel_guard.cpp`), on the
fields T5000 reads.

**There are three data paths, not thirty.**

1. **BACnet private-data** — `GetPrivateData_Blocking`. The struct read.
2. **Modbus register maps** — same structs, different register offsets.
   Resolved by name through `_P()` (`T3000RegAddress.cpp:42`), which is
   **data-driven, not a per-product switch**.
3. **Tstat registers** — `CTStatInputView` reads `product_register_value[]`
   directly and never touches the point structs at all. This is a genuinely
   separate data model and the only place the scope really widens.

So what all-products adds over a Tstat/T3-only build is: a capability table, a
point-count table, three register maps, and one second data model. That is one
implementation pass, not thirty.

---

## Corrections to the surveys

Four things did not survive checking. Recording them because three of the four
would have sized the project wrongly.

**`_P()` has three register maps, not six.** The plan claimed six, listing the
three real ones plus "product-specific variants" and "two additional industrial
variants". `T3000RegAddress.cpp:42-85` has exactly three branches —
`T3000_5ABCDFG_LED_ADDRESS`, `T3000_5EH_LCD_ADDRESS`, `T3000_6_ADDRESS` — and
returns `-1` for anything else. All three are legacy thermostat maps.

**CM5 outputs are not unfinished.** A verifier reported `PRODUCT_CM5` missing
from the output initialisation and concluded CM5 outputs were stub code. It is
at `BacnetOutput.cpp:262`; the verifier searched lines 421-550 and concluded
absence from a partial read. Claim withdrawn.

**`PM_*` and `T3_*` are two different axes, and I had been conflating them.**

| | `PM_*` | `T3_*` |
|---|---|---|
| Where | `ProductModel.h` | `global_define.h:1305-1338` |
| Count | ~90 | 30 |
| Meaning | what the **hardware reports** as `product_class_id` | what the **panel is configured as** (`Device_Basic_Setting.reg.mini_type`) |
| Drives | protocol and data path | point counts |

`BacnetInput.cpp:1305-1306` is where they meet:

```cpp
if ((Bacnet_Private_Device(selected_product_Node.product_class_id)) && Device_Basic_Setting.reg.mini_type != 0)
    bacnet_device_type = Device_Basic_Setting.reg.mini_type;
```

This matters for the scope question below: "all product families" means
something different depending on which list is meant.

**The two numbering schemes overlap, and share a variable.** This is new — no
survey found it. `bacnet_device_type` is assigned from `mini_type`, a `T3_*`
value (`BacnetOutput.cpp:1101`), and then compared in a single if-chain against
constants from *both* schemes (`BacnetOutput.cpp:421-550` tests `BIG_MINIPANEL`
and `T3_ESP_LW` alongside `PM_T38AI8AO6DO`, `PM_T322AI`, `STM32_CO2_NET`).

The high `T3_*` values deliberately mirror `PM_*` — which is why the enum has
gaps at 43, 44, 46, 53, 95:

```
MIRROR   43  PID_T322AI    == PM_T322AI          COLLIDE   9  T3_TSTAT10  vs PM_TSTAT8
MIRROR   44  T38AI8AO6DO   == PM_T38AI8AO6DO     COLLIDE  10  T3_BMS      vs PM_TSTAT10
MIRROR   46  PID_T3PT12    == PM_T3PT12          COLLIDE  21  T3_ESP_LW   vs PM_T3IOA
MIRROR   53  PID_T332AI    == PM_T332AI_ARM      COLLIDE  22  T3_NG3      vs PM_T332AI
MIRROR   95  PID_T36CTA    == PM_T36CTA          COLLIDE  26  T3_3IIC     vs PM_T3PT10
                                                 COLLIDE  29  T3_RMC1232  vs PM_T36CT
                                                 COLLIDE  31  T3_TSTAT11  vs PM_FWMTRANSDUCER
```

It works today only because the colliding cases are unreachable in practice — a
private-data panel's `mini_type` is never a Tstat model. That is an invariant
held by convention, not by the compiler.

**T5000 must not inherit this.** Two distinct types, not one `int`. This is
cheap to get right now and expensive to unpick later, and a misrouted product
id means reading the wrong registers on live equipment.

---

## A large fraction of "all products" was never finished

This finding matters most for scope. Several products exist
as enum entries that the shipping app does not fully implement. Supporting them
is **new product development, not porting** — there is no behaviour to copy.

**Never started** — enum entry only, marked "TBD" in the source:
`T3_ESP_TRANSDUCER`, `T3_ESP_TSTAT9`, `T3_ESP_SAUTER`.

**Zero I/O by design** — `T3_BMS`. Verified: `global_define.h:1402-1405` sets
all four counts to 0. A BMS is comms-only, so this is correct, not broken.

**Reported incomplete** by the surveys, each needing confirmation before being
either built or dropped: `T3_TSTAT11` (no count constants anywhere),
`T3_3IIC` (inputs only), `T3_OEM` / `T3_OEM_12I` (no init code),
`T3_TB_11I` (outputs only), `MINIPANELARM_NB` (zero points).

I have verified `T3_BMS` and refuted the `PRODUCT_CM5` claim directly. The rest
come from the surveys, and given that one of the six was wrong on inspection, I
would check each before acting on it.

---

## Screens by product: the rule and its exceptions

- **Every product** has Inputs and Outputs.
- **Full controllers** get all 14 in-scope screens.
- **Tstats** get the Tstat screen and use the separate register path — no
  Programs, no Controller.
- **Sensor and meter modules** get Inputs, Outputs, Settings, Remote Points.
- **Stubs** get nothing until someone decides they are real.

---

## Stages

Each ships on its own. Ordered by dependency, not by difficulty.

| Stage | Delivers | Changed by all-products? | State, 2026-09-30 |
|---|---|---|---|
| **0** | Discovery, selection, firmware detection, **product-identity model** | **Larger** — two id axes, capability table | Done (#9, #12, #13, #15). The device list is saved between runs (#19), and takes devices added by hand (#22, #24), whose inputs can be configured offline (#29), and devices found at an address (#31) |
| **1** | Inputs + Outputs + Variables read | Unchanged — one shared layout | Inputs done (#14, #16, #17), except the Panel and Type columns. Outputs done (#27), except those and Product Name. Variables done (#28) |
| **2** | Write support for points, then Arrays, PVar | Unchanged | Started 2026-09-27: a write can be encoded and its reply classified, byte for byte as T3000's stack does, but nothing can send one yet (W1a, #32). W1b, the first write that is sent, is next. See [Writes](#writes) |
| **3** | Device settings, user login | Slightly larger — per-product field ranges | Not started. The settings block is already read and guarded, for Inputs |
| **4** | PID loops, then Tstat | **Larger** — Tstat is a second data model | Not started |
| **5** | Weekly + annual schedules | Larger — Tstats encode schedules differently | Not started |
| **6** | Trend logs + alarms | Unchanged | Not started |
| **7** | Programs | Blocked on a decision; controllers only | Not started |

Stage 0 absorbs nearly all the widened scope. Stages 1, 2 and 6 are unaffected
because the struct layout is shared.

**Stage 0 now includes the product-identity model** — the typed distinction
between `product_class_id` and `mini_type`, plus the capability table. Getting
this right is what keeps every later stage from growing per-product branches.

**Stage 1 needs three source-side dependencies** that are not behind a DLL
export and must be ported as source: `GetInputLabelEx`
(`BacnetInput.cpp:2228`), `GetInputFullLabelEx` (`:2273`), and the
`Device_Basic_Setting` global — all called from `InputsData.cpp:62-63, 87`.
Plus the units tables (`global_define.h:823-894`), which are `CString` arrays.
That is the source-side `CString` cost, and it lands here, not later.

*Done for Inputs.* The grid itself, `BacnetInput.cpp:951-1237`, turned out to
be the thing to port rather than the two label helpers. It is in
`display/input_text.cpp`, with the tables in `display/tables.h`, checked
against `global_define.h` by `conformance/tables_guard.cpp` on every build of
T3000's solution. `Device_Basic_Setting` is read too
(`READ_SETTING_COMMAND`, `app/inputs_read.cpp`), and so are the custom range
tables; the row limits and per-model labels they drive are ported.

*Done for Outputs,* the same way: the grid, `BacnetOutput.cpp:658-1141`, in
`display/output_text.cpp`; the row limits, hand-off-auto switches and external
outputs by model in `device/output_rows.cpp`; the reads in
`app/outputs_read.cpp`, sharing the settings and custom-name reads with Inputs
(`app/panel_read.cpp`). Still to come: the Panel and Type columns, and
Outputs' Product Name.

*Done for Variables,* the same way: the grid, `BacnetVariable.cpp:209-478`, in
`display/variable_text.cpp`; the panel's custom units and multi-state tables
in `display/variable_ranges.cpp`; the reads in `app/variables_read.cpp`, with
the multi-state and unit reads beside the others in `app/panel_read.cpp`.
`conformance/variables_guard.cpp` checks the literals the port copies that no
constant names.

**Stage 2 is the cliff** — the first code that writes to live equipment.
Writes get their own transport, separate from the read path, which cannot
express one. Each write needs an explicit approval, and is done only when a
read-back confirms it. The scan's repairs are Modbus register writes, which a
private-transfer write cannot send, so they wait for the register path. See
[Writes](#writes) for how it is being built, and [what the write path must
not reproduce](#what-the-write-path-must-not-reproduce).

## Next

On 2026-09-27 the owner asked for every T3000 function in T5000, and allowed
writes: to loopback test panels first, then to a real device, with the owner
running the hardware checks. In this order:

1. **Stage 2, writes.** W1b first: one input's Filter, approved in the page
   and confirmed by reading it back. Then W2 to W10, as [Writes](#writes)
   orders them. Editing a device's points offline (item 2) changes the saved
   configuration, not a controller, and reaches one through the write path's
   review step (W6 there).
2. **Devices added by hand and virtual devices,** then **importing T3000's
   building database.** Parts 2 and 3 of [the device
   list](#the-device-list-and-virtual-devices). The owner settled
   decisions A to E there on 2026-09-26. Adding a device by hand is built,
   and so are configuring its inputs offline, every column T3000's grid lets
   be changed, D, importing its inputs from a `.prog` file saved from it,
   E, virtual devices, and exporting a device configured offline as a
   `.prog` file for T3000. Next comes C, in the order given there.
3. **Serial ports.** Scanning is built: a port picked on the Devices page is
   opened and scanned at each of the six rates, and the devices found are
   listed and saved with their port, rate and id. It is tested on com0com's
   pair only. Reading them is next, and MS/TP (join the ring) is designed
   with the owner first; see [Serial ports](#serial-ports).
4. **Finish Inputs and Outputs:** the Panel and Type columns, and Outputs'
   Product Name. Type needs `GetOutputType` and its input counterpart, and
   Product Name needs T3000's product names, which are not T5000's.
5. **The first hardware check,** which the owner runs on a controller, after
   W1b; [Writes](#writes) says what it looks at. It also settles two things
   about reads:
   - the serial check: the settings' `n_serial_number` must equal the serial
     in the scan response, or for a device from the saved list the saved
     serial, or the page refuses the panel;
   - whether a controller replies to the port a request came from.
     `READ_PATH.md` explains why that decides whether T5000 can run beside
     T3000.

   Later, when there is a Modbus device on firmware below 525 to try, the
   firmware gate (see the end of Risks).
6. **The other screens,** each read first, on the read path, and then
   written, admitting one write command at a time, as [Writes](#writes) does.
   In stage order: Arrays and PVars (2), device settings and user login (3), PID
   loops and the Tstat screen (4), weekly and annual schedules (5), trend logs
   and alarms (6), and programs (7), which still waits on its decision. Remote
   points, which no stage names yet, are placed when they are reached.
7. **The register path** for Tstats and the Modbus modules, starting with the
   guard on the Tstat registers described under Risks, and then a Modbus
   write path for Tstats and the scan's repairs.
8. **Files and firmware:** loading and saving `.prog` files (item 2's import
   and export, built for inputs), and updating firmware as T3000's ISP
   does. The Firmware page, which checks a file against a device and
   sends it nowhere, is built (F1), and so are the synthetic bootloaders
   in the self-test (F2a); their hosts on com0com and loopback are next.
   The firmware steps, and what waits for the owner, are in [the
   firmware plan](t5000-firmware-plan.md).

Smaller loose ends:

- `build_device_json` (`app/scan_json.h`) has tests but no route. It is for a
  device detail pane that does not exist yet.
- The Connection dialog saves settings that nothing reads yet. They will
  matter for MS/TP and Modbus. Until then, a device is read at the address
  its scan response came from.
- The device list shows the scan's firmware as a raw number. Check how
  T3000's device list shows it before changing it. The Inputs banner already
  shows the panel's firmware as T3000 does (`60.5`).

---

## The device list and virtual devices

T3000 keeps every device it has found in a database, one file per building
(`Database\Buildings\<main>\<building>.db`, table `ALL_NODE`, keyed on
`Serial_ID`). A scan updates the rows and never removes one, so a device that
is off still shows, under building, floor and room. A **virtual device** is a
row with no hardware behind it: T3000 keeps its points in a `.prog` file and
opens it like a real controller, with nothing sent anywhere.

T5000 does the same in four parts, each its own pull request.

**1. The saved list (#19).** `T5000.db`, beside the exe, or wherever `--db`
says. One file for every building, with building, floor and room as fields
the operator fills in, and the list grouped by them. A scan saves each device
that answered. A rescan updates what it saw and leaves the name, the location
and the first sighting alone. Each device says whether it answered the last
scan, or when it was last seen. It can be forgotten, which removes it from the
list and the file and does nothing to the device.

- The storage is the SQLite that ships with Windows (`winsqlite3.dll`), not
  the repository's `SQLiteDriver`, which is SQLite 3.4.0 from 2007 behind an
  MFC wrapper. `store/sqlite.h` says what that rules out.
- A device known only from the saved list takes no part in the
  duplicate-Modbus-id check. The list spans buildings and months, so two
  devices on id 5 in it are not a conflict until both answer.
- For the same reason, its Inputs are read only once its settings give its
  saved serial. A refusal or serial 0 stops the read after the settings, as a
  different serial does, and the page says to scan first
  (`READ_PATH.md`, "A device known only from the saved list must prove it").
- A file that is not a T5000 list (one of T3000's, say), or that a newer
  T5000 wrote, is refused and left as it was. T5000 then runs with the list in
  memory, and the page says it is not being saved.

**2. Devices added by hand, and virtual devices.** One design for both: a
device in the list that T5000 has not reached, with a configuration prepared
before anyone can reach it. A device added by hand stands for a real device
and is keyed on that device's serial. A virtual device stands for none.

*Built: adding a device by hand.* The operator picks a model and gives the
serial, both required, and a name and location if they like. A model is a
product and a panel type, as T3000's Add virtual device list has them: a
T3-OEM is a TSTAT10 set up as panel type `T3_OEM`. The list is
`device::known_models()`, T3000's entries in its order under the names its
Settings page uses (`Getminitypename`), held to both by
`conformance/models_guard.cpp`. Each product with models also has "Model not
known", panel type 0, and every other product in the capability table is
listed as itself, less the third-party entry, whose serial no scan reports.
The panel type chosen is shown as chosen, never as read, and a scan that
finds the device drops it, since a scan does not report one. The device is saved (schema 2 of `T5000.db` adds `added_by_hand`) and
listed as "added by hand". Nothing is sent to it (`plan_inputs_read` refuses
it): its Inputs page shows the configuration kept for it, below, or says why
there is none. When a scan finds its serial, that
device takes the entry's place, keeping the name and location, and is read
from then on. Adding only ever inserts, so an entry typed in cannot overwrite
a device a scan found.

What T3000 does:

- **Add Remote Device** (`ARDDlg.cpp`) needs the device online. It connects
  (`:206-258`) and reads the serial and product off it; both fields are
  read-only (`:58-59`). If it cannot connect it refuses (`:374`). It then
  deletes any row with that serial and inserts a new one on `floor1`, `room1`
  (`:343-358`), so adding a device again loses where it was.
- **Load File** (`MainFrm.cpp:15900`) reads a `.prog` into memory
  (`LoadBacnetBinaryFile`, `global_function.cpp:10506`), checking only the
  `55 FF` and the version. It then writes every section to whichever device
  is open (`Send_Set_Config_Command_Thread`, `MainFrm.cpp:5166`): inputs
  (`:5364`), outputs, variables, programs, PID loops, screens, holidays and
  schedules. Last comes the settings block (`WRITE_SETTING_COMMAND`, `:5764`).
  Of that, it keeps the open device's serial, object instance, panel number,
  Modbus id, IP address, subnet, gateway, MAC address and panel name
  (`global_function.cpp:11243-11272`), and writes the rest from the file:
  among it the panel type, DHCP or static, the serial ports' settings, the
  Modbus port and the time zone. It reads nothing first and nothing back, and
  no serial is compared, so one panel's file loaded onto another gives the
  second the first one's points, programs and schedules.
  *Corrected 2026-09-27:* this said the whole settings block was written,
  identity included.
- **A virtual device** (`BacnetAddVirtualDevice.cpp:187-220`) gets a random
  serial and a `.prog` file in `Database\temp`. Nothing writes one to
  hardware.

**Decisions for the owner, and what the owner decided (2026-09-26).**

*A. Where the offline configuration lives.*

1. `.prog` files, one per device, as T3000 keeps them. T3000 can open them,
   and their layout is already guarded. But a `.prog` holds a value for every
   field, so it cannot say which ones the operator changed, which C3 needs.
   And T3000 leaves the file behind when the device is deleted.
2. `T5000.db`. Each point is kept as the `ud_str.h` bytes it goes to the
   device as, with what the operator changed and what the device held when
   last read. One file, saved in transactions, that can say what changed.
   T3000 cannot read it.
3. **Recommended:** 2, with import and export of `.prog` files. An imported
   file is matched to a device by the serial in its settings block, and an
   export is for T3000.

**Decided: 3,** `T5000.db`, with import and export of a device's `.prog`
file.

*B. What must be entered.* A model and serial, as built. The panel type
(`mini_type`) decides how many points a MiniPanel, MiniPanel ARM or ESP32 T3
has, so there is no knowing what to configure without it. Picking a model
gives one, and "Model not known" leaves it out, so it is optional to add a
device, as recommended. Whether it is needed before its points are edited
offline:
**Recommended:** required.
**Taken as recommended,** without asking; the owner was told (2026-09-26).
A device's points are not configured offline until its model is chosen.

*C. How the offline configuration is compared with the device before any
write.* Every option goes through Stage 2's write path: a transport of its
own, and first the identity check (the settings give the entry's serial, and
the product matches). Then approval for each action, and a read-back. None
writes the identity fields of the settings block (serial, IP address, panel
number, Modbus id) from an offline configuration.

1. Write everything, as T3000's Load File does. Simplest, but it overwrites
   anything changed at the device since, by anyone.
2. Read the device, show every difference, and write the ones approved.
3. **Recommended:** only what was changed offline, three ways. For each
   field the operator changed, compare the device's value now with the value
   the change started from (the default, for a device never read).
   - If they match, the change is written.
   - If the device has moved as well, both are shown and the operator picks.
   - A field not changed offline is never written.

   It writes the least, and never undoes a change made at the device.
4. Never from T5000: export a `.prog` and load it with T3000.

**Decided: 3,** only what was changed offline.

*D. A device on another subnet.* Broadcast does not cross subnets, so a scan
never finds a device added by hand on another subnet, and it never becomes
readable.

1. Leave it so.
2. **Recommended:** let the operator give an address and find the device
   there: the same discovery query, sent to that one address instead of
   broadcast. That is the scan path, read-only by construction, and the
   device is matched by the serial in its answer.
   `Provenance::BacnetUnicast` exists for this.
3. Read it at the address given, under `Identity::MustConfirm`, without a
   scan.

**Decided: 2,** find it at an address. **Changed on 2026-09-26, before it
was built:** Find reads the panel's settings at the address instead of
sending it the discovery query. T3000 sends that query only to broadcast
addresses, so a controller's answer to one sent to its own address was
unproven, and T3000's own way of adding a device on another subnet (Add
Remote Device, `BacnetAddRemoteDevice.cpp:146-300`) talks BACnet to 47808,
not the query to 1234. The settings read is the one every points page starts
with, on the read-only transport, and gives the serial the device is matched
by. *Built;* see below.

*E. Virtual devices.*

1. **Recommended:** a virtual device is a configuration with no device
   behind it. It has kind `'virtual'` (the column allows it) and a serial
   from a range T5000 hands out and checks, and a scan never matches it. It
   reaches a real device only by copying its configuration to that device's
   entry, which then goes through C.
2. As T3000 does it: a random serial, matched like any other.

**Decided: 1,** a configuration with no device behind it. **And on
2026-09-27:** a `.prog` file imported into a virtual device is matched by
its panel type only, not its serial, since a virtual device's serial is
T5000's and no file carries it. A device added by hand, or found, is still
matched by its serial (A).

The order:

1. B's panel type. *Built.*
2. A's storage, with editing Inputs offline. *Built,* for every column
   T3000's grid lets be changed.
3. Import and export. *Built,* for inputs; see *Built: exporting* below.
4. D. *Built.*
5. E. *Built,* for inputs; see *Built: virtual devices* below.
6. C, the apply, once Stage 2's write transport and the first hardware check
   exist.

*Built: configuring a device's inputs offline.* A device added by hand whose
product T5000 reads by private transfer (the CM5, MiniPanel ARM, T3 Series
(ESP32) and TSTAT10), and whose model is chosen, has its inputs on the Inputs
page as T3000 holds them before it has read a panel: `Initial_All_Point`'s
defaults (`global_function.cpp:17693`), IN1 on, a filter of 5 and the rest
zero, in as many rows as the model has. The model is chosen with Edit on the
Devices page. It cannot be changed to one with fewer rows while an input past
them is changed. A CM5 needs no model: T3000 names none, and panel type 0 is
the CM5 itself. A MiniPanel cannot be configured offline: T3000's list names
no model of it either, and for a MiniPanel the model decides what its inputs
are.

Nine columns can be changed, every one T3000's grid lets the operator
change, by its rules (`Fresh_Input_Item`, `BacnetInput.cpp:451`, its clicks
on Value, Sign and Auto/Manual, `:1507-1654`, and its Range dialog, `:1656`
and `BacnetRange.cpp`). Which of them an input lets be
changed depends on the input as it stands, as T3000's grid enables its cells
(`Fresh_Input_List`, `:1010-1022` and `:1113-1120`), and the page offers
only those:

- **Full Label:** under 21 characters, and no input, output, variable, PVAR
  or program may have it already, case and all (`Check_FullLabel_Exsit`,
  `global_function.cpp:3151`).
- **Label:** under 9 characters, '-' made '_', a-z put in capitals, and no
  other input may have it (`Check_Label_Exsit`, `:3242`).
- **Auto/Manual:** flipped by a click.
- **Value:** only in Manual (`:1509`). An analog input's, or a digital one's
  on range 0, is typed, and kept as T3000 keeps it: the number times 1000,
  truncated (`:601`), so 1.001 is 1000 thousandths. A digital input on
  ranges 1-22 is switched between its two states by a click, which changes
  its state (the control byte) and leaves its value (`:1537-1550`). On the
  device's custom digital ranges, 23-30, it cannot be changed: T3000 waits
  for their names, which are read from the panel.
- **Range:** chosen from T3000's Range dialog's ranges, numbered as its box
  numbers them: 0 Unused, 1-22 a digital range, and above 30 the analog
  range 30 below (`OnOK`, `BacnetRange.cpp:1158`). Each input is offered
  what the dialog offers it (`Initial_static`, `:917-1026`). The fast pulse
  count and RPM replace the slow pulse count on the inputs that count fast
  pulses (a T3-BB's 27-32, a T3-LB's 11-17), and are the only choices on a
  T3-OEM's inputs 9-12; a T3-OEM's input 13 takes only a 10K Type2 sensor;
  the T3-OEM-12I's rows are four on from the T3-OEM's. A T3-FAN-MODULE's
  input 5, and the T3-OEM's other inputs, which the dialog names no branch
  for, are offered all three pulse ranges; every other input the slow one
  only. The rows T3000 fixes cannot be
  changed at all (`OnNMClickList1`, `BacnetInput.cpp:1657`): a T3-OEM's
  inputs 14-18, a T3-OEM-12I's 18-22, a TSTAT10's or TSTAT11's 10-13, and of the ESP32 T3
  panels a T3-RMC-1232's 9-12 and 33-48, a T3-BMS's 33-48, a T3-RMC's 17-18
  and a T3-NG2's 25-30. A range changes only whether the input is analog and
  its range; 0 makes it analog, as T3000's answer does (`:1778`).
- **Calibration:** an analog input's, in Auto as well as Manual. It is typed
  with its sign and kept as T3000 keeps it: the number as a float, times 10
  as a float, truncated, and its size in two bytes, up to 6553.5
  (`:631-661`). T3000 builds for x86 with the compiler's default floating
  point, so the product is rounded to a float before it is truncated: 0.7 is
  7 tenths, and 6553.59999 is refused.
- **Sign:** an analog input's. It is a button of its own in the Calibration
  cell, and changes once confirmed, as T3000's Sign column asks first
  (`:1553-1614`).
- **Filter:** 0 to 255, on an analog input only. T3000's grid disables the
  cell on a digital one, and a new input is digital, so it needs an analog
  range first. #29 let a digital input's filter be changed; that was wrong.
- **Signal Type:** an analog input's, while its range is one of the custom
  tables (Table 1-5, ranges 20-24), and never a T3-PT12's (`:1897`). It is
  chosen from T3000's list, which leaves out JumperStatus's index 4
  (`Initial_List`, `:416`), and the status in the same byte is kept.
  Thermistor Dry Contact is stored as 4, as T3000's grid stores it: it
  compares the name with each of JumperStatus's, with no break, and keeps
  the last that matches, and index 4 has index 0's name (`:678-691`).

Not offered, though the dialog has a button for each: PT 1K, which the dialog
enables only when the panel's settings say it has one (`special_flag`,
`BacnetRange.cpp:1017`), and nothing has been read from a device configured
offline; the
custom digital ranges 23-30, which are named from the panel and blank until
read; the buttons with no name; and the multi-state ranges, which the input
grid looks up in `Digital_Units_Array`, of 23 entries (`BacnetInput.cpp:1877`).

A change that leaves an input as it was is not saved. Each changed input is
kept in `T5000.db` (schema 3, table `offline_points`) as the 46
`Str_in_point` bytes it would be sent as, beside the bytes it started from,
which C compares with the device. An input put back as it started (Undo) is
removed, and forgetting the device removes all of its changes. The grid tints
each changed cell, as T3000 tints a cell it has changed
(`LIST_ITEM_CHANGED_BKCOLOR`, `BacnetInput.cpp:698`).

When a scan finds the device, it is read like any other, and its changes are
kept. Its Inputs page says which inputs were changed offline, and that they
are not written: T5000 cannot write to a device yet (C).

`conformance/offline_guard.cpp`, `conformance/input_range_guard.cpp` and
`conformance/input_cells_guard.cpp` hold all of this to T3000's source as
text; the second compares each range's caption with `T3000.rc`, and the third
checks that neither T3000 nor T5000 changes the compiler's floating-point
defaults. Where T5000 differs, on purpose:

- Text that does not fit in 20 or 8 bytes of the ANSI code page is refused.
  T3000 counts characters, not bytes, and keeps 21 or 9 bytes, which can drop
  the terminator.
- A character the code page cannot hold is refused, where T3000 keeps `?` or
  a look-alike, and so is a control character.
- A filter must be a whole number, and a value or calibration a number. T3000
  reads "12abc" as 12, and "abc" as 0.
- A value that does not fit a 32-bit count of thousandths is refused. T3000
  keeps -2147483.648 for it.
- The calibration's sign follows what is typed: -2 is minus, 2 is plus.
  T3000 sets minus for a number below zero and leaves the sign as it was for
  any other, so 2 typed after -2 is still -2 (`:635`). **Decided by the owner
  on 2026-09-26.** A calibration refused changes nothing, where T3000 has
  already set the sign when it refuses the size.
- A digital input's value is switched by the click alone. T3000 also opens
  the cell's editor after the click, and what is typed there goes to the
  value, which the grid does not show for a digital input.
- A digital input on a range above 30 has no value to change. T3000 opens
  the editor there, and keeps what is typed without showing it.
- The sign's confirmation always shows the calibration. T3000's shows it
  only when both of its bytes are non-zero (`:1564`), so not for 0.1 or 25.6.
- The page sends nothing when the signal type already shown is chosen again,
  since T3000 would store Thermistor Dry Contact as 4 where it was 0, which
  looks the same. Chosen in place of another type, it is stored as 4.
- Typing an input's own full label or label again changes nothing, and is not
  refused as a repeat.
- A full label is compared with the other points' default names (OUT1, VAR1,
  PVAR1, PRG1 and on), since only the inputs are configured. A device's own
  names may differ, so C must compare again with the device before a write.
- A range is chosen from those offered. T3000's dialog also takes any number
  typed in its box, offered or not, and keeps the range 30 below one above
  30 in a byte: 311 becomes range 25.
- Choosing Table 1-5 leaves the signal type as it is. T3000 copies the one
  its custom-table dialog leaves (`BacnetInput.cpp:1816`), and that is 0xff,
  signal type 15 and status 15, when the table's button was not clicked
  (`:1723`). The signal type is changed in its own column.

*Built: importing a device's inputs from a `.prog` file.* On the Inputs page
of a device configured offline, Import .prog takes a file T3000 saved, with
Save File, from that device. The page sends the file itself; T5000 reads
nothing from disk. It is imported only if:

- it is the format T3000 writes today: `55 FF`, version 5 to 8, and exactly
  as long as that version is (65,956 to 67,184 bytes). An older T3000's INI
  file is not read; T3000 opens it and saves it again as this format;
- the serial in its settings block is this device's, and not 0 (decision A);
- its panel type is the model chosen for the device. A CM5's is 0, its own.

The server first says what the import would do, and the page asks before it
is done. It replaces every change made on the page to the device's inputs,
in one transaction. Of each input the model shows, it keeps what the
operator sets: the full label and label, Auto/Manual, the range, the filter,
the calibration and its sign, and the signal type. It keeps the value, and
the control byte a digital input's value is, only for an input in Manual:
in Auto those are what the panel measured when the file was saved. It never
keeps the status or the external module's bytes, which the panel sets, so
C can only write what an operator set. Nothing else in the file is kept: not
its outputs, variables, programs, schedules or settings. Nothing is sent to
any device.

The operator's columns are kept as T3000 saved them, even where the page's
rules are stricter. So an imported input can be one the page would not let
be typed: a label with no terminating 0, a range the Range dialog does not
offer the row (PT 1K, a custom digital range, a fixed row's other range), a
filter or calibration on a digital input, a signal type while the range is
not Table 1-5, or a value in Manual on a digital input past range 22. Each is
what the panel held when the file was saved; refusing or changing it would
lose it.

For C: an imported input's change starts from T3000's default, so on the
device the file came from, every imported field is already the edited
value. C should treat a field whose device value equals the edited value as
nothing to write, not as the device having moved.

`offline/prog_file.cpp` reads the file; `conformance/prog_file_guard.cpp`
holds each table's count and item size to `global_define.h` and
`CM5/ud_str.h`, Save File's order, and Load File's tests of the first bytes
and of each version, and reads the sample file as T5000 does. Where T5000
differs, on purpose:

- A file whose length is not its version's is refused. T3000 reads past the
  end of one cut short.
- A file from another serial or model is refused. T3000 loads any file onto
  the open device.
- When T3000 opens a file offline, it makes each input with range 0 analog
  (`global_function.cpp:11315`). The import keeps the file's byte, as
  Load File does for a device that is online.

*Export, for the owner.* T3000's Load File writes every table of a `.prog`
file to the open panel, and the settings apart from its identity (above).
T5000 knows only the inputs of a device configured offline, so an export
would hold T3000's defaults for everything else: loaded onto a panel, it
would put back its outputs, variables, programs, PID loops, schedules and
screens as new, and could set its panel type, DHCP and serial ports from
the file. Options:

1. Export only for a device T5000 has read in full: every table, and the
   settings. Not possible until the other screens are read.
2. Export what T5000 has, with the defaults, behind a warning that says what
   Load File would put back.
3. No export for now. A device configured offline reaches a panel through
   C, which writes only what the operator changed, after checking the
   device's serial. Export follows 1 when T5000 reads every table.
   (Recommended before the owner decided.)

**Decided: 2** (2026-09-27), export with T3000's defaults, behind a warning.
The defaults are T3000's own for a panel it has not read
(`Initial_All_Point`) and for a new virtual device's settings
(`Initial_Virtual_Device_Setting`), with the device's serial and panel type.
The warning says what this file would put back over a panel's if loaded with
T3000's Load File, from the values it actually holds. Export is offered only
where T5000 holds the configuration: a device added by hand and not yet
found, and a virtual device.

*Built: exporting a device configured offline as a `.prog` file.* On the
Inputs page of a device configured offline, while the list is saved, Export
.prog makes a version 8 file of 67184 bytes. It holds the device's inputs as
configured here, and for every other table what T3000's Add virtual device
saves for a new panel (`BacnetAddVirtualDevice.cpp:201-225`):

- outputs OUT1 to OUT64 with the hand switch at Auto, variables VAR1 to
  VAR128, and programs PRG1 to PRG16 with no code (`Initial_All_Point`);
- the schedules' time flags all 0xFF, and every other table 0
  (`Initial_All_Point`, `ClearBacnetData`);
- the settings 0 but for the serial, the panel type, and
  `Initial_Virtual_Device_Setting`'s: ports 0 and 2 at 115200 baud, IP
  192.168.0.3, Modbus TCP port 502. Not the Modbus id, object instance or
  name T3000's dialog gives a virtual device: T5000 has none of them, and
  Load File keeps a panel's own.

Before the file is saved, the server says what T3000's Load File would do
with it, read back from the file's bytes rather than restated: the tables it
puts in place of the panel's and the ones it empties; what Load File keeps
of the settings (the serial, name, panel number, Modbus id, object instance,
IP address, subnet, gateway and MAC); and what it sets from the file, which
is the panel type, the IP address's mode, each serial port's mode and rate,
the Modbus TCP port, the MS/TP network and max master, the product field,
and whether any other setting is not 0. The page shows that and saves the
file only if the operator agrees. The file comes back in base64 and the
browser saves it; T5000 writes nothing to disk, and nothing is sent to any
device.

Loaded onto a panel, such a file sets serial ports 0 and 2 to not used at
115200 baud and port 1 to not used at 1200, the MS/TP network and max master
to 0, the product field to 0, and the IP address's mode to 0, which T3000's
Settings shows as Use The Following IP Address (below). The warning says
each of these, and to check the panel's address after loading the file.

The IP address's mode: T3000's Settings shows a `tcp_type` of 1 as Obtain
IP Address Automatically and 0 or 2 as Use The Following IP Address, and
writes 0 for the second (`BacnetSetting.cpp:361-371`,
`BacnetSettingTcpip.cpp:153-156` and `:204-207`). `ud_str.h`'s comment on
the field, and T3000's webview export (`BacnetWebView_Exports.cpp:380`),
say 0 is DHCP. The warning names the mode as the dialog shows it, says a
comment in T3000's source calls it DHCP, and asks for the panel's address
to be checked after loading; for 1, where every source agrees, it says the
panel takes its address from DHCP. The conformance checks pin the dialog.
What a panel does with 0 is for a hardware check.

Export is offered for every device configured offline: a controller T5000
reads by BACnet private transfer, added by hand and not yet found or
virtual, with its model chosen. The file's panel type is that model's. An
export imports back into the same device unchanged, but for the value of an
input in Auto, which an import does not keep.

*Built: finding a device at an address (D).* A device in the list that has
not answered a scan since T5000 started - added by hand, from the saved list,
or found before - and whose settings T5000 reads by private transfer has a
Find button on the Devices page. The operator gives an IPv4 address and a
port, 47808 unless changed. When Find is clicked, and only then, T5000 sends
that address one request, for the panel's settings (sent once more if nothing
answers), and nothing else. It is found when the settings give the device's
serial:

- The entry becomes the device (`Provenance::BacnetUnicast`, shown as
  "found"), at that address, with the panel type, Modbus id, instance and
  name its settings give. The product stays the entry's, since the settings
  do not give one, and the firmware is left for a scan. The name, the
  location and any inputs changed offline are kept, and the Inputs page lists
  those as not written.
- It is saved, and comes back next time from the saved list like any other
  device.
- Every read of it is held to the stricter rule (`Identity::FoundAtAddress`):
  unless its settings give its serial again, nothing more is read, and the
  page says to find it again. A scan that finds it vouches for it from then
  on.

Another serial, none, a refusal or silence is not found, and the list and the
file are left as they were. The address must be four numbers; a name is not
looked up, and 0.0.0.0, the broadcast address, multicast and the reserved
range from 240.0.0.0 are refused before anything is sent. Find is not offered
for a device a scan found this session (its address is the scan's), one on
another controller's bus, or a product whose settings T5000 does not read.

What T3000's virtual devices are, from `BacnetAddVirtualDevice.cpp` and
`global_function.cpp`:

- The product list is `init_product_list` (`global_function.cpp:11980`), 16
  entries of name, product id, `mini_type` and point counts. Only products 74
  and 88 get a `.prog` file (`BacnetAddVirtualDevice.cpp:187-203`). Its names,
  products and panel types are now `device::known_models()`, which
  `conformance/models_guard.cpp` reads from the source; its point counts
  are not ported yet.
- The `.prog` format is `SaveBacnetBinaryFile` (`global_function.cpp:12724`):
  `55 FF` then a version byte, then the point sections in a fixed order, as
  the `ud_str.h` structs T5000 already guards. Version 5 is 65,956 bytes,
  version 6 adds variable units (66,056), 7 adds multi-state ranges (66,608),
  and 8, which T3000 writes, adds schedule flags (67,184).
  `Documentation/BTUMeterRev22.prog` is a version 6 file to test against. The
  loader never checks the length; T5000's does.
- A new device's points are `Initial_All_Point`'s defaults (`global_function.cpp:17693`)
  and its settings `Initial_Virtual_Device_Setting`'s (`:17621`).
- Not to copy: the serial is `rand() % 10000000 + 1000000`, which on MSVC
  is 1,000,000 to 1,032,767, with no check for one already in use; a
  non-T3 product saves over whatever `.prog` was open last; adding one
  overwrites the open device's settings; the panel name can overflow its
  20 bytes; the name goes into SQL unescaped; deleting one leaves its
  `.prog` behind; and the `T3_3IIC` entry has its analog output count set
  from its input count, and its `sub_pid` set to `T3_NG3` (`:12113`), so
  a virtual 3IIC is made as an NG2.

The saved list's `kind` column already allows `'virtual'`, so the table does
not have to be rebuilt for this. A device added by hand is kind `'scanned'`,
with `added_by_hand` set. It is a real device a scan can find, and it must stay
one row with that device, which `UNIQUE (kind, serial)` holds only while both
are the same kind.

*Built: virtual devices.* The Devices page makes one (Add virtual device)
from one of `device::known_models()`, T3000's Add virtual device list, and
never "Model not known": a virtual device is nothing but its model and its
configuration. T5000 gives it the lowest serial from 0xFF000000
(4,278,190,080) that no device in the list has. Neither of T3000's serial
repairs can make one there: they give 200,000 to 300,000, or four bytes of
`rand() % 255`, none of which is 0xFF. It is saved as kind `'virtual'`, with
no address, and comes back as a virtual device after a restart. The registry
never merges it with a device that answers with its serial. That device is
listed apart, with its own offline changes, and forgetting one leaves the
other. Nothing is ever sent for it: `plan_points_read` refuses it for Inputs,
Outputs and Variables before it looks for an address, and Find is not offered
for it. Its inputs are configured offline as a device added by hand's are,
and a `.prog` file saved from any device of its model is imported into it
without comparing the file's serial (decided 2026-09-27); a device added by
hand still takes only a file with its own serial. Its model can be changed,
to another model only.

This build raises `T5000.db` to schema 5, with no table changes. Schema 4
lists and forgets only kind `'scanned'`, so an older T5000 would hide a
virtual device, and leave it behind when told to forget every device; it
refuses a schema 5 file instead, as one written by a newer T5000. It is
exported as a device added by hand is (*Built: exporting* above), in a file
named for its serial. Not built: its outputs and variables, and copying its
configuration to a real device's entry, which goes through C.

**3. Importing T3000's building database.** Read-only: T5000 reads the
`ALL_NODE` rows into its own list and never writes T3000's file. `ALL_NODE`
reuses columns (the IP address is in `Bautrate`, the port in `Com_Port`, and
`Screen_Name` holds the virtual flag), so each needs mapping, not copying.

**4. Editing a device's points offline**, for a device added by hand or a
virtual one. These are the first edit screens, working against the saved
configuration rather than a controller. See A above and Next. Inputs have
started; see *Built: configuring a device's inputs offline* above.

---

## Serial ports

The owner asked (2026-09-25) for devices on a serial line, RS485 or a USB
adapter to one, to be reached through the computer's COM ports. T5000 does
this with its own code: Win32 calls on `\\.\COMn`, and none of T3000's Modbus
DLL or BACnet stack.

What T3000 does:

- It lists the ports from `HKLM\HARDWARE\DEVICEMAP\SERIALCOMM`
  (`GetSerialComPortNumber1`, `global_function.cpp:989-1041`).
- Its scan list has one entry for each port and rate (`m_scan_info`,
  `TStatScanner.cpp:596-700`). At each, it first listens for MS/TP
  (`Test_Comport`, `:584`), and a line that runs it is left to the BACnet
  scan.
- On a Modbus line it sends Temco's range query, `FF 19 hi lo` and a CRC, to
  address 255 (`common.cpp:7229-7235`). Every device whose id is in the range
  answers. It halves the range until each device answers alone
  (`binarySearchforComDevice`, `TStatScanner.cpp:1324`, `:1586-1606`), then
  reads registers 0-9 from each id it found (`:1432`).
- It writes during the scan, without asking:
  - register 10, to move one of two devices that share an id (`:1215`,
    `:1657`);
  - a random serial, to a device that reports 0 (`:1459-1485`). It means to
    do the same for all ones, but tests `255 * 255 * 255 * 255`, a
    different number.
- It asks a garbled id again, with no limit (`:1596-1604`).
- A clean nine-byte reply with a few stray bytes after it, to a query for
  one id, makes it stop scanning the port, as if the line ran MS/TP
  (`common.cpp:7406-7407`, `TStatScanner.cpp:1363-1371`). After a five-byte
  reply, it takes them for a second device (`common.cpp:7383-7387`).

*Built: the first slice.* Listing ports and the scan itself, before any
port was opened.

- `serial/ports` lists the ports under `SERIALCOMM` without opening any, in
  port-number order, and marks the USB-serial drivers it knows by their
  device's name. `/api/interfaces` returns them beside the network
  interfaces, and the Devices page lists them under "Serial ports".
- `serial/rtu` builds the only two frames a serial scan can send, the range
  query and the read of registers 0-9 (`ScanFrame`), and reads their replies
  by T3000's rules. `ScanFrame` has no constructor that takes bytes and no
  builder that takes a function code, so no write can be expressed.
- `discovery/serial_scan` is the scan, over a `SerialScanTransport` that can
  send a `ScanFrame` and nothing else.
  - It listens first. A line that is already talking, MS/TP or another
    Modbus master, is left alone, and nothing is sent.
  - It halves the range as T3000 does.
  - An id two devices share is reported, not moved. A device with no serial
    gets the AssignSerialNumber repair, as on the network. Nothing is
    written.
  - Each question is asked a set number of times, 3 by default, and then the
    id is reported as unreadable. So is an id whose clean reply has a few
    stray bytes after it every time, too few for a second reply. Two devices whose replies collide on one id
    cannot be told from noise, so they are reported as unreadable, not as
    sharing it.
  - Each device found becomes a record found by a serial scan and reached
    over Modbus RTU on the port, rate and id that answered.
- The tests use a scripted line (`testing/fake_serial_line.h`). Its devices
  read the bytes sent, check them with their own CRC, and answer one after
  another or on top of each other. `conformance/crc_oracle.cpp` checks the
  CRC and every frame the scan can build against T3000's `CRC16` and its
  tables.

*Built: opening and scanning a port* (2026-09-27). Tested on com0com's pair
only: T5000 opens CNCA0, and a scripted device holds CNCB0. No real adapter
has been opened.

- `discovery/com_port_line` is the port. It opens `\\.\` and the name as
  T3000 does (`ModbusDllforVc/common.cpp:5030-5098`): exclusive, 8 data
  bits, no parity, 1 stop bit, with flow control, DTR and RTS left as the
  driver has them, and the buffers purged before each frame. It is a
  `SerialLine`, so it too can be handed a `ScanFrame` and nothing else.
  - Only a plain name (letters, digits and underscores, at most 32) is
    opened, and `/api/scan` passes only a name the registry lists at that
    moment.
  - A port another program holds, one that has gone, and one that stops
    mid-scan each have their own message. T3000 says "Cannot open the COM
    Port" for all of them (`TStatScanner.cpp:729-731`).
  - A reply is read until the line has been quiet for 160 ms, T3000's
    `ReadIntervalTimeout`, rather than for a fixed 13 bytes, so a reply that
    arrives in pieces is read whole and a second device's is not cut off.
  - An adapter's echo of the frame is taken off the front of the reply,
    unless the whole is one frame by its CRC: a reply that only begins like
    the query.
  - `fAbortOnError` is cleared, so one framing error does not stop every
    read after it.
- `scan_serial_port` scans the port at each of the six rates in turn (S2),
  listening first at each.
  - MS/TP heard at a rate stops the scan of the port: a line runs one
    protocol, and the rates after would send Modbus into it.
  - A rate where something else is talking is skipped.
  - A port that fails, or will not take a rate, stops the scan, and what was
    found before is kept.
  - A device heard at several rates is listed once, at the first. On a real
    line a device answers at its own rate only; com0com has no rates, and an
    adapter that loops back hears everything.
- The Devices page lists the ports beside the network interfaces. "All
  network interfaces" never includes them: a port is opened only when it is
  picked and Scan is pressed, and it is closed before the answer comes back.
  When a port finds nothing, the page says why: in use, gone, MS/TP, busy
  rates, ids more than one device answers to, ids that could not be read,
  and the wiring to check.
- The devices found are saved with their port, rate and id (schema 4), and
  come back on the port after a restart. A device is reached where it was
  last found. A serial scan moves it off the network address a scan or Find
  gave it, and a network scan moves it back.
- A Modbus id conflicts only with devices on the same bus. Id 5 on COM3, on
  COM4 and on the network is three devices, not a conflict.
- Checked end to end on the pair, with 80 checks: one device, several, a
  shared id, serial 0, a device that will not be read, garbled replies, a
  silent line, MS/TP, another master talking, an echoing adapter, a device
  at one rate only, a port another program holds, a name Windows does not
  list, and a restart. Every frame the scripted devices received was a range
  query or a read of registers 0-9.

Still to build:

1. **Reading them.** On a Modbus line, this is the register path under Next.
   On an MS/TP line it is BACnet over MS/TP, joining the ring (S3), designed
   with the owner before it is built.
2. **The first scan of a real adapter,** which the owner runs: COM3 with a
   device on it. A real line's timing - the turnaround, and a USB adapter's
   latency - is not tested until then.

Decisions for the owner, and what the owner decided (2026-09-26, and S1 again
on 2026-09-27):

*S1. How opening a port is tested.* Opening one is the first step that sends
anything, and the rule is never to open a port with hardware on it. This
machine's COM3 is an FTDI adapter (`\Device\VCP0`), and there was no virtual
null-modem pair.

- com0com: a pair of virtual ports wired to each other, with a scripted
  device on the far end.
- A spare adapter with nothing connected to it. This proves the port opens,
  but nothing answers.

**Recommended:** com0com.
**Decided: not yet.** No port is opened. What can be built without opening
one is built first, and opening one waits until the owner says how it is
tested.

**Changed on 2026-09-27: com0com,** as recommended. The owner installed it
(2.2.2.0, the signed build), and its pair, CNCA0 and CNCB0, passes bytes both
ways when opened as `\\.\CNCA0` and `\\.\CNCB0`. Only that pair is opened,
with a scripted device on the far end. COM3 is never opened.

*S2. Which rates a scan tries.* The default is 38400, as
`device::Connection` has it. T3000 scans every port at each rate in its scan
list. `device::supported_baud_rates()` has six, from T3000's list
(`global_define.h:1450`). Every rate tried sends frames that a device at
another rate hears as noise.
**Recommended:** the chosen rate only, with "try every rate" as a choice.
**Decided: all six rates,** in turn, as T3000's scan list does.

*S3. MS/TP.* A line that runs MS/TP is left alone for now.

- Listing its devices passively: listening to the token being passed, for
  the addresses in the ring. This sends nothing.
- Joining the ring as a master, as T3000 does through its BACnet stack
  (`dlmstp`). This sends frames and takes a turn with the token.

**Recommended:** passive first.
**Decided: join the ring as a master.** Joining sends frames, so it comes
after opening a port, and is designed with the owner before it is built.

---

## Writes

The owner allowed writes on 2026-09-27, when asking for all of T3000's
functions: against loopback test panels first, then real devices, with the
owner running the checks that need one. COM ports are opened only on
com0com's virtual pair, never on a real adapter.

**What T3000 does.** A write is `WritePrivateData`
(`T3000/global_function.cpp:1636`): a read's frame, with a write code
(`WRITEINPUT_T3000` is 102), `total_length` = 7 + n x entity size, and each
point's raw struct appended (`:1786-1871`). The Inputs grid writes the one row
it changed (`BacnetInput.cpp:700`). The queue sends at most 5 points a request
(`MainFrm.cpp:11641-11646`) and write-all 10 (`:5359-5364`); the 480-byte
encode buffer (`global_function.cpp:1644`) caps a request at 476 bytes of
payload. Four things about it are not to be copied:

- **It cannot tell a failed write from a good one on BACnet/IP.** Its only
  test is whether the invoke id was freed in time, and the stack frees it for
  an Error, a Reject or an Abort as for an acknowledgement
  (`BacNetDllforVc/Src/apdu.c:563-642`), so a refused write is reported as
  written. A SimpleACK for a private transfer is never freed (`:500-535`), so
  that would be a timeout. And the queued grid write reports success even on a
  timeout: `if (ret_cusunits)` (`MainFrm.cpp:11659`) is true for the -1 that
  means one (`global_function.cpp:1627`).
- **It writes back its decoded copy.** `fill_in_input` folds '-' and '.' to
  '_' in a label and blanks text that overflows (`global_function.cpp:3472-3498`),
  so writing any field of an input rewrites the label too.
- **Its dangerous actions are bytes in a payload.** Factory default (88),
  clear (150), identify (77), reboot (111) and time sync (99) are values of
  `reset_default` in the 400-byte settings block that `WRITE_SETTING_COMMAND`
  (198) sends whole (`ud_str.h:859`; `BacnetSetting.cpp:1804-2081`), and the
  cache is never cleared after, so the next settings edit can send the action
  again. Writing to flash (122) goes through the read function
  (`BacnetView.cpp:5835`).
- **Some writes happen without being asked for:** time sync on reading the
  panel's time, a rename sent on connect, and writes when a field loses focus.

What a Temco panel sends back to a write is not in the repository: its
firmware is not here, and T3000 treats every answer alike.

**How T5000 does it.**

- **A write is its own type.** `bacnet/write_command.h` admits writes one at a
  time; every other write code in `ud_str.h` is held, or never written (clear
  panel, flash, the Modbus tunnel, the health reset, the subnet database, the
  trend-log erase). `conformance/write_command_guard.cpp` checks that the
  lists name every write code in the header once, and each number against its
  name. `conformance/write_separation_guard.cpp` fails the build if the read
  path, or anything above `bacnet/`, includes the write code. The oracle checks
  the encoder against the bytes T3000's stack sends.
- **Reading back decides.** The panel's answer is recorded and shown, never
  trusted: a write is done when reading the point back shows the new value.
- **Only the field edited changes.** A write is built from the point as read a
  moment before, with that field's bytes replaced. The panel's own bytes -
  value, status - go back as it gave them.
- **One approval, one change.** The page proposes; the operator approves that
  change on that device; the proposal is spent before anything is sent, and
  the serial is confirmed again just before the write.
- **Loopback until told otherwise.** Only 127.x.x.x is written to unless
  T5000 is started with `--allow-device-writes`.

**Order,** one PR each:

| | Delivers |
|---|---|
| W1a | Write encoding, reply classification and guards; nothing can send. Done (#32) |
| W1b | One input's Filter, approved in the page and read back |
| W2 | Evidence: a log of each write with the panel's raw answer, and a check of T3000's reply handling through its DLL. The test panel's default answer becomes what the first hardware check, run after W1b, recorded |
| W3 | Inputs' Auto/Manual |
| W4, W5 | Outputs (101) and Variables (103), with their grids' rules |
| W6 | Staging and review: changes collected, then read, compared and written as a batch. Applying offline changes to a found device (C, above) goes through it |
| W7 | Full Label and Label, once it is decided what to do about names on kinds of point T5000 cannot read yet |
| W8 | Range, Value, Calibration, Sign and Signal type, with the range dialog |
| W9 | Custom tables (114, 134, 136, 142) |
| W10 | Settings (198), through a typed write that copies the identity fields from a fresh read and forces `reset_default` to 0; then each panel action on its own, identify first |

Later: PID, schedules, programs and their code, the clock, and a Modbus
write path for Tstats and the scan's repairs. Held until the owner decides:
passwords and keys (115, 143, 144) and writing to flash (122).

**For the owner, when their PR comes:** whether to send only the edited field
(the default) or T3000's decoded copy; whether device writes stay behind the
start flag after the first hardware check; whether a matching serial is
enough, or the model must match too; how to admit flash, which a panel never
answers; whether a batch can be approved at once; and what a new label is
checked against.

**The first hardware check,** after W1b: set one unused input's Filter up by
one and back, and note what the panel answers, which of its bytes change on
their own, which port it answers from, and whether the change survives a power
cycle without writing to flash.

## Carried over from the first plan

The first plan, `docs/new-config-tool-plan.md` (removed 2026-09-24; it is in
git history), was written before any of T5000 existed. Most of it has since been superseded by what was built: T5000
speaks BACnet private transfer itself, instead of calling T3000's
`GetPrivateData_Blocking` and `WritePrivateData_Blocking`, and it includes
T3000's struct header rather than editing a copy. Three parts still hold, and
are kept here. The rest is in git history.

### The wire format is `T3000/CM5/ud_str.h`, not the BACnet library's copy

There are two `ud_str.h` files, and they define different structs under the
same names. `T3000/global_variable.h:5` includes `CM5\ud_str.h`, so that is the
one the shipping app and the devices use. `BacNetDllforVc/include/ud_str.h` has
`sen_on` and `sen_off` where the live one has `sub_id` and `sub_product`, and a
one-byte `calibration` where the live one splits calibration into two bytes.
The stale copy is the one that looks portable (it includes only `stdint.h`
and `stdbool.h`), which is what makes it dangerous: a tool built on it would
compile cleanly and misread every point.

T5000's conformance checks include the live header unmodified, through the
shims in `conformance/cm5_header.h`, and `conformance/wire_guard.cpp` checks
every field's offset and size against it. A `sizeof` check alone would not catch the difference above,
because the two copies differ by which field sits at an offset, not by size.

### What the write path must not reproduce

T3000 handles writes badly in four ways. The first three are in the Inputs
grid, and the last is in the Variables dialog:

- **A write reported as a success is often never checked.** After a write
  succeeds, `BacnetInput.cpp:85` asks the device for the point again only when
  the product is a private-data one and the protocol is not MS/TP, a
  BACnet-to-Modbus bridge, or PTP transfer mode (`SPECIAL_BAC_TO_MODBUS`,
  `global_define.h:2550`). Otherwise the status bar says "Success!" and the
  grid shows the typed value without the device having been asked.
- **Where it is checked, the check is a timer.** `Post_Refresh_One_Message`,
  then `SetTimer(2, 2000, NULL)` (`:87-89`). The refresh races the write.
- **A failed write throws away what was typed.** `:96` restores the old point
  from `m_temp_Input_data`, and the only trace is "Fail!" in the status pane.
  Someone editing fifteen rows cannot tell which were rejected, or what they
  had entered.
- **Background refresh can overwrite an edit in progress.** The Variables
  dialog runs `SetTimer(1, BAC_LIST_REFRESH_TIME)` and `SetTimer(4, 15000)`
  (`BacnetVariable.cpp:124-125`).

So in T5000, a write is not done until a read-back has confirmed it. A
rejected value stays on screen with the reason beside it. A refresh never
touches a row being edited.

### The UI binds to loopback

The server binds 127.0.0.1 only. The tool reads and will write building
equipment; remote or tablet access is a later, deliberate step, and it comes
with a decision about authentication.

Loopback keeps other machines out, not other web pages: any page open in the
technician's browser can send a request to 127.0.0.1:8730. So the server
refuses, before any route runs, a request whose `Origin` is not its own page
or whose `Host` is not 127.0.0.1 or localhost on its port (#19,
`from_this_tool` in `http/server.h`). Every write route will depend on
that check.

---

## Not worth porting

Ruled out when this plan was first written. Graphics has since been ruled out
too, by decision:

- **MFC itself** — the reason this tool exists.
- **Excel COM automation** (`excel9.cpp`, 5,591 lines) — needs Office installed.
- **The five legacy `.txt` config formats** (`fileRW.cpp`, 8,043 lines) — no
  spec exists. An import path beats format parity.
- **The Access/MDB + BADO layer** — unknown schema. Use SQLite if trends are
  wanted.
- **`BacnetScreenEdit.cpp`** — the drawing canvas. Out of scope by decision.
- **`DFTrace` / `g_Print`** — replace with ordinary logging.

---

## Which list "all the product families" means: settled by building it

There were two candidate lists:

- **~90 `PM_*` hardware models** — includes CO2 sensors, humidity sensors,
  pressure transducers, water sensors, BTU meters, power meters, Zigbee
  repeaters, a boat monitor and a tester jig. Most are not controllers and have
  no configuration screens beyond Inputs/Outputs/Settings.
- **30 `T3_*` panel configurations** — the controller shapes.

The recommended route was taken: the capability table is keyed by `PM_*`, so
it can describe any of the ~90, and it is populated first for the controllers
and Tstats. `device/product.cpp` has 21 rows today: the 5 private-data
controllers, 8 Tstats on the register path, 7 Modbus I/O modules, and a
third-party row that says the device is not Temco's. Any other `PM_*` value is
reported as unknown, with no screens. A sensor costs a table row, not a code
path. The `T3_*` configurations are a second table, keyed by `mini_type`, in
the same file.

---

## Risks specific to the widened scope

**The two id schemes will be conflated again.** They already share a variable
in the old code and the collisions are live numbers. *Cheapest guard: make them
distinct types in T5000 so a mix-up fails to compile — the same trick that
makes the wire guard work. No hardware needed.*

*Done.* `ProductClassId` and `MiniType` are separate `enum class` types
(`device/product.h`), and `product_selftest.cpp` asserts the collisions above,
so a renumbering is noticed. `conformance/product_guard.cpp` checks all
ninety product codes against `ProductModel.h`. The one place T3000 mixes them, the
`bacnet_device_type` row chain, is ported with the mix kept explicit as a
plain `int` (`device/input_rows.h`).

**The Tstat register model is a second wire format with no guard.** The point
structs are protected by `conformance/wire_guard.cpp`; `product_register_value[]` has
nothing equivalent, and it is hundreds of named indices. *Cheapest experiment:
pick the ten registers the Tstat screen actually reads and assert their indices
against the header the same way, before building on them.*

**The stub list is unreliable.** One of six claims was wrong on inspection, and
each one wrongly marked "finished" becomes a product that silently misreads.
*Cheapest experiment: for each, grep the init switch in both `BacnetInput.cpp`
and `BacnetOutput.cpp` and record the line or its absence — an hour, no
hardware, and it converts the whole list from hearsay to fact.*

*Done.* `device/product.cpp` holds the result per `mini_type`, from whole-file
greps, and a second independent pass agreed on every count. It also explains
the survey's biggest miss: OEM, OEM-12I and TSTAT11 are absent from the init
chains because they are variants of a TSTAT10, not because they are
unfinished. (An earlier version of this note called them row masks. The
code at `BacnetInput.cpp:1662-1683` is in the grid's click handler and only
stops some rows' range from being edited; every row is still shown.)

**Still unclosed, and above all of these:** the firmware gate in
`read_path.cpp` is a correct reading of a guard clause and nothing more until
two devices — one BACnet, one Modbus below firmware 525 — say otherwise.
