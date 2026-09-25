#include "scanner.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <map>

#pragma comment(lib, "ws2_32.lib")

namespace t5000::discovery
{
    namespace
    {
        std::string socket_error(const std::string& what)
        {
            return std::string(what) + " failed (WSA error " +
                   std::to_string(WSAGetLastError()) + ")";
        }
    }

    std::string ipv4_text(uint32_t host_order)
    {
        return std::to_string((host_order >> 24) & 0xFF) + "." +
               std::to_string((host_order >> 16) & 0xFF) + "." +
               std::to_string((host_order >> 8) & 0xFF) + "." +
               std::to_string(host_order & 0xFF);
    }

    device::DeviceRecord to_record(const ScanResponse& r, uint32_t sender_ip)
    {
        using namespace t5000::device;

        DeviceRecord d;
        d.serial_number = r.serial_number;
        d.product       = static_cast<ProductClassId>(r.product_id);
        d.firmware      = (int)r.software_version;
        d.provenance    = Provenance::BacnetBroadcast;

        // A scan response is a complete look at the device, so a merge may
        // replace its repair list - including with an empty one when the
        // problem has been fixed since the last scan.
        d.observation_complete = true;

        // It answered, so it is demonstrably there - even if it answered from
        // its bootloader.
        d.reached = true;

        // Two addresses, and the one that answered is the one used. The
        // address a device writes into its response is its own idea of
        // itself; behind NAT, or on a controller with a second interface, it
        // can be an address this machine cannot reach. The address the
        // response came from demonstrably can. T3000 uses that one
        // (TStatScanner.cpp:2032, :2369).
        d.reported_ip   = r.ip_text();
        d.answered_from = sender_ip != 0 ? ipv4_text(sender_ip) : std::string();

        d.connection.transport = device::Transport::BacnetIp;
        d.connection.host      = d.answered_from.empty() ? d.reported_ip : d.answered_from;

        // The port has no such second source. The response came from the
        // device's discovery socket, not its BACnet one, so its source port
        // says nothing about where BACnet is - this is still the device's
        // own claim, NAT or not.
        if (r.bacnet_port != 0)
            d.connection.udp_port = r.bacnet_port;

        // What the list shows is what T5000 will contact. The panel's name
        // has its own field and its own column.
        d.address_note = d.connection.host;
        d.panel_name   = r.panel_name;

        // What it said, before any defaulting. 0 stays 0 here; the
        // Connection above is how we would reach it, which is a different
        // question with a different default.
        d.modbus_id_reported = r.modbus_id;

        // The BACnet device instance. Parsed from the response since the
        // parser was written and dropped here until now, so every scanned
        // device carried 0 - harmless while nothing read it, and wrong the
        // moment anything addressed a device by instance. T3000 uses it as
        // g_bac_instance (MainFrm.cpp:7053). 0 is left as 0: the merge keeps
        // a previously learned instance when a response does not carry one.
        d.connection.device_instance = (int)r.object_instance;

        // Which controller answered for it, if any. A suspect parent has
        // already been zeroed by the parser, as T3000 zeroes it.
        d.parent_serial = r.parent_serial_number;
        // Assigned unconditionally, so a device that reported nothing carries
        // 0 rather than Connection's struct default of 1. A defaulted 1 is a
        // value the device never reported, and it is indistinguishable from a
        // device genuinely on id 1 - which is what made duplicate detection
        // unusable when it read this field. 0 here means "not known", and
        // anything addressing by Modbus has to check.
        d.connection.modbus_slave_id = r.modbus_id;

        // A device with no usable serial cannot be told apart from any other
        // in the same state. T3000 fixes this during the scan without asking;
        // we describe what it would take and wait.
        if (is_uninitialised_serial(r.serial_number))
        {
            Repair repair;
            repair.kind    = RepairKind::AssignSerialNumber;
            repair.problem = "This device reports no serial number (" +
                             std::to_string(r.serial_number) +
                             "), so it cannot be told apart from any other device "
                             "in the same state.";
            repair.action  = "Write a randomly chosen serial number in the range "
                             "200000-300000 to registers 0 and 2, preceded by an "
                             "init code of 142 to register 16.";
            repair.consequence =
                "The device gets a permanent identity. The number is RANDOM, so "
                "two devices repaired within the same second can receive the "
                "same one - repair them one at a time and re-scan between.";

            // Writing a serial is not undoable from this tool: there is no
            // record of what the device had before, because it had nothing.
            repair.reversible = false;
            d.repairs.push_back(repair);
        }

        return d;
    }

