#include "firmware_check.h"

#include <ctype.h>

namespace t5000::firmware
{
    namespace
    {
        // GetFirmwareUpdateName's cases, by the product ids T3000's
        // ProductModel.h gives them. T5000Conformance reads the function
        // and the header and holds this table to them, row for row.
        struct Named
        {
            int         product;
            const char* name;
        };

        const Named kFirmwareNames[] = {
            { 2, "TStat5A" },           // PM_TSTAT5A
            { 1, "TStat5B" },           // PM_TSTAT5B
            { 3, "TStat5B2" },          // PM_TSTAT5B2
            { 4, "TStat5C" },           // PM_TSTAT5C
            { 10, "PID10" },            // PM_TSTAT10
            { 12, "TStat5D" },          // PM_TSTAT5D
            { 16, "TStat5E" },          // PM_TSTAT5E
            { 51, "PM5EARM" },          // PM_PM5E_ARM
            { 41, "PM5E" },             // PM_PM5E
            { 17, "TStat5F" },          // PM_TSTAT5F
            { 18, "TStat5G" },          // PM_TSTAT5G
            { 19, "TStat5H" },          // PM_TSTAT5H
            { 6, "TStat6" },            // PM_TSTAT6
            { 8, "TStat5I" },           // PM_TSTAT5i
            { 7, "TStat7" },            // PM_TSTAT7
            { 9, "TStat8" },            // PM_TSTAT8
            { 91, "TStat8Wifi" },       // PM_TSTAT8_WIFI
            { 92, "TStat8Occ" },        // PM_TSTAT8_OCC
            { 93, "TStat7ARM" },        // PM_TSTAT7_ARM
            { 94, "TStat8220V" },       // PM_TSTAT8_220V
            { 100, "NC" },              // PM_NC
            { 50, "CM5" },              // PM_CM5
            { 120, "LC" },              // PM_LightingController
            { 20, "T3-8I13O" },         // PM_T38I13O
            { 21, "T3-8IOA" },          // PM_T3IOA
            { 22, "T3-32AI" },          // PM_T332AI
            { 23, "T3-8AI160" },        // PM_T38AI16O
            { 24, "ZigBee" },           // PM_ZIGBEE
            { 25, "FlexDriver" },       // PM_FLEXDRIVER
            { 26, "T3-PT10" },          // PM_T3PT10
            { 27, "T3-PERFORMANCE" },   // PM_T3PERFORMANCE
            { 28, "T3-4AO" },           // PM_T34AO
            { 29, "T3-6CT" },           // PM_T36CT
            { 30, "Solar" },            // PM_SOLAR
            { 31, "FWMTRANSDUCER" },    // PM_FWMTRANSDUCER
            { 35, "MiniPanel" },        // PM_MINIPANEL
            { 74, "MiniPanel" },        // PM_MINIPANEL_ARM
            { 40, "Pressure" },         // PM_PRESSURE
            { 13, "AirQuality" },       // PM_AirQuality
            { 14, "TstatHUM" },         // PM_HUMTEMPSENSOR
            { 42, "HUM-R" },            // PM_HUM_R
            { 15, "TStatRunar" },       // PM_TSTATRUNAR
            { 32, "CO2 Net" },          // PM_CO2_NET
            { 33, "CO2" },              // PM_CO2_RS485
            { 45, "Pressure" },         // PM_PRESSURE_SENSOR
            { 46, "T3PT12" },           // PM_T3PT12
            { 95, "T36CTA" },           // PM_T36CTA
            { 34, "CO2 Node" },         // PM_CO2_NODE
            { 43, "T322I" },            // PM_T322AI
            { 44, "T38IO" },            // PM_T38AI8AO6DO
            { 121, "BTU METER" },       // PM_BTU_METER
            { 47, "T322AIVG" },         // PM_T322AIVG
            { 48, "T38IOVG" },          // PM_T38IOVG
            { 49, "T3PTVG" },           // PM_T3PTVG
            { 210, "CO2NET" },          // STM32_CO2_NET
            { 211, "CO2RS485" },        // STM32_CO2_RS485
            { 212, "HUMNET" },          // STM32_HUM_NET
            { 213, "HUMRS485" },        // STM32_HUM_RS485
            { 104, "PWMTRANX" },        // PWM_TRANSDUCER
            { 214, "PSNET" },           // STM32_PRESSURE_NET
            { 215, "PSRS485" },         // STM32_PRESSURE_RS485
            { 216, "CO2 NODE" },        // STM32_CO2_NODE
            { 73, "PWMETER" },          // PM_PWMETER
            { 75, "WS" },               // PM_WEATHER_STATION
            { 52, "PM2.5" },            // STM32_PM25
        };

