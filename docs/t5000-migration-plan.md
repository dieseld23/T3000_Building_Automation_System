# Bringing T3000's functionality into T5000

How the 16 screens of `T3000.exe` map onto the new standalone tool, what
genuinely blocks what, and what is not worth bringing across.

Derived by reading the source — 9 parallel surveys over the screen clusters,
each claim then checked by an adversarial pass that tried to refute it. 15 of
the port-class claims were refuted and revised. Where this document states a
byte count or a guard condition, it was read out of the file named, not
inferred.

**Nothing in T5000 has yet touched a live controller.** Every claim here is
source-against-source. The hardware gate is entirely ahead of us, and several
risks below can only be closed against real equipment.

---

## The screens

`global_define.h:1281-1297` is authoritative — 16 tabs:

| # | Constant | # | Constant |
|---|---|---|---|
| 0 | `WINDOW_INPUT` | 8 | `WINDOW_MONITOR` |
| 1 | `WINDOW_OUTPUT` | 9 | `WINDOW_ALARMLOG` |
| 2 | `WINDOW_VARIABLE` | 10 | `WINDOW_TSTAT` |
| 3 | `WINDOW_PROGRAM` | 11 | `WINDOW_SETTING` |
| 4 | `WINDOW_CONTROLLER` | 12 | `WINDOW_USER_LOGIN` |
| 5 | `WINDOW_SCREEN` | 13 | `WINDOW_REMOTE_POINT` |
| 6 | `WINDOW_WEEKLY` | 14 | `WINDOW_ARRAY` |
| 7 | `WINDOW_ANNUAL` | 15 | `WINDOW_PVAR` |

Behind them: 171 dialog classes, 329,875 lines.

---

## What actually blocks what

The ordering below is derived from the dependency graph, not from guessed
difficulty. Nothing here is ranked by how hard it felt to read.

### The real prerequisite: device selection

Every screen is blocked on the same thing, and it is not a screen. Until a
device has been found and selected, the globals that every read path reads —
`g_bac_instance`, `g_tstat_id`, `g_protocol`, `g_mstp_deviceid`, `g_nComPort` —
are unset. T5000 currently has connection *settings* but no discovery and no
selected device.

This is Stage 0, and it is the only thing that can start immediately.

### The firmware gate, which binds late

`GetPrivateData_Blocking` (`global_function.cpp:2331`) opens with a guard, not
a route:

```c
if (g_protocol_support_ptp != PROTOCOL_MB_PTP_TRANSFER) {
    if ((g_protocol == MODBUS_RS485) ||
        (g_protocol == PROTOCOL_MB_TCPIP_TO_MB_RS485) ||
         g_protocol == PROTOCOL_THIRD_PARTY_BAC_BIP) return -1; }
```

`g_protocol_support_ptp` is only set after a successful firmware read
(`BacnetView.cpp:7736-7747`), requiring `software_version >= 525` — which
happens *after* device selection. So a Modbus device on firmware 500 returns
−1 from most read paths, and the honest answer is "this firmware cannot do
this", not "device not responding".

This affects the majority of read paths and is already encoded in
`T5000/device/read_path.cpp`.

### Dependency chain

```
Stage 0  discovery + selection + firmware detection + units tables
   │
   ├─→ Points read (Input, Output, Variable)   ← Inputs: decode done,
   │                                             label helpers not
   │      └─→ Points write → Arrays → PVar
   │
   ├─→ Device settings (IP, time, NTP, login)
   │
   ├─→ Controller (PID) ─→ Tstat
   │
   ├─→ Schedules (weekly, annual, time editor)
   │
   └─→ Monitoring ─→ Alarms
```

Programs and Graphics hang off Stage 0 too, but both need a decision before
they can be planned at all — see the questions below.

---

## Stages

Each stage ships something usable on its own.

