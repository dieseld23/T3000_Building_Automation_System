#pragma once

// Whether a firmware file read by firmware_file.h may go to a device, as ISP
// decides it before it flashes, and where T5000 is stricter than ISP, as
// docs/t5000-firmware-plan.md decides.
//
// ISP checks a file on one of these routes, picked by the path and the file:
//
//   serial, a .hex of data records   Flash_Modebus_Device calls
//       UpdataDeviceInformation_ex (ComWriter.cpp:368, 494, 1858-1954):
//       GetProductName's names, the file's name as its first 10 bytes, no
//       bootloader check
//   serial, a .hex of linear address records, or any .bin (FlashByCom gives
//       a .bin that type, ISPDlg.cpp:2542-2545)   the extended-format
//       threads call UpdataDeviceInformation (ComWriter.cpp:1432, 2524,
//       1983-2139): GetFirmwareUpdateName's names, the file's whole name,
//       trimmed, and check_bootloader_and_frimware
//   serial, a .hex of segment address records   BeginWirteByCom starts no
//       flash for it (ComWriter.cpp:96-244 has no branch for type 1)
//   network   the device's bootloader names itself in the TFTP handshake,
//       and ISP compares that name with the file's in three places, each
//       with aliases of its own (MySocket.cpp:130-185 and 213-270,
//       TFTPServer.cpp:1249-1300); TFTPServer calls
//       check_bootloader_and_frimware (TFTPServer.cpp:1385, 2117). T5000
//       checks before the flash, by the device's product and the extended
//       route's names; the handshake's own checks are F4's
//   through a controller   not checked yet (F5)
//
// T5000 is stricter:
//
//   - there is no setting that turns a check off. ISP skips the company and
//     product checks when its Setting.ini has Check_Temco_Firmware=0
//     (ComWriter.cpp:1867-1868, 1985-1986, HexFileParser.cpp:83-84,
//     MySocket.cpp:174);
//   - a .bin's company is checked. ISP's check of it never refuses
//     (BinFileParser.cpp:121-134);
//   - a device reporting product 0 or 255 is refused. ISP flashes it with
//     no product or bootloader check (ComWriter.cpp:1871-1874, 2020-2023);
//   - a header whose product name has no 0 in it is refused;
//   - on the network, the file is held to the device's product, by the
//     extended route's names. ISP takes a HUMNET, CO2NET or PSNET file for
//     any device there (MySocket.cpp:159-161, 246-248), and any file for a
//     device naming itself HUMNET, CO2NET, CO2 or PSNET
//     (TFTPServer.cpp:1284-1287);
//   - a file that needs a newer bootloader than the device has, or one T5000
//     cannot tell, is refused on every route, the data route too, where ISP
//     does not look: T5000 does not update bootloaders.
//
// Nothing here sends anything. It says what T5000 would do, and why.

#include <string>
#include <vector>

#include "firmware_file.h"

namespace t5000::firmware
{
    enum class Route
    {
        SerialData,       // UpdataDeviceInformation_ex
        SerialExtended,   // UpdataDeviceInformation
        SerialNone,       // a segment address .hex: ISP starts no flash
        Network,
        Controller,
    };

    const char* to_string(Route route);

    Route route_of(const FirmwareFile& file);

    // GetFirmwareUpdateName (ISP\global_function.cpp:346-551): the name the
    // extended-format route and the file's bootloader flags know a product
    // by. Any other product is "PID" and its number.
    std::string firmware_name(int product);

    // GetProductName (:556-762): the name the data route knows it by, which is
    // firmware_name's but for product 10, "TStat10" rather than "PID10".
    std::string product_name(int product);

    // The file's name as the extended-format route compares it
    // (ComWriter.cpp:2037-2069): the header's name, read with strlen,
    // lowered, "mini_arm" made "Minipanel", raised and trimmed.
    std::string extended_file_name(const Header& header);

    // ... as the data route compares it (ComWriter.cpp:1878-1887): its 10 bytes up to
    // the first 0, raised, not trimmed.
    std::string data_file_name(const Header& header);

    // Whether a route takes a file named `file` for a device named `device`
    // by that route's function: the same name, or one of its aliases
    // (ComWriter.cpp:2073-2125; :1898-1953).
    bool extended_names_match(const std::string& device, const std::string& file);
    bool data_names_match(const std::string& device, const std::string& file);

    // Whether ISP marks the file as needing the newer bootloader
    // (firmware_must_use_new_bootloader), from its name and version, on this
    // path (ISPDlg.cpp:2250-2290 on the network, 2590-2652 on serial).
    bool file_needs_new_bootloader(Path path, const Header& header);

    // What ISP's check_bootloader_and_frimware (global_function.cpp:1041-
    // 1131) makes of a device, once the file is marked.
    enum class BootloaderCall
    {
        Fine,           // no update wanted
        Update,         // ISP would flash the bootloader first
        RefusedSerial,  // ISP will not update it over a serial port (Ret -1)
    };
    BootloaderCall bootloader_call(int product, Path path, int bootloader, int firmware_low);

    // The products check_bootloader_and_frimware looks at (:1059-1072).
    bool bootloader_is_checked(int product);

    struct DeviceFacts
    {
        int product = 0;   // the product id the device reports

        // max(register 11, register 14) as ISP reads it, or what T5000
        // knows in its place (the settings' bootloader_rev), with where it
        // came from for the page to say.
        bool        bootloader_known = false;
        int         bootloader       = 0;
        std::string bootloader_from;

        // Register 4's low byte, which ISP passes as app_already_version
        // (ComWriter.cpp:2128): a CO2, humidity or pressure device at 59 or
        // more is not checked (global_function.cpp:1050-1055).
        bool firmware_low_known = false;
        int  firmware_low       = 0;
    };

    struct Verdict
    {
        bool ok = false;   // nothing below refuses it

        Route route = Route::SerialExtended;

        std::vector<std::string> refusals;   // each a sentence, the reason
        std::vector<std::string> notes;      // what the operator should know

        std::string device_name;   // as the route names the product
        std::string file_name;     // as the route names the file
        int         version = 0;   // the header's software_high * 256 + software_low
        bool        needs_new_bootloader = false;   // file_needs_new_bootloader
    };

    // Everything above, for `file` (read for its path) and the device.
    Verdict check_firmware(const FirmwareFile& file, const DeviceFacts& device);
}