        // ProductModel.h's ids for the products the rules name.
        constexpr int kTstat6        = 6;     // PM_TSTAT6
        constexpr int kTstat7        = 7;     // PM_TSTAT7
        constexpr int kTstat5i       = 8;     // PM_TSTAT5i
        constexpr int kTstat8        = 9;     // PM_TSTAT8
        constexpr int kTstat9        = 59;    // PM_TSTAT9
        constexpr int kTstat10       = 10;    // PM_TSTAT10
        constexpr int kMiniPanel     = 35;    // PM_MINIPANEL
        constexpr int kMiniPanelArm  = 74;    // PM_MINIPANEL_ARM
        constexpr int kPm25          = 52;    // STM32_PM25
        constexpr int kCo2Net        = 210;   // STM32_CO2_NET, the first of the six
        constexpr int kPressureRs485 = 215;   // STM32_PRESSURE_RS485, the last

        bool is_stm32_sensor(int product)
        {
            return (product >= kCo2Net && product <= kPressureRs485) || product == kPm25;
        }

        std::string upper(std::string s)
        {
            for (char& c : s)
                c = (char)toupper((unsigned char)c);
            return s;
        }

        std::string lower(std::string s)
        {
            for (char& c : s)
                c = (char)tolower((unsigned char)c);
            return s;
        }

        // CString's Trim: white space off both ends.
        std::string trimmed(const std::string& s)
        {
            size_t first = 0;
            size_t last  = s.size();
            while (first < last && isspace((unsigned char)s[first]))
                first++;
            while (last > first && isspace((unsigned char)s[last - 1]))
                last--;
            return s.substr(first, last - first);
        }

        bool one_of(const std::string& s, std::initializer_list<const char*> names)
        {
            for (const char* n : names)
                if (s == n)
                    return true;
            return false;
        }

        std::string run_on_name(const Header& header)
        {
            bool ends = false;
            return header.product_name(ends);
        }

        // The aliases both serial routes share, in ISP's order: its first
        // four after the same name, and its last two. The data route has one
        // more between them.
        bool serial_aliases(const std::string& d, const std::string& f)
        {
            if (one_of(d, { "TSTAT5E", "TSTAT5H", "TSTAT5G" }) && f == "TSTAT5LCD")
                return true;
            if (one_of(d, { "TSTAT5A", "TSTAT5B", "TSTAT5C", "TSTAT5D", "TSTAT5F" }) && f == "TSTAT5LED")
                return true;
            if (one_of(d, { "CO2NET", "CO2RS485", "HUMNET", "HUMRS485", "PM2.5", "PSNET" }) && f == "CO2ALL")
                return true;
            if (f == "TSTAT6" && d == "TSTAT5I")
                return true;
            if (f == "CM5_ARM" && d == "CM5")
                return true;
            return false;
        }
    }

    const char* to_string(Route route)
    {
        switch (route)
        {
        case Route::SerialData:     return "serial, a .hex of data records";
        case Route::SerialExtended: return "serial, a .hex of linear address records or a .bin";
        case Route::SerialNone:     return "serial, a .hex of segment address records";
        case Route::Network:        return "network";
        case Route::Controller:     return "through a controller";
        }
        return "unknown";
    }

    Route route_of(const FirmwareFile& file)
    {
        if (file.path == Path::Network)
            return Route::Network;
        if (file.path == Path::Controller)
            return Route::Controller;
        if (file.kind == FileKind::Bin || file.format == HexFormat::LinearAddress)
            return Route::SerialExtended;
        if (file.format == HexFormat::SegmentAddress)
            return Route::SerialNone;
        return Route::SerialData;
    }

    std::string firmware_name(int product)
    {
        for (const Named& n : kFirmwareNames)
            if (n.product == product)
                return n.name;
        return "PID" + std::to_string(product);
    }

    std::string product_name(int product)
    {
        return product == kTstat10 ? "TStat10" : firmware_name(product);
    }

