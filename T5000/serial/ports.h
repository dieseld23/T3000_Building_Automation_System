#pragma once

// Which serial ports a scan could use, read from the registry without opening
// any of them.
//
// T3000 lists ports the same way: the values under
// HKLM\HARDWARE\DEVICEMAP\SERIALCOMM, one per port, each named after the
// driver's device and holding the port's name (GetSerialComPortNumber1,
// global_function.cpp:989-1041). Listing opens nothing, so it cannot disturb a
// port another program holds, or whatever is wired to one. Opening a port to
// scan it is a separate step, which T5000 does not take yet.

#include <string>
#include <utility>
#include <vector>

namespace t5000::serial
{
    struct Port
    {
        std::string name;         // "COM3"
        int         number = 0;   // 3, or 0 when the name is not COMn
        std::string device;       // the driver's device: "\Device\VCP0"

        // True when the device is one of the USB-serial drivers named in
        // ports.cpp. A guess from the driver's name, and shown as one: an
        // adapter whose driver is not on the list is just not marked.
        bool usb = false;
    };

    // The ports that SERIALCOMM's values name, as (value name, value data)
    // pairs, in port-number order, with names that are not COMn after the
    // rest. Separate from the registry so it can be tested with what other
    // machines hold.
    std::vector<Port> ports_from_values(const std::vector<std::pair<std::string, std::string>>& values);

    // Reads SERIALCOMM. A machine with no serial ports has no such key; that
    // is an empty list, not an error.
    std::vector<Port> list_ports(std::string& error);
}
