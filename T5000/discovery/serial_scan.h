#pragma once

// Finding devices on a serial line - RS485, or a USB adapter to one - without
// changing any of them.
//
// Read-only by shape, as the network scan is. The line below can send a
// serial::ScanFrame and nothing else, and a ScanFrame is the range query or
// the identity read (serial/rtu.h). No write can be expressed, so none can
// be sent.
//
// What T3000 does on the same line, and what is left out:
//
//   - It halves the id range 1-254 until each device answers alone
//     (binarySearchforComDevice, TStatScanner.cpp:1324), and reads registers
//     0-9 from each id it finds (:1432). So does this.
//   - Where two devices answer to one id, it moves one of them by writing
//     register 10 (:1215, :1657). Here the id is reported, and nothing is
//     written.
//   - A device with serial 0 gets a random serial written to it
//     (:1459-1485). Here it gets the AssignSerialNumber repair, as on the
//     network. (T3000 means to do the same for all ones, but tests
//     255*255*255*255, a different number: see device/registry.h.)
//   - A garbled reply at a single id makes it ask the same id again, with no
//     limit (:1596-1604). Here an id is asked a set number of times, then
//     reported.
//   - A reply with a few stray bytes after it, at a single id, makes it stop
//     scanning the port, as if the line ran MS/TP (common.cpp:7406-7407,
//     TStatScanner.cpp:1363-1371). Here it is a garbled reply, asked again.
//
// Before sending anything it listens. A line running BACnet MS/TP is left
// alone, as T3000 leaves it (common.cpp:7334-7341, TStatScanner.cpp:1364).
// So is a line where anything else is talking: another Modbus master already
// polling it would collide with every query sent.

#include <stdint.h>

#include <string>
#include <vector>

#include "../device/registry.h"
#include "../serial/rtu.h"

namespace t5000::discovery
{
    // The only things a serial scan can do to a line.
    class SerialScanTransport
    {
    public:
        virtual ~SerialScanTransport() = default;

        // Sends a scan frame. An empty one is refused.
        virtual bool send(const serial::ScanFrame& frame, std::string& error) = 0;

        // Collects what arrives for up to timeout_ms, up to `capacity` bytes.
        //   > 0  bytes received
        //     0  nothing arrived
        //   < 0  the line failed; `error` says why and the scan stops
        virtual int receive(uint8_t* buffer, int capacity, int timeout_ms, std::string& error) = 0;
    };

    struct SerialScanSettings
    {
        int com_port = 0;
        int baud     = 38400;

        // Listening before the first query.
        int listen_ms = 1500;

        // Waiting for each reply. T3000 waits 100 ms after sending
        // (common.cpp:7280), then reads with a timeout of 360 ms plus 20 for
        // each byte asked for (:5083-5087).
        int reply_ms = 500;

        // How often one question is asked before its answer is given up on.
        // At least once; a scan with 0 is refused.
        int attempts = 3;

        uint8_t lowest  = serial::kLowestId;
        uint8_t highest = serial::kHighestId;
    };

    struct SerialScanStats
    {
        int frames_sent = 0;
        int garbled     = 0;

        // Ids more than one device answers to. Neither can be read, so
        // neither is listed; the page says which ids.
        std::vector<int> shared_ids;

        // Ids where the replies never checked out, and ids that answered the
        // range query but not the identity read.
        std::vector<int> unreadable_ids;
    };

    struct SerialScanResult
    {
        std::vector<device::DeviceRecord> devices;
        SerialScanStats stats;

        // Set when the line was not free, and then nothing was sent:
        // runs_mstp when MS/TP was heard, otherwise someone else talking.
        bool        runs_mstp = false;
        std::string line_busy;

        // Set when the line failed.
        std::string error;
    };

    SerialScanResult scan_serial(SerialScanTransport& line, const SerialScanSettings& settings = {});

    // What a device found on a serial line becomes in the list: reached over
    // Modbus RTU on the port, at the rate, on the id that answered.
    device::DeviceRecord serial_record(const serial::DeviceIdentity& identity, uint8_t answered_id,
                                       const SerialScanSettings& settings);
}
