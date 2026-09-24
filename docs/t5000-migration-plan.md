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

---

## The headline: additive, not multiplicative

The worry was that supporting every product multiplies the wire-format work.
It does not.

**There is one point-struct layout.** Every product that uses the struct path
uses the same `Str_in_point` / `Str_out_point` / `Str_variable_point`. All
three are now guarded field-by-field in `T5000/wire/`, compiler-enforced:

| Struct | Size | Guarded |
|---|---|---|
| `InputPoint` | 46 | ✓ every field |
| `OutputPoint` | 45 | ✓ every field |
| `VariablePoint` | 39 | ✓ every field |

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

This is the finding that most affects your scope answer. Several products exist
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

| Stage | Delivers | Changed by all-products? |
|---|---|---|
| **0** | Discovery, selection, firmware detection, **product-identity model** | **Larger** — two id axes, capability table |
| **1** | Inputs + Outputs + Variables read | Unchanged — one shared layout |
| **2** | Write support for points, then Arrays, PVar | Unchanged |
| **3** | Device settings, user login | Slightly larger — per-product field ranges |
| **4** | PID loops, then Tstat | **Larger** — Tstat is a second data model |
| **5** | Weekly + annual schedules | Larger — Tstats encode schedules differently |
| **6** | Trend logs + alarms | Unchanged |
| **7** | Programs | Blocked on a decision; controllers only |

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
against `global_define.h` on every build. `Device_Basic_Setting` is read too
(`READ_SETTING_COMMAND`, `app/inputs_read.cpp`), and so are the custom range
tables; the row limits and per-model labels they drive are ported. Still to
come: the Panel and Type columns, and Outputs and Variables.

**Stage 2 is the cliff** — the first code that writes to live equipment.

---

## Not worth porting

Unchanged from the first plan, minus graphics, which is now out by decision:

- **MFC itself** — the reason this tool exists.
- **Excel COM automation** (`excel9.cpp`, 5,591 lines) — needs Office installed.
- **The five legacy `.txt` config formats** (`fileRW.cpp`, 8,043 lines) — no
  spec exists. An import path beats format parity.
- **The Access/MDB + BADO layer** — unknown schema. Use SQLite if trends are
  wanted.
- **`BacnetScreenEdit.cpp`** — the drawing canvas. Out of scope by decision.
- **`DFTrace` / `g_Print`** — replace with ordinary logging.

---

## One question left, and it is smaller than the last two

**Which list did "all the product families" mean?**

- **~90 `PM_*` hardware models** — includes CO2 sensors, humidity sensors,
  pressure transducers, water sensors, BTU meters, power meters, Zigbee
  repeaters, a boat monitor and a tester jig. Most are not controllers and have
  no configuration screens beyond Inputs/Outputs/Settings.
- **30 `T3_*` panel configurations** — the controller shapes. This is the list
  I had been planning against.

*Recommendation: build the capability table so it is keyed by `PM_*` and can
describe any of the ~90, but populate it first for the controllers and Tstats.
A sensor then costs a table row, not a code path. That gets you "all products"
without paying for ninety of them up front.*

If you want it stated as a default: I will take that route unless you say
otherwise, since it does not foreclose anything.

---

## Risks specific to the widened scope

**The two id schemes will be conflated again.** They already share a variable
in the old code and the collisions are live numbers. *Cheapest guard: make them
distinct types in T5000 so a mix-up fails to compile — the same trick that
makes the wire guard work. No hardware needed.*

**The Tstat register model is a second wire format with no guard.** The point
structs are protected by `wire_guard.cpp`; `product_register_value[]` has
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
