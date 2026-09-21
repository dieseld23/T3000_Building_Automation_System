#pragma once

// Deciding how a given device's points can be read.
//
// There is no single answer, and the answer is not a build-time choice - it
// depends on the product, its firmware and the transport in use at runtime.
// READ_PATH.md in this project has the full derivation and citations; the short
// version is that GetPrivateData_Blocking refuses Modbus transports outright
// unless the PTP tunnel is active, and the tunnel needs firmware >= 525.
//
// This is separated from any actual I/O so it can be tested exhaustively without
// a device. The combinations that matter - a Tstat one firmware revision below
// the cutoff, say - are exactly the ones that are awkward to get hold of.

#include <stdint.h>

namespace t3000::device
{
    enum class ReadPath
    {
        // GetPrivateData_Blocking, over a transport it already accepts.
        PrivateData,

        // GetPrivateData_Blocking, over Modbus via the PTP tunnel.
        PrivateDataOverPtp,

        // Raw Modbus register reads through Read_Multi. Needs a register map;
        // does not yield Str_in_point structs directly.
        ModbusRegisters,
    };

    struct Decision
    {
        ReadPath    path;
        const char* summary;   // short, for a status line
        char        detail[192];   // why, in a sentence a technician can act on
    };

    // The firmware cutoff for the PTP tunnel, from BacnetView.cpp:7736.
    inline constexpr int kPtpMinimumFirmware = 525;

    // Mirrors Bacnet_Private_Device in T3000/global_function.cpp.
    bool is_private_data_device(int product_id);

    // The exact set of transports GetPrivateData_Blocking refuses. Kept as its
    // own function because the guard lists three protocols and it is easy to
    // assume it means "anything Modbus" - MODBUS_TCPIP, for instance, is NOT in
    // the guard.
    bool is_refused_by_private_data(int protocol);

    // Whether BacnetView.cpp:7736 would enable the PTP tunnel for this device.
    bool ptp_would_be_enabled(int product_id, int software_version);

    // software_version is the value BacnetView computes as
    // read_data[5] * 10 + read_data[4].
    Decision choose_read_path(int product_id, int software_version, int protocol);
}
