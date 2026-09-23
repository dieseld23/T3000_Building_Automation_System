#include "point_read.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <thread>

#include "../wire/decode.h"

#pragma comment(lib, "ws2_32.lib")

namespace t5000::bacnet
{
    namespace
    {
        // Big enough for any BACnet/IP frame (MAX_MPDU is 1497 for BIP), so a
        // reply is never truncated by the buffer rather than by the sender.
        constexpr int kReceiveBuffer = 2048;

        constexpr uint16_t kBacnetPort      = 47808;
        constexpr int      kBacnetPortTries = 4;       // 47808-47811

        std::string socket_error(const std::string& what)
        {
            return what + " failed (WSA error " + std::to_string(WSAGetLastError()) + ")";
        }

        std::string range_text(const ReadRequest& r)
        {
            return "points " + std::to_string(r.first) + "-" + std::to_string(r.last);
        }

        // ASHRAE 135 clause 21, the values a technician might actually meet.
        // Unknown numbers are still reported, as numbers.
        const char* error_class_name(uint32_t c)
        {
            switch (c)
            {
            case 0: return "device";
            case 1: return "object";
            case 2: return "property";
            case 3: return "resources";
            case 4: return "security";
            case 5: return "services";
            case 6: return "vt";
            case 7: return "communication";
            }
            return "unknown class";
        }

        const char* reject_reason_name(uint8_t r)
        {
            switch (r)
            {
            case 0: return "other";
            case 1: return "buffer overflow";
            case 2: return "inconsistent parameters";
            case 3: return "invalid parameter data type";
            case 4: return "invalid tag";
            case 5: return "missing required parameter";
            case 6: return "parameter out of range";
            case 7: return "too many arguments";
            case 8: return "undefined enumeration";
            case 9: return "unrecognized service";
            }
            return "unknown reason";
        }

        const char* abort_reason_name(uint8_t r)
        {
            switch (r)
            {
            case 0:  return "other";
            case 1:  return "buffer overflow";
            case 2:  return "invalid APDU in this state";
            case 3:  return "preempted by a higher-priority task";
            case 4:  return "segmentation not supported";
            case 5:  return "security error";
            case 6:  return "insufficient security";
            case 7:  return "window size out of range";
            case 8:  return "application exceeded reply time";
            case 9:  return "out of resources";
            case 10: return "transaction timed out";
            case 11: return "APDU too long";
            }
            return "unknown reason";
        }

        // What happened to one request.
        enum class Wait
        {
            Accepted,
            TimedOut,
            Failed,      // outcome.error says why; stop the whole read
        };