    ScanResult scan(ScanTransport& transport, const ScanSettings& settings)
    {
        ScanResult result;

        std::string error;
        if (!transport.broadcast_query(error))
        {
            result.error = error.empty() ? "could not send the discovery query" : error;
            return result;
        }

        uint8_t buffer[1024];

        // Measured against a real clock, not accumulated from the timeouts we
        // asked for.
        //
        // The first version of this only advanced its counter when receive()
        // TIMED OUT, so any datagram arriving before the deadline advanced it
        // by nothing. On a subnet with continuous traffic - broadcast chatter
        // from anything at all, not necessarily our devices - the loop would
        // never reach its deadline and the scan would not end. The device cap
        // does not save it, because ignored and malformed datagrams produce
        // no device.
        const auto started = std::chrono::steady_clock::now();
        const auto elapsed_ms = [&started]() {
            return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - started).count();
        };

        // A second, independent bound. A flood of unparseable traffic would
        // otherwise spin this loop as fast as the socket can deliver until
        // the deadline, which is a hot loop rather than a wait.
        const int max_datagrams = settings.max_devices * 8 + 1024;

        int elapsed = 0;
        while (elapsed < settings.total_timeout_ms)
        {
            if (result.stats.datagrams_received >= max_datagrams)
            {
                result.error = "stopped after " + std::to_string(max_datagrams) +
                               " datagrams without finishing; the list is incomplete";
                break;
            }

            const int slice = settings.slice_timeout_ms < 1 ? 1 : settings.slice_timeout_ms;
            const int left  = settings.total_timeout_ms - elapsed;
            const int wait  = left < slice ? left : slice;

            std::string recv_error;
            uint32_t    sender = 0;
            const int n = transport.receive(buffer, sizeof(buffer), wait, sender, recv_error);
            elapsed = elapsed_ms();

            if (n < 0)
            {
                // A broken socket ends the scan, but whatever was already
                // found is still real and is returned.
                result.error = recv_error.empty() ? "the scan socket failed" : recv_error;
                break;
            }

            if (n == 0)
                continue;   // timed out with nothing waiting; elapsed is already current

            result.stats.datagrams_received++;

            ScanResponse parsed;
            std::string why_not;
            if (!parse_response(buffer, n, parsed, why_not))
            {
                // Distinguish "not for us" from "for us but wrong". The first
                // is ordinary traffic on a shared socket - including our own
                // broadcast - and the second is a device worth asking about.
                if (n > 0 && buffer[0] == kResponseMessage) result.stats.malformed++;
                else                                        result.stats.ignored++;
                continue;
            }

            result.stats.responses_parsed++;
            if (parsed.in_bootloader) result.stats.in_bootloader++;
            if (device::is_uninitialised_serial(parsed.serial_number)) result.stats.without_serial++;

            if ((int)result.devices.size() >= settings.max_devices)
            {
                result.error = "stopped after " + std::to_string(settings.max_devices) +
                               " devices; the list is incomplete";
                break;
            }

            result.devices.push_back(to_record(parsed, sender));
        }

        // stats.duplicate_modbus_ids is deliberately NOT set here. Duplicates
        // are found over the whole device list once these records have been
        // merged into it; a count taken from one scan would miss a pair whose
        // members answered on different scans.

