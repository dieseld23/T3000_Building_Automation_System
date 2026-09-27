// Tests for Find: looking for one device in the list at an address the
// operator gives.
//
// The properties most worth guarding are what is sent and what is believed.
// Nothing is sent unless every check before it passes, and then one read of
// the settings, no more. A device is found only when those settings give its
// serial; anything else - another serial, none, a refusal, silence - leaves
// the list and the file exactly as they were. And a device that is found is
// still held to the stricter rule on every later read.

#include "find_device.h"

#include "device_list.h"
#include "inputs_plan.h"
#include "inputs_read.h"
#include "offline_inputs.h"
#include "outputs_plan.h"
#include "scan_json.h"
#include "variables_plan.h"
#include "../store/sqlite.h"
#include "../testing/check.h"
#include "../testing/fake_transport.h"
#include "../testing/temp_file.h"
#include "../wire/panel.h"

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;
    namespace bacnet  = t5000::bacnet;
    namespace store   = t5000::store;
    namespace offline = t5000::offline;
    namespace w       = t5000::wire;

    constexpr int64_t kNow = 1790000000;   // 2026-09-21, give or take

    const std::vector<t5000::net::Interface> kNoNetworks;

    bool has(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    // A panel's settings, as the Str_Setting_Info it sends.
    struct Settings
    {
        uint32_t    serial         = 920001;
        uint8_t     mini_type_byte = 0x80 | 5;   // APM chip bits over panel type 5
        uint8_t     modbus_id      = 7;
        uint32_t    instance       = 123456;
        const char* name           = "AHU 2   ";

        Bytes block() const
        {
            Bytes b(w::kSettingsWireSize, 0);
            b[w::settings_at::mini_type]     = mini_type_byte;
            b[w::settings_at::firmware_main] = 63;
            b[w::settings_at::firmware_sub]  = 7;
            memcpy(&b[w::settings_at::panel_name], name, strlen(name));
            for (int i = 0; i < 4; i++)
            {
                b[w::settings_at::serial_number + i]   = (uint8_t)(serial >> (8 * i));
                b[w::settings_at::object_instance + i] = (uint8_t)(instance >> (8 * i));
            }
            b[w::settings_at::modbus_id] = modbus_id;
            return b;
        }
    };

    // A panel that answers every request with these settings, from `ip`.
    void answers(FakeTransport& t, const Settings& s, uint32_t ip = kDeviceIp)
    {
        const Bytes block = s.block();
        t.respond = [block, ip](const FakeTransport::Sent& sent, size_t, FakeTransport& tr) {
            tr.reply(ack(sent.request, sent.invoke_id, block), ip);
        };
    }

    void refuses(FakeTransport& t)
    {
        t.respond = [](const FakeTransport::Sent& sent, size_t, FakeTransport& tr) {
            tr.reply(refusal(sent.invoke_id));
        };
    }

    DeviceRecord typed_in(uint32_t serial, ProductClassId product = ProductClassId::Esp32T3Series)
    {
        DeviceRecord d;
        d.serial_number = serial;
        d.product       = product;
        d.provenance    = Provenance::ManuallyAdded;
        return d;
    }

    DeviceRecord restored(uint32_t serial, const std::string& host)
    {
        DeviceRecord d;
        d.serial_number        = serial;
        d.product              = ProductClassId::Esp32T3Series;
        d.provenance           = Provenance::Restored;
        d.reached              = true;
        d.connection.host      = host;
        d.connection.udp_port  = 47808;
        d.address_note         = host;
        d.answered_from        = host;
        d.reported_ip          = "10.9.9.9";
        d.modbus_id_reported   = 3;
        d.first_seen           = kNow - 86400;
        d.last_seen            = kNow - 3600;
        return d;
    }

    // A saved list in memory, with devices added as the Add dialog adds them.
    struct Bench
    {
        Registry        registry;
        store::DeviceDb db;
        StoreStatus     status;
        ScanSummary     summary;
        uint8_t         invoke = 1;

        bool open()
        {
            std::string error;
            if (!db.open(":memory:", error))
            {
                printf("  (could not open a database in memory: %s)\n", error.c_str());
                return false;
            }
            status.saving = true;
            status.path   = ":memory:";
            return true;
        }

        Handle add(uint32_t serial, ProductClassId product, int mini_type, const std::string& name)
        {
            HandAdded device;
            device.serial         = serial;
            device.product        = product;
            device.mini_type      = mini_type;
            device.placement.name = name;
            Handle handle = kNoHandle;
            std::string message;
            if (!add_device(registry, db, device, status, handle, message))
                printf("  (could not add %u: %s)\n", serial, message.c_str());
            return handle;
        }

        const DeviceRecord* get(Handle handle) const
        {
            for (const auto& d : registry.devices())
                if (d.handle == handle)
                    return &d;
            return nullptr;
        }

        // Find, as the route carries it out, over a scripted transport.
        bool find(Handle handle, const std::string& host, int port, FakeTransport& t, std::string& message)
        {
            FindRequest request;
            request.handle = handle;
            request.host   = host;
            request.port   = port;

            DeviceRecord d;
            bacnet::Endpoint at;
            if (!plan_find(registry, request, kNoNetworks, d, at, message))
                return false;
            return find_device(registry, db, d, at, t, instant(), invoke, kNow, summary, message);
        }

        std::vector<DeviceRecord> saved()
        {
            std::vector<DeviceRecord> out;
            std::string error;
            check(db.load(out, error), "the saved list loads");
            return out;
        }
    };

    // ------------------------------------------------------------ requests

    void test_the_request_is_read_strictly()
    {
        section("a Find request is read strictly");

        FindRequest r;
        std::string message;
        check(read_find_request("{\"handle\":\"12\",\"host\":\"192.168.1.50\",\"port\":\"47900\"}", r, message),
              "a whole request is read");
        check(r.handle == to_handle(12) && r.host == "192.168.1.50" && r.port == 47900, "  handle, host and port");

        check(read_find_request("{\"handle\":\"12\",\"host\":\"10.0.0.1\",\"port\":47901}", r, message) &&
                  r.port == 47901,
              "the port as a JSON number");
        check(read_find_request("{\"handle\":\"12\",\"host\":\"10.0.0.1\"}", r, message) && r.port == 47808,
              "no port is 47808");
        check(read_find_request("{\"handle\":\"12\",\"host\":\"10.0.0.1\",\"port\":\" \"}", r, message) &&
                  r.port == 47808,
              "an empty port is 47808");
        check(read_find_request("{\"handle\":\"12\",\"host\":\"10.0.0.1\",\"port\":\"1\"}", r, message) && r.port == 1,
              "port 1 is a port");
        check(read_find_request("{\"handle\":\"12\",\"host\":\"10.0.0.1\",\"port\":\"65535\"}", r, message) &&
                  r.port == 65535,
              "and so is 65535");

        const char* bad_ports[] = { "\"0\"", "\"65536\"", "\"abc\"", "\"-1\"", "\"4.5\"", "\"47808x\"" };
        for (const char* p : bad_ports)
        {
            const std::string body = std::string("{\"handle\":\"12\",\"host\":\"10.0.0.1\",\"port\":") + p + "}";
            message.clear();
            check(!read_find_request(body, r, message) && has(message, "from 1 to 65535"), p);
        }

        check(!read_find_request("{\"host\":\"10.0.0.1\"}", r, message) && has(message, "handle"), "no handle");
        check(!read_find_request("{\"handle\":\"0\",\"host\":\"10.0.0.1\"}", r, message), "handle 0");
        check(!read_find_request("{\"handle\":\"x\",\"host\":\"10.0.0.1\"}", r, message), "a handle that is not a number");
        check(!read_find_request("{\"handle\":\"12\"}", r, message) && has(message, "host"), "no host");
        check(!read_find_request("{\"handle\":\"12\",\"host\":10}", r, message) && has(message, "host"),
              "a host that is not a string");
        check(!read_find_request("{\"handle\":\"12\",", r, message) && has(message, "could not be read"),
              "a body that is not an object");
    }

    void test_an_address_must_be_one_device()
    {
        section("an address must be one device's, as four numbers");

        bacnet::Endpoint at;
        std::string message;
        check(find_endpoint("192.168.1.50", 47808, kNoNetworks, at, message) && at.ip == kDeviceIp && at.port == 47808,
              "192.168.1.50:47808");
        check(find_endpoint("  10.1.2.3 ", 47900, kNoNetworks, at, message) && at.text() == "10.1.2.3:47900", "trimmed");
        check(find_endpoint("127.0.0.1", 47900, kNoNetworks, at, message) && at.text() == "127.0.0.1:47900",
              "loopback, where the synthetic panels are");
        check(find_endpoint("0.0.0.1", 47808, kNoNetworks, at, message), "a number in every part, zero included");

        const char* not_ipv4[] = {
            "192.168.1", "192.168.1.50.1", "192.168.1.256", "192.168.01.50", "010.1.1.1", "1234.1.1.1",
            "a.b.c.d", "controller.local", "1.2.3.4:47808", "+1.2.3.4", "1..2.3", "1.2.3.", ".1.2.3",
            "1.2.3.4 5", "1,2,3,4", "::1",
            "4294967297.0.0.1",   // a part that would wrap round to 1 if its digits were not limited
        };
        for (const char* h : not_ipv4)
        {
            bacnet::Endpoint untouched = at;
            message.clear();
            check(!find_endpoint(h, 47808, kNoNetworks, at, message) && has(message, "is not an IPv4 address") &&
                      at == untouched,
                  h);
        }

        check(!find_endpoint("", 47808, kNoNetworks, at, message) && has(message, "Enter the address"), "nothing typed");
        check(!find_endpoint("   ", 47808, kNoNetworks, at, message) && has(message, "Enter the address"), "only spaces");

        check(!find_endpoint("0.0.0.0", 47808, kNoNetworks, at, message) && has(message, "not a device's address"), "0.0.0.0");
        check(!find_endpoint("255.255.255.255", 47808, kNoNetworks, at, message) && has(message, "broadcast address"),
              "the broadcast address");
        check(!find_endpoint("224.0.0.1", 47808, kNoNetworks, at, message) && has(message, "multicast"), "224.0.0.1");
        check(!find_endpoint("239.255.255.255", 47808, kNoNetworks, at, message) && has(message, "multicast"), "239.255.255.255");
        check(!find_endpoint("240.0.0.1", 47808, kNoNetworks, at, message) && has(message, "reserved"), "240.0.0.1");
        check(!find_endpoint("254.255.255.255", 47808, kNoNetworks, at, message) && has(message, "reserved"), "254.255.255.255");
        check(find_endpoint("223.255.255.254", 47808, kNoNetworks, at, message), "223.255.255.254 is one device's");

        check(!find_endpoint("10.0.0.1", 0, kNoNetworks, at, message) && has(message, "from 1 to 65535"), "port 0");
        check(!find_endpoint("10.0.0.1", 65536, kNoNetworks, at, message) && has(message, "from 1 to 65535"), "port 65536");
    }

    t5000::net::Interface network(const char* ip, int prefix_length, const char* name)
    {
        t5000::net::Interface i;
        i.ip            = ip;
        i.prefix_length = prefix_length;
        i.name          = name;
        return i;
    }

    void test_a_local_network_address_is_refused()
    {
        section("the broadcast and network addresses of this computer's networks are refused");

        const std::vector<t5000::net::Interface> local = {
            network("192.168.1.23", 24, "Ethernet"),
            network("10.20.0.5", 16, "VPN"),
            network("127.0.0.1", 8, "Loopback"),
            network("172.16.5.9", 32, "Host only"),
            network("172.17.5.9", 31, "Point to point"),
            network("172.18.5.9", 0, "No prefix"),
            network("not an address", 24, "Odd"),
        };

        bacnet::Endpoint at;
        std::string message;
        check(!find_endpoint("192.168.1.255", 47808, local, at, message) &&
                  has(message, "192.168.1.255 is the broadcast address of 192.168.1.0/24, the network of this "
                               "computer's Ethernet"),
              "a /24's broadcast address, named");
        check(!find_endpoint("192.168.1.0", 47808, local, at, message) &&
                  has(message, "192.168.1.0 is the address of the network 192.168.1.0/24"),
              "  and its network address");
        check(find_endpoint("192.168.1.254", 47808, local, at, message), "  but not a device on it");
        check(find_endpoint("192.168.1.1", 47808, local, at, message), "  at either end");
        check(!find_endpoint("10.20.255.255", 47808, local, at, message) && has(message, "10.20.0.0/16"),
              "a /16's broadcast address");
        check(!find_endpoint("10.20.0.0", 47808, local, at, message), "  and its network address");
        check(find_endpoint("10.20.1.255", 47808, local, at, message), "  but not a .255 inside it");
        check(find_endpoint("10.20.1.0", 47808, local, at, message), "  or a .0");
        check(!find_endpoint("127.255.255.255", 47808, local, at, message) && has(message, "Loopback"),
              "loopback's broadcast address");
        check(find_endpoint("127.0.0.1", 47900, local, at, message), "  but not loopback itself");
        check(find_endpoint("192.168.2.255", 47808, local, at, message),
              "another network's broadcast address cannot be told from a device's");
        check(find_endpoint("172.16.5.9", 47808, local, at, message) &&
                  find_endpoint("172.17.5.8", 47808, local, at, message) &&
                  find_endpoint("172.17.5.9", 47808, local, at, message),
              "a /32 and a /31 have neither");
        check(find_endpoint("172.18.255.255", 47808, local, at, message) &&
                  find_endpoint("172.18.5.255", 47808, local, at, message),
              "and a network with no prefix given is not guessed at");

        Bench b;
        if (!b.open())
            return;
        FindRequest r;
        r.handle = b.add(920001, ProductClassId::Esp32T3Series, 0, "");
        r.host   = "192.168.1.255";
        DeviceRecord d;
        check(!plan_find(b.registry, r, local, d, at, message) && has(message, "broadcast address of 192.168.1.0/24") &&
                  has(message, "Nothing was sent"),
              "the plan refuses it, and sends nothing");
    }

    // ------------------------------------------------------------ who

    void test_find_applies_only_where_nothing_vouches()
    {
        section("Find is offered for a device with a serial that no scan this session vouches for");

        check(why_not_findable(typed_in(920001)).empty(), "an entry added by hand");
        check(why_not_findable(typed_in(920001, ProductClassId::Cm5)).empty(), "a CM5");
        check(why_not_findable(typed_in(920001, ProductClassId::Tstat10)).empty(), "a TSTAT10");
        check(why_not_findable(restored(920001, "10.0.0.5")).empty(), "a device from the saved list");

        DeviceRecord found = restored(920001, "10.0.0.5");
        found.provenance = Provenance::BacnetUnicast;
        check(why_not_findable(found).empty(), "one found by Find before");

        DeviceRecord scanned = restored(920001, "10.0.0.5");
        scanned.provenance    = Provenance::BacnetBroadcast;
        scanned.answered_scan = 1;
        check(has(why_not_findable(scanned), "answered a scan since T5000 started"), "not one a scan found");

        check(has(why_not_findable(typed_in(0)), "no serial number"), "not one with serial 0");
        check(has(why_not_findable(typed_in(0xFFFFFFFFu)), "no serial number"), "nor all-FF");

        DeviceRecord behind = restored(920001, "10.0.0.5");
        behind.parent_serial = 700001;
        check(has(why_not_findable(behind), "RS485 bus of controller 700001"), "not one on a controller's bus");

        check(has(why_not_findable(typed_in(920001, ProductClassId::Tstat8)), "is not one of them"),
              "not a product whose settings T5000 does not read");
        check(has(why_not_findable(typed_in(920001, ProductClassId::Unknown)), "is not one of them"),
              "nor one it does not know");
    }

    void test_the_plan_sends_nothing_when_it_refuses()
    {
        section("nothing is sent unless the device, Find and the address all pass");

        Bench b;
        if (!b.open())
            return;
        const Handle h = b.add(920001, ProductClassId::Esp32T3Series, 0, "Boiler AHU");

        FindRequest r;
        r.handle = h;
        r.host   = "192.168.1.50";
        r.port   = 47900;

        DeviceRecord d;
        bacnet::Endpoint at;
        std::string message;
        check(plan_find(b.registry, r, kNoNetworks, d, at, message), "a device added by hand, at an address");
        check(d.handle == h && d.serial_number == 920001, "  the device is the entry");
        check(at.ip == kDeviceIp && at.port == 47900, "  and the endpoint the address and port");

        FindRequest gone = r;
        gone.handle = to_handle(999);
        check(!plan_find(b.registry, gone, kNoNetworks, d, at, message) && has(message, "no longer in the list") &&
                  has(message, "Nothing was sent"),
              "a handle for nothing");

        FindRequest none = r;
        none.handle = kNoHandle;
        check(!plan_find(b.registry, none, kNoNetworks, d, at, message) && has(message, "no longer in the list"), "handle 0");

        FindRequest typo = r;
        typo.host = "192.168.1.500";
        check(!plan_find(b.registry, typo, kNoNetworks, d, at, message) && has(message, "is not an IPv4 address") &&
                  has(message, "Nothing was sent"),
              "an address that is not one");

        FindRequest broadcast = r;
        broadcast.host = "255.255.255.255";
        check(!plan_find(b.registry, broadcast, kNoNetworks, d, at, message) && has(message, "broadcast") &&
                  has(message, "Nothing was sent"),
              "the broadcast address");

        // A device the scan found: its address is the scan's.
        DeviceRecord scanned = restored(920002, "10.0.0.6");
        scanned.provenance    = Provenance::BacnetBroadcast;
        scanned.answered_scan = b.registry.begin_scan();
        const int i = b.registry.add_or_merge(scanned);
        FindRequest vouched = r;
        vouched.handle = b.registry.devices()[(size_t)i].handle;
        check(!plan_find(b.registry, vouched, kNoNetworks, d, at, message) && has(message, "answered a scan") &&
                  has(message, "Nothing was sent"),
              "a device the scan found");
    }

    // ------------------------------------------------------------ the read

    void test_a_match_gives_the_device_at_the_address()
    {
        section("settings that give the serial: found, with what they say");

        Registry registry;
        DeviceRecord entry = typed_in(920001);
        entry.handle = to_handle(4);

        FakeTransport t;
        answers(t, Settings());
        uint8_t invoke = 9;
        const FindOutcome o = find_at(t, device_at(kDeviceIp, 47900), entry, registry, instant(), invoke, kNow);

        check(o.found, "found");
        if (!require(t.sent.size() == 1, "one request"))
            return;
        check(t.sent[0].request.command == bacnet::ReadCommand::Settings, "  for the settings");
        check(t.sent[0].request.first == 0 && t.sent[0].request.last == 0, "  the one block of them");
        check(o.requests_sent == 1, "  counted");
        check(invoke == 10, "  on the next invoke id");

        const DeviceRecord& f = o.record;
        check(f.handle == entry.handle, "for the entry asked about");
        check(f.serial_number == 920001, "  its serial");
        check(f.product == ProductClassId::Esp32T3Series, "  its product, which the settings do not give");
        check(f.provenance == Provenance::BacnetUnicast, "found at an address given");
        check(f.reached, "  and reached");
        check(f.answered_scan == 0, "  and not by a scan");
        check(!f.observation_complete, "  and not a complete look at it");
        check(f.first_seen == kNow && f.last_seen == kNow, "seen now");
        check(f.connection.transport == Transport::BacnetIp, "over BACnet/IP");
        check(f.connection.host == "192.168.1.50", "at the address");
        check(f.connection.udp_port == 47900, "  and the port");
        check(f.address_note == "192.168.1.50", "  which the list shows");
        check(f.answered_from == "192.168.1.50" && f.reported_ip.empty(),
              "  answered from there, with no address of its own reported");
        check_eq(f.mini_type, 5, "its panel type, without the chip bits");
        check_eq(f.modbus_id_reported, 7, "its Modbus id");
        check_eq(f.connection.modbus_slave_id, 7, "  which it is addressed by");
        check_eq(f.connection.device_instance, 123456, "its instance");
        check(f.panel_name == "AHU 2", "its name, trailing blanks dropped");
        check_eq(f.firmware, 0, "the firmware is left for a scan");
        check(o.message == "The panel at 192.168.1.50:47900 gives serial 920001 in its settings. Its pages are "
                           "read from that address now.",
              "the message says where, and which serial");
    }

    void test_anything_but_the_serial_is_not_found()
    {
        section("another serial, none, a refusal or silence: not found");

        Registry registry;
        DeviceRecord boiler = restored(555001, "10.0.0.7");
        boiler.placement.name = "Boiler";
        registry.add_or_merge(boiler);

        const DeviceRecord entry = typed_in(920001);

        {
            Settings other;
            other.serial = 999999;
            FakeTransport t;
            answers(t, other);
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(), entry, registry, instant(), invoke, kNow);
            check(!o.found, "another serial");
            check(has(o.message, "gives its serial number as 999999") && has(o.message, "not 920001") &&
                      has(o.message, "another device") && has(o.message, "Nothing was saved."),
                  "  says whose, and that nothing was saved");
            check(!has(o.message, "in the list already"), "  and names no entry when it is not in the list");
            check(t.sent.size() == 1, "  after one request");
        }
        {
            Settings listed;
            listed.serial = 555001;
            FakeTransport t;
            answers(t, listed);
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(), entry, registry, instant(), invoke, kNow);
            check(!o.found, "the serial of another device in the list");
            check(has(o.message, "A device with serial 555001 is in the list already, named \"Boiler\"."),
                  "  names it");
        }
        {
            Settings zero;
            zero.serial = 0;
            FakeTransport t;
            answers(t, zero);
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(), entry, registry, instant(), invoke, kNow);
            check(!o.found, "serial 0");
            check(has(o.message, "gives no serial number") && has(o.message, "whether it is serial 920001") &&
                      has(o.message, "Nothing was saved."),
                  "  says it cannot tell");
        }
        {
            FakeTransport t;
            refuses(t);
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(), entry, registry, instant(), invoke, kNow);
            check(!o.found, "a refusal");
            check(has(o.message, "The device refused to read the panel's settings") &&
                      has(o.message, "Nothing was saved."),
                  "  says so");
            check(t.sent.size() == 1, "  after one request");
        }
        {
            FakeTransport t;
            t.respond = [](const FakeTransport::Sent&, size_t, FakeTransport& f) {
                f.inbox.push_back({ bacnet::Endpoint(), {}, bacnet::ReadTransport::kPortUnreachable });
            };
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(kDeviceIp, 47999), entry, registry, instant(), invoke, kNow);
            check(!o.found, "nothing listening on the port");
            check(o.message == "192.168.1.50 answered that nothing is listening on UDP port 47999, so something is "
                               "at that address, but not on that port. Check the port: BACnet/IP devices use 47808. "
                               "Nothing was saved.",
                  "  says the port is wrong, and not as if a scan had found it");
            check(t.sent.size() == 1, "  after one request");
        }
        {
            FakeTransport t;
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(kDeviceIp, 47900), entry, registry, instant(), invoke, kNow);
            check(!o.found, "silence");
            check(has(o.message, "Nothing answered at 192.168.1.50:47900 after 2 attempts of 0.2 s") &&
                      has(o.message, "UDP 47900") && has(o.message, "Nothing was saved."),
                  "  says where, how long, and what to check");
            check(!has(o.message, "since the scan"), "  and not as if a scan had found it");
            check(t.sent.size() == 2, "  after the request and its one repeat");
            check(o.requests_sent == 2, "  counted");
        }
        {
            bacnet::ReadSettings once = instant();
            once.attempts = 1;
            FakeTransport t;
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(), entry, registry, once, invoke, kNow);
            check(has(o.message, "after 1 attempt of 0.2 s"), "one attempt is one attempt");
        }
        {
            // The right settings, from somewhere else.
            FakeTransport t;
            answers(t, Settings(), kOtherIp);
            uint8_t invoke = 1;
            const FindOutcome o = find_at(t, device_at(), entry, registry, instant(), invoke, kNow);
            check(!o.found && has(o.message, "Nothing answered"), "an answer from another address is not one");
        }
    }

    // ------------------------------------------------------------ the list

    void test_a_found_device_takes_the_entry_and_is_saved()
    {
        section("a device found takes its entry's place, keeps what the operator gave, and is saved");

        Bench b;
        if (!b.open())
            return;
        const Handle h = b.add(920001, ProductClassId::Tstat10, 11, "Boiler AHU");
        b.registry.select_by_handle(h);

        InputEditRequest edit;
        edit.handle = h;
        edit.index  = 2;
        edit.field  = offline::InputField::Label;
        edit.value  = "OAT";
        std::string message;
        if (!require(edit_offline_input(b.registry, b.db, b.status, edit, message), "an input changed offline"))
            return;

        FakeTransport t;
        answers(t, Settings());
        check(b.find(h, "192.168.1.50", 47808, t, message), "found");
        check(has(message, "gives serial 920001 in its settings"), "  and says so");
        check(!has(message, "not being saved"), "  with nothing about saving");
        check(t.sent.size() == 1, "  after one request");

        const DeviceRecord* d = b.get(h);
        if (!require(d != nullptr, "the entry is still there, by its handle"))
            return;
        check(b.registry.size() == 1, "  and there is still one device");
        check(b.registry.selected_handle() == h, "  still selected");
        check(d->provenance == Provenance::BacnetUnicast, "found at an address given");
        check(d->connection.host == "192.168.1.50" && d->connection.udp_port == 47808, "with its address");
        check(d->placement.name == "Boiler AHU", "the name given stays");
        check_eq(d->mini_type, 5, "the panel type is the device's now, not the model chosen");
        check(d->reached, "reached");
        check(!is_configured_offline(*d), "no longer configured offline: it is read from its address");
        check(has(pending_offline_note(b.db, *d), "Changes to input 3"), "the change made offline is kept, unwritten");

        const std::vector<DeviceRecord> saved = b.saved();
        if (!require(saved.size() == 1, "one device saved"))
            return;
        const DeviceRecord& s = saved[0];
        check(s.provenance == Provenance::Restored, "next time: from the saved list, not added by hand");
        check(s.reached, "  reached before");
        check(s.connection.host == "192.168.1.50" && s.connection.udp_port == 47808, "  at the address found");
        check(s.answered_from == "192.168.1.50" && s.reported_ip.empty(), "  answered from there");
        check(s.placement.name == "Boiler AHU", "  with its name");
        check_eq(s.mini_type, 5, "  and panel type");
        check_eq(s.modbus_id_reported, 7, "  and Modbus id");
        check(s.first_seen == kNow && s.last_seen == kNow, "  first and last seen now");
        check(s.panel_name == "AHU 2", "  and its own name");

        // Found this session: held to the stricter rule, and said so.
        const InputsPlan plan = plan_inputs_read(*d);
        check(plan.can_read, "its inputs can be read");
        check(plan.identity == Identity::FoundAtAddress, "  as a device found at an address");
        check(plan.endpoint == device_at(kDeviceIp, 47808), "  from there");
        check(!plan.seen_this_session, "  which is not a scan");
        check(has(plan.sighting, "Found by Find at the address given, ") &&
                  has(plan.sighting, "every read first checks its serial"),
              "  and the page is told how it was found");

        // Next session: from the saved list, like any other.
        const InputsPlan next = plan_inputs_read(s);
        check(next.identity == Identity::MustConfirm, "next time: known only from the saved list");
        check(has(next.sighting, "Not seen since T5000 started"), "  and said so");
    }

    void test_a_failed_find_changes_nothing()
    {
        section("a Find that finds nothing leaves the list and the file as they were");

        Bench b;
        if (!b.open())
            return;
        const Handle h = b.add(920001, ProductClassId::Esp32T3Series, 0, "Boiler AHU");
        const DeviceRecord before = *b.get(h);

        Settings other;
        other.serial = 999999;
        Settings zero;
        zero.serial = 0;

        for (int kind = 0; kind < 4; kind++)
        {
            FakeTransport t;
            if (kind == 0)
                answers(t, other);
            else if (kind == 1)
                answers(t, zero);
            else if (kind == 2)
                refuses(t);
            // and 3 is silence

            std::string message;
            check(!b.find(h, "192.168.1.50", 47808, t, message), "not found");
            const DeviceRecord* d = b.get(h);
            if (!require(d != nullptr, "  the entry is still there"))
                return;
            check(d->provenance == Provenance::ManuallyAdded && !d->reached, "  still added by hand");
            check(d->connection.host.empty() && d->address_note.empty() && d->answered_from.empty(),
                  "  with no address");
            check(d->mini_type == before.mini_type && d->panel_name.empty() && d->modbus_id_reported == 0,
                  "  and nothing from the panel");
            check(d->last_seen == 0 && d->first_seen == 0, "  never seen");
            check(b.registry.size() == 1, "  and nothing else is in the list");

            const std::vector<DeviceRecord> saved = b.saved();
            check(saved.size() == 1 && saved[0].provenance == Provenance::ManuallyAdded &&
                      saved[0].connection.host.empty() && saved[0].last_seen == 0,
                  "  and saved as it was");
        }
    }

    void test_a_find_the_file_refuses_changes_nothing()
    {
        section("a device found that cannot be saved is not taken into the list either");

        TempFile file(L"find-locked");
        store::DeviceDb db;
        std::string error;
        if (!require(db.open(file.utf8(), error), "the saved list opens"))
            return;

        Registry registry;
        StoreStatus status;
        status.saving = true;
        HandAdded added;
        added.serial  = 920001;
        added.product = ProductClassId::Esp32T3Series;
        Handle h = kNoHandle;
        std::string message;
        if (!require(add_device(registry, db, added, status, h, message), "a device added by hand"))
            return;

        // Another connection holds the file, as a second T5000 could.
        store::Database other;
        if (!require(other.open(file.utf8(), error) && other.exec("BEGIN IMMEDIATE", error),
                     "another connection holds the file"))
            return;

        FakeTransport t;
        answers(t, Settings());
        ScanSummary summary;
        FindRequest r;
        r.handle = h;
        r.host   = "192.168.1.50";
        DeviceRecord d;
        bacnet::Endpoint at;
        uint8_t invoke = 1;
        const bool ok = plan_find(registry, r, kNoNetworks, d, at, message) &&
                        find_device(registry, db, d, at, t, instant(), invoke, kNow, summary, message);
        check(!ok, "refused");
        check(has(message, "It was found, but the list could not be saved, so nothing was changed"), "  and says why");
        check(registry.devices()[0].provenance == Provenance::ManuallyAdded &&
                  registry.devices()[0].connection.host.empty(),
              "  the list is as it was");

        other.exec("ROLLBACK", error);
        std::vector<DeviceRecord> saved;
        check(db.load(saved, error) && saved.size() == 1 && saved[0].connection.host.empty(),
              "  and so is the file");
    }

    void test_without_a_saved_list_it_is_kept_for_the_session()
    {
        section("with no saved list, a device found is kept until T5000 closes, and said so");

        Registry registry;
        DeviceRecord entry = restored(920001, "10.0.0.5");
        registry.add_or_merge(entry);
        store::DeviceDb closed;
        ScanSummary summary;

        FakeTransport t;
        answers(t, Settings());
        DeviceRecord d = registry.devices()[0];
        uint8_t invoke = 1;
        std::string message;
        check(find_device(registry, closed, d, device_at(), t, instant(), invoke, kNow, summary, message), "found");
        check(has(message, "gives serial 920001 in its settings.") &&
                  has(message, "not being saved, so the address is kept only until T5000 closes"),
              "  and told it is not saved");
        check(registry.devices()[0].connection.host == "192.168.1.50", "  at the address found");
    }

    void test_a_record_for_another_entry_is_not_recorded()
    {
        section("a device found is recorded only into the entry it was found for");

        Bench b;
        if (!b.open())
            return;
        b.add(920001, ProductClassId::Esp32T3Series, 0, "");

        DeviceRecord stray = typed_in(920002);
        stray.provenance = Provenance::BacnetUnicast;
        stray.handle     = to_handle(77);
        std::string message;
        check(!record_found(b.registry, b.db, stray, b.summary, message) && has(message, "no longer in the list"),
              "not when that entry has gone");
        check(b.registry.size() == 1, "  nothing added to the list");
        check(b.saved().size() == 1, "  or to the file");
    }

    // ------------------------------------------------------------ afterwards

    void test_found_is_held_to_the_stricter_rule()
    {
        section("a device found is held to the stricter rule on every read, and told to find it again");

        {
            Settings zero;
            zero.serial = 0;
            FakeTransport t;
            answers(t, zero);
            uint8_t invoke = 1;
            const InputsPageRead r = read_inputs_page(t, device_at(), ProductClassId::Esp32T3Series, 920001,
                                                      Identity::FoundAtAddress, instant(), invoke);
            check(!r.ok, "settings with no serial stop the read");
            check(has(r.error, "gives no serial number") &&
                      has(r.error, "Its address is the one it was found at") &&
                      has(r.error, "Find it again on the Devices page, and then open Inputs again."),
                  "  and it is told to find it again");
            check(t.sent.size() == 1, "  after the settings alone");
        }
        {
            Settings other;
            other.serial = 999999;
            FakeTransport t;
            answers(t, other);
            uint8_t invoke = 1;
            const InputsPageRead r = read_inputs_page(t, device_at(), ProductClassId::Esp32T3Series, 920001,
                                                      Identity::FoundAtAddress, instant(), invoke);
            check(!r.ok && has(r.error, "not 920001, the serial saved for this device.") &&
                      has(r.error, "Find it again"),
                  "another serial stops it");
        }
        {
            FakeTransport t;
            refuses(t);
            uint8_t invoke = 1;
            const InputsPageRead r = read_inputs_page(t, device_at(), ProductClassId::Esp32T3Series, 920001,
                                                      Identity::FoundAtAddress, instant(), invoke);
            check(!r.ok && has(r.error, "did not give settings T5000 could use") && has(r.error, "Find it again"),
                  "a refusal of the settings stops it");
            check(t.sent.size() == 1, "  after the settings alone");
        }
        {
            Settings zero;
            zero.serial = 0;
            FakeTransport t;
            answers(t, zero);
            uint8_t invoke = 1;
            const InputsPageRead r = read_inputs_page(t, device_at(), ProductClassId::Esp32T3Series, 920001,
                                                      Identity::MustConfirm, instant(), invoke);
            check(!r.ok && has(r.error, "Its address comes from the saved list") &&
                      has(r.error, "Scan, or find it at its address on the Devices page, and then open Inputs again."),
                  "one from the saved list is told to scan, or to find it");
        }

        // The payload of a read that got past the settings says the serial
        // was checked, as for a device from the saved list.
        DeviceRecord found = restored(920001, "192.168.1.50");
        found.provenance = Provenance::BacnetUnicast;
        found.last_seen  = kNow;
        const InputsPlan plan = plan_inputs_read(found);
        check(plan.identity == Identity::FoundAtAddress, "a device found this session is planned as found");
        InputsPageRead read;
        read.ok     = true;
        read.points = std::vector<t5000::wire::InputPoint>(3);
        const std::string json = inputs_payload(found, plan, read);
        check(has(json, "Found by Find at the address given") &&
                  has(json, "Its settings give serial 920001, the one saved for it, so it is the same device."),
              "  and its payload says the serial was checked");

        OutputsPageRead outputs;
        outputs.ok     = true;
        outputs.points = std::vector<t5000::wire::OutputPoint>(2);
        const std::string o = outputs_payload(found, plan_outputs_read(found), outputs);
        check(has(o, "Found by Find at the address given") &&
                  has(o, "Its settings give serial 920001, the one saved for it, so it is the same device."),
              "  and so does Outputs'");

        VariablesPageRead variables;
        variables.ok     = true;
        variables.points = std::vector<t5000::wire::VariablePoint>(2);
        const std::string v = variables_payload(found, plan_variables_read(found), variables);
        check(has(v, "Found by Find at the address given") &&
                  has(v, "Its settings give serial 920001, the one saved for it, so it is the same device."),
              "  and Variables'");

        // After a scan has vouched for it, it is read as any scanned device.
        DeviceRecord vouched = found;
        vouched.answered_scan = 1;
        const InputsPlan scanned = plan_inputs_read(vouched);
        check(scanned.identity == Identity::VouchedForByScan && scanned.sighting.empty(),
              "once a scan has found it, the scan vouches for it");
    }

    void test_duplicate_ids_are_worked_out_again()
    {
        section("a device found takes part in the duplicate Modbus ids at once");

        Bench b;
        if (!b.open())
            return;

        DeviceRecord scanned = restored(700001, "10.0.0.8");
        scanned.provenance         = Provenance::BacnetBroadcast;
        scanned.modbus_id_reported = 7;
        scanned.answered_scan      = b.registry.begin_scan();
        b.registry.add_or_merge(scanned);
        const Handle h = b.add(920001, ProductClassId::Esp32T3Series, 0, "");

        FakeTransport t;
        answers(t, Settings());   // Modbus id 7
        std::string message;
        check(b.find(h, "192.168.1.50", 47808, t, message), "found");
        check_eq(b.summary.stats.duplicate_modbus_ids, 2, "both devices on id 7 are counted");
        int flagged = 0;
        for (const auto& d : b.registry.devices())
            for (const auto& r : d.repairs)
                if (r.kind == RepairKind::ResolveDuplicateModbusId)
                    flagged++;
        check_eq(flagged, 2, "  and each carries the repair");
    }

    void test_a_scan_afterwards_vouches_for_it()
    {
        section("a scan that finds a device Find found vouches for it");

        Registry registry;
        DeviceRecord found = typed_in(920001);
        found.provenance      = Provenance::BacnetUnicast;
        found.connection.host = "192.168.1.50";
        found.answered_from   = "192.168.1.50";
        found.mini_type       = 5;
        registry.add_or_merge(found);

        DeviceRecord answer = typed_in(920001);
        answer.provenance           = Provenance::BacnetBroadcast;
        answer.observation_complete = true;
        answer.reached              = true;
        answer.connection.host      = "192.168.1.50";
        answer.answered_from        = "192.168.1.50";
        answer.reported_ip          = "192.168.1.50";
        answer.answered_scan        = registry.begin_scan();
        registry.add_or_merge(answer);

        const DeviceRecord& d = registry.devices()[0];
        check(d.provenance == Provenance::BacnetBroadcast, "it answered a broadcast now");
        check_eq(d.mini_type, 5, "the panel type Find read stays, since a scan reports none");
        check(plan_inputs_read(d).identity == Identity::VouchedForByScan, "and the scan vouches for it");
    }

    void test_an_older_scans_address_pair_goes()
    {
        section("Find replaces an older scan's pair of addresses");

        Bench b;
        if (!b.open())
            return;
        DeviceRecord old = restored(920001, "10.0.0.5");
        old.reported_ip = "10.9.9.9";
        old.firmware    = 637;
        std::string error;
        if (!require(b.db.save_scanned({ old }, error), "a device from an earlier session is saved"))
            return;
        std::vector<DeviceRecord> loaded;
        if (!require(b.db.load(loaded, error) && loaded.size() == 1, "and restored"))
            return;
        b.registry.add_or_merge(loaded[0]);
        const Handle h = b.registry.devices()[0].handle;
        check(b.registry.devices()[0].address_mismatch(), "it came with an older pair that disagrees");

        FakeTransport t;
        answers(t, Settings());
        std::string message;
        check(b.find(h, "192.168.1.50", 47808, t, message), "found at a new address");
        const DeviceRecord* d = b.get(h);
        if (!require(d != nullptr, "  still there"))
            return;
        check(d->answered_from == "192.168.1.50" && d->reported_ip.empty(), "  answered from there");
        check(!d->address_mismatch(), "  so no older address is named as the one contacted");
        check(d->connection.host == "192.168.1.50" && d->address_note == "192.168.1.50", "  which is the new one");
        check_eq(d->modbus_id_reported, 7, "  with the Modbus id it gives now");
        check(d->first_seen == kNow - 86400 && d->last_seen == kNow, "  first seen as before, last seen now");

        const std::vector<DeviceRecord> saved = b.saved();
        check(saved.size() == 1 && saved[0].answered_from == "192.168.1.50" && saved[0].reported_ip.empty() &&
                  !saved[0].address_mismatch(),
              "  and so it is saved");
        check(d->firmware == 637 && saved.size() == 1 && saved[0].firmware == 637,
              "  keeping the firmware a scan gave, which Find does not read");
    }

    void test_the_list_says_where_find_applies()
    {
        section("the device list says which devices Find is offered for");

        Registry registry;
        registry.add_or_merge(typed_in(920001));
        DeviceRecord scanned = restored(920002, "10.0.0.6");
        scanned.provenance          = Provenance::BacnetBroadcast;
        scanned.answered_scan       = registry.begin_scan();
        scanned.connection.udp_port = 47900;
        registry.add_or_merge(scanned);

        const std::string json = build_devices_json(registry, ScanSummary());
        const size_t second = json.find("\"serialNumber\":920002");
        if (!require(second != std::string::npos, "both are listed"))
            return;
        check(has(json.substr(0, second), "\"canFind\":true"), "an entry added by hand: offered");
        check(has(json.substr(second), "\"canFind\":false"), "a device the scan found: not");
        check(has(json.substr(0, second), "\"port\":47808"), "the port it would be read at, 47808 by default");
        check(has(json.substr(second), "\"port\":47900"), "  or its own, so Find opens on it");
    }
}

int run_find_device_tests()
{
    test_the_request_is_read_strictly();
    test_an_address_must_be_one_device();
    test_a_local_network_address_is_refused();
    test_find_applies_only_where_nothing_vouches();
    test_the_plan_sends_nothing_when_it_refuses();
    test_a_match_gives_the_device_at_the_address();
    test_anything_but_the_serial_is_not_found();
    test_a_found_device_takes_the_entry_and_is_saved();
    test_a_failed_find_changes_nothing();
    test_a_find_the_file_refuses_changes_nothing();
    test_without_a_saved_list_it_is_kept_for_the_session();
    test_a_record_for_another_entry_is_not_recorded();
    test_found_is_held_to_the_stricter_rule();
    test_duplicate_ids_are_worked_out_again();
    test_a_scan_afterwards_vouches_for_it();
    test_an_older_scans_address_pair_goes();
    test_the_list_says_where_find_applies();
    return 0;
}
