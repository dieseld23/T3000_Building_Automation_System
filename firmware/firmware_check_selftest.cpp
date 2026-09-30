// Tests for whether a firmware file may go to a device: the route ISP checks
// it on, ISP's names and aliases on each, its bootloader rules, and where
// T5000 refuses what ISP would not.
//
// The property most worth guarding is that T5000 never says yes where ISP
// would say no, or would first flash a bootloader: T5000 does not update
// bootloaders, so a file that needs one is refused.

#include "firmware_check.h"

#include <string.h>

#include "../testing/check.h"

namespace
{
    using namespace t5000::firmware;
    using namespace t5000::testing;

    constexpr HexFormat kData    = HexFormat::Data;
    constexpr HexFormat kSegment = HexFormat::SegmentAddress;
    constexpr HexFormat kLinear  = HexFormat::LinearAddress;

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    bool any_contains(const std::vector<std::string>& texts, const std::string& part)
    {
        for (const std::string& t : texts)
            if (contains(t, part))
                return true;
        return false;
    }

    Header header_of(const char* company, const char* name, int version)
    {
        Header h;
        memcpy(&h.bytes[header_at::company], company, strnlen(company, 5));
        memcpy(&h.bytes[header_at::product_name], name, strnlen(name, 10));
        h.bytes[header_at::software_low]  = (uint8_t)(version & 0xFF);
        h.bytes[header_at::software_high] = (uint8_t)(version >> 8);
        return h;
    }

    // A file as read: a .hex of linear address records is for a 32K ARM
    // chip unless a test says otherwise.
    FirmwareFile file_of(const char* name, int version, HexFormat format, Path path = Path::Serial,
                         const char* company = "Temco", FileKind kind = FileKind::Hex)
    {
        FirmwareFile f;
        f.kind      = kind;
        f.format    = kind == FileKind::Bin ? HexFormat::Data : format;
        f.chip      = kind == FileKind::Hex && format == kLinear ? Chip::Arm32K : Chip::Asix;
        f.path      = path;
        f.header    = header_of(company, name, version);
        f.data_size = 1;
        return f;
    }

    FirmwareFile bin_of(const char* name, int version, Path path = Path::Serial, const char* company = "Temco")
    {
        return file_of(name, version, kData, path, company, FileKind::Bin);
    }

    DeviceFacts device_of(int product)
    {
        DeviceFacts d;
        d.product = product;
        return d;
    }

    DeviceFacts device_of(int product, int bootloader)
    {
        DeviceFacts d = device_of(product);
        d.bootloader_known = true;
        d.bootloader       = bootloader;
        d.bootloader_from  = "the panel's settings";
        return d;
    }

    void test_the_routes()
    {
        section("firmware: the route ISP checks a file on");

        check(route_of(file_of("T", 1, kData)) == Route::SerialData, "a .hex of data records on serial");
        check(route_of(file_of("T", 1, kLinear)) == Route::SerialExtended, "a .hex of linear address records on serial");
        check(route_of(bin_of("T", 1)) == Route::SerialExtended, "a .bin on serial, which ISP gives the linear type");
        check(route_of(file_of("T", 1, kSegment)) == Route::SerialNone, "a .hex of segment address records on serial");
        for (const HexFormat f : { kData, kSegment, kLinear })
        {
            check(route_of(file_of("T", 1, f, Path::Network)) == Route::Network, "any .hex on the network");
            check(route_of(file_of("T", 1, f, Path::Controller)) == Route::Controller, "any .hex through a controller");
        }
        check(route_of(bin_of("T", 1, Path::Network)) == Route::Network, "a .bin on the network");
    }

