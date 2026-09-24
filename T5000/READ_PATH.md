# How a point actually gets read, and why it depends on firmware

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

Path 1 over BACnet/IP, for Inputs: `bacnet/private_transfer.cpp` (the bytes)
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

These bytes are not taken on trust from this reading. A self-test
(`bacnet/private_transfer_oracle.cpp`) builds the request exactly as
`GetPrivateData` does, lets the DLL send it to a loopback socket, and requires
T5000's encoder to match it byte for byte. Mutating the encoder AND its
hand-written expected bytes the same wrong way - a misreading written down
twice - is caught by that test and by no other.

**Where it goes.** Straight to the IP and BACnet port the device reported in
its scan response. T3000 instead broadcasts Who-Is for the device instance and
binds whatever address the I-Am comes from (`BacnetView.cpp:4307`). Skipping
it means no broadcast when a device is read. What it gives up is confirmation
that the device at that address is still the one scanned; nothing in an Inputs
reply identifies its sender, so a controller replaced since the scan would be
read under the old one's serial until the next scan.

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

**How values are shown.** As T3000's Inputs grid shows them: `display/input_text.cpp`
ports the loop at `BacnetInput.cpp:951-1237` column by column. The unit and
range names are copied from `global_define.h` into `display/tables.h`, and a
self-test re-reads that header on every build and fails if an entry or a count
differs. Where T3000 would show something T5000 cannot, the row carries a note
instead of a guess. That covers a custom range, whose names are stored on the
device and read with commands T5000 does not send yet (`READUNIT_T3000`,
`READANALOG_CUS_TABLE_T3000`), and a cell T3000 leaves holding the previous
row's text.

**Not yet:** the panel's settings (`READ_SETTING_COMMAND`), which T3000 uses
for how many rows a model shows, a few per-model labels, and the Panel and
Type columns. An ESP32 T3 on newer firmware can have more than 64 inputs;
T3000 reads 64 first too, and only widens the count after reading the settings
block. Path 2 (Modbus
registers) and the PTP tunnel are not implemented, and nor is reading a
sub-device through its controller.

The device's address comes from the IP written inside its scan response
(bytes 16-22). T3000 records the address the response actually came from, as
`recvfrom` reports it (`TStatScanner.cpp:2032`, `:2369`). The two agree for a
device on the same subnet with one address; behind NAT, or on a controller
with more than one interface, they may not, and T3000's is the one known to
answer. Switching to the sender address is a follow-up.

**None of this has touched hardware.** The synthetic devices used to test it
answer in the format this same reading of the source says they should, so if
the reading is wrong they are wrong in the same way. The oracle closes that gap
for the request; nothing closes it for the reply except a controller.
