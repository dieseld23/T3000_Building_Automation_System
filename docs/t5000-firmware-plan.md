# Firmware updates in T5000: plan

This is a plan. Its first step (F1) is built: `T5000/firmware/` reads a
file and says whether ISP would take it for a device, and the Firmware
page checks a file from disk against a device in the list. Nothing in
T5000 can send firmware to a device.

## What the owner decided (2026-09-27)

- **Port ISP into T5000.** T5000 does not run ISP.exe. ISP's transports are
  written again in T5000, one at a time.
- **Firmware files come from disk.** T5000 does not fetch them from Temco's
  server.
- **One device or several,** one after another.
- **No bootloader updates.** A file that needs a newer bootloader than the
  device has is refused, and the page says so.
- **Tested against synthetic bootloaders only.** Flashing a real device is
  the owner's check, on a device they choose.

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

T5000 does the same from its own Firmware page, with ISP's transports in
its own code, so the page can show each block as it goes.

## How ISP reaches a device

Which way ISP goes depends on how the device is reached, not its product
(`Judge_Flash_Type`, `ISPDlg.cpp:1124-1147`):

- **On a serial port: Modbus RTU** (`FlashByComport`, `ComWriter.cpp`). A
  device with no IP address, or only a serial rate, in T3000's list
  (`Dowmloadfile.cpp:234-240`).
- **On the network, directly: TFTP** (`FlashByEthernet`, `TFTPServer.cpp`).
  A device with an IP address and no parent serial
  (`Dowmloadfile.cpp:222-226`).
- **Behind a controller: Modbus TCP** (`OnFlashSubID`, `ISPDlg.cpp:2123`),
  through the controller, to port 10000 unless the list gives another
  (`Dowmloadfile.cpp:268-270`), addressed by the device's Modbus id.

`TCPFlasher.cpp` is not used: ISP's `m_pTCPFlasher` is a `CComWriter`
(`ISPDlg.h:169`). And the UDP broadcast of `0xEE 0x10` to port 1234
(`SendFlashCommand`, `TFTPServer.cpp:1917`) is never called.

### Serial Modbus RTU

1. Write 127 to register 16 to enter the bootloader (`ComWriter.cpp:264`).
2. Wait for register 11 to read more than 1: the bootloader is running.
   ISP polls once a second, up to 15 times (`:1491-1501`). Its other wait
   (`:275-285`) ends after one try, as its `while` test is the wrong way
   round. T5000 waits the 15 seconds.
3. Send the file in 128-byte blocks. The file's MD5 and size go to
   registers 1993-1998 (`:868-936`), which lets a flash that was cut off
   carry on where it stopped.

### TFTP, on the network

1. Over TCP to the device's port, a 64-byte packet starting `0xEE 0x10`
   (`TFTPServer.cpp:1070`, `:1142-1143`). The device answers with 40 bytes
   starting `0x65 0x00` (`MySocket.cpp:86-92`).
2. A 45-byte "Temcocontrols" packet with the device's IP address and subnet
   mask, to UDP port 10000, both broadcast and to the device
   (`TFTPServer.cpp:511-562`, `:1161`, `:1170`).
3. The file in 512-byte DATA packets, opcode 3 and a big-endian block
   number, to the device's port 10000. Each is acknowledged with opcode 4
   and its block number; each is tried up to 10 times (`:321`, `:1565-1593`,
   `MySocket.cpp:291-292`). ISP listens on UDP port 69.
4. The last block is shorter than 512 bytes, and "FLASH DONE" follows. The
   device says it is done with `0x00 0x04 0xFF 0xFF` (`MySocket.cpp:309`).

The TFTP broadcast reaches the whole subnet. T5000 sends it only on the
interface the operator picked for that device, as a scan does.

### Modbus TCP, behind a controller

As serial Modbus, over a TCP socket to the controller, with the device's
Modbus id (`ComWriter.cpp:2228`, `:2262`). It picks its thread otherwise
than serial does (`BeginWirteByTCP`, `ComWriter.cpp:2220-2267`): an ARM
chip's `.hex` of linear address records goes to
`flashThread_ForExtendFormatHexfile_RAM`, and every other file to
`Flash_Modebus_Device`, and so to its checks (below). F5 follows that.

