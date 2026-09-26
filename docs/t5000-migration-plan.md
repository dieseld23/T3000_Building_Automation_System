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

**Where it stands, 2026-09-25:** Stage 0 is done, and the device list is now
saved between runs, as T3000's building database is ([The device
list](#the-device-list-and-virtual-devices)). In Stage 1, Inputs are read and
shown as T3000 shows them, after each panel's settings and custom range names
(#17). The Panel and Type columns are still to do, and Outputs and Variables
are not started. The stage table below has each stage's state, and
[Next](#next) is the list of what comes after.
[`T5000/README.md`](../T5000/README.md) describes the tool as it is today.

T5000 builds on its own, from `T5000/T5000.sln`: it compiles nothing of
T3000's, links only Windows libraries, and CI builds it from a checkout of
its folder alone. What it copies from T3000 - layouts, codes, tables - is
checked against the originals by `T5000/conformance/`, which
`T3000 - VS2019.sln` builds with T3000's BACnet stack and runs.

---

## The headline: additive, not multiplicative

The worry was that supporting every product multiplies the wire-format work.
It does not.

**There is one point-struct layout.** Every product that uses the struct path
uses the same `Str_in_point` / `Str_out_point` / `Str_variable_point`. All
three are copied in `T5000/wire/` and guarded field-by-field against
`CM5/ud_str.h` by `T5000/conformance/`, compiler-enforced when
`T3000 - VS2019.sln` builds:

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
                                                 COLLIDE  27  T3_TSTAT11  vs PM_T3PERFORMANCE
                                                 COLLIDE  29  T3_RMC1232  vs PM_T36CT
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

| Stage | Delivers | Changed by all-products? | State, 2026-09-25 |
|---|---|---|---|
| **0** | Discovery, selection, firmware detection, **product-identity model** | **Larger** — two id axes, capability table | Done (#9, #12, #13, #15). The device list is saved between runs (#19), and takes devices added by hand |
| **1** | Inputs + Outputs + Variables read | Unchanged — one shared layout | Inputs done (#14, #16, #17), except the Panel and Type columns. Outputs and Variables not started |
| **2** | Write support for points, then Arrays, PVar | Unchanged | Not started |
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
`T5000/display/input_text.cpp`, with the tables in `display/tables.h`, checked
against `global_define.h` by `conformance/tables_guard.cpp` on every build of
T3000's solution. `Device_Basic_Setting` is read too
(`READ_SETTING_COMMAND`, `app/inputs_read.cpp`), and so are the custom range
tables; the row limits and per-model labels they drive are ported. Still to
come: the Panel and Type columns, and Outputs and Variables.

**Stage 2 is the cliff** — the first code that writes to live equipment.
Writes get their own transport, separate from the read path, which cannot
express one. Each write needs an explicit approval, and is done only when a
read-back confirms it. The scan's repairs are the first things an approve
button would act on. See also [what the write path must not
reproduce](#what-the-write-path-must-not-reproduce).

## Next

In order:

1. **Devices added by hand and virtual devices,** then **importing T3000's
   building database.** Parts 2 and 3 of [the device
   list](#the-device-list-and-virtual-devices). Adding a device by hand is
   built. Configuring one offline waits on the owner's decisions A to E
   there: where the configuration lives, what must be entered, how it is
   compared with the device before a write, how a device on another subnet
   is found, and what a virtual device is.
2. **Serial ports.** The first slice is built: the computer's COM ports are
   listed, and a serial scan that cannot write is tested against a scripted
   line. Opening a port waits on the owner's decisions S1 to S3 under
   [Serial ports](#serial-ports).
3. **Finish Inputs:** the Panel and Type columns.
4. **Outputs and Variables.** Same struct path as Inputs, and their structs
   are already guarded. T3000 also reads multi-state ranges
   (`READ_MSV_COMMAND`) and variable units (`READVARUNIT_T3000`) when it
   connects (`BacnetView.cpp:6483-6575`); the port will need both.
5. **The first hardware check,** once a controller is available and the owner
   agrees. Two things above all:
   - the serial check: the settings' `n_serial_number` must equal the serial
     in the scan response, or for a device from the saved list the saved
     serial, or the page refuses the panel;
   - whether a controller replies to the port a request came from.
     `READ_PATH.md` explains why that decides whether T5000 can run beside
     T3000.

   Later, when there is a Modbus device on firmware below 525 to try, the
   firmware gate (see the end of Risks).
6. **Stage 2, writes,** as above. Editing a device's points offline comes
   first. It changes the saved configuration, not a controller, so the edit
   screens can be built before there is a write transport.
7. **The register path** for Tstats and the Modbus modules, starting with the
   guard on the Tstat registers described under Risks.

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
  MFC wrapper. `T5000/store/sqlite.h` says what that rules out.
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
listed as "added by hand". Nothing is sent to it: its Inputs page says there is
no device to read (`plan_inputs_read`). When a scan finds its serial, that
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
  schedules. Last comes the settings block (`WRITE_SETTING_COMMAND`, `:5764`),
  which holds the serial, IP address and panel number. It reads nothing first
  and nothing back, so loading one panel's file onto another gives the second
  panel the first one's identity.
- **A virtual device** (`BacnetAddVirtualDevice.cpp:187-220`) gets a random
  serial and a `.prog` file in `Database\temp`. Nothing writes one to
  hardware.

**Decisions for the owner.** The rest of part 2 waits on these.

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

*B. What must be entered.* A model and serial, as built. The panel type
(`mini_type`) decides how many points a MiniPanel, MiniPanel ARM or ESP32 T3
has, so there is no knowing what to configure without it. Picking a model
gives one, and "Model not known" leaves it out, so it is optional to add a
device, as recommended. Still to decide:
**Recommended:** required before its points can be edited offline.

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

*E. Virtual devices.*

1. **Recommended:** a virtual device is a configuration with no device
   behind it. It has kind `'virtual'` (the column allows it) and a serial
   from a range T5000 hands out and checks, and a scan never matches it. It
   reaches a real device only by copying its configuration to that device's
   entry, which then goes through C.
2. As T3000 does it: a random serial, matched like any other.

Once these are settled, the order is:

1. B's panel type.
2. A's storage, with editing Inputs offline.
3. Import and export.
4. D.
5. E.
6. C, the apply, once Stage 2's write transport and the first hardware check
   exist.

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
  loader never checks the length; T5000's should.
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

**3. Importing T3000's building database.** Read-only: T5000 reads the
`ALL_NODE` rows into its own list and never writes T3000's file. `ALL_NODE`
reuses columns (the IP address is in `Bautrate`, the port in `Com_Port`, and
`Screen_Name` holds the virtual flag), so each needs mapping, not copying.

**4. Editing a device's points offline**, for a device added by hand or a
virtual one. These are the first edit screens, working against the saved
configuration rather than a controller. See A above and Next.

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

*Built: the first slice.* Nothing opens a port yet, so none of this has sent a
byte to a line.

- `serial/ports` lists the ports under `SERIALCOMM` without opening any, in
  port-number order, and marks the USB-serial drivers it knows by their
  device's name. `/api/interfaces` returns them beside the network
  interfaces, and the Devices page shows them under "Serial ports - not
  scanned yet", where none can be picked.
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

Still to build, once the decisions below are made:

1. **The port.** `CreateFile` on `\\.\COMn`, with the line settings T3000
   uses, read from its source first. The transport has to handle what the
   scripted line does not:
   - a port another program holds, T3000 included, which cannot be opened;
   - an adapter that echoes what it sends, putting the query in front of
     the reply;
   - a reply that arrives in pieces;
   - a USB adapter unplugged during a scan.
2. **Scanning from the page:** a port and a rate, and the devices found
   added to the list.
3. **Saving them.** The saved list keeps no port or rate yet.
4. **Reading them.** On a Modbus line, this is the register path under Next.
   On an MS/TP line it is BACnet, over MS/TP.

Decisions for the owner:

*S1. How opening a port is tested.* Opening one is the first step that sends
anything, and the rule is never to open a port with hardware on it. This
machine's COM3 is an FTDI adapter (`\Device\VCP0`), and there is no virtual
null-modem pair.

- com0com: a pair of virtual ports wired to each other, with a scripted
  device on the far end.
- A spare adapter with nothing connected to it. This proves the port opens,
  but nothing answers.

**Recommended:** com0com.

*S2. Which rates a scan tries.* The default is 38400, as
`device::Connection` has it. T3000 scans every port at each rate in its scan
list. `device::supported_baud_rates()` has six, from T3000's list
(`global_define.h:1450`). Every rate tried sends frames that a device at
another rate hears as noise.
**Recommended:** the chosen rate only, with "try every rate" as a choice.

*S3. MS/TP.* A line that runs MS/TP is left alone for now.

- Listing its devices passively: listening to the token being passed, for
  the addresses in the ring. This sends nothing.
- Joining the ring as a master, as T3000 does through its BACnet stack
  (`dlmstp`). This sends frames and takes a turn with the token.

**Recommended:** passive first.

---

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
`from_this_tool` in `T5000/http/server.h`). Every write route will depend on
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
