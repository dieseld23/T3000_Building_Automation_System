#include "inputs_plan.h"

namespace t5000::app
{
    InputsPlan plan_inputs_read(const device::DeviceRecord& d)
    {
        using device::Transport;

        InputsPlan plan;

        if (d.connection.transport != Transport::BacnetIp)
        {
            plan.reason = std::string("T5000 reads inputs over BACnet/IP only so far, and this "
                                      "device is reached over ") +
                          device::transport_name(d.connection.transport) + ".";
            return plan;
        }

        // Before anything else about the device: if another controller
        // answered for it, the address is that controller's, and a read sent
        // there comes back - correctly framed, the right size, passing every
        // check - with the controller's own points.
        if (d.parent_serial != 0)
        {
            plan.reason = "This device is on the RS485 bus of controller " +
                          std::to_string(d.parent_serial) +
                          ", which answered the scan for it, so the address in the scan is "
                          "that controller's. T3000 reads a device like this through its "
                          "controller, over Modbus; T5000 does not yet. A read sent to that "
                          "address would come back with the controller's inputs under this "
                          "device's serial, so none is sent.";
            return plan;
        }

        plan.decision = device::choose_read_path((int)d.product, d.firmware, kProtocolBacnetIp);
        if (plan.decision.path != device::ReadPath::PrivateData)
        {
            plan.reason = plan.decision.detail +
                          " T5000 does not read Modbus registers yet, so nothing was read.";
            return plan;
        }

        if (d.connection.host.empty())
        {
            plan.reason = "The scan did not report an address for this device, so there is "
                          "nowhere to send the request.";
            return plan;
        }
        if (!bacnet::parse_endpoint(d.connection.host, d.connection.udp_port, plan.endpoint))
        {
            plan.reason = "'" + d.connection.host + "' port " + std::to_string(d.connection.udp_port) +
                          " is not an IPv4 address and port T5000 can send to.";
            return plan;
        }

        // T3000's first read of any device is 64 inputs (BacnetView.cpp:4435),
        // so this matches it. Only afterwards, having read the settings block,
        // does it widen the count for an ESP32 T3 on firmware 63.7 or later
        // (ReInital_Someof_Point, global_function.cpp:17634). That second
        // step needs the settings read, which T5000 does not do yet.
        if (d.product == device::ProductClassId::Esp32T3Series)
        {
            plan.note = "ESP32 T3 controllers on newer firmware can have more than 64 inputs. "
                        "These are the first 64 - what T3000 also reads first - and any "
                        "beyond them are not shown.";
        }

        plan.can_read = true;
        return plan;
    }
}