        return result;
    }

    // ---------------------------------------------------------------------

    UdpTransport::UdpTransport(const std::string& local_ip, uint16_t local_port)
        : m_local_ip(local_ip), m_local_port(local_port), m_socket((uintptr_t)INVALID_SOCKET)
    {
    }

    UdpTransport::~UdpTransport()
    {
        close();
        if (m_winsock_started)
            WSACleanup();
    }

    bool UdpTransport::open(std::string& error)
    {
        WSADATA wsa;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        {
            error = "WSAStartup failed";
            return false;
        }
        m_winsock_started = true;

        SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
        {
            error = socket_error("socket");
            return false;
        }

        BOOL broadcast = TRUE;
        if (::setsockopt(s, SOL_SOCKET, SO_BROADCAST,
                         (const char*)&broadcast, sizeof(broadcast)) != 0)
        {
            error = socket_error("enabling broadcast");
            ::closesocket(s);
            return false;
        }

        // Bind the chosen interface explicitly. On a machine with more than
        // one NIC the default choice is frequently not the one the
        // controllers are on, and the symptom is a scan that finds nothing
        // with no error to explain it.
        sockaddr_in bind_addr = {};
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port   = htons(m_local_port);

        if (m_local_ip.empty())
        {
            bind_addr.sin_addr.s_addr = INADDR_ANY;
        }
        else if (::inet_pton(AF_INET, m_local_ip.c_str(), &bind_addr.sin_addr) != 1)
        {
            error = "'" + m_local_ip + "' is not an IPv4 address";
            ::closesocket(s);
            return false;
        }

        if (::bind(s, (sockaddr*)&bind_addr, sizeof(bind_addr)) != 0)
        {
            const int err = WSAGetLastError();
            error = socket_error("binding " +
                                 (m_local_ip.empty() ? std::string("all interfaces")
                                                     : m_local_ip) +
                                 " port " + std::to_string(m_local_port));
            if (err == WSAEADDRINUSE)
                error += " - T3000 may already be running and holding that port";
            else if (err == WSAEADDRNOTAVAIL)
                error += " - no network interface on this machine has that address."
                         " It may have been unplugged, or the address may have changed";
            else if (err == WSAEACCES)
                error += " - permission denied, which usually means a firewall or"
                         " security policy is blocking the broadcast";
            ::closesocket(s);
            return false;
        }

        sockaddr_in bound = {};
        int bound_len = sizeof(bound);
        m_bound_port = ::getsockname(s, (sockaddr*)&bound, &bound_len) == 0
                           ? ntohs(bound.sin_port)
                           : m_local_port;

        m_socket = (uintptr_t)s;
        return true;
    }

    void UdpTransport::close()
    {
        if ((SOCKET)m_socket != INVALID_SOCKET)
        {
            ::closesocket((SOCKET)m_socket);
            m_socket = (uintptr_t)INVALID_SOCKET;
        }
    }

    bool UdpTransport::broadcast_query(std::string& error)
    {
        if ((SOCKET)m_socket == INVALID_SOCKET)
        {
            error = "the scan socket is not open";
            return false;
        }

        uint8_t query[16];
        const int len = build_query(query, sizeof(query));

        sockaddr_in to = {};
        to.sin_family      = AF_INET;
        to.sin_port        = htons(kBroadcastPort);
        to.sin_addr.s_addr = INADDR_BROADCAST;

        const int sent = ::sendto((SOCKET)m_socket, (const char*)query, len, 0,
                                  (sockaddr*)&to, sizeof(to));
        if (sent != len)
        {
            error = socket_error("broadcasting the discovery query");
            return false;
        }
        return true;
    }

    int UdpTransport::receive(uint8_t* buffer, int capacity, int timeout_ms,
                              uint32_t& sender_ip, std::string& error)
    {
        sender_ip = 0;
        if ((SOCKET)m_socket == INVALID_SOCKET)
        {
            error = "the scan socket is not open";
            return -1;
        }

        fd_set readable;
        FD_ZERO(&readable);
        FD_SET((SOCKET)m_socket, &readable);

        timeval tv;
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        const int ready = ::select(0, &readable, nullptr, nullptr, &tv);
        if (ready == 0) return 0;
        if (ready < 0)
        {
            error = socket_error("waiting for a response");
            return -1;
        }

        sockaddr_in from = {};
        int from_len = sizeof(from);
        const int n = ::recvfrom((SOCKET)m_socket, (char*)buffer, capacity, 0,
                                 (sockaddr*)&from, &from_len);
        if (n == SOCKET_ERROR)
        {
            const int err = WSAGetLastError();

            // A datagram larger than the buffer, or an ICMP rejection from a
            // previous send, are both ordinary on a broadcast socket and must
            // not end the scan.
            if (err == WSAEMSGSIZE || err == WSAECONNRESET)
                return 0;

            error = socket_error("receiving a response");
            return -1;
        }

        if (from.sin_family == AF_INET)
            sender_ip = ntohl(from.sin_addr.s_addr);
        return n;
    }
}