    void test_the_names()
    {
        section("firmware: the names ISP gives products and files");

        check_streq(firmware_name(74).c_str(), "MiniPanel", "74, the ARM MiniPanel");
        check_streq(firmware_name(35).c_str(), "MiniPanel", "35, the MiniPanel");
        check_streq(firmware_name(9).c_str(), "TStat8", "9, TStat8");
        check_streq(firmware_name(10).c_str(), "PID10", "10, TStat10, is PID10 to the extended route");
        check_streq(product_name(10).c_str(), "TStat10", "... and TStat10 to the data route");
        check_streq(product_name(9).c_str(), "TStat8", "the data route's other names are the same");
        check_streq(firmware_name(52).c_str(), "PM2.5", "52");
        check_streq(firmware_name(32).c_str(), "CO2 Net", "32, with its space");
        check_streq(firmware_name(216).c_str(), "CO2 NODE", "216");
        check_streq(firmware_name(59).c_str(), "PID59", "59, TStat9, is not named, so PID59");
        check_streq(product_name(59).c_str(), "PID59", "... on either route");
        check_streq(firmware_name(0).c_str(), "PID0", "0");
        check_streq(firmware_name(999).c_str(), "PID999", "999");

        check_streq(extended_file_name(header_of("TEMCO", "mini_arm", 1)).c_str(), "MINIPANEL",
                    "on the extended route, a file named mini_arm is for a MiniPanel");
        check_streq(extended_file_name(header_of("TEMCO", "Mini_ARM", 1)).c_str(), "MINIPANEL", "... in any case");
        check_streq(extended_file_name(header_of("TEMCO", " mini_arm", 1)).c_str(), "MINI_ARM",
                    "... but not with a space before it, which is trimmed after");
        check_streq(extended_file_name(header_of("TEMCO", "tstat8  ", 1)).c_str(), "TSTAT8", "names are raised and trimmed");
        check_streq(extended_file_name(header_of("TEMCO", "ABCDEFGHIJ", 'K' + 'L' * 256)).c_str(), "ABCDEFGHIJKL",
                    "a name filling its ten bytes runs on, and is compared so");

        check_streq(data_file_name(header_of("TEMCO", "mini_arm", 1)).c_str(), "MINI_ARM",
                    "on the data route, mini_arm is not made Minipanel");
        check_streq(data_file_name(header_of("TEMCO", "tstat8  ", 1)).c_str(), "TSTAT8  ", "... names are not trimmed");
        check_streq(data_file_name(header_of("TEMCO", "ABCDEFGHIJ", 'K' + 'L' * 256)).c_str(), "ABCDEFGHIJ",
                    "... and are at most ten bytes");
        Header gap = header_of("TEMCO", "TSTAT8", 1);
        gap.bytes[header_at::product_name + 8] = 'X';
        check_streq(data_file_name(gap).c_str(), "TSTAT8", "... up to the first 0");
    }

    void test_the_aliases()
    {
        section("firmware: the names ISP takes for one another");

        for (const auto match : { &extended_names_match, &data_names_match })
        {
            check(match("TStat8", "TSTAT8"), "the same name, in any case");
            check(!match("TStat8", "TSTAT7"), "another name is not");
            for (const char* d : { "TStat5E", "TStat5H", "TStat5G" })
                check(match(d, "TSTAT5LCD"), "TSTAT5LCD for a TStat5E, H or G");
            check(!match("TStat5A", "TSTAT5LCD"), "... not for a TStat5A");
            for (const char* d : { "TStat5A", "TStat5B", "TStat5C", "TStat5D", "TStat5F" })
                check(match(d, "TSTAT5LED"), "TSTAT5LED for a TStat5A, B, C, D or F");
            check(!match("TStat5E", "TSTAT5LED"), "... not for a TStat5E");
            for (const char* d : { "CO2NET", "CO2RS485", "HUMNET", "HUMRS485", "PM2.5", "PSNET" })
                check(match(d, "CO2ALL"), "CO2ALL for the CO2, humidity, pressure and PM2.5 sensors");
            check(!match("PSRS485", "CO2ALL"), "... but not PSRS485, which ISP leaves out");
            check(!match("CO2 NODE", "CO2ALL"), "... or the CO2 node");
            check(match("TStat5I", "TSTAT6"), "a TSTAT6 file for a TStat5I");
            check(!match("TStat6", "TSTAT5I"), "... not the other way");
            check(match("CM5", "CM5_ARM"), "a CM5_ARM file for a CM5");
            check(!match("CM5_ARM", "CM5"), "... not the other way");
            check(!match("MiniPanel", "MINI"), "on serial, MINI is not a MiniPanel");
        }
        check(data_names_match("CO2", "CO2 NET"), "on the data route, a CO2 NET file for a CO2");
        check(!extended_names_match("CO2", "CO2 NET"), "... which the extended route does not take");
        check(!data_names_match("CO2 NET", "CO2"), "... nor the other way");
    }

