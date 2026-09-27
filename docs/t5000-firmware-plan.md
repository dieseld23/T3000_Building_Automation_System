# Firmware updates in T5000: plan

This is a plan. Nothing in it is built, and nothing in T5000 can send
firmware to a device.

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
Modbus id (`ComWriter.cpp:2228`, `:2262`).

## What ISP checks, and what T5000 checks instead

A `.hex` file's header is at 0x8200 or 0x10200 for an ARM chip of 32K or
64K, told apart by its extended linear address records (0x0800, or 0x0801
and up), and at 0x100 otherwise (`HexFileParser.cpp:60-68`, `:251-258`). A
`.bin` file's header is at 0x100, or failing that 0x200
(`BinFileParser.cpp:101-104`). The header gives the company, the product
name and the version (`Bin_Info`, `Global_Struct.h:604-605`).

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
  (`ComWriter.cpp:1867`), or when the device reports product 0 or 255
  (`:1871`). ISP names the device's product and compares that name with the
  file's, with one list of aliases on serial (`ComWriter.cpp:1898-1947`) and
  another on the network (`TFTPServer.cpp:1279-1287`).
- **A file too large for the chip,** with a message, not a refusal.

The two paths also read files differently: serial and Modbus TCP read a
`.hex` only, into a buffer of 0x1FFFFF filled with 0xFF
(`ISPDlg.cpp:2106-2111`); the network tries a `.bin` first, filled with
0xFF, then a `.hex`, filled with 0x00, in 0x3FFFFF (`:2214-2237`). T5000
reads each as ISP does on the path it would go on, fill included.

T5000 refuses, with the reason, and has no setting to turn it off:

- a file whose company is not Temco's, `.bin` or `.hex`;
- a file for another product than the device's, by the alias list of the
  path it would go on; and a device reporting product 0 or 255;
- a file that needs a newer bootloader than the device has (below);
- a file ISP would say is for a larger chip than the device's. How ISP
  tells is for F1 to pin down.

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

It also refuses a bootloader older than 62 on a MiniPanel ARM or MiniPanel
on serial, 54 on a TSTAT10, 56 on the STM32 devices, and 48 or less (but
not 0) on a TSTAT8 (`ISP\global_function.cpp:1083-1114`). A CO2, humidity
or pressure device whose bootloader reads 0 and whose firmware is 59 or
more is not checked (`:1050-1056`).

Where ISP would update the bootloader first, T5000 refuses the file and says
which bootloader it needs. T5000 has no way to flash a bootloader. Where
T5000 cannot read the bootloader's version, it refuses any file ISP's table
would check.

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

Each step is its own PR. The code that sends firmware lives in a module of
its own, `firmware/`, and the separation guard (`write_separation_guard.cpp`)
is extended: nothing that reads, and nothing in the write path, includes
it, and its list of the places that send (S5) gains only `firmware/`'s
transports.

**F1. The Firmware page, checking only.** A page in the bar of pages that
lists the devices with their product, firmware version, bootloader version
and bootloader state. A file is picked from disk and checked by everything
in *What T5000 checks*, against a device the operator picks. Nothing is
sent. The checks are built and tested here, and held to ISP's source by
the conformance checks.

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
