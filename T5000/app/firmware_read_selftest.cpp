// Tests for the one request the Firmware page can send, a panel's settings,
// and for the list the page shows.
//
// The properties most worth guarding: the read is one request, for the
// settings, sent only where the points pages would send one; and what it
// keeps is kept only when the settings give the device's own serial.

#include "firmware_read.h"

#include "../testing/check.h"
#include "../testing/fake_transport.h"

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;
    namespace w = t5000::wire;

    constexpr uint32_t kSerial = 134341184;

    bool has(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    DeviceRecord scanned(ProductClassId product = ProductClassId::MiniPanelArm)
    {
        DeviceRecord d;
        d.serial_number        = kSerial;
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

    Bytes settings(uint32_t serial, uint8_t bootloader)
    {
        Bytes b(w::kSettingsWireSize, 0);
        for (int i = 0; i < 4; i++)
            b[w::settings_at::serial_number + i] = (uint8_t)(serial >> (8 * i));
        b[w::settings_at::bootloader_rev] = bootloader;
        return b;
    }

    // A panel that answers its settings with `block`, and refuses anything
    // else.
    void answers(FakeTransport& t, const Bytes& block)
    {
        t.respond = [block](const FakeTransport::Sent& s, size_t, FakeTransport& tr) {
            if (s.request.command == t5000::bacnet::ReadCommand::Settings)
                tr.reply(ack(s.request, s.invoke_id, block));
            else
                tr.reply(refusal(s.invoke_id));
        };
    }

    struct Bench
    {
        Registry registry;
        Handle   handle = kNoHandle;
        uint8_t  invoke = 1;

        explicit Bench(const DeviceRecord& d)
        {
            const int i = registry.add_or_merge(d);
            handle = registry.devices()[(size_t)i].handle;
        }

        const DeviceRecord& device() const
        {
            for (const auto& d : registry.devices())
                if (d.handle == handle)
                    return d;
            return registry.devices().front();
        }

        bool read(FakeTransport& t, std::string& message)
        {
            const PointsPlan plan = plan_firmware_read(device());
            if (!plan.can_read)
            {
                message = plan.reason;
                return false;
            }
            return read_bootloader(registry, device(), plan, t, instant(), invoke, message);
        }
    };

    void test_one_request_for_the_settings()
    {
        section("firmware read: one request, for the settings, and the bootloader's version kept");

        Bench b(scanned());
        FakeTransport t;
        answers(t, settings(kSerial, 62));
        std::string message;
        check(b.read(t, message), "read and kept");
        if (!require(t.sent.size() == 1, "one request"))
            return;
        check(t.sent[0].request.command == t5000::bacnet::ReadCommand::Settings, "  for the settings");
        check(t.sent[0].request.first == 0 && t.sent[0].request.last == 0, "  the one block of them");
        check(b.invoke == 2, "  on the next invoke id");
        check(b.device().bootloader_known && b.device().bootloader == 62, "the bootloader's version is kept");
        check(b.device().bootloader_from == "its settings, read by the Firmware page", "  as read by the Firmware page");
        check(message == "Its settings give bootloader version 62. Nothing else was read.", "  and said so");
    }

    void test_settings_that_are_not_its_own_keep_nothing()
    {
        section("firmware read: settings that do not give the device's serial keep nothing");

        {
            Bench b(scanned());
            FakeTransport t;
            answers(t, settings(0, 62));
            std::string message;
            check(!b.read(t, message), "serial 0, on a read the scan vouched for: nothing kept");
            check(!b.device().bootloader_known, "  at all");
            check(has(message, "gives no serial number in its settings") && has(message, "kept nothing"),
                  "  and said so");
            check_eq((long)t.sent.size(), 1, "  after the one request");
        }
        {
            Bench b(scanned());
            FakeTransport t;
            answers(t, settings(kSerial, 0));
            std::string message;
            check(!b.read(t, message), "a bootloader's version of 0: nothing kept");
            check(!b.device().bootloader_known, "  at all");
            check(has(message, "gives 0 for its bootloader's version"), "  and said so");
        }
        {
            Bench b(scanned());
            FakeTransport t;
            answers(t, settings(kSerial + 1, 62));
            std::string message;
            check(!b.read(t, message), "another serial: nothing kept");
            check(!b.device().bootloader_known, "  at all");
            check(has(message, "gives its serial number as"), "  and said so");
        }
        {
            Bench b(scanned());
            FakeTransport t;
            t.respond = [](const FakeTransport::Sent& s, size_t, FakeTransport& tr) {
                tr.reply(refusal(s.invoke_id));
            };
            std::string message;
            check(!b.read(t, message), "refused: nothing kept");
            check(!b.device().bootloader_known, "  at all");
            check(has(message, "did not give settings T5000 could use"), "  and said so");
            check_eq((long)t.sent.size(), 1, "  and nothing more was asked for");
        }
        {
            DeviceRecord saved = scanned();
            saved.answered_scan = 0;
            saved.provenance    = Provenance::Restored;
            Bench b(saved);
            FakeTransport t;
            answers(t, settings(0, 62));
            std::string message;
            check(!b.read(t, message), "a device from the saved list whose settings give no serial: nothing kept");
            check(has(message, "cannot confirm it is serial"), "  held to the stricter rule");
        }
    }

    void test_only_where_the_points_pages_would_read()
    {
        section("firmware read: nothing is sent where the points pages would send nothing");

        struct Case
        {
            const char*  what;
            DeviceRecord d;
            const char*  why;
        };
        std::vector<Case> cases;

        DeviceRecord tstat = scanned(ProductClassId::Tstat8);
        cases.push_back({ "a Tstat8, not a private-data product", tstat, "Modbus registers" });

        DeviceRecord serial = scanned();
        serial.connection.transport = Transport::ModbusRtu;
        cases.push_back({ "a device on a serial port", serial, "BACnet/IP only" });

        DeviceRecord sub = scanned();
        sub.parent_serial = 555;
        cases.push_back({ "a device on a controller's bus", sub, "RS485 bus of controller" });

        DeviceRecord typed = scanned();
        typed.provenance    = Provenance::ManuallyAdded;
        typed.answered_scan = 0;
        cases.push_back({ "a device added by hand and never found", typed, "added by hand" });

        DeviceRecord made = scanned();
        made.serial_number = kFirstVirtualSerial;
        made.provenance    = Provenance::Virtual;
        cases.push_back({ "a virtual device", made, "virtual device" });

        for (const Case& c : cases)
        {
            Bench b(c.d);
            FakeTransport t;
            answers(t, settings(c.d.serial_number, 62));
            std::string message;
            check(!b.read(t, message) && t.sent.empty(), c.what);
            check(has(message, c.why), "  and says why");
            check(!plan_firmware_read(c.d).can_read, "  the plan says so");
        }

        const PointsPlan ok = plan_firmware_read(scanned());
        check(ok.can_read, "a panel the scan found on BACnet/IP can be read");
        check(ok.identity == Identity::VouchedForByScan, "  vouched for by the scan");
    }

    void test_the_request()
    {
        section("firmware read: the request, read strictly");

        Handle h = kNoHandle;
        std::string message;
        check(read_firmware_read_request("{\"handle\":\"12\"}", h, message) && h == to_handle(12), "a handle as text");
        check(read_firmware_read_request("{\"handle\":12}", h, message) && h == to_handle(12), "  or as a number");
        check(!read_firmware_read_request("{\"handle\":\"0\"}", h, message), "0 is no device");
        check(!read_firmware_read_request("{\"handle\":\"-1\"}", h, message), "a negative one is refused");
        check(!read_firmware_read_request("{}", h, message), "none is refused");
        check(!read_firmware_read_request("{\"handle\":\"1\",\"x\":1}", h, message), "anything else with it is refused");
        check(!read_firmware_read_request("handle=1", h, message) && has(message, "JSON object"), "not JSON is refused");
    }

    void test_the_list()
    {
        section("firmware read: the list the page shows");

        Registry registry;
        registry.add_or_merge(scanned());
        DeviceRecord tstat = scanned(ProductClassId::Tstat8);
        tstat.serial_number = kSerial + 5;
        registry.add_or_merge(tstat);

        const std::string j = firmware_list_json(registry);
        check(has(j, "{\"devices\":[{"), "the devices");
        check(has(j, "\"serialNumber\":134341184") && has(j, "\"serialNumber\":134341189"), "  both of them");
        check(has(j, "\"canRead\":true,\"readWhy\":\"\""), "  the panel's settings can be read");
        check(has(j, "\"canRead\":false,\"readWhy\":\"") && has(j, "Modbus registers"), "  the Tstat8's cannot, and why");
        check(has(j, "\"largestFile\":16777216}"), "and the largest file the page may send");

        Registry empty;
        check(firmware_list_json(empty) == "{\"devices\":[],\"largestFile\":16777216}", "an empty list");
    }
}

int run_firmware_read_tests()
{
    test_one_request_for_the_settings();
    test_settings_that_are_not_its_own_keep_nothing();
    test_only_where_the_points_pages_would_read();
    test_the_request();
    test_the_list();
    return 0;
}