    void test_the_files_that_need_a_new_bootloader()
    {
        section("firmware: the files ISP marks as needing the new bootloader");

        struct Mark
        {
            const char* name;
            int         below;   // the highest version not marked
            bool        serial;
            bool        network;
        };
        const Mark marks[] = {
            { "MINI_ARM", 5999, true, true },
            { "CO2ALL", 58, true, true },
            { "TSTAT8", 100, true, false },
            { "PID10", 5108, true, false },
        };
        for (const Mark& m : marks)
        {
            for (const Path path : { Path::Serial, Path::Network, Path::Controller })
            {
                const bool marked = path == Path::Serial ? m.serial : path == Path::Network ? m.network : false;
                const std::string what = std::string(m.name) + " on " + to_string(path);
                check(!file_needs_new_bootloader(path, header_of("TEMCO", m.name, m.below)),
                      (what + ": not at the version below").c_str());
                check(file_needs_new_bootloader(path, header_of("TEMCO", m.name, m.below + 1)) == marked,
                      (what + ": " + (marked ? "marked" : "not marked") + " at the version").c_str());
                check(file_needs_new_bootloader(path, header_of("TEMCO", m.name, 0xFFFF)) == marked,
                      (what + ": " + (marked ? "marked" : "not marked") + " above it").c_str());
            }
        }
        check(file_needs_new_bootloader(Path::Serial, header_of("TEMCO", " mini_arm", 6000)),
              "the name is trimmed and raised");
        check(!file_needs_new_bootloader(Path::Serial, header_of("TEMCO", "MINI_ARM12", 6000)),
              "a name filling its ten bytes runs on into the version, and is not MINI_ARM");
        check(!file_needs_new_bootloader(Path::Serial, header_of("TEMCO", "MINIPANEL", 0xFFFF)),
              "MINIPANEL is not marked, only MINI_ARM");
        check(!file_needs_new_bootloader(Path::Serial, header_of("TEMCO", "TSTAT7", 0xFFFF)), "other names are not");
    }

    void test_the_bootloader_rules()
    {
        section("firmware: what ISP does about a device's bootloader");

        struct Rule
        {
            int         product;
            int         fine_from;   // the lowest version ISP leaves alone
            const char* what;
        };
        const Rule rules[] = {
            { 74, 62, "the ARM MiniPanel" },
            { 35, 62, "the MiniPanel" },
            { 10, 54, "TStat10" },
            { 210, 56, "CO2NET" },
            { 215, 56, "PSRS485" },
            { 52, 56, "PM2.5" },
        };
        for (const Rule& r : rules)
        {
            const bool minipanel = r.product == 74 || r.product == 35;
            check(bootloader_is_checked(r.product), (std::string(r.what) + " is checked").c_str());
            check(bootloader_call(r.product, Path::Network, r.fine_from, 0) == BootloaderCall::Fine,
                  (std::string(r.what) + ": fine at its version").c_str());
            check(bootloader_call(r.product, Path::Network, r.fine_from - 1, 0) == BootloaderCall::Update,
                  (std::string(r.what) + ": updated below it, on the network").c_str());
            check(bootloader_call(r.product, Path::Serial, r.fine_from - 1, 0) ==
                      (minipanel ? BootloaderCall::RefusedSerial : BootloaderCall::Update),
                  (std::string(r.what) + (minipanel ? ": refused on serial" : ": updated on serial")).c_str());
            check(bootloader_call(r.product, Path::Controller, r.fine_from - 1, 0) ==
                      (minipanel ? BootloaderCall::RefusedSerial : BootloaderCall::Update),
                  (std::string(r.what) + ": through a controller as on serial").c_str());
            check(bootloader_call(r.product, Path::Serial, 0, 0) != BootloaderCall::Fine,
                  (std::string(r.what) + ": a version of 0 is below it").c_str());
        }

        check(bootloader_call(9, Path::Serial, 48, 0) == BootloaderCall::Update, "TStat8 at 48 is updated");
        check(bootloader_call(9, Path::Serial, 49, 0) == BootloaderCall::Fine, "... at 49 is fine");
        check(bootloader_call(9, Path::Serial, 1, 0) == BootloaderCall::Update, "... at 1 is updated");
        check(bootloader_call(9, Path::Serial, 0, 0) == BootloaderCall::Fine, "... at 0, which ISP calls unknown, is not");

        check(bootloader_is_checked(59), "TStat9 is checked");
        check(bootloader_call(59, Path::Serial, 0, 0) == BootloaderCall::Fine, "... but has no version ISP wants");

        for (const int p : { 210, 211, 212, 213, 214, 215, 52 })
        {
            check(bootloader_call(p, Path::Network, 0, 59) == BootloaderCall::Fine,
                  "a CO2, humidity, pressure or PM2.5 sensor whose firmware is 59 or more is not checked");
            check(bootloader_call(p, Path::Network, 0, 58) == BootloaderCall::Update, "... one at 58 is");
        }
        for (const int p : { 216, 1, 6, 7, 50, 100, 0, 255 })
        {
            check(!bootloader_is_checked(p), "other products are not checked");
            check(bootloader_call(p, Path::Serial, 0, 0) == BootloaderCall::Fine, "... so are fine");
        }
    }

