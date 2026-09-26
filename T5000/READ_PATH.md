# How a point actually gets read, and why it depends on firmware

How T5000 reads points from a controller, and the T3000 behaviour each part
follows. The first sections explain why there are two read paths and which
one a device gets. [What T5000 now reads, and how](#what-t5000-now-reads-and-how)
is where the current behaviour starts.

## A correction: `GetPrivateData_Blocking` does not route Modbus

Correcting an earlier claim in this project's history. The commit message on
"Pin the point wire format before building anything on it" says
`GetPrivateData_Blocking` "routes MODBUS_RS485 and PROTOCOL_MB_TCPIP_TO_MB_RS485
alongside BACnet internally". **That is wrong.** It rejects them.

The mistake came from grepping the function body, seeing `g_protocol ==
MODBUS_RS485` in it, and reading a guard clause as a routing branch.

## What it actually does

`global_function.cpp`, first statement of `GetPrivateData_Blocking`:

```c
if (g_protocol_support_ptp != PROTOCOL_MB_PTP_TRANSFER)
{
    if ((g_protocol == MODBUS_RS485) ||
        (g_protocol == PROTOCOL_MB_TCPIP_TO_MB_RS485) ||
         g_protocol == PROTOCOL_THIRD_PARTY_BAC_BIP)
    {
        return -1;
    }
}
```

So on a Modbus transport it returns `-1` immediately and reads nothing - unless
the PTP tunnel is active, in which case the guard is skipped and the private-data
read proceeds over Modbus.

## When the PTP tunnel is active

`BacnetView.cpp:7736-7743`, after reading the device's identity:

```c
if ((Bacnet_Private_Device(read_data[7])) && (software_version >= 525))
    g_protocol_support_ptp = PROTOCOL_MB_PTP_TRANSFER;
else if (read_data[7] == PM_ESP32_T3_SERIES)
    g_protocol_support_ptp = PROTOCOL_MB_PTP_TRANSFER;
else
    g_protocol_support_ptp = PROTOCOL_UNKNOW;
```

`Bacnet_Private_Device` returns true for `PM_CM5`, `PM_MINIPANEL`,
`PM_MINIPANEL_ARM`, `PM_ESP32_T3_SERIES` and `PM_TSTAT10`.

## The decision tree for a Tstat

| Device | Firmware | `GetPrivateData_Blocking` |
| --- | --- | --- |
| TSTAT10 | >= 525 | works - PTP tunnel carries private data over Modbus |
| TSTAT10 | < 525 | returns -1, reads nothing |
| ESP32 T3 series | any | works - PTP enabled unconditionally |

## What this means for the tool

There are two read paths, not one, and which applies is a **runtime** property of
the device rather than a build-time choice:

1. **Private data over PTP** - `GetPrivateData_Blocking(deviceid,
   READINPUT_T3000, i, i, sizeof(Str_in_point))`, returning the point struct that
   `wire/decode.cpp` already decodes. Requires firmware >= 525 on a Tstat.
2. **Raw Modbus registers** - `Read_Multi(device, buffer, start_address, length,
   retries)` (`global_function.h:141`), for anything the first path refuses.
   This needs a register map, which the point-struct path does not.

So the earlier conclusion that "the Modbus MFC cleanup may not be on the critical
path at all" only holds for firmware >= 525. Supporting older Tstats means
path 2, and therefore the `ModbusDllforVc` work.

The first milestone should read the device identity, decide which path applies,
and **say so explicitly** rather than silently returning no points - a tool that
shows an empty Inputs grid because the firmware is too old, with no explanation,
is precisely the kind of behaviour this project exists to stop repeating.

## What T5000 now reads, and how

Path 1 over BACnet/IP, for Inputs and Outputs: `bacnet/private_transfer.cpp` (the bytes)
and `bacnet/point_read.cpp` (the requests). Everything else still reports why
it is not read rather than reading it.

**Which devices.** Only the five `Bacnet_Private_Device` products. That is not
a simplification: `MainFrm.cpp:7375-7380` opens T3000's private-data view for
exactly those five, whatever the transport. The `bacnet_device_type` chain just
inside that gate (`:7421-7434`), which names T3-8AI8AO6DO, T3-22AI, T3-PT12,
T3-6CTA and T3-LC, can never run - its outer condition excludes every product
it tests for. `choose_read_path` used to answer "private data" for any product
on a transport the guard did not refuse, which would have sent a Tstat8 a read
T3000 has never sent it.

**What goes on the wire** - for points 0-9, invoke id `II`:

```
81 0A 00 1A | 01 04 | 00 05 II 12 | 0A 01 04 | 19 01 | 2E 65 07 [07 00 02 00 09 2E 00] 2F
```

Two values in there are not what anyone would guess:

- **Vendor id 260, not 148.** 148 is Temco's registered BACnet vendor id.
  `BACNET_VENDOR_ID` is 260 in `BacNetDllforVc/include/config.h:71`, and
  neither the DLL project the solution builds nor T3000's overrides it, so 260
  is what every private-data request T3000 sends carries.
- **The Temco header is raw memory.** `GetPrivateData` hands the address of a
  packed `Str_user_data_header` to `bacapp_parse_application_data`, which
  `Set_transfer_length` has turned into a byte copy (`bacstr.c:744-749`; the
  hex parser above it is `#if 0`, :704-740). So `total_length` and `entitysize` go out
  little-endian, inside a BACnet frame that is otherwise big-endian.

These bytes are not taken on trust from this reading. A conformance check
(`conformance/private_transfer_oracle.cpp`, which T3000's solution builds
and runs, since it needs T3000's stack) builds the request exactly as
`GetPrivateData` does, lets the DLL send it to a loopback socket, and requires
T5000's encoder to match it byte for byte. Mutating the encoder AND its
hand-written expected bytes the same wrong way - a misreading written down
twice - is caught by that test and by no other.

**Where it goes.** Straight to the address the device's scan response came
from, on the BACnet port the response names. The address is taken from
`recvfrom`, as T3000 takes it (`TStatScanner.cpp:2032`, `:2369`), and not from
the IP the device writes inside the response (bytes 16-22). The two agree on
an ordinary subnet. Behind NAT, or on a controller with more than one
interface, they may not, and only the sender has shown it can be reached.
The written one is kept, and the device list flags a device whose two
addresses differ. The port has no such second source: the response comes from
the device's discovery socket, not its BACnet one, so the BACnet port is still
the device's own claim (bytes 60-61), NAT or not.

The read goes to that address directly. T3000 instead broadcasts Who-Is for
the device instance and binds whatever address the I-Am comes from (`BacnetView.cpp:4307`). Skipping
it means no broadcast when a device is read. What the I-Am would have given -
confirmation that the device at that address is still the one scanned - comes
from the settings read instead, which is sent first: it carries the panel's
serial number, and a panel whose serial is not the one the scan found at that
address is not read further. T3000's own web view makes the same check
(`BacnetWebView.cpp:1655-1665`). A panel whose settings give serial 0 is read,
and the page says its identity could not be confirmed.

**A device known only from the saved list must prove it.** Its address is the
one it answered from when it was last seen, which may be months ago and in
another building, so the panel there now may be a different one. Nothing this
session vouches for it, so its settings have to: unless they give its saved
serial, nothing after the settings read is sent. A refusal, a reply that does
not match the request, and serial 0 all stop the read, as a different serial
does, and the page says to scan first (`Identity::MustConfirm`,
`app/inputs_read.h`). A device that has answered a scan since T5000 started
is read as the paragraph before this one says: only a different serial stops
it, and a refusal, a reply that does not match or serial 0 leaves a note and
the read goes on, because that scan saw the serial answer from that address.
It counts as seen whichever scan of the session it answered, not only the last.
The page says when a device has not been seen this session, and when it last
was (`plan_inputs_read`, `app/inputs_plan.cpp`).

**A device added by hand is not read until a scan finds it.** It is an entry
the operator typed in: its product and model are their word, and nothing has answered
from any address for it. `plan_inputs_read` refuses it before anything else,
and the page says there is no device to read. Nothing is sent. A scan that
finds its serial replaces the entry with the device, which is read from then
on under the rules above. `ManuallyAdded` is also the provenance a record gets
when whoever built it set none, so a record that says nothing about where it
came from is refused the same way.

**Devices a controller answered for are not read.** A Minipanel or T3 answers
the scan for the Tstats on its RS485 bus, from its own address, with the
sub-device's serial and a non-zero parent serial - T3000 deliberately stops
treating a repeated IP as a duplicate for exactly this reason
(`TStatScanner.cpp:2091-2092`). The IP and port in such a response are the
controller's. T3000 reaches the sub-device through the controller, over
Modbus TCP to RS485 (`MainFrm.cpp:7580-7588`). A private-transfer read sent to
that address would be answered by the controller with its own inputs, and the
page would show them under the sub-device's serial - so `plan_inputs_read`
refuses any device with a parent, names the parent, and sends nothing. The
scanner now keeps the parent serial (it was parsed and dropped before).

**The rest are assumed to be BACnet/IP.** A scan response does not say which
protocol the device speaks; T5000 records every scanned device as BACnet/IP,
and the product gate above is what keeps the non-private products off this
path. T3000 does the same in effect: without a parent, and not one of the
MS/TP bridge protocols, a selected device gets `PROTOCOL_BACNET_IP`
(`MainFrm.cpp:7601`).

**Which address it sends from.** The read socket binds UDP 47808 (then
47809-47811, T3000's order, `global_function.cpp:8121`) on the one local
address that routes to the device - found by connecting a throwaway UDP socket
to the device and asking which address it was given - not on 0.0.0.0. This
matters when T3000 is running. T3000 binds its BACnet socket on the local
address in the device's subnet, not the wildcard (`Open_bacnetSocket2`,
`global_function.cpp:8654-8702`, `:8754`), and on Windows a wildcard bind on
the same port coexists with that one, with a datagram to the specific address
delivered to the specific bind. Tested: with one socket on 0.0.0.0:47808 and
one on 127.0.0.1:47808, a datagram to 127.0.0.1:47808 reached only the
second. So a T5000 on 0.0.0.0 would have sent its requests and never seen the
replies - T3000 would have received them, into a handler that decodes every
private-transfer ACK without looking at its sender
(`global_function.cpp:7198-7217`). Binding the specific address makes the
conflict a failed bind instead: T5000 moves to 47809, or says that
47808-47811 are all in use.

That fixes where the replies go only if the device replies to the port the
request came from. One that replies to 47808 regardless would still be
answering T3000, and to T5000 that is silence. Which kind these controllers
are has not been checked against hardware, so when a read gets no answer at
all from a port other than 47808, the error says which port it went out from
and that closing the other program may be the fix.

(T3000's comment above its bind loop, `global_function.cpp:8108-8109`, says it
no longer binds 47808 and starts at 47809. The code does not do that: the loop
is `BACNETIP_PORT + 0..3` and `BACNETIP_PORT` is 47808,
`global_define.h:242`.)

**Stricter than T3000 in four places**, each a case T3000 gets wrong:

| T3000 | T5000 |
| --- | --- |
| Checks only that a reply is a whole number of points (`global_function.cpp:4165`), then loops over the range the reply states - reading past the end of a short reply, and placing a reply for 10-19 onto 10-19 even if 0-9 was asked | The reply must name the command, range and exact point count that was sent |
| No error handler for private transfer (`:8324-8325`): a refusal is silence, then a timeout | Error, Reject and Abort are decoded and reported, with class, code or reason |
| A chunk that times out is logged and skipped (`BacnetView.cpp:4447`), leaving a grid with holes | All or nothing: a partial read shows no points and names the range that failed |
| Up to 10 retries x 3 attempts x 3 s per chunk | 2 attempts x 3 s, then the page is told |

**What else is read, and in what order.** T3000 reads three things from a
panel when it connects, before it shows any point, and T5000 reads them each
time the Inputs page is opened (`app/inputs_read.cpp`), since it has no
connection to keep them in:

| Read | Command | Requests | What it decides |
| --- | --- | --- | --- |
| Settings | `READ_SETTING_COMMAND` (98), one 400-byte block | 1 | the panel's model (`mini_type`), and so its row count and per-model labels; the serial number; on an ESP32 T3 from firmware 63.7, how many inputs it has |
| Custom digital ranges | `READUNIT_T3000` (14), eight 25-byte units | 1 | the state names of digital ranges 23-30 |
| Custom analog tables | `READANALOG_CUS_TABLE_T3000` (34), five 105-byte tables | 2 | the unit names of analog ranges 20-24 |
| Inputs | `READINPUT_T3000` (2) | 7 | the points |

So opening the page sends 11 requests where it used to send 7, about 1.1 s on
loopback with T3000's 100 ms pacing between them. All four are on the
`ReadCommand` whitelist, and the oracle checks each request against T3000's
stack in the shape T3000 sends it.

When one of them does not come back, T5000 goes further than T3000, and
says so:

- **The settings.** When nothing answers, the read stops there, as T3000's
  does: it treats a panel whose settings do not come back as not connected
  (`BacnetView.cpp:5905-5955`). When the panel answers with a refusal, T3000
  would still show nothing. T5000 reads the inputs anyway, shows every row
  with no per-model rules, and the page says how that differs. The same goes
  for a reply that does not match the request. Neither applies to a device
  known only from the saved list, which stops there (see above).
- **The custom names.** A refusal leaves those names missing, and each row
  that needed them has a note. When nothing answers, the note says so, and
  the page goes on; the inputs are still asked for. As in T3000, the analog
  tables are asked for whether or not the digital names came back
  (`BacnetView.cpp:6472`, `:6563`), and table 4 only when tables 0-3 did.

**How values are shown.** As T3000's Inputs grid shows them: `display/input_text.cpp`
ports the loop at `BacnetInput.cpp:951-1237` column by column. The unit and
range names are copied from `global_define.h` into `display/tables.h`, and a
self-test re-reads that header on every build and fails if an entry or a count
differs; the integer constants T5000 copies from it are checked the same way.
The custom names are cut out of the replies by ports of T3000's receive
handlers (`display/custom_ranges.cpp`), including the parts a plain copy would
miss: a digital name of 12 or more characters is dropped, a non-zero `direct`
byte swaps off and on, and a 0xEF in a table name's ninth byte is a precision
marker, not text, unless the name has no NUL in its first 22 bytes. The rows
T3000 shows are those below its row limit for the panel's model
(`device/input_rows.cpp`, from `BacnetInput.cpp:736-807`); a T3-8AI8AO6DO
shows 8, and T3000 leaves the rest of its 64 rows empty, so T5000 leaves them
out. Where T3000 would show something T5000 cannot, the row carries a note
instead of a guess - a custom range whose names did not come back, or a cell
T3000 leaves holding the previous row's text.

**Outputs.** The same path, with `READOUTPUT_T3000` (1): seven requests of
ten 45-byte points, or more on an ESP32 T3 from firmware 63.7. Before them
the Outputs page (`app/outputs_read.cpp`) reads the settings and the custom
digital range names, and not the analog table names, which only input ranges
use: 9 requests in all. A failure of either is handled as on the Inputs page;
the two share that code (`app/panel_read.cpp`). The points are decoded as
`fill_in_output` decodes them (`wire/decode.cpp`), which is not quite as
inputs are: a description that does not fit is cut to 18 characters rather
than blanked, and a voltage above 12.0 V is zeroed. For both, whether text
fits is decided by strlen over the wire, so the byte after a full field
decides it. The grid is `BacnetOutput.cpp:658-1141`, ported in
`display/output_text.cpp`, with the row limit, the outputs a hand-off-auto
switch covers, and whether a model shows external outputs from the chain at
`:418-545` (`device/output_rows.cpp`). A switched output at off or hand
shows MAN-OFF or MAN-ON and a marked row, and its Auto/Man cell is empty,
as T3000 leaves it on a first open; the note keeps the setting. An output
on a T3 expansion module is external, and shows that module's switch.

**Not yet:** the Panel and Type columns, on both pages, and Outputs' Product
Name. Variables, which will use this same path with their own commands. Path 2 (Modbus registers) and the
PTP tunnel are not implemented, and nor is reading a sub-device through its
controller. The migration plan's [Next](../docs/t5000-migration-plan.md#next)
list has the order.

**None of this has touched hardware.** The synthetic devices used to test it
answer in the format this same reading of the source says they should, so if
the reading is wrong they are wrong in the same way. The oracle closes that gap
for the request; nothing closes it for the reply except a controller.
