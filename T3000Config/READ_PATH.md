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
