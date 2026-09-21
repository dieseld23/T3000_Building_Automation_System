# A new standalone configuration tool for Tstat/T3 units

Status: plan, not yet started. Written 2026-09-21.

This replaces the earlier "add a WebView2 screen inside T3000.exe" approach, which
was merged as PR #7 and is now a dead path. The new tool is a separate process
with a web UI. The existing app stays as-is and is treated as a reference for
device semantics, not as a foundation.

Scope, in order: Inputs, Outputs, Variables, then Schedules/Programs.

## What was verified before planning

Each of these was checked against the source directly, because an earlier
multi-agent analysis produced confident claims in both directions on most of them.

### The wire format is `T3000/CM5/ud_str.h`, NOT the BACnet library's copy

There are two `ud_str.h` files and they define *different* structs under the same
name. `T3000/global_variable.h:5` includes `CM5\ud_str.h`, so that is the one the
shipping app uses, and the one the devices actually speak.

| `BacNetDllforVc/include/ud_str.h` | `T3000/CM5/ud_str.h` |
| --- | --- |
| `sen_on`, `sen_off` | `sub_id`, `sub_product` |
| `calibration_increment`, `unused` | `sub_number`, `calibration_h` |
| `calibration` (one byte) | `calibration_l` (calibration is two bytes) |

`T3000/WebUI/InputsData.cpp` reads `sub_id`, `calibration_h` and `calibration_l`,
which confirms the CM5 layout is live. The BACnet library's copy is stale.

This matters more than it looks: the BACnet copy is the one that *appears*
portable (it includes only `stdint.h`/`stdbool.h` and has no MFC anywhere near
it), so it is the obvious thing to build on. Doing that would produce a tool that
compiles cleanly, looks right, and silently misreads every point on real
hardware - while writing to live equipment.

`CM5/ud_str.h` also includes only `stdint.h` and `stdbool.h`. It fails to compile
standalone for two shallow reasons, both confined to the schedule structs:
`byte` (undefined without the Windows headers) at lines 419-422, 445-446, 456,
and a `public:` inside `Point_T3000` at line 419. The Inputs/Outputs/Variables
structs need no changes at all.

### Tstat units are Modbus, not BACnet

`Post_Write_Message` routes on `g_protocol` at `T3000/global_function.cpp:1245`:
`MODBUS_RS485`, `MODBUS_TCPIP`, `PROTOCOL_MSTP_TO_MODBUS`,
`PROTOCOL_BIP_T0_MSTP_TO_MODBUS` and `PROTOCOL_MB_TCPIP_TO_MB_RS485` all post
`MY_RS485_WRITE_LIST` to the Modbus worker thread. `PROTOCOL_MB_TCPIP_TO_MB_RS485`
is defined at `global_define.h:262` with a comment naming TSTAT10.

So `ModbusDllforVc` is on the critical path for the primary target. That was the
single finding most likely to invalidate the plan, and it landed.

It is survivable, and this was measured rather than inferred from the header.
`ModbusDllforVc/ModbusDllforVc/common.cpp` is 11,076 lines and contains 99 MFC
references in total: 63 `CString`, 28 `AfxMessageBox`, 7 `CStdioFile`, 1
`CCriticalSection`, and no `HWND` at all. Under 1% of the file. In the public
header, four of 52 declarations take `CString` (`Open_Socket`, `Open_Socket2`,
`write_T3000_log_file`, `Get_NowTime`); the rest of the MFC use is logging and
INI state.

So this is `std::string` / `std::ofstream` / `std::mutex` substitution plus 28
error-path call sites, not a reimplementation of the protocol engine. Call it two
to three days.

Worth noting separately: all 28 `Afx` references are `AfxMessageBox`. A protocol
library that opens modal dialogs on a comms error is itself part of why the
current app behaves badly - the thread blocks on a message box until someone
clicks it. In the new backend these become error returns.

The Tstat write entry point is already clean:

```c
int Write_Multi_org_short(unsigned char device_var, unsigned short *to_write,
                          unsigned short start_address, int length,
                          int retry_times = 3);   // global_function.h:144
```

Plain C throughout - no `CString`, no `HWND`. It is what `BacnetSettingTcpip.cpp`
and `fileRW.cpp` call with `g_tstat_id`, so it is the real Tstat write path and
it is callable as-is.

### The blocking primitives are the way in, not `Post_Write_Message`