    void test_a_file_that_fits()
    {
        section("firmware: a file that fits its device");

        Verdict v = check_firmware(file_of("TSTAT8", 100, kLinear), device_of(9));
        check(v.ok, "a TSTAT8 file for a TStat8 is fine");
        check(v.route == Route::SerialExtended, "... on the extended route");
        check(v.refusals.empty(), "... with nothing refused");
        check_streq(v.device_name.c_str(), "TStat8", "the device's name");
        check_streq(v.file_name.c_str(), "TSTAT8", "the file's name");
        check_eq(v.version, 100, "the file's version");
        check(!v.needs_new_bootloader, "... not marked as needing the new bootloader");
        check(v.notes.empty(), "... with nothing to note");

        v = check_firmware(file_of("TSTAT8", 100, kData), device_of(9));
        check(v.ok, "... and as a .hex of data records");
        check(v.route == Route::SerialData, "... on the data route");

        v = check_firmware(file_of("mini_arm", 5999, kLinear, Path::Network), device_of(74));
        check(v.ok, "a mini_arm file for an ARM MiniPanel, on the network");
        check(v.route == Route::Network, "... on the network route");
        check(any_contains(v.notes, "bootloader reports"), "... noting the flash checks the bootloader's name");

        v = check_firmware(file_of("mini_arm", 5999, kLinear), device_of(74));
        check(v.ok, "... and on serial as linear address records");
        check(!check_firmware(file_of("mini_arm", 5999, kData), device_of(74)).ok,
              "... but not as data records, where mini_arm is not made Minipanel");

        check(check_firmware(file_of("CO2ALL", 1, kLinear), device_of(212)).ok, "a CO2ALL file for a HUMNET");

        v = check_firmware(file_of("MINI_ARM", 6000, kLinear), device_of(74, 62));
        check(v.ok, "a MINI_ARM file needing the new bootloader, for a device that has it");
        check(v.needs_new_bootloader, "... is marked");

        check(check_firmware(file_of("TSTAT7", 0xFFFF, kLinear), device_of(7)).ok,
              "a file ISP does not mark needs no bootloader version");
        check(check_firmware(file_of("TSTAT8", 101, kLinear), device_of(9, 0)).ok,
              "a marked TSTAT8 file for a TStat8 whose bootloader reads 0, which ISP leaves alone");
        check(check_firmware(bin_of("MINI_ARM", 6000), device_of(74, 62)).ok, "a .bin");

        check(check_firmware(file_of("TSTAT8", 1, kLinear, Path::Serial, "TEMCO"), device_of(9)).ok, "TEMCO in capitals");
        check(check_firmware(file_of("TSTAT8", 1, kLinear, Path::Serial, "xco2x"), device_of(9)).ok,
              "a .hex whose company holds CO2");
        check(check_firmware(bin_of("TSTAT8", 1, Path::Serial, "co2"), device_of(9)).ok, "a .bin whose company is CO2");
    }

    void test_the_routes_differ()
    {
        section("firmware: a file one route takes and the other does not");

        Verdict v = check_firmware(file_of("TStat10", 1, kData), device_of(10));
        check(v.ok, "a TStat10 file for a TStat10, as data records");
        check_streq(v.device_name.c_str(), "TStat10", "... whom the data route calls TStat10");
        check(!check_firmware(file_of("TStat10", 1, kLinear), device_of(10)).ok, "... not as linear address records");
        check(check_firmware(file_of("PID10", 1, kLinear), device_of(10)).ok, "a PID10 file, as linear address records");
        check(!check_firmware(file_of("PID10", 1, kData), device_of(10)).ok, "... not as data records");

        check(check_firmware(file_of("CO2 NET", 1, kData), device_of(33)).ok, "a CO2 NET file for a CO2, as data records");
        check(!check_firmware(file_of("CO2 NET", 1, kLinear), device_of(33)).ok, "... not as linear address records");

        check(check_firmware(file_of("TSTAT8 ", 1, kLinear), device_of(9)).ok,
              "a TSTAT8 file with a space after its name, as linear address records");
        v = check_firmware(file_of("TSTAT8 ", 1, kData), device_of(9));
        check(!v.ok, "... not as data records, which are not trimmed");
        check(any_contains(v.refusals, "The file is for TSTAT8 , and this device is a TSTAT8."), "... saying so");

        v = check_firmware(file_of("TSTAT8", 1, kSegment), device_of(9));
        check(!v.ok, "a .hex of segment address records on serial");
        check(v.route == Route::SerialNone, "... which ISP starts nothing for");
        check(any_contains(v.refusals, "segment address records"), "... saying so");
        check(check_firmware(file_of("TSTAT8", 1, kSegment, Path::Network), device_of(9)).ok,
              "... but on the network, it is checked as any other");
    }

