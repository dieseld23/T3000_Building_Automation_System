// Tests for deciding whether a device's Inputs are read at all.

#include "inputs_plan.h"
#include "../discovery/scanner.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::app;
    using namespace t5000::device;
    using namespace t5000::testing;

    DeviceRecord scanned(ProductClassId product, const char* host = "192.168.1.50", int port = 47808)
    {
        DeviceRecord d;
        d.serial_number        = 800100;
        d.product              = product;
        d.firmware             = 600;
        d.connection.transport = Transport::BacnetIp;
        d.connection.host      = host;
        d.connection.udp_port  = port;
        return d;
    }

    void test_a_private_data_controller_is_read()
    {
        section("a scanned private-data controller is read, at the address its scan gave");

        const InputsPlan p = plan_inputs_read(scanned(ProductClassId::Cm5, "10.1.2.3", 47809));
        check(p.can_read, "a CM5 on BACnet/IP can be read");
        check(p.endpoint.ip == 0x0A010203 && p.endpoint.port == 47809,
              "at the scanned address and port, not a default");
        check(p.decision.path == ReadPath::PrivateData, "by private data");
        check(p.note.empty(), "with nothing to add");
    }

    void test_everything_else_is_refused_with_a_reason()
    {
        section("every device that is not read says why");

        const InputsPlan tstat8 = plan_inputs_read(scanned(ProductClassId::Tstat8));
        check(!tstat8.can_read, "a Tstat8 - not a private-data product - is not sent a request");
        check(tstat8.reason.find("does not support private-data") != std::string::npos &&
              tstat8.reason.find("does not read Modbus registers yet") != std::string::npos,
              "  and says both that it is the product and what T5000 lacks");

        DeviceRecord serial = scanned(ProductClassId::Cm5);
        serial.connection.transport = Transport::ModbusRtu;
        const InputsPlan s = plan_inputs_read(serial);
        check(!s.can_read && s.reason.find("BACnet/IP only") != std::string::npos,
              "a serial connection is not read, and says so");

        const InputsPlan nohost = plan_inputs_read(scanned(ProductClassId::Cm5, ""));
        check(!nohost.can_read && nohost.reason.find("did not report an address") != std::string::npos,
              "no address, no request");

        const InputsPlan badhost = plan_inputs_read(scanned(ProductClassId::Cm5, "controller.local"));
        check(!badhost.can_read, "a hostname is not an address T5000 sends to");

        const InputsPlan badport = plan_inputs_read(scanned(ProductClassId::Cm5, "10.1.2.3", 0));
        check(!badport.can_read, "nor is port 0");
    }

    void test_a_device_behind_a_controller_is_not_read()
    {
        section("a device a controller answered for is never sent a read");

        // A TSTAT10 on a T3-BB's RS485 bus: a private-data product, on
        // BACnet/IP by every other test here - and at the T3-BB's address.
        DeviceRecord child = scanned(ProductClassId::Tstat10);
        child.parent_serial = 500001;

        const InputsPlan p = plan_inputs_read(child);
        check(!p.can_read, "not read");
        check(p.reason.find("500001") != std::string::npos,
              "  names the controller it is behind");
        check(p.reason.find("controller's inputs") != std::string::npos,
              "  and says what a read would actually return");

        child.product = ProductClassId::Cm5;
        check(!plan_inputs_read(child).can_read, "whatever the product");
    }

    void test_a_device_behind_a_controller_is_not_read_from_the_address_it_came_from()
    {
        section("a sub-device stays refused now that the address is the one it answered from");

        // The controller answers for its sub-device, so the sub-device's
        // response arrives FROM the controller. Using the sender address
        // makes that explicit - the host is now certainly the controller's -
        // and the refusal must not depend on which address was chosen.
        t5000::discovery::ScanResponse r;
        r.serial_number        = 800200;
        r.product_id           = static_cast<uint8_t>(ProductClassId::Cm5);
        r.parent_serial_number = 800100;
        r.ip[0] = 192; r.ip[1] = 168; r.ip[2] = 1; r.ip[3] = 77;
        r.bacnet_port          = 47808;

        const DeviceRecord child = t5000::discovery::to_record(r, 0xC0A80132);   // from 192.168.1.50
        check(child.connection.host == "192.168.1.50", "its host is the controller's address");

        const InputsPlan p = plan_inputs_read(child);
        check(!p.can_read, "and it is still not read");
        check(p.reason.find("800100") != std::string::npos, "  naming the controller");
    }

    void test_an_esp32_read_states_its_limit()
    {
        section("an ESP32 T3 read says it may not be the whole list");

        const InputsPlan p = plan_inputs_read(scanned(ProductClassId::Esp32T3Series));
        check(p.can_read, "an ESP32 T3 is read");
        check(p.note.find("more than 64") != std::string::npos,
              "and the page is told it may have more than the 64 shown");
    }
}

int run_inputs_plan_tests()
{
    test_a_private_data_controller_is_read();
    test_everything_else_is_refused_with_a_reason();
    test_a_device_behind_a_controller_is_not_read();
    test_a_device_behind_a_controller_is_not_read_from_the_address_it_came_from();
    test_an_esp32_read_states_its_limit();
    return 0;
}
