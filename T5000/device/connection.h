#pragma once

// How to reach a controller.
//
// The existing product gets these from its own settings dialogs and global
// state. A standalone tool has to carry its own, which is what this is.
//
// The fields mirror what the stacks actually need to come up, traced from
// T3000/global_function.cpp:8193 onwards:
//
//   BACnet/IP    set_datalink_protocol(PROTOCOL_BACNET_IP)
//                bip_set_port(htons(47808))
//   BACnet MSTP  set_datalink_protocol(MODBUS_BACNET_MSTP)
//                dlmstp_set_baud_rate(baud)
//                dlmstp_set_max_master(max_master)
//                dlmstp_init("COMn")
//   Modbus       Open_Socket(host) / serial, addressed by slave id
//
// Nothing here connects. Validation and persistence are separated from I/O so
// a bad setting is caught and explained before anything is attempted against
// live equipment.

#include <string>
#include <vector>

namespace t5000::device
{
    enum class Transport
    {
        BacnetIp,
        BacnetMstp,
        ModbusTcp,
        ModbusRtu,
    };

    const char* transport_name(Transport t);
    bool        transport_from_name(const std::string& name, Transport& out);
    bool        transport_is_serial(Transport t);

    // The baud rates the product offers (global_define.h:1450). Not an
    // arbitrary range: an unlisted rate is a typo, not a preference.
    const std::vector<int>& supported_baud_rates();

    struct Connection
    {
        Transport transport = Transport::BacnetIp;

        // Network transports.
        std::string host;
        int  udp_port = 47808;   // BACnet/IP; fixed in practice, see below
        int  tcp_port = 502;     // Modbus TCP

        // Serial transports.
        int  com_port = 1;       // COMn
        int  baud     = 38400;

        // Addressing.
        int  device_instance = 0;    // BACnet device id, T3000's g_bac_instance
        int  mstp_max_master = 127;
        int  modbus_slave_id = 1;    // T3000's g_tstat_id
    };

    struct ValidationError
    {
        std::string field;
        std::string message;
    };

    // Returns every problem, not just the first. A settings form that reports
    // one error per submit is a form people submit five times.
    std::vector<ValidationError> validate(const Connection& c);

    std::string to_json(const Connection& c);

    // Tolerant on input: unknown keys are ignored and missing keys keep their
    // default, so a config written by a newer build still loads. Returns false
    // only when the text is not usable at all.
    bool from_json(const std::string& json, Connection& out, std::string& error);

    // Beside the executable, not in %APPDATA%. This is a tool a technician
    // copies to a laptop or a stick; a config that travels with it is less
    // surprising than one that silently stays behind.
    std::string default_config_path();

    bool load(const std::string& path, Connection& out, std::string& error);
    bool save(const std::string& path, const Connection& c, std::string& error);
}