    std::string extended_file_name(const Header& header)
    {
        // Lowered, "mini_arm" made "Minipanel" before it is trimmed, then
        // raised and trimmed (ComWriter.cpp:2058-2069). Left(10) at :2041
        // is thrown away, so the whole name counts.
        std::string name = lower(run_on_name(header));
        if (name == "mini_arm")
            name = "Minipanel";
        return trimmed(upper(name));
    }

    std::string data_file_name(const Header& header)
    {
        // Ten characters appended one by one (ComWriter.cpp:1878-1881); the
        // compares stop at the first 0 among them.
        std::string name;
        for (size_t i = 0; i < 10 && header.bytes[header_at::product_name + i] != 0; i++)
            name += (char)header.bytes[header_at::product_name + i];
        return upper(name);
    }

    bool extended_names_match(const std::string& device, const std::string& file)
    {
        const std::string d = upper(device);
        const std::string f = upper(file);
        return f == d || serial_aliases(d, f);
    }

    bool data_names_match(const std::string& device, const std::string& file)
    {
        const std::string d = upper(device);
        const std::string f = upper(file);
        // ComWriter.cpp:1935-1939, which the extended route does not have.
        return f == d || serial_aliases(d, f) || (d == "CO2" && f == "CO2 NET");
    }

    bool file_needs_new_bootloader(Path path, const Header& header)
    {
        // The name trimmed and raised (ISPDlg.cpp:2258-2262, 2591-2595),
        // and the version as each branch reads it: divided by 100 for
        // MINI_ARM and on the network, not otherwise. In whole numbers, as
        // the comparisons come out in ISP's floats: v/100 >= 60 is v >= 6000,
        // and v/100 > 0.58 is v >= 59.
        const std::string name = upper(trimmed(run_on_name(header)));
        const int v = header.version();
        if (path == Path::Network)
        {
            if (name == "MINI_ARM")
                return v >= 6000;
            if (name == "CO2ALL")
                return v >= 59;
            return false;   // PID10's is commented out (:2273-2281)
        }
        if (path == Path::Serial)
        {
            // TSTAT8, PID10 and CO2ALL only while new_bootload is 0, which
            // it always is for T5000: it never flashes a bootloader.
            if (name == "MINI_ARM")
                return v >= 6000;
            if (name == "TSTAT8")
                return v >= 101;
            if (name == "PID10")
                return v >= 5109;
            if (name == "CO2ALL")
                return v >= 59;
            return false;
        }
        return false;   // through a controller: ISP marks nothing (OnFlashSubID)
    }

    bool bootloader_is_checked(int product)
    {
        return product == kTstat8 || product == kTstat9 || product == kMiniPanelArm || product == kMiniPanel ||
               product == kTstat10 || is_stm32_sensor(product);
    }

    BootloaderCall bootloader_call(int product, Path path, int bootloader, int firmware_low)
    {
        // comport is 0 from ComWriter, on serial and through a controller
        // (ComWriter.cpp:2128), and 1 from TFTPServer (TFTPServer.cpp:1385,
        // 2117). ISP decides only while com_port_flash_status is 0, flashing
        // what the operator chose rather than a bootloader it chose itself,
        // which is all T5000 does.
        const bool on_serial = path != Path::Network;
        if (is_stm32_sensor(product) && firmware_low >= 59)
            return BootloaderCall::Fine;
        if (!bootloader_is_checked(product))
            return BootloaderCall::Fine;
        if (product == kTstat8 && bootloader <= 48 && bootloader != 0)
            return BootloaderCall::Update;
        if ((product == kMiniPanelArm || product == kMiniPanel) && bootloader < 62)
            return on_serial ? BootloaderCall::RefusedSerial : BootloaderCall::Update;
        if (product == kTstat10 && bootloader < 54)
            return BootloaderCall::Update;
        if (is_stm32_sensor(product) && bootloader < 56)
            return BootloaderCall::Update;
        return BootloaderCall::Fine;
    }

