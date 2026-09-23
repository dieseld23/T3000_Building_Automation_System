// Tests for deciding whether a device's Inputs are read at all.

#include "inputs_plan.h"
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
    test_an_esp32_read_states_its_limit();
    return 0;
}