| Stage | Delivers | Shape |
|---|---|---|
| **0** | Discovery, selection, firmware detection, units tables | New problem |
| **1** | Outputs + Variables read | Mechanical, *after* the label helpers and units tables are ported |
| **2** | Write support for points, then Arrays, then PVar | New problem (first write path) |
| **3** | Device settings: IP, time, NTP, user login | Mostly mechanical |
| **4** | PID loops, then Tstat | Mechanical after Stage 2 |
| **5** | Weekly + annual schedules | New problem (three write paths) |
| **6** | Trend logs + alarms | New problem (async + storage) |
| **7** | Programs | Blocked on a decision |
| **8** | Graphics metadata | Blocked on a decision |

**Stage 1 is the cheap one, but "Inputs is done" needs qualifying.** What
T5000 has is the decode path: the wire layout, the guard, and JSON over a
fixture. What it does *not* have is the three source-side dependencies that
`T3000/WebUI/InputsData.cpp` calls directly to render a row —
`GetInputLabelEx` (`BacnetInput.cpp:2228`) and `GetInputFullLabelEx`
(`:2273`), both called at `InputsData.cpp:62-63`, and the
`Device_Basic_Setting` global read at `:87`. None of those is behind a DLL
export, so all three must be ported as source, and every points screen needs
them.

Beyond that, Outputs and Variables really are the same shape — read a struct
array, decode it, serve it as JSON. The extras are the custom-units lookup
tables (`Input_List_Analog_Units[]`, `OutPut_List_Analog_Units[]`,
`Digital_Units_Array[]` — `global_define.h:823-894`) and, for Outputs,
`hw_switch_status` / `pwm_period`, which Inputs has no equivalent of.

Note that the units tables and the label helpers are the *same* dependency
class as Risk 3 below: source-side `CString`. Stage 1 is where that risk
first has to be paid, not a later stage.

**Stage 2 is where the tool stops being read-only,** and that is a genuine
threshold: it is the first code that changes state on live building equipment.
It deserves its own review and its own hardware gate.

---

## Corrections to the survey

Three findings did not survive checking, and matter enough to record.

**Outputs are 45 bytes, not 46, and the header's comments lie.**
`Str_out_point` declares `description[STR_OUT_DESCRIPTION_LENGTH-2]` with the
macro at 21, so the field is **19** bytes — while the inline comment beside it
says "21 bytes" and the struct's trailing comment says "= 40". The real total
is 45. Both comments are wrong in the live header, and the *identical* wrong
comment appears in the stale one. Two wrong comments agreeing is not
corroboration.

This number is not arithmetic done in a document. `wire::OutputPoint` is now
in `wire/points.h` with the full field-by-field guard, so the compiler asserts
it on every build — and changing the 45 to 46 fails that build, which is how
the guard was confirmed to be live rather than vacuous.

The two headers also disagree on fields, not just size:

| Live `T3000/CM5/ud_str.h` | Stale `BacNetDllforVc/include/ud_str.h` |
|---|---|
| `low_voltage`, `high_voltage` | *absent* |
| `hw_switch_status` | `access_level` |
| `sub_id`, `sub_product` | `m_del_low`, `s_del_high` |
| `sub_number`, `pwm_period` (2×u8) | `delay_timer` (u16) |

A size check alone would not catch this — several of those swaps preserve the
total. Outputs now has the same field-by-field `wire_guard.cpp` treatment
Inputs got, and every struct after it needs the same before it is trusted.

**`Point_T3000` is not a risk — it already compiles.** The survey flagged its
`public:` specifier and Windows `byte` type as a possible blocker for
schedules, with a proposed `gcc -std=c99` experiment. That experiment tests a
constraint that does not exist: T5000 is C++17 under MSVC, and
`wire_guard.cpp:33-41` already includes the *entire* live header — schedules
structs included — behind two shims (`typedef unsigned char byte`,
`struct CString { void* opaque; }`). `public:` inside a struct is a no-op for
layout and cannot move an offset. No experiment needed.