## What ISP checks, and what T5000 checks instead

A `.hex` file's header is at 0x8200 or 0x10200 for an ARM chip of 32K or
64K, told apart by the address its first line gives (0x0800, or 0x0801
and up), and at 0x100 otherwise (`HexFileParser.cpp:58-69`, `:237-260`). A
`.bin` file's header is at 0x100, or failing that 0x200
(`BinFileParser.cpp:96-134`). The header gives the company, the product
name and the version (`Bin_Info`, `Global_Struct.h:598-607`).

Each path reads a file into a buffer of its own, and what lies past the
file's data is sent as the buffer holds it:

| Path | A `.bin` | A `.hex` |
|---|---|---|
| Serial (`FlashByCom`, `ISPDlg.cpp:2472-2523`) | 0x3FFFFF bytes of 0xFF | 0x1FFFFF bytes of 0x00 |
| Network (`FlashByEthernet`, `:2185-2237`) | 0x3FFFFF bytes of 0xFF | 0x3FFFFF bytes of 0x00 |
| Behind a controller (`OnFlashSubID`, `:2090-2112`) | not read | 0x1FFFFF bytes of 0xFF |

On serial, the file's kind decides which thread flashes it and so how it
is checked (`BeginWirteByCom`, `ComWriter.cpp:96-244`; a `.bin` is given
the linear type, `ISPDlg.cpp:2542-2545`):

| File | Thread | Check |
|---|---|---|
| A `.hex` of data records | `Flash_Modebus_Device` | `UpdataDeviceInformation_ex`: the device named by `GetProductName`, the file by the first 10 bytes of its name, its own aliases, no bootloader check |
| A `.hex` of linear address records, or a `.bin` | `flashThread_ForExtendFormatHexfile`, or its `_RAM` twin for an ARM chip's `.hex` | `UpdataDeviceInformation`: the device named by `GetFirmwareUpdateName`, the file by its whole name, `mini_arm` made `Minipanel`, trimmed, its aliases, and the bootloader check |
| A `.hex` of segment address records | none | ISP starts nothing |

The two name lists differ only for product 10, `TStat10` to one and
`PID10` to the other. On the network, the device's bootloader names
itself in the handshake, and ISP compares that name with the file's in
three places, each with aliases of its own (`MySocket.cpp:130-185` and
`:213-270`, `TFTPServer.cpp:1249-1300`).

What ISP lets through, which T5000 does not:

- **A `.bin` from anyone.** Its company check never refuses a file: a
  failure sets a variable nothing reads, and the file is read all the same
  (`BinFileParser.cpp:121-134`). A
  `.bin` passes if it starts "ASIX" or has "Temco" in bytes 512-531
  (`:48-76`). A `.hex` is refused unless its company is "TEMCO" or holds
  "CO2" (`HexFileParser.cpp:85`).
- **Any version.** The version comparison in `ComWriter.cpp:2194-2206` is
  never reached: it follows a `return` (`:2193`).
- **Another product,** when `Check_Temco_Firmware=0` in ISP's `Setting.ini`
  (`ComWriter.cpp:1867`, `:1985`), or when the device reports product 0 or
  255 (`:1871`, `:2020`). On the network, a HUMNET, CO2NET or PSNET file
  for any device (`MySocket.cpp:159-161`, `:246-248`), and any file for a
  device naming itself HUMNET, CO2NET, CO2 or PSNET
  (`TFTPServer.cpp:1284-1287`).
- **A file needing a newer bootloader, on the data route,** which does not
  look at the bootloader.

For a TStat6, TStat7 or TStat5i, ISP also checks the chip by register 11:
below 37 is the 64K chip, which takes only a `.hex` of data records, and
37 or more the 128K chip, which takes the rest. `Flash_Modebus_Device`
reads the register from the running device, before it jumps to the
bootloader (`ComWriter.cpp:429`, `:461`, `:504-568`);
`flashThread_ForExtendFormatHexfile` reads it in the bootloader (`:1479`,
`:1507-1570`). An ARM chip's `.hex` goes to the `_RAM` thread, which does
not check. The same check in `WriteCommandtoReset` (`:290-330`) is never
called.

T5000 refuses, with the reason, and has no setting to turn it off:

