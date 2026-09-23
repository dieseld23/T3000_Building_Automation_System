#pragma once

// Finding devices on a subnet, without changing any of them.
//
// The scan is READ-ONLY, and that is enforced by shape rather than by
// discipline. The transport below can do exactly two things: send the
// five-byte discovery query, and receive. It has no way to express a register
// write at all, so no amount of editing inside Scanner can produce one. The
// closest T3000 equivalent makes nine automatic writes during a scan (see
// device/registry.h); none of them is reachable from here.
//
// Where the scan notices a problem it would like to fix, it records a Repair
// on the device record and moves on. Nothing is sent.
//
// Splitting the transport out also makes the interesting half testable. Real
// sockets need a real subnet with real controllers on it; deciding what a
// response means does not, and that is where the mistakes live.

#include <stdint.h>
#include <string>
#include <vector>

#include "scan_response.h"
#include "../device/registry.h"

namespace t5000::discovery
{
    // The only two operations a scan is allowed.
    //
    // Deliberately not a general "send these bytes" interface. If this could
    // send arbitrary bytes, a future change could make the scanner write to a
    // device and nothing here would object. It cannot, so it cannot.
    class ScanTransport
    {
    public:
        virtual ~ScanTransport() = default;

        // Broadcasts the discovery query. The query is built internally from
        // build_query(); callers do not supply its contents.
        virtual bool broadcast_query(std::string& error) = 0;

        // Waits up to timeout_ms for one datagram.
        //   > 0  bytes received
        //     0  timed out, nothing waiting
        //   < 0  the socket failed; `error` says why and the scan stops
        virtual int receive(uint8_t* buffer, int capacity, int timeout_ms,
                            std::string& error) = 0;
    };

    struct ScanStats
    {
        int datagrams_received = 0;
        int responses_parsed   = 0;

        // Datagrams that were not discovery responses. Not an error: other
        // message types share this socket, and our own broadcast comes back.
        int ignored = 0;

        // Responses that looked like ours but could not be parsed. These ARE
        // worth surfacing - a device answering with something unreadable is a
        // firmware or version problem, not background noise.
        int malformed = 0;

        int in_bootloader = 0;
        int duplicate_modbus_ids = 0;
        int without_serial = 0;
    };

    struct ScanResult
    {
        std::vector<device::DeviceRecord> devices;
        ScanStats stats;

        // Set only when the scan could not run at all. An empty device list
        // with no error is a valid, meaningful answer: nothing is out there.
        std::string error;

        bool ok() const { return error.empty(); }
    };

    struct ScanSettings
    {
        // T3000 waits in 3-second slices and gives up after three of them
        // (TStatScanner.cpp:1961, 1986-1990). Devices answer a broadcast
        // quickly, so most of this is waiting for stragglers.
        int total_timeout_ms = 9000;
        int slice_timeout_ms = 3000;

        // A subnet can hold a lot of controllers. The cap exists so a
        // misbehaving device that answers continuously cannot spin the scan
        // forever; reaching it is reported rather than passed off as the end
        // of the list.
        int max_devices = 512;
    };

    // Runs one scan. Never writes to a device.
    ScanResult scan(ScanTransport& transport, const ScanSettings& settings = {});

    // Turns one parsed response into a device record, including any repairs
    // its contents imply. Exposed because it is the part worth testing.
    device::DeviceRecord to_record(const ScanResponse& response);

    // Duplicate Modbus ids used to be detected here, over one scan's results.
    // They are now found by device::Registry::refresh_duplicate_modbus_ids
    // over the whole known list, because two devices sharing an id can answer
    // on different scans and never appear in the same result. See that
    // function for what the per-scan version missed.

    // The real thing: a broadcast UDP socket. Binds the given local address
    // on kLocalBindPort and broadcasts to kBroadcastPort.
    //
    // local_ip selects which interface to scan from, and matters: a machine
    // with several NICs will otherwise broadcast from whichever one Windows
    // prefers, which is frequently not the one the controllers are on.
    class UdpTransport : public ScanTransport
    {
    public:
        explicit UdpTransport(const std::string& local_ip);
        ~UdpTransport() override;

        UdpTransport(const UdpTransport&) = delete;
        UdpTransport& operator=(const UdpTransport&) = delete;

        bool open(std::string& error);
        void close();

        bool broadcast_query(std::string& error) override;
        int  receive(uint8_t* buffer, int capacity, int timeout_ms,
                     std::string& error) override;

    private:
        std::string m_local_ip;
        uintptr_t   m_socket;    // SOCKET, kept opaque so this header stays clean
        bool        m_winsock_started = false;
    };
}
