#pragma once

// Reading a device's points over BACnet/IP private transfer.
//
// READ-ONLY BY SHAPE, as the scanner is. The transport below can do two
// things: send a read request, which it encodes itself from a ReadRequest
// whose command is a ReadCommand, and receive. There is no operation that
// sends arbitrary bytes, so nothing built on top of this can write to a
// controller however it is edited. Writes will arrive, when they arrive, as a
// separate transport with a separate approval path - not as a widening of
// this one.
//
// The device is addressed directly, by the IP and port its scan response gave.
// T3000 instead sends Who-Is for the device instance up to three times and
// waits for the I-Am to bind an address (BacnetView.cpp:4307). Skipping that
// means no broadcast goes out when a device is read, and the address used is
// the one the operator just saw in the device list. What it gives up is a
// check that the device at that address is still the instance the scan
// found - see the note on identity in read_inputs().

#include <stdint.h>
#include <string>
#include <vector>

#include "private_transfer.h"
#include "../wire/points.h"

namespace t5000::bacnet
{
    struct Endpoint
    {
        uint32_t ip   = 0;    // host byte order
        uint16_t port = 0;

        std::string text() const;
        bool operator==(const Endpoint& o) const { return ip == o.ip && port == o.port; }
    };

    bool parse_endpoint(const std::string& ip, int port, Endpoint& out);

    class ReadTransport
    {
    public:
        virtual ~ReadTransport() = default;

        // Encodes the request with encode_read_request and sends it to the
        // device this transport was opened for. Callers do not supply bytes.
        virtual bool send_read(const ReadRequest& request, uint8_t invoke_id,
                               std::string& error) = 0;

        // Waits up to timeout_ms for one datagram from anyone.
        //   > 0  bytes received, and `from` says who sent them
        //     0  timed out
        //    -1  the socket failed; `error` says why
        //    -2  the device's network stack answered that nothing is
        //        listening on the port (ICMP port unreachable) - a much more
        //        specific answer than a timeout, so it is kept distinct
        static constexpr int kPortUnreachable = -2;
        virtual int receive(uint8_t* buffer, int capacity, int timeout_ms,
                            Endpoint& from, std::string& error) = 0;

        // The local UDP port requests go out from, or 0 when not known.
        virtual uint16_t local_port() const { return 0; }
    };

    struct ReadSettings
    {
        // T3000 polls 300 times at 10 ms for each attempt
        // (global_function.cpp:2387-2389).
        int reply_timeout_ms = 3000;

        // Per request, including the first. T3000 retries far more - its
        // default of 10, times three send attempts, is up to 90 seconds on a
        // single chunk of a device that is not there. This tool answers an
        // HTTP request while it waits, so it gives up sooner and says so.
        int attempts = 2;

        // Between requests. T3000 sleeps SEND_COMMAND_DELAY_TIME, 100 ms,
        // after each chunk (BacnetView.cpp:4445). Kept, because the devices
        // this talks to are small controllers and T3000's pacing is the only
        // pacing they have ever been tested against.
        int pause_between_requests_ms = 100;
    };

    struct ReadOutcome
    {
        bool ok = false;

        // count * entity_size bytes, in point order, when ok.
        std::vector<uint8_t> entities;

        // Why not, in a sentence that says what to check. Empty when ok.
        std::string error;

        int requests_sent     = 0;
        int replies_accepted  = 0;
        int datagrams_ignored = 0;   // other traffic on the port; not an error
    };

    // Reads entities [0, count) in groups of group_size, one request per
    // group, stopping at the first group that fails. All or nothing: a grid
    // with holes in it where some requests timed out looks like a device
    // with blank points, which is a different and false statement. T3000
    // does exactly that - it logs a timed-out chunk and carries on
    // (BacnetView.cpp:4447).
    //
    // next_invoke_id is advanced for each request, so successive reads on
    // one transport do not reuse an id while a late reply to it could still
    // be in flight. A retry reuses its request's id, as BACnet intends: a
    // late answer to the first attempt is then still the right answer.
    ReadOutcome read_entities(ReadTransport& transport, const Endpoint& device,
                              ReadCommand command, int count, int group_size,
                              uint16_t entity_size, const ReadSettings& settings,
                              uint8_t& next_invoke_id);

    // ------------------------------------------------------------ the real one

    class UdpReadTransport : public ReadTransport
    {
    public:
        explicit UdpReadTransport(const Endpoint& device);
        ~UdpReadTransport() override;

        UdpReadTransport(const UdpReadTransport&) = delete;
        UdpReadTransport& operator=(const UdpReadTransport&) = delete;

        // Binds UDP 47808 on the local address that routes to the device -
        // not on all of them - falling back through 47809-47811 when it is
        // taken, which is T3000's own order (global_function.cpp:8121).
        //
        // The specific address is what keeps T5000 and a running T3000 apart:
        // see open() for what a wildcard bind did. 47808 first, because it is
        // the port BACnet devices expect replies to come from, and some
        // firmware answers the well-known port rather than the one the
        // request came from; which kind these controllers are has not been
        // checked against hardware.
        bool open(std::string& error);

        // For tests: bind exactly this address and port (0 for any port).
        bool open_at(uint32_t local_ip, uint16_t local_port, std::string& error);

        void close();

        // The local address actually bound, for the report.
        Endpoint local() const { return m_local; }

        bool send_read(const ReadRequest& request, uint8_t invoke_id,
                       std::string& error) override;
        int  receive(uint8_t* buffer, int capacity, int timeout_ms,
                     Endpoint& from, std::string& error) override;
        uint16_t local_port() const override { return m_local.port; }

    private:
        bool bind_to(uint32_t local_ip, uint16_t local_port, int& wsa_error);

        Endpoint  m_device;
        Endpoint  m_local;
        uintptr_t m_socket;    // SOCKET, kept opaque so this header stays clean
        bool      m_winsock_started = false;
    };

    // ------------------------------------------------------------------ inputs

    // T3000's numbers for an Inputs read: BAC_INPUT_ITEM_COUNT = 64 and
    // BAC_READ_INPUT_GROUP_NUMBER = 10 (global_define.h:441, :388), so seven
    // requests: 0-9, 10-19, ... 60-63.
    inline constexpr int kInputCount      = 64;
    inline constexpr int kInputsPerRequest = 10;

    struct InputsRead
    {
        bool ok = false;
        std::vector<wire::InputPoint> points;   // index is the point number
        std::string error;
        ReadOutcome transfer;                   // the counters, for the report
    };

    // Reads all inputs of the device at `device`.
    //
    // Identity: nothing in an Inputs reply says which controller sent it.
    // The address came from a scan in which the device reported its serial,
    // so a reply from that address is from that device unless it has been
    // replaced or renumbered since - a window the operator controls by
    // rescanning, and which the page states rather than hides.
    InputsRead read_inputs(ReadTransport& transport, const Endpoint& device,
                           const ReadSettings& settings, uint8_t& next_invoke_id);
}