- a file whose company is not Temco's, `.bin` or `.hex`;
- a file for another product than the device's, by the names and aliases
  of the route ISP would check it on. On the network, where ISP checks
  only in the handshake, by the device's product and the second route's
  names, before anything is sent;
- a device reporting product 0 or 255;
- a `.hex` of segment address records on serial, which ISP does not flash;
- a file that needs a newer bootloader than the device has (below), on
  every route;
- a device behind a controller, until F5.

For a TStat6, TStat7 or TStat5i it notes the chip check, which needs
register 11, read by Modbus. F3 reads it where ISP does on each route and
refuses a file that does not fit.

## Bootloaders

ISP reads the bootloader's version as the larger of Modbus registers 11 and
14 (`ISP\global_function.cpp:1045-1082`), on every path. A panel's settings
also carry it (`bootloader_rev` in `Str_Pro_Info`, `ud_str.h:796`), in the
settings T5000 already reads by BACnet.

ISP decides a file needs a newer bootloader from the file's version, and
the limits differ by path (`ISPDlg.cpp:2250-2290` on the network,
`:2600-2652` on serial):

| Product | On the network | On serial |
|---|---|---|
| MiniPanel ARM | 60 and up (divided by 100) | 60 and up (divided by 100) |
| TSTAT8 | - | 101 and up |
| PID10 | - | 5109 and up |
| CO2 (all) | over 0.58 (divided by 100) | 59 and up |

In whole numbers, as ISP's float arithmetic comes out, the network's CO2
limit is 59 and up, as on serial.

