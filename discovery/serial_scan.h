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
//   - A clean nine-byte reply with a few stray bytes after it, at a single
//     id, makes it stop scanning the port, as if the line ran MS/TP
//     (common.cpp:7406-7407, TStatScanner.cpp:1363-1371). After a five-byte
//     reply, it takes them for a second device (common.cpp:7383-7387).
//     Here both are garbled replies, asked again: a second reply would be
//     five bytes or more.
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

    // A port that can be moved from one rate to the next, for a scan that
    // tries each rate in turn. Still only a scan transport: changing the rate
    // sends nothing.
    class SerialLine : public SerialScanTransport
    {
    public:
        // Sets the port's rate and drops anything already received. False,
        // with the reason, when the port refuses it.
        virtual bool set_rate(int baud, std::string& error) = 0;
    };

    struct SerialScanSettings
    {
        // The port as the registry names it: "COM3", or "CNCA0" for a virtual
        // one. com_port is its number when it is COMn, and 0 otherwise.
        std::string port_name;
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

    // One rate's part of a scan of a port.
    struct RateScan
    {
        int baud = 0;
        SerialScanResult result;
    };

    // A scan of one port at each rate in turn, as T3000's scan list has an
    // entry for each port and rate (m_scan_info, TStatScanner.cpp:596-700) and
    // the owner decided (S2 in the migration plan).
    struct PortScanResult
    {
        std::string port;

        // Each rate tried, in the order tried. A rate is not tried after the
        // scan stopped.
        std::vector<RateScan> rates;

        // Every device found, once. On a real line a device answers at its
        // own rate only; one that answers at several - a virtual line, or an
        // adapter that loops back - is listed at the first, and counted in
        // repeats. A device is known by its serial, or by the id it answered
        // on when it has none: two devices cannot answer one id on a line
        // without colliding.
        std::vector<device::DeviceRecord> devices;
        int repeats = 0;

        // The line runs BACnet MS/TP, heard at mstp_baud. The scan stopped
        // there: a line is one protocol, and the rates after would send
        // Modbus queries into it.
        bool runs_mstp = false;
        int  mstp_baud = 0;

        // Set when the scan stopped because the port failed, or would not
        // take a rate. What was found before that is kept.
        std::string error;

        int frames_sent() const;

        // Rates at which something else was talking, so nothing was sent.
        int busy_rates() const;
    };

    // Scans `line` at each rate in `rates`. The line must already be open.
    // settings.baud is ignored: each rate sets its own.
    PortScanResult scan_serial_port(SerialLine& line, const std::vector<int>& rates,
                                    const SerialScanSettings& settings);

    // What a device found on a serial line becomes in the list: reached over
    // Modbus RTU on the port, at the rate, on the id that answered. The port
    // is settings.port_name, or COMn from settings.com_port when that is
    // empty.
    device::DeviceRecord serial_record(const serial::DeviceIdentity& identity, uint8_t answered_id,
                                       const SerialScanSettings& settings);
}