`Post_Write_Message` takes `HWND` and `CString` and completes by posting
`MY_RESUME_DATA` (`WM_USER+300`), so it is not callable from a process without a
message pump. But it is a notification wrapper, not the write itself:
`WritePrivateData_Blocking` (`global_function.h:218`) and
`GetPrivateData_Blocking` do the actual work, take no `HWND` or `CString`, and
complete by polling `tsm_invoke_id_free()` with `Sleep(10)` rather than by
waiting on a message.

`MainFrm.cpp:11652` already calls `WritePrivateData_Blocking` from a background
thread with no dialog context, which is the existing proof that the write path
and the notification path separate.

The new backend calls the blocking primitives directly and never adopts the
`HWND` completion model.

## Defects in the current UI that the new one must not reproduce

These were verified in the source; they are concrete answers to "it's buggy,
doesn't work well".

**A successful-looking write to a Tstat is never verified.**
`T3000/BacnetInput.cpp:85-90` re-reads the point after a write only when
`(!SPECIAL_BAC_TO_MODBUS) && Bacnet_Private_Device(...)`. Tstat units route over
Modbus, so that branch is skipped: the status bar says "Success!" and the grid
keeps showing the typed value without the device ever being asked. On the primary
target hardware, the confirmation is not a confirmation.

**Where the re-read does happen, it is a 2-second timer, not a confirmed read.**
Same block: `Post_Refresh_One_Message(...)` followed by `SetTimer(2, 2000, NULL)`.
The refresh races the write.

**A failed write silently discards what was typed.**
`BacnetInput.cpp:96` restores the point with `memcpy_s` from `m_temp_Input_data`
and repaints. The only trace is "Fail!" in the status pane. A technician editing
fifteen rows cannot tell which ones were rejected, or what they had entered.

**Background timers can overwrite an in-progress edit.**
`BacnetVariable.cpp:124-125` starts `SetTimer(1, BAC_LIST_REFRESH_TIME)` and
`SetTimer(4, 15000)` on the Variables dialog.

The design response: an edit is not "done" until the value has been read back
from the device; a rejected value stays on screen with the reason attached; and
live refresh never touches a row being edited.

## Architecture

- **Process shape:** plain console/service process, no message-only window. This
  follows from the blocking primitives above - nothing in the read or write path
  needs a pump once `Post_Write_Message` is bypassed.
- **Wire format:** a vendored copy of `CM5/ud_str.h` with `byte` replaced by
  `uint8_t` and the `public:` removed. Point structs unchanged. A build-time
  `static_assert` on `sizeof(Str_in_point)` guards against drift from the app's
  copy.
- **Transport:** `ModbusDllforVc` for Tstat, the BACnet stack for T3 devices,
  behind one internal interface so the screens do not know which is in use.
- **Write path:** `WritePrivateData_Blocking` / `GetPrivateData_Blocking`
  directly, wrapped as `Result<T> write(...)` returning success only after a
  verifying read-back.
- **HTTP:** binds `127.0.0.1` only by default. This writes to live building
  equipment; remote and tablet access is a later, deliberate step with its own
  decision about authentication.
- **Build:** a new `.vcxproj` in `T3000 - VS2019.sln`, so the existing CI and
  `scripts/ci-local.ps1` cover it with no changes.

## Milestones

1. **Read one device's inputs in a browser.** Vendored structs, Modbus transport,
   `GetPrivateData_Blocking`, one HTTP endpoint, the existing `inputs.html` grid.
   Proves the wire format against real hardware - which is the whole risk.
2. **Write one Full Label, with real verification.** Read-back confirmation, and
   the rejected-value-stays-on-screen behaviour.
3. **Outputs and Variables** on the shared grid component.
4. **Schedules/Programs.** Needs the `Point_T3000` fixes and is a different order
   of work; scope it after 1-3 are real.

## Risks

1. **The vendored struct drifts from the app's copy.** The guard is the
   milestone-1 byte-level diff against a live device, and nothing weaker. A
   `static_assert` on `sizeof` would not have caught the divergence documented
   above: the two definitions differ by *field identity at the same offsets*
   (`sen_on` vs `sub_id`, `calibration` vs `calibration_l`), and both plausibly
   total the same number of bytes. Size agreement proves nothing here.
2. **Modbus decoupling is worse than the counts suggest.** The 99 MFC references
   were counted, not read. A `CString` threaded through a data structure costs
   more than one in a signature. Cheapest probe before committing: build
   `ModbusDllforVc` with `UseOfMfc` off and count the errors.
3. **T3 devices need the BACnet path before milestone 3**, which brings back the
   `BacNet_hwd` extern and the library's `UseOfMfc=Static` setting. Not on the
   critical path for Tstat, so it is deferred, not solved.