        Wait await_reply(ReadTransport& transport, const Endpoint& device,
                         const ReadRequest& request, uint8_t invoke_id,
                         int timeout_ms, ReadOutcome& outcome)
        {
            using clock = std::chrono::steady_clock;
            const auto deadline = clock::now() + std::chrono::milliseconds(timeout_ms);

            uint8_t buffer[kReceiveBuffer];

            for (;;)
            {
                const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                           deadline - clock::now()).count();
                if (remaining <= 0)
                    return Wait::TimedOut;

                Endpoint from;
                std::string error;
                const int n = transport.receive(buffer, sizeof(buffer), (int)remaining, from, error);

                if (n == 0)
                    return Wait::TimedOut;

                if (n == ReadTransport::kPortUnreachable)
                {
                    outcome.error = device.text() + " reported that nothing is listening on UDP port " +
                                    std::to_string(device.port) + ". The device is reachable, but "
                                    "is not answering BACnet on that port - check the port its "
                                    "scan reported, and that it is not a Modbus-only product.";
                    return Wait::Failed;
                }
                if (n < 0)
                {
                    outcome.error = "Receiving the reply failed: " + error;
                    return Wait::Failed;
                }

                // Only the device we asked. Port 47808 carries every BACnet
                // broadcast on the subnet, and none of it is for us.
                if (from.ip != device.ip)
                {
                    outcome.datagrams_ignored++;
                    continue;
                }

                Reply reply;
                std::string why_not;
                if (!decode_reply(buffer, (size_t)n, reply, why_not) ||
                    reply.invoke_id != invoke_id)
                {
                    outcome.datagrams_ignored++;
                    continue;
                }

                switch (reply.kind)
                {
                case ReplyKind::ComplexAck:
                {
                    if (reply.service_choice != kServicePrivateTransfer)
                    {
                        outcome.datagrams_ignored++;
                        continue;
                    }

                    const uint8_t* entities = nullptr;
                    if (!check_read_payload(request, reply.payload, entities, why_not))
                    {
                        outcome.error = "The device answered the request for " +
                                        range_text(request) + ", but " + why_not +
                                        ". Nothing from this read is shown, because a "
                                        "reply that does not match its request cannot be "
                                        "placed on the right points.";
                        return Wait::Failed;
                    }

                    outcome.entities.insert(outcome.entities.end(), entities,
                                            entities + (size_t)request.count() * request.entity_size);
                    outcome.replies_accepted++;
                    return Wait::Accepted;
                }

                case ReplyKind::Error:
                    // T3000 registers no error handler for private transfer
                    // (global_function.cpp:8324-8325 cover ReadProperty and
                    // WriteProperty only), so there this is silence followed by
                    // a timeout. Here it is the answer.
                    outcome.error = "The device refused to read " + range_text(request);
                    if (reply.has_error_codes)
                    {
                        outcome.error += ": error class " + std::to_string(reply.error_class) +
                                         " (" + error_class_name(reply.error_class) +
                                         "), code " + std::to_string(reply.error_code);
                    }
                    outcome.error += ".";
                    return Wait::Failed;

                case ReplyKind::Reject:
                    outcome.error = "The device rejected the request for " + range_text(request) +
                                    " as malformed (" + reject_reason_name(reply.reason) +
                                    "). This points at the request encoding, not the device.";
                    return Wait::Failed;

                case ReplyKind::Abort:
                    outcome.error = std::string("The ") +
                                    (reply.abort_from_server ? "device" : "network") +
                                    " aborted the request for " + range_text(request) + " (" +
                                    abort_reason_name(reply.reason) + ").";
                    return Wait::Failed;

                case ReplyKind::SegmentedAck:
                    outcome.error = "The device sent its answer for " + range_text(request) +
                                    " in segments, although the request said segmentation "
                                    "is not accepted. T5000 does not reassemble segments.";
                    return Wait::Failed;
                }
            }
        }
    }

    std::string Endpoint::text() const
    {
        return std::to_string((ip >> 24) & 0xFF) + "." + std::to_string((ip >> 16) & 0xFF) + "." +
               std::to_string((ip >> 8) & 0xFF) + "." + std::to_string(ip & 0xFF) + ":" +
               std::to_string(port);
    }

    bool parse_endpoint(const std::string& ip, int port, Endpoint& out)
    {
        in_addr a = {};
        if (port <= 0 || port > 65535 || ::inet_pton(AF_INET, ip.c_str(), &a) != 1)
            return false;

        // Addresses that are never one device: unspecified, the limited
        // broadcast, and multicast. A read is a unicast to one controller; a
        // scan response carrying one of these is malformed, not a target.
        const uint32_t host = ntohl(a.s_addr);
        if (host == 0 || host == 0xFFFFFFFFu || (host >> 28) == 0xE)
            return false;

        out.ip   = ntohl(a.s_addr);
        out.port = (uint16_t)port;
        return true;
    }

    ReadOutcome read_entities(ReadTransport& transport, const Endpoint& device,
                              ReadCommand command, int count, int group_size,
                              uint16_t entity_size, const ReadSettings& settings,
                              uint8_t& next_invoke_id)
    {
        ReadOutcome outcome;

        // The point index travels in one byte each way, so a count above 256
        // cannot be asked for - and a group that does not fit one reply is a
        // caller bug, not a device problem.
        if (count <= 0 || count > 256 || group_size <= 0 || entity_size == 0 ||
            settings.attempts <= 0)
        {
            outcome.error = "Internal: a read of " + std::to_string(count) + " points in groups of " +
                            std::to_string(group_size) + " was requested.";
            return outcome;
        }

        outcome.entities.reserve((size_t)count * entity_size);

        for (int first = 0; first < count; first += group_size)
        {
            if (first > 0 && settings.pause_between_requests_ms > 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(settings.pause_between_requests_ms));

            ReadRequest request;
            request.command     = command;
            request.first       = (uint8_t)first;
            request.last        = (uint8_t)((first + group_size < count ? first + group_size : count) - 1);
            request.entity_size = entity_size;

            const uint8_t invoke_id = next_invoke_id++;

            Wait result = Wait::TimedOut;
            for (int attempt = 0; attempt < settings.attempts && result == Wait::TimedOut; attempt++)
            {
                std::string error;
                if (!transport.send_read(request, invoke_id, error))
                {
                    outcome.error = "Sending the request for " + range_text(request) + " failed: " + error;
                    outcome.entities.clear();
                    return outcome;
                }
                outcome.requests_sent++;

                result = await_reply(transport, device, request, invoke_id,
                                     settings.reply_timeout_ms, outcome);
            }

            if (result == Wait::Failed)
            {
                outcome.entities.clear();
                return outcome;
            }

            if (result == Wait::TimedOut)
            {
                outcome.error = "No answer from " + device.text() + " for " + range_text(request) +
                                " after " + std::to_string(settings.attempts) + " attempt" +
                                (settings.attempts == 1 ? "" : "s") + " of " +
                                std::to_string(settings.reply_timeout_ms / 1000.0).substr(0, 3) +
                                " s.";

                // Silence on the very first request and silence halfway
                // through are different problems with different next steps.
                if (outcome.replies_accepted == 0)
                {
                    outcome.error += " Nothing answered at all: the device may have moved "
                                     "or gone offline since the scan, a firewall may be "
                                     "blocking UDP 47808, or it may not support private-data "
                                     "reads over BACnet/IP.";
                }
                else
                {
                    outcome.error += " Earlier requests were answered, so the device stopped "
                                     "responding partway through.";
                }
                outcome.entities.clear();
                return outcome;
            }
        }

        outcome.ok = true;
        return outcome;
    }

    InputsRead read_inputs(ReadTransport& transport, const Endpoint& device,
                           const ReadSettings& settings, uint8_t& next_invoke_id)
    {
        InputsRead result;
        result.transfer = read_entities(transport, device, ReadCommand::Inputs,
                                        kInputCount, kInputsPerRequest,
                                        (uint16_t)wire::kInputPointWireSize,
                                        settings, next_invoke_id);
        if (!result.transfer.ok)
        {
            result.error = result.transfer.error;
            return result;
        }

        result.points.resize(kInputCount);
        for (int i = 0; i < kInputCount; i++)
        {
            const uint8_t* at = result.transfer.entities.data() + (size_t)i * wire::kInputPointWireSize;
            if (!wire::decode_input_point(at, wire::kInputPointWireSize, result.points[i]))
            {
                result.points.clear();
                result.error = "Input " + std::to_string(i) + " could not be decoded.";
                return result;
            }
        }

        result.ok = true;
        return result;
    }

    // --------------------------------------------------------------------------

    UdpReadTransport::UdpReadTransport(const Endpoint& device)
        : m_device(device), m_socket((uintptr_t)INVALID_SOCKET)
    {
    }

    UdpReadTransport::~UdpReadTransport()
    {
        close();
        if (m_winsock_started)
            WSACleanup();
    }

    bool UdpReadTransport::bind_to(uint32_t local_ip, uint16_t local_port, int& wsa_error)
    {
        wsa_error = 0;

        SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
        {
            wsa_error = WSAGetLastError();
            return false;
        }

        sockaddr_in addr = {};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = htonl(local_ip);
        addr.sin_port        = htons(local_port);

        if (::bind(s, (sockaddr*)&addr, sizeof(addr)) != 0)
        {
            wsa_error = WSAGetLastError();
            ::closesocket(s);
            return false;
        }

        sockaddr_in bound = {};
        int bound_len = sizeof(bound);
        ::getsockname(s, (sockaddr*)&bound, &bound_len);

        m_local.ip   = ntohl(bound.sin_addr.s_addr);
        m_local.port = ntohs(bound.sin_port);
        m_socket     = (uintptr_t)s;
        return true;
    }

    bool UdpReadTransport::open(std::string& error)
    {
        WSADATA wsa;
        if (!m_winsock_started)
        {
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            {
                error = "WSAStartup failed";
                return false;
            }
            m_winsock_started = true;
        }

        // Bind the local address that routes to the device, not all of them.
        //
        // T3000 binds a specific address on 47808 (Open_bacnetSocket2,
        // global_function.cpp:8654-8702). Windows lets a wildcard bind on the
        // same port succeed alongside it, and then delivers a datagram
        // addressed to that specific address to T3000's socket, not ours -
        // tested, not assumed. So with T3000 running, a wildcard socket
        // would see nothing while T3000's reply handler, which checks no
        // invoke id, decoded our answers into its own Inputs table. Binding
        // the same specific address instead makes the conflict a failed bind,
        // which moves us on to 47809, as it moves T3000.
        //
        // A connected UDP socket is how to ask the routing table which local
        // address it would use. connect() on UDP sends nothing.
        uint32_t local_ip = INADDR_ANY;
        {
            SOCKET probe = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            if (probe != INVALID_SOCKET)
            {
                sockaddr_in to = {};
                to.sin_family      = AF_INET;
                to.sin_addr.s_addr = htonl(m_device.ip);
                to.sin_port        = htons(m_device.port);
                if (::connect(probe, (sockaddr*)&to, sizeof(to)) == 0)
                {
                    sockaddr_in mine = {};
                    int len = sizeof(mine);
                    if (::getsockname(probe, (sockaddr*)&mine, &len) == 0)
                        local_ip = ntohl(mine.sin_addr.s_addr);
                }
                ::closesocket(probe);
            }
        }
        if (local_ip == INADDR_ANY)
        {
            error = "No network interface on this machine has a route to " + m_device.text() +
                    ". It may be on a network this machine is not connected to.";
            return false;
        }

        int wsa_error = 0;
        for (int i = 0; i < kBacnetPortTries; i++)
        {
            if (bind_to(local_ip, (uint16_t)(kBacnetPort + i), wsa_error))
                return true;

            // Taken is the expected failure, and the reason to try the next
            // one. Anything else will not be fixed by a different port.
            if (wsa_error != WSAEADDRINUSE && wsa_error != WSAEACCES)
            {
                error = "Binding UDP port " + std::to_string(kBacnetPort + i) +
                        " failed (WSA error " + std::to_string(wsa_error) + ")";
                return false;
            }
        }

        Endpoint where;
        where.ip = local_ip;
        const std::string address = where.text().substr(0, where.text().find(':'));
        error = "UDP ports 47808-47811 are all in use on " + address + ". T3000, or another "
                "BACnet tool, is probably running - close it and try again.";
        return false;
    }

    bool UdpReadTransport::open_at(uint32_t local_ip, uint16_t local_port, std::string& error)
    {
        WSADATA wsa;
        if (!m_winsock_started)
        {
            if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
            {
                error = "WSAStartup failed";
                return false;
            }
            m_winsock_started = true;
        }

        int wsa_error = 0;
        if (!bind_to(local_ip, local_port, wsa_error))
        {
            error = "Binding the test socket failed (WSA error " + std::to_string(wsa_error) + ")";
            return false;
        }
        return true;
    }

    void UdpReadTransport::close()
    {
        if ((SOCKET)m_socket != INVALID_SOCKET)
        {
            ::closesocket((SOCKET)m_socket);
            m_socket = (uintptr_t)INVALID_SOCKET;
        }
    }

    bool UdpReadTransport::send_read(const ReadRequest& request, uint8_t invoke_id,
                                     std::string& error)
    {
        if ((SOCKET)m_socket == INVALID_SOCKET)
        {
            error = "the socket is not open";
            return false;
        }

        uint8_t datagram[kReadRequestLength];
        const size_t len = encode_read_request(request, invoke_id, datagram, sizeof(datagram));
        if (len == 0)
        {
            error = "the request could not be encoded";
            return false;
        }

        sockaddr_in to = {};
        to.sin_family      = AF_INET;
        to.sin_addr.s_addr = htonl(m_device.ip);
        to.sin_port        = htons(m_device.port);

        const int sent = ::sendto((SOCKET)m_socket, (const char*)datagram, (int)len, 0,
                                  (sockaddr*)&to, sizeof(to));
        if (sent != (int)len)
        {
            error = socket_error("sending to " + m_device.text());
            return false;
        }
        return true;
    }

    int UdpReadTransport::receive(uint8_t* buffer, int capacity, int timeout_ms,
                                  Endpoint& from, std::string& error)
    {
        if ((SOCKET)m_socket == INVALID_SOCKET)
        {
            error = "the socket is not open";
            return -1;
        }

        fd_set readable;
        FD_ZERO(&readable);
        FD_SET((SOCKET)m_socket, &readable);

        timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        const int ready = ::select(0, &readable, nullptr, nullptr, &tv);
        if (ready == 0)
            return 0;
        if (ready < 0)
        {
            error = socket_error("waiting for a reply");
            return -1;
        }

        sockaddr_in sender = {};
        int sender_len = sizeof(sender);
        const int n = ::recvfrom((SOCKET)m_socket, (char*)buffer, capacity, 0,
                                 (sockaddr*)&sender, &sender_len);

        from.ip   = ntohl(sender.sin_addr.s_addr);
        from.port = ntohs(sender.sin_port);

        if (n == SOCKET_ERROR)
        {
            const int err = WSAGetLastError();

            // Windows reports an ICMP port-unreachable for an earlier sendto
            // as a failed receive on the same socket.
            if (err == WSAECONNRESET)
                return kPortUnreachable;

            // Larger than any BACnet/IP frame. Hand back what fitted; its BVLC
            // length will not match and it will be ignored like any other
            // stranger on the port.
            if (err == WSAEMSGSIZE)
                return capacity;

            error = socket_error("receiving a reply");
            return -1;
        }
        return n;
    }
}