    void test_the_chip_notes()
    {
        section("firmware: the chip size ISP checks for a TStat6, TStat7 or TStat5i");

        for (const int p : { 6, 7, 8 })
        {
            const char* name = p == 6 ? "TSTAT6" : p == 7 ? "TSTAT7" : "TSTAT5I";
            Verdict v = check_firmware(file_of(name, 1, kData), device_of(p));
            check(v.ok, "a data .hex for it is not refused");
            check(any_contains(v.notes, "the 64K one"), "... it notes ISP wants the 64K chip");

            FirmwareFile asix = file_of(name, 1, kLinear);
            asix.chip = Chip::Asix;
            v = check_firmware(asix, device_of(p));
            check(any_contains(v.notes, "the 128K one"), "a linear .hex not for an ARM chip notes the 128K chip");
            check(any_contains(check_firmware(bin_of(name, 1), device_of(p)).notes, "the 128K one"), "... so does a .bin");

            v = check_firmware(file_of(name, 1, kLinear), device_of(p));
            check(!any_contains(v.notes, "chip"), "a .hex for an ARM chip notes nothing: ISP does not check");
            v = check_firmware(file_of(name, 1, kData, Path::Network), device_of(p));
            check(!any_contains(v.notes, "chip"), "... nor does the network");
        }
        check(!any_contains(check_firmware(file_of("TSTAT8", 1, kData), device_of(9)).notes, "chip"),
              "other products note nothing");
    }

    void test_what_is_refused()
    {
        section("firmware: what is refused");

        for (const HexFormat f : { kData, kLinear })
        {
            Verdict v = check_firmware(file_of("TSTAT7", 1, f), device_of(9));
            check(!v.ok, "a TSTAT7 file for a TStat8");
            check(any_contains(v.refusals, "The file is for TSTAT7, and this device is a TSTAT8."), "... saying both");
        }

        check(!check_firmware(file_of("TSTAT7", 1, kLinear, Path::Network), device_of(9)).ok, "... on the network too");
        for (const char* any : { "HUMNET", "CO2NET", "PSNET" })
            check(!check_firmware(file_of(any, 1, kLinear, Path::Network), device_of(9)).ok,
                  "a HUMNET, CO2NET or PSNET file for a TStat8 on the network, which ISP takes");
        for (const int any : { 212, 210, 33, 214 })
            check(!check_firmware(file_of("TSTAT7", 1, kLinear, Path::Network), device_of(any)).ok,
                  "a TSTAT7 file for a HUMNET, CO2NET, CO2 or PSNET on the network, which ISP takes on a broadcast");
        check(check_firmware(file_of("CO2ALL", 1, kLinear, Path::Network), device_of(212)).ok,
              "a CO2ALL file for a HUMNET on the network, by the extended route's names");

        Verdict v = check_firmware(file_of("TSTAT8", 1, kLinear, Path::Serial, "Acme"), device_of(9));
        check(!v.ok, "a file whose company is not Temco's");
        check(any_contains(v.refusals, "\"Acme\""), "... naming it");
        check(!check_firmware(file_of("TSTAT8", 1, kData, Path::Serial, "Acme"), device_of(9)).ok, "... on the data route too");
        check(!check_firmware(bin_of("TSTAT8", 1, Path::Serial, "xco2x"), device_of(9)).ok,
              "a .bin whose company only holds CO2");
        check(!check_firmware(bin_of("TSTAT8", 1, Path::Serial, "Acme"), device_of(9)).ok,
              "a .bin whose company is not Temco's, which ISP takes");

        for (const int p : { 0, 255 })
        {
            for (const HexFormat f : { kData, kLinear })
            {
                v = check_firmware(file_of(p == 0 ? "PID0" : "PID255", 1, f), device_of(p));
                check(!v.ok, "a device reporting product 0 or 255, which ISP flashes without a check");
                check(any_contains(v.refusals, "says nothing about what it is"), "... saying why");
            }
        }

        FirmwareFile runs_on = file_of("TSTAT8", 1, kLinear);
        memset(runs_on.header.bytes, 'X', header_at::size);
        v = check_firmware(runs_on, device_of(9));
        check(!v.ok, "a header whose name does not end");
        check(any_contains(v.refusals, "read past it"), "... saying so");

        v = check_firmware(file_of("TSTAT8", 1, kData, Path::Controller), device_of(9));
        check(!v.ok, "a device behind a controller, not checked yet");
        check(any_contains(v.refusals, "behind a controller"), "... saying so");
    }

