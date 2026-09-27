# Firmware updates in T5000: plan

This is a plan. Nothing in it is built, and nothing in T5000 can send
firmware to a device. The first step that could, F2 below, waits for the
owner's go-ahead, and so does every step after it.

## What T3000 does

T3000 does not flash a device itself. Its Tools menu's "Load firmware for a
single device" (`T3000.rc:11530`) hands the job to ISP.exe, a program of its
own, built from `ISP\` in the same solution:

- T3000 writes the device and the file to `AutoFlashFile.ini`, beside
  itself, in its `[Data]` section: `Command=1`, `COM_OR_NET`, `COMPORT` and
  `Baudrate` or `IPAddress` and `IPPort`, the Modbus `ID`, `Subnote` and
  `SubID` for a device behind a controller, and `FirmwarePath`
  (`Dowmloadfile.cpp:146-328`).
- It starts ISP.exe with no arguments, hidden, and waits for it to end
  (`WinExecAndWait`, `global_function.cpp:13604`). ISP reads the file and,
  finding `Command=1`, flashes without waiting for the operator.
- T3000 then reads `Command` back: 2 is success, 3 no answer, 4 any other
  failure (`global_define.h:70-74`, and the same in
  `ISP\Global_Struct.h:583-587`). That is all it learns.
- For several devices, `Flash_Multy.cpp` does the same once per device, one
  after another.
- The file is picked from disk. T3000 can also fetch it: it asks a Temco
  server (newfirmware.com) and falls back to sending the operator to
  `temcocontrols.com/ftp/firmware/` (`Dowmloadfile.cpp:92-132`).

ISP.exe speaks three ways to a device:

- **Serial, Modbus RTU** (`ComWriter.cpp`): write 127 to register 16 to
  enter the bootloader (`:264`), wait for it on register 11, then send the
  file in 128-byte blocks. The file's MD5 and size go to registers 1993-1998
  (`:868-936`), which lets a flash that was cut off carry on where it
  stopped.
- **TCP**: a start packet, 128-byte blocks and an end packet, with 3-second
  timeouts (`TCPFlasher.cpp`).
- **TFTP**: 512-byte blocks to UDP port 10000, after a broadcast of
  `0xEE 0x10` to port 1234 starts the bootloader (`TFTPServer.cpp:160`,
  `:1094`).

A device behind a controller on MS/TP is reached through the controller
(`BacnetMstp.cpp`).

What ISP checks, and what it lets through:

- A `.hex` file must carry Temco's tag near its start, and each line's
  checksum must be right. Nothing checks the file as a whole.
- A `.bin` file is checked by its extension alone.
- The device's product must match the file's. But `Check_Temco_Firmware=0`
  in ISP's `Setting.ini` turns that off (`ComWriter.cpp:1867`), and a
  device reporting product 0 or 255 skips it (`:1871`).
- A file built for a larger chip gets a message, not a refusal.

ISP is MFC, built with the v140 toolset. T5000 uses neither MFC nor T3000's
DLLs, so none of ISP's code can be linked into T5000 as it is.

## What T5000 already knows

- A scan gives each device's product, firmware version and whether it is in
  its bootloader (`discovery/scan_response.h:87`); the Devices page counts
  the ones that are.
- A serial scan gives the port, the rate and the Modbus id, and they are
  saved (schema 4).
- A device added by hand has no firmware version until a scan or Find
  reaches it.
- `WRITEPRGFLASH_COMMAND` (122) is not a firmware update. It tells a panel
  to save its settings to flash, and T5000 never sends it
  (`bacnet/write_command.h:28`).

## Two ways to do it

**A. Run ISP.exe, as T3000 does.** T5000 writes `AutoFlashFile.ini`, starts
ISP.exe, waits, and reads the result. Each transport is ISP's own, already
used on real devices. T5000 learns only the result, so the page can show
that a flash is running but not how far it has got, and ISP.exe and its
DLLs have to be shipped beside T5000.

**B. Port ISP's transports into T5000,** one at a time, each tested first
against a synthetic bootloader on loopback. The page can show each block,
and T5000 needs nothing beside it. It is far more work, and each transport
is new code until the owner has checked it on a real device.

**Recommended: A first, then B one transport at a time,** starting with
serial Modbus. A gives a Firmware page that flashes exactly as T3000 does.
B then replaces ISP.exe where T5000's own code has been checked against it.

## Steps

Each step is its own PR. F1 sends nothing to any device and can be built
without waiting. F2 on each wait for the owner.

**F1. The Firmware page, checking only.** A page in the bar of pages that
lists the devices with their product, firmware version and bootloader state.
A file is picked from disk and checked: its format, Temco's tag, each line's
checksum, and whether its product matches the device's. Nothing is sent.
This is where the checks below are built and tested, before anything can
use them.

**F2. Flash through ISP.exe.** *Waits for the owner.* The page writes
`AutoFlashFile.ini`, starts ISP.exe and shows the result. Before it does,
T5000 makes its own checks (below), whatever ISP's `Setting.ini` says. It is
tested with ISP.exe pointed at a synthetic bootloader on loopback, then by
the owner on a device they choose.

**F3. Serial Modbus in T5000.** *Waits for the owner.* ComWriter's
bootloader entry, blocks, MD5 and resume, in T5000, tested against a
synthetic bootloader on the com0com pair (CNCA0 and CNCB0 only), then by the
owner.

**F4. TCP, then F5. TFTP,** each the same way, against synthetic bootloaders
on loopback ports.

**F6. Several devices,** one after another, as `Flash_Multy.cpp` does, with
a stop between devices.

## T5000's own checks before anything is sent

- The device answered a read in this session: a scan or Find reached it. A
  device added by hand and never reached cannot be flashed.
- The file passes F1's checks, and its product is the device's. There is no
  setting to turn this off, and a device reporting product 0 or 255 is
  refused, where ISP lets both through.
- The operator has confirmed a summary: the device, its address or port, its
  firmware now and the file's.
- One device at a time.
- A serial port is opened only when the operator picks it for that device.

## Testing without hardware

- A synthetic bootloader for each transport, in the style of the synthetic
  panels the read and write paths are tested against: loopback TCP and UDP
  ports for TCP and TFTP, and the com0com pair for serial. Each records what it received,
  and the tests compare that with the file.
- T5000Conformance holds each ported transport to ISP's source as text, as
  it holds the grid's rules to T3000's now.
- Only a real device shows its bootloader's real timing, a noisy serial
  line, and what a device does when its power is cut mid-flash. Those checks
  are the owner's.

## Decisions for the owner

1. **Go-ahead for F2,** the first step that can flash. Until then nothing
   past F1 is built.
2. **A, B, or A then B** (recommended: A then B).
3. **Where files come from:** from disk only (recommended at first), or also
   fetched from Temco's server as T3000 does, which is T5000 reaching out to
   the internet.
4. **Several devices at once** (F6): wanted, or one at a time only?
5. **Bootloader updates:** some firmware needs a newer bootloader. Refuse
   and say so (recommended), or port ISP's bootloader flashing too?