**The DLL boundary does not cover everything, and this is the real risk.**
Both protocol stacks are DLLs with C-decorated exports, so MFC inside them
costs nothing — that finding holds. But `CString` also appears *source-side*,
notably in the units tables (`Input_List_Analog_Units[]` is a `CString` array
consumed directly by `GetInputValueEx`). Those must be ported as source, and
every points screen depends on them. This is the one risk the survey raised
that got stronger under checking, not weaker.

---

## Not worth porting

- **MFC itself** — dialogs, message maps, `PostThreadMessage`. Replacing it is
  the entire reason this tool exists.
- **Excel COM automation** (`excel9.cpp`, 5,591 lines) — requires Office
  installed. CSV export covers the real need.
- **The five legacy `.txt` config formats** (`fileRW.cpp`, 8,043 lines) — no
  specification exists in the codebase. An import path is worth more than
  format parity, and is optional.
- **The Access/MDB + BADO layer** — Windows ADO/OLEDB with an unknown schema.
  If trend storage is wanted, SQLite.
- **`BacnetScreenEdit.cpp`** — a 2,000-line MFC drawing canvas. A config tool
  needs screen *metadata*, not a canvas.
- **`DFTrace` / `g_Print` debug plumbing** — replace with ordinary logging.

---

## Two questions that change the size of this

Everything above assumes answers to these. They are worth settling before
Stage 0 rather than during it.

**1. Does T5000 cover the graphics screens (`WINDOW_SCREEN`)?**
This is the drawing editor, and it is what T3000Webview already is — which you
have said three times is not this tool. Excluding it drops
`BacnetScreenEdit.cpp` plus most of `BacnetScreen.cpp` (8,293 lines between
them) and removes Stage 8 entirely. Including it makes T5000 a drawing
application as well as a configuration tool.

*Recommendation: exclude. Serve screen metadata read-only if anything.*

**2. Which product families?**
`global_define.h:1305-1338` lists roughly 28 models — CM5, three MINIPANEL
variants, the ARM variants, T3_BMS, T3_OEM, T3_AIRLAB, the ESP32 series,
TSTAT10/11, T322AI, T38AI8AO6DO, T3PT12, T332AI, T36CTA and more. The stated
goal has been Tstat/T3 units. Narrowing to those drops a large fraction of the
171 dialogs and most of the product-specific register maps; covering all 28
multiplies the wire-format work by roughly the number of distinct layouts.

*Recommendation: Tstat/T3 only, and make the tool say plainly when it meets a
product it does not handle.*

Three smaller decisions can wait for their stage: whether Programs gets the
~320-line compiler or stays view-only (Stage 7); whether discovery writes
device-identity repairs immediately or stages them for approval (Stage 0 —
note T3000 writes immediately, including random serials to zero-serial
devices); and whether firmware update (ISP) is in scope at all.

---

## Risks that need hardware

**The firmware gate cannot be validated without two devices.** One BACnet unit
and one Modbus unit below firmware 525. Read `READ_MISC` from each and confirm
the Modbus one returns −1 before the PTP tunnel is enabled and data after.
Until that runs, `read_path.cpp` is a correct reading of the source and
nothing more.

**Discovery mutates devices.** `TStatScanner.cpp` writes random serials to
devices reporting serial 0 and reassigns Modbus IDs on conflict. Porting it
faithfully means porting code that changes equipment during a scan. That
behaviour should be a deliberate choice, not inherited.

**Write paths are firmware-dependent in ways reading cannot settle.**
Schedules alone have three different write paths gated on firmware ≥ 492
(`BacnetScheduleTime.cpp:121, 388-438`). Which one a given controller accepts
is a question for a controller.

---

## Where this leaves us

Stage 0 is the only thing not blocked, and it is also the largest piece of new
work. Stage 1 is nearly free once Stage 0 lands. The cliff is Stage 2, where
the tool first writes to live equipment.

Answering the two questions above changes the total size of this materially —
they are worth settling first.