    void test_a_file_that_needs_a_new_bootloader()
    {
        section("firmware: a file that needs a newer bootloader is refused");

        Verdict v = check_firmware(file_of("MINI_ARM", 6000, kLinear), device_of(74));
        check(!v.ok, "a marked file for a device whose bootloader T5000 does not know");
        check(v.needs_new_bootloader, "... is marked");
        check(any_contains(v.refusals, "does not know this device's bootloader version"), "... saying so");
        check(any_contains(v.refusals, "does not update bootloaders"), "... and that T5000 will not update it");
        check(!any_contains(v.refusals, "ISP does not look"), "... not saying ISP does not look, since it does");

        v = check_firmware(file_of("MINI_ARM", 6000, kLinear), device_of(74, 61));
        check(!v.ok, "on serial, for a device whose bootloader is older");
        check(any_contains(v.refusals, "version 61 (the panel's settings)"), "... naming its version and where from");
        check(any_contains(v.refusals, "will not update one over a serial port"), "... and that ISP would refuse it");

        v = check_firmware(file_of("MINI_ARM", 6000, kLinear, Path::Network), device_of(74, 61));
        check(!v.ok, "on the network");
        check(any_contains(v.refusals, "ISP would update the bootloader first"), "... where ISP would update it");

        v = check_firmware(file_of("TSTAT8", 101, kData), device_of(9, 48));
        check(!v.ok, "a marked file on the data route, where ISP does not look at the bootloader");
        check(any_contains(v.refusals, "ISP does not look at the bootloader for a file like this one"), "... saying so");

        check(!check_firmware(file_of("CO2ALL", 59, kLinear), device_of(210, 55)).ok,
              "a marked CO2ALL file for a CO2NET whose bootloader is 55");

        DeviceFacts exempt = device_of(210, 55);
        exempt.firmware_low_known = true;
        exempt.firmware_low       = 59;
        check(check_firmware(file_of("CO2ALL", 59, kLinear), exempt).ok, "... but not when its firmware is 59 or more");
        exempt.firmware_low = 58;
        check(!check_firmware(file_of("CO2ALL", 59, kLinear), exempt).ok, "... and refused when it is 58");

        DeviceFacts unknown_but_exempt = device_of(210);
        unknown_but_exempt.firmware_low_known = true;
        unknown_but_exempt.firmware_low       = 59;
        check(check_firmware(file_of("CO2ALL", 59, kLinear), unknown_but_exempt).ok,
              "a sensor whose firmware is 59 or more needs no bootloader version");

        check(!check_firmware(file_of("TSTAT8", 101, kLinear), device_of(9, 48)).ok,
              "a marked TSTAT8 file for a TStat8 whose bootloader is 48");
        check(check_firmware(file_of("TSTAT8", 101, kLinear, Path::Network), device_of(9)).ok,
              "... but a TSTAT8 file is not marked on the network");

        check(check_firmware(file_of("TSTAT5LED", 0xFFFF, kLinear), device_of(2)).ok,
              "a product ISP does not check the bootloader of needs no version");
    }
}

int run_firmware_check_tests()
{
    test_the_routes();
    test_the_names();
    test_the_aliases();
    test_the_files_that_need_a_new_bootloader();
    test_the_bootloader_rules();
    test_a_file_that_fits();
    test_the_routes_differ();
    test_the_chip_notes();
    test_what_is_refused();
    test_a_file_that_needs_a_new_bootloader();
    return 0;
}