Once a file is marked, ISP looks at the device's bootloader
(`check_bootloader_and_frimware`, `ISP\global_function.cpp:1041-1131`). It
updates it first when it is older than 54 on a TSTAT10, older than 56 on a
CO2, humidity, pressure or PM2.5 device, or 48 or less (but not 0) on a
TSTAT8. On a MiniPanel ARM or MiniPanel older than 62 it updates it on the
network. On serial it answers -1 instead, which stops the flash in the
`_RAM` thread (an ARM chip's `.hex`), which goes on only on 1, but not in
`flashThread_ForExtendFormatHexfile` (a `.bin`, or another linear `.hex`),
which goes on on anything but 0 (`ComWriter.cpp:1432`, `:2524-2526`). A
CO2, humidity, pressure or PM2.5
device whose firmware's low byte is 59 or more is not looked at
(`:1050-1056`), and neither is any device on serial's data route.

Where ISP would update the bootloader first, T5000 refuses the file and says
which bootloader it needs. T5000 has no way to flash a bootloader. Where
T5000 cannot read the bootloader's version, it refuses any marked file for
a product ISP looks at. Whether a panel's `bootloader_rev` is the number
ISP reads from registers 11 and 14 is for the owner's bench check before
F3 relies on it. Until then the Firmware page checks by it, and says it
came from the panel's settings and which read gave them.

A panel whose settings give 0 there is taken to give none, as ISP passes
over a TSTAT8's 0 (`global_function.cpp:1083`): nothing is kept, and a
marked file for it is refused as for any device whose bootloader is not
known. The firmware's low byte, which spares a CO2, humidity or pressure
device at 59 or more, is not passed on either: ISP reads it from
register 4, which T5000 does not read before F3, and the scan's firmware
number has not been shown to be it. So such a device is checked as if
the byte were under 59, which refuses more than ISP would, never less.

## What T5000 already knows

- A scan gives each device's product, firmware version and whether it is in
  its bootloader (`discovery/scan_response.h:87`); the Devices page counts
  the ones that are.
- A serial scan gives the port, the rate and the Modbus id, and they are
  saved (schema 4).
- A device added by hand, and a virtual device, has no firmware version. A
  virtual device is never flashed.
- `WRITEPRGFLASH_COMMAND` (122) is not a firmware update. It tells a panel
  to save its settings to flash, and T5000 never sends it
  (`bacnet/write_command.h:28`).

## Steps

Each step is its own PR. The firmware code lives in a module of its own,
`firmware/`. The separation guard (`write_separation_guard.cpp`) holds it
to reading the bytes it is given, naming no socket, serial port or file
(S6), until a transport step lets that step's own file send, and its list
of the places that send (S5) gains only those files. The Firmware page's
check is held the same way, with what it includes (S7).

**F1. The Firmware page, checking only,** in two PRs.

- **F1a, the checks** (built). `T5000/firmware/` reads a file as ISP reads
  it on each path, and checks it against a device by everything in *What
  ISP checks, and what T5000 checks instead*. Its self-tests build files to trip each rule;
  `conformance/firmware_guard.cpp` parses ISP's name tables, alias chains,
  file flags and bootloader rules and compares T5000 with them over every
  name, product and version, pins the rest as text, and reads the
  repository's own `.hex` files.
- **F1b, the page** (built). `/firmware`, last in the bar of pages: T3000
  has it in its Tools menu (`T3000.rc:11530`), and opens it on Ctrl+R
  (`MainFrm.cpp:6827`); the Ctrl+F2 its menu shows offers to reset a
  device to its factory defaults (`:6785`). It lists each
  device with its product, firmware version, the path ISP would take to
  it (serial, the network, or behind a controller), its bootloader's
  version and where that came from, and whether the last scan found it
  in its bootloader. **Read** asks a panel for its settings, one request
  (sent once more if nothing answers) and only where the points pages
  would send one, for its
  `bootloader_rev`; Find and the Inputs, Outputs and Variables pages keep
  it too, from the settings they read. It is kept for the session, not
  saved, and only when the settings give the device's own serial.
  **Check a file...** sends a `.hex` or `.bin` from disk to T5000 itself,
  as the body of `POST /api/firmware/check`, the one route that takes up
  to 16 MiB, and only from T5000's own page or a local program that
  names no other page; every other request stays at 256 KB, and a larger
  one is refused before it is read. ISP copies a `.bin` of any length
  into its 0x3FFFFF-byte buffer, past its end; of a linear `.hex`, it
  refuses a record whose address is more than the buffer's length and
  writes any other whole, past the end when it runs over. T5000 refuses
  all of these, and 16 MiB takes the
  `.hex` of sixteen-byte records that
  fills it, about 11.5 MB. T5000 reads the file as ISP would on the
  device's path and shows what ISP and T5000 make of it, and the device
  as the check was told it. Nothing is sent to any device. The separation
  guard holds `app/firmware_page.*`, which does the checking, to reaching
  no transport at any depth (S7).

  A device Find reached, and no scan has, is not sent a file: Find reads
  no product, so the one in the list may be the model it was added by
  hand as, and ISP checks a file against the product the device reports.

**F2. Synthetic bootloaders.** Test code only: a serial Modbus bootloader
on com0com's CNCB0 (T5000 opens CNCA0, and never any other port), a TFTP
bootloader and a Modbus TCP controller on loopback ports. Each records what
it was sent, and can stop answering, drop a block or cut off, so the tests
can check the retries and the resume.

**F3. Serial Modbus RTU.** Register 16, the wait on register 11, the
128-byte blocks, the MD5 and the resume, against F2's bootloader. Then the
owner flashes a device of their choosing.

**F4. TFTP,** then **F5. Modbus TCP behind a controller,** each the same way.

**F6. Several devices,** one after another, as `Flash_Multy.cpp` does. Each
is checked before the first is sent anything, and the operator can stop
between devices.

## T5000's own checks before anything is sent

- The device answered a read in this session: a scan or Find reached it. A
  device added by hand and never reached, or a virtual device, is not
  flashed.
- A scan in this session reported its product. Find reads no product, and
  a scan that gives product 0 reports none, so a device only those reached
  may still carry the model it was added by hand as.
- T5000 knows an address for it: T3000 hands a device with no IP address
  to ISP as on a serial port, and one with neither has no path.
- The file passes every check above for that device, on the path it would
  go on.
- The operator has confirmed a summary: the device, its address or port,
  its firmware and bootloader now, and the file's.
- A serial port is opened only when the operator picks it for that device.
- The network's broadcast goes only on the interface the operator picked.

## Testing without hardware

- F2's synthetic bootloaders, in the style of the synthetic panels the read
  and write paths are tested against. The tests compare what each was sent
  with the file.
- T5000Conformance holds each ported transport, and each check, to ISP's
  source as text, as it holds the grid's rules to T3000's now.
- Only a real device shows its bootloader's real timing, a noisy serial
  line, and what a device does when its power is cut mid-flash. Those checks
  are the owner's.
