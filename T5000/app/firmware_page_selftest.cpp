// Tests for the Firmware page's checks: which path a device's file would go
// on, what the check is told of the device, T5000's own reasons, the
// request's query, and the check of a file end to end.
//
// The property most worth guarding is that the page never says a file would
// be sent where ISP would refuse it, or where T5000 would: a device not
// reached this session, a virtual one, or one whose bootloader is too old or
// not known for a file that needs a newer one.

#include "firmware_page.h"

#include <string.h>

#include "../testing/check.h"

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;
    namespace fw = t5000::firmware;

    bool has(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    bool any_has(const std::vector<std::string>& texts, const std::string& part)
    {
        for (const std::string& t : texts)
            if (has(t, part))
                return true;
        return false;
    }

    // A device the scan found this session, on the network.
    DeviceRecord scanned(uint32_t serial, ProductClassId product = ProductClassId::MiniPanelArm)
    {
        DeviceRecord d;
        d.serial_number        = serial;
        d.product              = product;
        d.firmware             = 600;
        d.provenance           = Provenance::BacnetBroadcast;
        d.reached              = true;
        d.answered_scan        = 1;
        d.connection.transport = Transport::BacnetIp;
        d.connection.host      = "192.168.1.50";
        d.connection.udp_port  = 47808;
        d.address_note         = "192.168.1.50";
        return d;
    }

    // A .bin as ISP reads one: "ASIX", and the header at 0x100.
    std::string bin_file(const char* name, int version, const char* company = "TEMCO")
    {
        std::string b(0x400, '\x5A');
        memcpy(&b[0], "ASIX", 4);
        char* h = &b[0x100];
        memset(h, 0, 20);
        memcpy(h + fw::header_at::company, company, strnlen(company, 5));
        memcpy(h + fw::header_at::product_name, name, strnlen(name, 10));
        h[fw::header_at::software_low]  = (char)(version & 0xFF);
        h[fw::header_at::software_high] = (char)(version >> 8);
        return b;
    }

    FirmwareCheckRequest request_for(Handle handle, const std::string& name = "mini.bin")
    {
        FirmwareCheckRequest r;
        r.handle = handle;
        r.name   = name;
        return r;
    }

    // ------------------------------------------------------------ the path

    void test_the_path()
    {
        section("firmware page: the path a device's file would go on, as T3000 hands it to ISP");

        fw::Path p = fw::Path::Network;
        DeviceRecord d = scanned(1001);
        check(firmware_path(d, p) && p == fw::Path::Network, "BACnet/IP: on the network");
        d.connection.transport = Transport::ModbusTcp;
        check(firmware_path(d, p) && p == fw::Path::Network, "Modbus TCP: on the network");
        d.connection.transport = Transport::ModbusRtu;
        check(firmware_path(d, p) && p == fw::Path::Serial, "Modbus RTU: by its serial port");
        d.connection.transport = Transport::BacnetMstp;
        check(firmware_path(d, p) && p == fw::Path::Serial, "BACnet MS/TP: by its serial port, as T3000 does");

        d = scanned(1002);
        d.parent_serial = 555;
        check(firmware_path(d, p) && p == fw::Path::Controller, "on a controller's bus: through the controller");
        d.connection.transport = Transport::ModbusRtu;
        check(firmware_path(d, p) && p == fw::Path::Controller, "  whatever it is reached over");

        d = scanned(kFirstVirtualSerial);
        d.provenance = Provenance::Virtual;
        check(!firmware_path(d, p), "a virtual device has no path");

        check_streq(path_key(fw::Path::Network), "network", "the page's words: network");
        check_streq(path_key(fw::Path::Serial), "serial", "  serial");
        check_streq(path_key(fw::Path::Controller), "controller", "  controller");
        check(has(path_text(fw::Path::Network), "TFTP") && has(path_text(fw::Path::Serial), "Modbus RTU") &&
                  has(path_text(fw::Path::Controller), "Modbus TCP"),
              "  and in a sentence, each naming how ISP sends it");
    }

    // ------------------------------------------------------------ the facts

    void test_what_the_check_is_told()
    {
        section("firmware page: the check is told the product, and the bootloader only when read this session");

        DeviceRecord d = scanned(1101, ProductClassId::Tstat8);
        fw::DeviceFacts f = device_facts(d);
        check_eq(f.product, 9, "the product the device reports");
        check(!f.bootloader_known, "no bootloader read: not known");
        check(!f.firmware_low_known, "the firmware's low byte is never passed on");

        d.bootloader_known = true;
        d.bootloader       = 49;
        d.bootloader_from  = "its settings, read by Find";
        d.firmware         = 0x3B3B;
        f = device_facts(d);
        check(f.bootloader_known && f.bootloader == 49, "a bootloader read this session: its version");
        check(f.bootloader_from == "its settings, read by Find", "  and where it came from");
        check(!f.firmware_low_known, "  and still not the firmware's low byte, whatever the scan gave");
    }

    // ------------------------------------------------------------ T5000's own

    void test_t5000s_own_reasons()
    {
        section("firmware page: T5000's own reasons, whatever the file");

        DeviceRecord d = scanned(1201);
        check(reached_this_session(d), "answered a scan this session: reached");
        check(own_refusals(d).empty(), "  and nothing of T5000's own against it");

        d.answered_scan = 0;
        d.provenance    = Provenance::BacnetUnicast;
        check(reached_this_session(d), "found by Find this session: reached");
        check(own_refusals(d).empty(), "  and nothing against it");

        d.provenance = Provenance::Restored;
        check(!reached_this_session(d), "only from the saved list: not reached this session");
        check(own_refusals(d).size() == 1 && any_has(own_refusals(d), "since T5000 started"), "  and said so");

        d.provenance = Provenance::ManuallyAdded;
        check(!reached_this_session(d), "added by hand and never found: not reached");
        check(any_has(own_refusals(d), "added by hand"), "  and said so");

        d = scanned(kFirstVirtualSerial);
        d.provenance = Provenance::Virtual;
        const std::vector<std::string> v = own_refusals(d);
        check(v.size() == 1 && any_has(v, "virtual device") && any_has(v, "never flashed"),
              "a virtual device is never flashed");
    }

    void test_the_bootloader_state()
    {
        section("firmware page: whether a device is in its bootloader, only as a network scan said it");

        DeviceRecord d = scanned(1301);
        check_streq(bootloader_state_key(d), "unknown", "nothing has said: unknown");
        check(has(bootloader_state_text(d), "network scan"), "  and why");

        d.bootloader_state_known = true;
        check_streq(bootloader_state_key(d), "firmware", "said its firmware runs");
        d.in_bootloader = true;
        check_streq(bootloader_state_key(d), "bootloader", "said it is in its bootloader");
        check(has(bootloader_state_text(d), "not reads"), "  which answers the scan but not reads");

        DeviceRecord s = scanned(1302);
        s.connection.transport = Transport::ModbusRtu;
        check(has(bootloader_state_text(s), "serial scan does not say"), "a serial device: a serial scan does not say");
    }

    // ------------------------------------------------------------ the list

    void test_a_device_as_listed()
    {
        section("firmware page: a device as the page lists it");

        DeviceRecord d = scanned(1401);
        d.handle           = to_handle(7);
        d.placement.name   = "Boiler \"A\"";
        d.bootloader_known = true;
        d.bootloader       = 62;
        d.bootloader_from  = "its settings, read by the Inputs page";
        const std::string j = firmware_device_json(d, true, "ignored");

        check(has(j, "\"handle\":\"7\""), "its handle, as text");
        check(has(j, "\"serialNumber\":1401"), "its serial");
        check(has(j, "\"name\":\"Boiler \\\"A\\\"\""), "the name given it, escaped");
        check(has(j, "\"productId\":74"), "its product");
        check(has(j, "\"path\":\"network\""), "its path");
        check(has(j, "\"known\":true,\"version\":62,\"from\":\"its settings, read by the Inputs page\""),
              "its bootloader, and where it came from");
        check(has(j, "\"canRead\":true,\"readWhy\":\"\""), "whether its settings may be read, with no reason when so");
        check(has(j, "\"own\":[]"), "nothing of T5000's own against it");
        check(has(j, "\"state\":\"unknown\""), "not said whether it is in its bootloader");

        DeviceRecord v = scanned(kFirstVirtualSerial);
        v.provenance = Provenance::Virtual;
        const std::string vj = firmware_device_json(v, false, "There is no device to read.");
        check(has(vj, "\"path\":\"\""), "a virtual device: no path");
        check(has(vj, "\"virtual\":true"), "  said to be virtual");
        check(has(vj, "\"canRead\":false,\"readWhy\":\"There is no device to read.\""), "  not read, and why");
        check(has(vj, "\"known\":false,\"version\":0,\"from\":\"\""), "  no bootloader");
        check(has(vj, "never flashed"), "  never flashed");
    }

    // ------------------------------------------------------------ the query

    void test_percent_decoding()
    {
        section("firmware page: a file's name, percent-decoded strictly");

        std::string out;
        check(percent_decode("a%20b.hex", out) && out == "a b.hex", "%20 is a space");
        check(percent_decode("a+b.hex", out) && out == "a+b.hex", "a + stays a +");
        check(percent_decode("%2B%25%26%23%3D", out) && out == "+%&#=", "+, %, &, # and =");
        check(percent_decode("%C5%81%c3%b3d%C5%BA.HEX", out) && out == "\xC5\x81\xC3\xB3" "d" "\xC5\xBA.HEX",
              "UTF-8 bytes, in either case of hex digit");
        check(percent_decode("", out) && out.empty(), "nothing is nothing");
        check(!percent_decode("%", out), "a % alone is refused");
        check(!percent_decode("a%2", out), "a % with one digit is refused");
        check(!percent_decode("%G0.hex", out), "a % without hex digits is refused");
        check(!percent_decode("%0G.hex", out), "  either of them");
    }

    void test_the_query()
    {
        section("firmware page: the check's query, read strictly");

        FirmwareCheckRequest r;
        std::string message;
        check(read_firmware_check_request("handle=3&name=T3-BB%20v2.bin", r, message), "handle and name");
        check(r.handle == to_handle(3) && r.name == "T3-BB v2.bin", "  read");
        check(read_firmware_check_request("name=a%26b.hex&handle=12", r, message), "in either order");
        check(r.handle == to_handle(12) && r.name == "a&b.hex", "  an & in the name stays in it");

        const char* refused[] = {
            "",
            "handle=3",
            "name=a.hex",
            "handle=3&name=a.hex&x=1",
            "handle=3&handle=4&name=a.hex",
            "handle=3&name=a.hex&name=b.hex",
            "handle=0&name=a.hex",
            "handle=-1&name=a.hex",
            "handle=3x&name=a.hex",
            "handle=&name=a.hex",
            "handle=3&name=",
            "handle=3&name=a%0A.hex",
            "handle=3&name=a%7F.hex",
            "handle=3&name=a%2.hex",
            "handle=3&name",
            "handle=3&&name=a.hex",
        };
        for (const char* q : refused)
        {
            FirmwareCheckRequest kept;
            kept.handle = to_handle(99);
            const bool ok = read_firmware_check_request(q, kept, message);
            check(!ok && !message.empty() && kept.handle == to_handle(99), q);
        }

        const std::string longest(255, 'a');
        check(read_firmware_check_request("handle=3&name=" + longest, r, message), "a name of 255 bytes");
        check(!read_firmware_check_request("handle=3&name=" + longest + "a", r, message), "  but not 256");
    }

    // ------------------------------------------------------------ a check

    struct Bench
    {
        Registry registry;
        Handle   handle = kNoHandle;

        explicit Bench(const DeviceRecord& d)
        {
            const int i = registry.add_or_merge(d);
            handle = registry.devices()[(size_t)i].handle;
        }

        std::string checked(const std::string& file, const std::string& name = "mini.bin")
        {
            return check_firmware_json(registry, request_for(handle, name), file);
        }
    };

    void test_a_file_that_fits()
    {
        section("firmware page: a file ISP takes, for a device T5000 would send it to");

        Bench b(scanned(1501));
        b.registry.note_bootloader(b.handle, 1501, 62, "its settings, read by Find");

        const std::string j = b.checked(bin_file("mini_arm", 6100));
        check(has(j, "\"ok\":true"), "ok");
        check(has(j, "Nothing was sent"), "  and nothing was sent");
        check(has(j, "\"path\":\"network\""), "on the network");
        check(has(j, "\"read\":true,\"kind\":\"bin\""), "the file read, as a .bin");
        check(has(j, "\"headerAt\":256"), "  its header at 0x100");
        check(has(j, "\"company\":\"TEMCO\""), "  its company");
        check(has(j, "\"productName\":\"mini_arm\""), "  its product name as the header gives it");
        check(has(j, "\"version\":6100"), "  its version");
        check(has(j, "\"route\":\"network\""), "the route ISP checks it on");
        check(has(j, "\"needsNewBootloader\":true"), "marked as needing the newer bootloader");
        check(has(j, "\"refusals\":[]"), "no reason of ISP's against it");
        check(has(j, "\"own\":[]"), "none of T5000's own");
    }

    void test_a_file_the_bootloader_refuses()
    {
        section("firmware page: a file that needs a newer bootloader than the device has, or one not known");

        Bench unknown(scanned(1601));
        const std::string u = unknown.checked(bin_file("mini_arm", 6100));
        check(has(u, "\"ok\":false"), "the bootloader not read: refused");
        check(has(u, "does not know this device's bootloader version"), "  saying T5000 does not know it");

        Bench old(scanned(1602));
        old.registry.note_bootloader(old.handle, 1602, 61, "its settings, read by the Firmware page");
        const std::string o = old.checked(bin_file("mini_arm", 6100));
        check(has(o, "\"ok\":false"), "a bootloader older than 62 on the network: refused");
        check(has(o, "version 61 (its settings, read by the Firmware page)"), "  naming the version and where it came from");
        check(has(o, "ISP would update the bootloader first"), "  and what ISP would do");

        Bench unmarked(scanned(1603));
        const std::string m = unmarked.checked(bin_file("mini_arm", 5900));
        check(has(m, "\"ok\":true"), "a file not marked needs no bootloader known");
        check(has(m, "\"needsNewBootloader\":false"), "  and says it is not marked");
    }

    void test_a_file_for_another_product()
    {
        section("firmware page: a file for another product is refused, as ISP refuses it");

        Bench b(scanned(1701, ProductClassId::Tstat8));
        const std::string j = b.checked(bin_file("mini_arm", 5900));
        check(has(j, "\"ok\":false"), "refused");
        check(has(j, "\"verdict\":{\"ok\":false"), "  by the check of the file");
        check(has(j, "this device is a"), "  saying what each is for");
        check(has(j, "\"own\":[]"), "  and not for a reason of T5000's own");
        check(has(j, "T5000 would not send this file to this device"), "the message says so");
    }

    void test_a_file_isp_would_not_read()
    {
        section("firmware page: a file ISP would not read is said so, and nothing is checked");

        Bench b(scanned(1801));
        const std::string j = b.checked("hello", "notes.txt");
        check(has(j, "\"ok\":false"), "refused");
        check(has(j, "A firmware file is a .hex or a .bin."), "  with the reader's reason");
        check(has(j, "Nothing was sent"), "  and nothing sent");
        check(has(j, "\"read\":false"), "the file not read");
        check(has(j, "\"verdict\":null"), "  so no verdict");

        const std::string k = b.checked("not a bin", "a.bin");
        check(has(k, "neither starts with ASIX nor says Temco"), "a .bin that is not one");

        const std::string e = b.checked("", "a.bin");
        check(has(e, "\"ok\":false") && has(e, "\"size\":0"), "an empty file");
    }

    void test_t5000s_own_reasons_refuse_a_file_isp_takes()
    {
        section("firmware page: a file ISP takes, for a device T5000 would not send it to");

        DeviceRecord d = scanned(1901);
        d.answered_scan = 0;
        d.provenance    = Provenance::Restored;
        Bench b(d);
        const std::string j = b.checked(bin_file("mini_arm", 5900));
        check(has(j, "\"ok\":false"), "refused");
        check(has(j, "\"verdict\":{\"ok\":true"), "  though ISP would take it");
        check(has(j, "since T5000 started"), "  for T5000's own reason");
        check(has(j, "but T5000 would not send it yet"), "the message says both");
    }

    void test_a_virtual_or_a_gone_device()
    {
        section("firmware page: a virtual device, and a device gone from the list");

        DeviceRecord v = scanned(kFirstVirtualSerial);
        v.provenance = Provenance::Virtual;
        Bench b(v);
        const std::string j = b.checked(bin_file("mini_arm", 5900));
        check(has(j, "\"ok\":false"), "a virtual device: refused");
        check(has(j, "never flashed, so the file was not read"), "  without reading the file");
        check(!has(j, "\"verdict\""), "  and no verdict");

        Registry empty;
        const std::string g = check_firmware_json(empty, request_for(to_handle(5)), bin_file("mini_arm", 5900));
        check(g == "{\"ok\":false,\"message\":\"That device is no longer in the list. Nothing was sent.\"}",
              "a device gone from the list: said so");
    }

    void test_the_path_decides_the_route()
    {
        section("firmware page: the device's path decides how the file is read and checked");

        DeviceRecord s = scanned(2001);
        s.connection.transport   = Transport::ModbusRtu;
        s.connection.serial_port = "\\\\.\\CNCA0";
        Bench serial(s);
        const std::string j = serial.checked(bin_file("mini_arm", 5900));
        check(has(j, "\"path\":\"serial\""), "a serial device: on its serial port");
        check(has(j, "\"route\":\"serial, a .hex of linear address records or a .bin\""),
              "  a .bin checked on the extended route");

        DeviceRecord c = scanned(2002);
        c.parent_serial = 777;
        Bench controller(c);
        const std::string k = controller.checked(bin_file("mini_arm", 5900));
        check(has(k, "\"path\":\"controller\""), "a device on a controller's bus: through it");
        check(has(k, "\"ok\":false") && has(k, "\"read\":false"), "  where ISP reads no .bin");
    }
}

int run_firmware_page_tests()
{
    test_the_path();
    test_what_the_check_is_told();
    test_t5000s_own_reasons();
    test_the_bootloader_state();
    test_a_device_as_listed();
    test_percent_decoding();
    test_the_query();
    test_a_file_that_fits();
    test_a_file_the_bootloader_refuses();
    test_a_file_for_another_product();
    test_a_file_isp_would_not_read();
    test_t5000s_own_reasons_refuse_a_file_isp_takes();
    test_a_virtual_or_a_gone_device();
    test_the_path_decides_the_route();
    return 0;
}
