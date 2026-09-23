#include "read_path.h"

#include <string>

// Both of these are standalone: ProductModel.h has no includes at all, and the
// protocol constants live among plain `const int`s. Using the real headers
// rather than vendoring the numbers means a renumbered product cannot silently
// desynchronise this file from the application.
#include "../../T3000/ProductModel.h"

namespace t5000::device
{
    // Transcribed from T3000/global_define.h:251-267 rather than included,
    // because global_define.h itself is not standalone. These are the only
    // protocol values this decision needs.
    namespace protocol
    {
        constexpr int kModbusRs485          = 0;    // MODBUS_RS485
        constexpr int kMbTcpipToMbRs485     = 13;   // PROTOCOL_MB_TCPIP_TO_MB_RS485
        constexpr int kThirdPartyBacBip     = 253;  // PROTOCOL_THIRD_PARTY_BAC_BIP
    }

    bool is_private_data_device(int product_id)
    {
        return product_id == PM_CM5            ||
               product_id == PM_MINIPANEL      ||
               product_id == PM_MINIPANEL_ARM  ||
               product_id == PM_ESP32_T3_SERIES||
               product_id == PM_TSTAT10;
    }

    bool is_refused_by_private_data(int protocol)
    {
        return protocol == protocol::kModbusRs485      ||
               protocol == protocol::kMbTcpipToMbRs485 ||
               protocol == protocol::kThirdPartyBacBip;
    }

    bool ptp_would_be_enabled(int product_id, int software_version)
    {
        // BacnetView.cpp:7736 - note the ESP32 branch is unconditional and does
        // not consult the firmware version at all.
        if (product_id == PM_ESP32_T3_SERIES)
            return true;

        return is_private_data_device(product_id) &&
               software_version >= kPtpMinimumFirmware;
    }

    Decision choose_read_path(int product_id, int software_version, int protocol)
    {
        Decision d;

        // The product decides first. T3000 opens the private-data view for
        // exactly the five Bacnet_Private_Device products and nothing else
        // (MainFrm.cpp:7375-7380), whatever the transport. This used to ask
        // only whether the transport was refused, so a Tstat8 on BACnet/IP
        // came back as "private data" - a read T3000 has never attempted, and
        // one this tool is now able to send.
        if (!is_private_data_device(product_id))
        {
            d.path    = ReadPath::ModbusRegisters;
            d.summary = "Modbus registers";
            d.detail  = "Product " + std::to_string(product_id) +
                        " does not support private-data reads, so points are read from "
                        "raw Modbus registers.";
            return d;
        }

        if (!is_refused_by_private_data(protocol))
        {
            d.path    = ReadPath::PrivateData;
            d.summary = "private data";
            // Written for the technician reading the page, which is where this
            // lands: the protocol number is kept for diagnosis, not led with.
            d.detail  = "Read by BACnet private transfer, the way T3000 reads this "
                        "product - each point arrives as the struct T3000 stores. "
                        "(Protocol " + std::to_string(protocol) + ".)";
            return d;
        }

        if (ptp_would_be_enabled(product_id, software_version))
        {
            d.path    = ReadPath::PrivateDataOverPtp;
            d.summary = "private data over PTP";
            d.detail  = "Modbus transport " + std::to_string(protocol) +
                        ", but firmware " + std::to_string(software_version) +
                        " supports the PTP tunnel, so points are read as structs "
                        "through it.";
            return d;
        }

        // A private-data product on a refused transport, below the PTP cutoff.
        // Say WHY, because this is the branch where the previous tool would
        // simply have shown nothing: the firmware being a few revisions short
        // is not something a technician can be expected to infer from an
        // empty grid.
        d.path    = ReadPath::ModbusRegisters;
        d.summary = "Modbus registers";
        d.detail  = "This device supports private-data reads, but its firmware is " +
                    std::to_string(software_version) +
                    " and the PTP tunnel needs " +
                    std::to_string(kPtpMinimumFirmware) +
                    " or newer. Falling back to raw Modbus registers. Updating the "
                    "firmware would enable the faster path.";
        return d;
    }
}