    Verdict check_firmware(const FirmwareFile& file, const DeviceFacts& device)
    {
        Verdict v;
        v.route   = route_of(file);
        v.version = file.header.version();

        const bool data = v.route == Route::SerialData;
        v.device_name   = data ? product_name(device.product) : firmware_name(device.product);
        v.file_name     = data ? data_file_name(file.header) : extended_file_name(file.header);

        if (v.route == Route::Controller)
            v.refusals.push_back("T5000 does not check firmware for a device behind a controller yet.");
        if (v.route == Route::SerialNone)
            v.refusals.push_back("ISP does not flash a .hex file of segment address records over a serial port: it "
                                 "starts nothing for one.");

        if (device.product == 0 || device.product == 255)
            v.refusals.push_back("The device reports product " + std::to_string(device.product) +
                                 ", which says nothing about what it is, so no firmware is matched to it.");

        // The company (HexFileParser.cpp:83-89; BinFileParser.cpp:121, where
        // ISP's refusal does nothing).
        const std::string company = upper(file.header.company());
        const bool temco = company == "TEMCO" ||
                           (file.kind == FileKind::Hex ? company.find("CO2") != std::string::npos : company == "CO2");
        if (!temco)
            v.refusals.push_back("The file's header does not say it is Temco's: its company is \"" + file.header.company() +
                                 "\".");

        bool ends = false;
        file.header.product_name(ends);
        if (!ends)
            v.refusals.push_back("The file's product name does not end within its header, so ISP would read past it.");

        const bool match = data ? data_names_match(v.device_name, v.file_name) : extended_names_match(v.device_name, v.file_name);
        if (!match)
            v.refusals.push_back("The file is for " + v.file_name + ", and this device is a " + upper(v.device_name) + ".");
        if (v.route == Route::Network)
            v.notes.push_back("On the network, the flash itself checks the file against the name the device's bootloader "
                              "reports; this is a check before it, by the device's product.");

        v.needs_new_bootloader = file_needs_new_bootloader(file.path, file.header);
        if (v.needs_new_bootloader && bootloader_is_checked(device.product))
        {
            const bool exempt = is_stm32_sensor(device.product) && device.firmware_low_known && device.firmware_low >= 59;
            const std::string unlike_isp = data ? " ISP does not look at the bootloader for a file like this one; T5000 does." : "";
            if (!exempt)
            {
                if (!device.bootloader_known)
                {
                    v.refusals.push_back("On some devices this file needs a newer bootloader, and T5000 does not know "
                                         "this device's bootloader version. T5000 does not update bootloaders." +
                                         unlike_isp);
                }
                else
                {
                    const BootloaderCall call = bootloader_call(device.product, file.path, device.bootloader,
                                                                device.firmware_low_known ? device.firmware_low : 0);
                    const std::string from = device.bootloader_from.empty() ? "" : " (" + device.bootloader_from + ")";
                    if (call == BootloaderCall::Update)
                        v.refusals.push_back("This file needs a newer bootloader than the device's, version " +
                                             std::to_string(device.bootloader) + from +
                                             ". ISP would update the bootloader first; T5000 does not update bootloaders." +
                                             unlike_isp);
                    else if (call == BootloaderCall::RefusedSerial)
                        v.refusals.push_back("This file needs a newer bootloader than the device's, version " +
                                             std::to_string(device.bootloader) + from +
                                             ", and ISP will not update one over a serial port." + unlike_isp);
                }
            }
        }

        // The chip size check for a TStat6, TStat7 or TStat5i, by register
        // 11. Flash_Modebus_Device wants the 64K chip for a data .hex, and
        // reads the register from the running device before it jumps to the
        // bootloader (ComWriter.cpp:429, 461, 504-568);
        // flashThread_ForExtendFormatHexfile wants the 128K one for the
        // rest, and reads it in the bootloader (:1479, 1507-1570). A .hex for
        // an ARM chip goes to flashThread_ForExtendFormatHexfile_RAM instead,
        // which has no such check.
        const bool chip_checked = device.product == kTstat6 || device.product == kTstat7 || device.product == kTstat5i;
        if (chip_checked && data)
            v.notes.push_back("When it flashes, ISP takes this file for a TStat6, TStat7 or TStat5i only if the device's chip "
                              "is the 64K one (register 11 below 37), which T5000 has not read.");
        if (chip_checked && v.route == Route::SerialExtended && file.chip == Chip::Asix)
            v.notes.push_back("When it flashes, ISP takes this file for a TStat6, TStat7 or TStat5i only if the device's chip "
                              "is the 128K one (register 11 37 or more), which T5000 has not read.");

        v.ok = v.refusals.empty();
        return v;
    }
}
