// Tests for listing serial ports.
//
// The list is built from registry values that other machines hold, given
// here as data. The registry itself is read once, to see that it can be, and
// what this machine holds is not checked: a build machine may have no ports.
// Nothing here opens a port.

#include "ports.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::serial;
    using namespace t5000::testing;

    void test_ports_are_in_number_order()
    {
        section("ports are listed by number, and names that are not COMn last");

        const auto ports = ports_from_values({
            { "\\Device\\VCP0", "COM3" },
            { "\\Device\\Serial0", "COM1" },
            { "\\Device\\com0com10", "CNCA0" },
            { "\\Device\\USBSER000", "COM12" },
            { "\\Device\\Serial1", "COM2" },
        });

        if (!require(ports.size() == 5, "all five are listed"))
            return;
        check_streq(ports[0].name.c_str(), "COM1", "COM1 first");
        check_streq(ports[1].name.c_str(), "COM2", "then COM2");
        check_streq(ports[2].name.c_str(), "COM3", "then COM3");
        check_streq(ports[3].name.c_str(), "COM12", "COM12 after COM3, by number, not as text");
        check_streq(ports[4].name.c_str(), "CNCA0", "a name that is not COMn after the rest");

        check_eq(ports[3].number, 12, "COM12 is port 12");
        check_eq(ports[4].number, 0, "CNCA0 has no number");
        check_streq(ports[2].device.c_str(), "\\Device\\VCP0", "the driver's device is kept");
    }

    void test_port_numbers()
    {
        section("a port's number comes from a name of COM and digits only");

        const auto ports = ports_from_values({
            { "a", "com7" },
            { "b", "COM0" },
            { "c", "COM" },
            { "d", "COM1x" },
            { "e", "LPT1" },
            { "f", "COM99999999" },
        });
        if (!require(ports.size() == 6, "all six are listed"))
            return;

        check_streq(ports[0].name.c_str(), "com7", "com7 is a port, in any case");
        check_eq(ports[0].number, 7, "numbered 7");
        for (size_t i = 1; i < ports.size(); i++)
            check_eq(ports[i].number, 0, (ports[i].name + " has no number").c_str());
    }

    void test_ports_with_no_name_are_left_out()
    {
        section("a value with no port name is left out");

        const auto ports = ports_from_values({ { "\\Device\\Serial0", "" }, { "\\Device\\Serial1", "COM2" } });
        if (require(ports.size() == 1, "one port"))
            check_streq(ports[0].name.c_str(), "COM2", "the one with a name");

        check(ports_from_values({}).empty(), "no values, no ports");
    }

    void test_usb_adapters_are_marked()
    {
        section("USB-serial drivers are marked, by the device's name");

        const auto ports = ports_from_values({
            { "\\Device\\Serial0", "COM1" },
            { "\\Device\\USBSER000", "COM2" },
            { "\\Device\\VCP0", "COM3" },
            { "\\Device\\Silabser0", "COM4" },
            { "\\Device\\ProlificSerial0", "COM5" },
            { "\\Device\\CH341SER_A64", "COM6" },
            { "\\Device\\usbser001", "COM7" },
            { "VCP5", "COM8" },
            { "\\Device\\SerialVCP", "COM9" },
        });
        if (!require(ports.size() == 9, "all nine are listed"))
            return;

        check(!ports[0].usb, "a motherboard port is not");
        check(ports[1].usb, "Windows' own CDC driver, USBSER, is");
        check(ports[2].usb, "FTDI's VCP is");
        check(ports[3].usb, "Silicon Labs' is");
        check(ports[4].usb, "Prolific's is");
        check(ports[5].usb, "WCH's CH341 is");
        check(ports[6].usb, "in any case");
        check(ports[7].usb, "with no \\Device\\ in front");
        check(!ports[8].usb, "not when the name only ends in one");
    }

    void test_the_registry_can_be_read()
    {
        section("this machine's ports are listed without opening any");

        std::string error = "left over";
        const auto ports = list_ports(error);
        check_streq(error.c_str(), "", "no error, with ports or without");
        for (const Port& p : ports)
            check(!p.name.empty(), "every port listed has a name");
    }
}

int run_serial_ports_tests()
{
    test_ports_are_in_number_order();
    test_port_numbers();
    test_ports_with_no_name_are_left_out();
    test_usb_adapters_are_marked();
    test_the_registry_can_be_read();
    return 0;
}
