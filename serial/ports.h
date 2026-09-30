#pragma once

// Which serial ports a scan could use, read from the registry without opening
// any of them.
//
// T3000 lists ports the same way: the values under
// HKLM\HARDWARE\DEVICEMAP\SERIALCOMM, one per port, each named after the
// driver's device and holding the port's name (GetSerialComPortNumber1,
// global_function.cpp:989-1041). Listing opens nothing, so it cannot disturb a
// port another program holds, or whatever is wired to one. Opening a port to
// scan it is a separate step (discovery/com_port_line.h), taken only for a
// port the operator picks, and only for one this list names.

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

    // "COM12" -> 12. 0 for anything else, including "COM0" and "COM" alone.
    int com_number(const std::string& name);

    // True for a name a port can be opened by: 1-32 letters, digits and
    // underscores, as SERIALCOMM's names are (COM3, CNCA0). The name goes
    // after \\.\ in CreateFile, so anything else - a backslash, a dot, a
    // colon - could name some other device, and is refused before that.
    bool is_plain_port_name(const std::string& name);

    // The port in `ports` with this name, ignoring case, or null. Only a port
    // the registry lists is ever opened: a name that is plain but not listed,
    // PhysicalDrive0 say, is not a serial port.
    const Port* find_port(const std::vector<Port>& ports, const std::string& name);
}
