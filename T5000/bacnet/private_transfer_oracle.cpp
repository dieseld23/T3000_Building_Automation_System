// Checks the private-transfer codec against T3000's own BACnet stack.
//
// Every other test of encode_read_request compares it with bytes written out
// by hand from a reading of the stack's source, and a misreading would be
// written out the same way twice. This one asks the stack. It builds a request
// the way GetPrivateData does (T3000/global_function.cpp:2703-2730) - the real
// Str_user_data_header, filled field by field, through Set_transfer_length,
// bacapp_parse_application_data and Send_ConfirmedPrivateTransfer - hands the
// DLL a socket on 127.0.0.1, catches what it sends on a second loopback
// socket, and compares that datagram with ours byte for byte.
//
// Loopback only. Nothing here can reach a network interface, let alone a
// controller.
//
// This is the one file in T5000 that includes the stack's headers, with the
// DLL's own include paths set on this file alone in T5000.vcxproj. It uses
// them so that every structure crossing into the DLL has the layout the DLL
// was compiled with; bacnet_link.cpp's hand-written prototypes were fine for a
// call with no arguments and would not be for these.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "bacdef.h"
#include "bacstr.h"
#include "bacapp.h"
#include "bacdcode.h"
#include "ptransfer.h"
#include "client.h"
#include "bip.h"
#include "datalink.h"

#include "../wire/cm5_header.h"
#include "private_transfer.h"
#include "../testing/check.h"

#pragma comment(lib, "ws2_32.lib")

namespace
{
    using namespace t5000::bacnet;
    using namespace t5000::testing;

    // The layouts the DLL was built with, checked here because this is where
    // they cross. A mismatch would corrupt the stack's state rather than fail.
    static_assert(sizeof(BACNET_ADDRESS) == 18, "BACNET_ADDRESS layout differs from the DLL's");
    static_assert(sizeof(BACNET_PRIVATE_TRANSFER_DATA) == 16,
                  "BACNET_PRIVATE_TRANSFER_DATA layout differs from the DLL's");
    static_assert(sizeof(Str_user_data_header) == kTemcoHeaderLength,
                  "Str_user_data_header is not the 7 bytes PRIVATE_HEAD_LENGTH says");
    static_assert(BACNET_VENDOR_ID == kVendorId,
                  "kVendorId is not the BACNET_VENDOR_ID the stack is built with");
    static_assert(PRIVATE_HEAD_LENGTH == kTemcoHeaderLength, "PRIVATE_HEAD_LENGTH moved");

    std::string hex(const uint8_t* p, size_t n)
    {
        std::string s;
        char b[4];
        for (size_t i = 0; i < n; i++)
        {
            snprintf(b, sizeof(b), "%02X ", p[i]);
            s += b;
        }
        return s;
    }

    SOCKET loopback_socket(uint16_t& port)
    {
        SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == INVALID_SOCKET)
            return s;

        sockaddr_in a = {};
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port        = 0;
        if (::bind(s, (sockaddr*)&a, sizeof(a)) != 0)
        {
            ::closesocket(s);
            return INVALID_SOCKET;
        }
        int len = sizeof(a);
        ::getsockname(s, (sockaddr*)&a, &len);
        port = ntohs(a.sin_port);
        return s;
    }

    int receive_one(SOCKET s, uint8_t* buffer, int capacity)
    {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(s, &readable);
        timeval tv = { 2, 0 };
        if (::select(0, &readable, nullptr, nullptr, &tv) != 1)
            return 0;
        return ::recvfrom(s, (char*)buffer, capacity, 0, nullptr, nullptr);
    }

    // GetPrivateData, global_function.cpp:2703-2730, minus the address lookup:
    // the destination is handed in, as address_get_by_device would have.
    int send_like_t3000(BACNET_ADDRESS& dest, uint8_t command, uint8_t start,
                        uint8_t end, int16_t entitysize)
    {
        Str_user_data_header private_data_chunk;
        private_data_chunk.total_length         = PRIVATE_HEAD_LENGTH;
        private_data_chunk.command              = command;
        private_data_chunk.point_start_instance = start;
        private_data_chunk.point_end_instance   = end;
        private_data_chunk.entitysize           = entitysize;

        // Large and static: BACNET_APPLICATION_DATA_VALUE is a union sized for
        // a 1470-byte octet string, which does not belong on this stack frame
        // twice.
        static BACNET_APPLICATION_DATA_VALUE data_value;
        memset(&data_value, 0, sizeof(data_value));
        uint8_t test_value[480] = { 0 };

        Set_transfer_length(PRIVATE_HEAD_LENGTH);
        if (!bacapp_parse_application_data(BACNET_APPLICATION_TAG_OCTET_STRING,
                                           (char*)&private_data_chunk, &data_value))
            return -100;

        BACNET_PRIVATE_TRANSFER_DATA private_data = { 0 };
        private_data.vendorID             = BACNET_VENDOR_ID;
        private_data.serviceNumber        = 1;
        private_data.serviceParameters    = &test_value[0];
        private_data.serviceParametersLen = bacapp_encode_application_data(&test_value[0], &data_value);

        return Send_ConfirmedPrivateTransfer(&dest, &private_data);
    }

    void test_request_matches_the_stack()
    {
        section("a read request is byte-identical to what T3000's BACnet stack sends");

        uint16_t device_port = 0, stack_port = 0;
        SOCKET device = loopback_socket(device_port);
        SOCKET stack  = loopback_socket(stack_port);
        if (!require(device != INVALID_SOCKET && stack != INVALID_SOCKET,
                     "two loopback sockets could be opened"))
        {
            if (device != INVALID_SOCKET) ::closesocket(device);
            if (stack != INVALID_SOCKET) ::closesocket(stack);
            return;
        }

        // What Initial_bac does for BACnet/IP (global_function.cpp:8138-8193),
        // pointed at loopback. bip.h takes these in network byte order.
        set_datalink_protocol(3);   // PROTOCOL_BACNET_IP
        bip_set_socket((int)stack);
        bip_set_addr(htonl(INADDR_LOOPBACK));
        bip_set_port(htons(stack_port));

        // A BACnet/IP address is the IP then the port, both in network order
        // (bip.c:158-159).
        BACNET_ADDRESS dest = {};
        dest.mac_len = 6;
        const uint32_t ip_n   = htonl(INADDR_LOOPBACK);
        const uint16_t port_n = htons(device_port);
        memcpy(&dest.mac[0], &ip_n, 4);
        memcpy(&dest.mac[4], &port_n, 2);
        dest.net = 0;
        dest.len = 0;

        struct Case { uint8_t first, last; };
        const Case cases[] = { { 0, 9 }, { 10, 19 }, { 60, 63 }, { 5, 5 } };

        for (const Case& c : cases)
        {
            char label[96];
            snprintf(label, sizeof(label), "points %d-%d", c.first, c.last);

            const int invoke = send_like_t3000(dest, READINPUT_T3000, c.first, c.last,
                                               (int16_t)sizeof(Str_in_point));
            if (!require(invoke >= 0, "Send_ConfirmedPrivateTransfer accepted the request"))
            {
                printf("        %s: it returned %d\n", label, invoke);
                continue;
            }

            uint8_t theirs[1500];
            const int n = receive_one(device, theirs, sizeof(theirs));
            if (!require(n > 0, "the stack's datagram arrived on loopback"))
                continue;

            ReadRequest request;
            request.command     = ReadCommand::Inputs;
            request.first       = c.first;
            request.last        = c.last;
            request.entity_size = (uint16_t)sizeof(Str_in_point);

            uint8_t ours[kReadRequestLength];
            const size_t len = encode_read_request(request, (uint8_t)invoke, ours, sizeof(ours));

            check_eq((long)len, (long)n, "same length as the stack's datagram");
            const bool same = (len == (size_t)n) && memcmp(ours, theirs, len) == 0;
            check(same, "same bytes as the stack's datagram");
            if (!same)
            {
                printf("        %s\n        stack: %s\n        ours:  %s\n", label,
                       hex(theirs, (size_t)n).c_str(), hex(ours, len).c_str());
            }
        }

        // Leave the stack as it was found: no socket.
        bip_set_socket(-1);
        ::closesocket(device);
        ::closesocket(stack);
    }

    // The reply side has no exported encoder to compare with, so the check runs
    // the other way: one reply, decoded by both, must yield the same bytes.
    // This is T3000's decode path - ptransfer_decode_service_request, then
    // decode_tag_number_and_value and decode_octet_string as
    // Bacnet_PrivateData_Handle calls them (global_function.cpp:3361-3374).
    //
    // Ten inputs is 467 bytes, which forces the octet string's length into its
    // two-byte extended form (65 FE 01 D3) - the case most worth a second
    // opinion, and the one every real Inputs reply takes.
    void test_reply_decodes_the_same_as_the_stack()
    {
        section("a reply decodes to the same payload as T3000's decode path");

        const size_t entities = 10 * sizeof(Str_in_point);
        std::vector<uint8_t> temco(kTemcoHeaderLength + entities);
        temco[0] = (uint8_t)(temco.size() & 0xFF);
        temco[1] = (uint8_t)(temco.size() >> 8);
        temco[2] = READINPUT_T3000;
        temco[3] = 0;
        temco[4] = 9;
        temco[5] = (uint8_t)sizeof(Str_in_point);
        temco[6] = 0;
        for (size_t i = kTemcoHeaderLength; i < temco.size(); i++)
            temco[i] = (uint8_t)(i * 7 + 3);

        // The service part: [0] vendor, [1] service, [2] { octet string }.
        std::vector<uint8_t> service = { 0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E, 0x65, 0xFE,
                                         (uint8_t)(temco.size() >> 8),
                                         (uint8_t)(temco.size() & 0xFF) };
        service.insert(service.end(), temco.begin(), temco.end());
        service.push_back(0x2F);

        // Theirs.
        BACNET_PRIVATE_TRANSFER_DATA data = { 0 };
        const int dlen = ptransfer_decode_service_request(service.data(),
                                                          (unsigned)service.size(), &data);
        if (!require(dlen >= 0, "the stack decodes the service part"))
            return;

        uint8_t  tag_number = 0;
        uint32_t len_value  = 0;
        const int tag_len = decode_tag_number_and_value(data.serviceParameters, &tag_number, &len_value);
        check_eq(tag_number, BACNET_APPLICATION_TAG_OCTET_STRING, "the stack sees an octet string");

        static BACNET_OCTET_STRING theirs;
        memset(&theirs, 0, sizeof(theirs));
        decode_octet_string(data.serviceParameters + tag_len, len_value, &theirs);
        check_eq((long)theirs.length, (long)temco.size(), "the stack's octet string length");

        // Ours, from the whole datagram.
        std::vector<uint8_t> datagram = { 0x81, 0x0A, 0, 0, 0x01, 0x00, 0x30, 0x42, 0x12 };
        datagram.insert(datagram.end(), service.begin(), service.end());
        datagram[2] = (uint8_t)(datagram.size() >> 8);
        datagram[3] = (uint8_t)(datagram.size() & 0xFF);

        Reply reply;
        std::string why;
        if (!require(decode_reply(datagram.data(), datagram.size(), reply, why),
                     "our decoder accepts the same reply"))
        {
            printf("        %s\n", why.c_str());
            return;
        }

        check(reply.payload.size() == theirs.length &&
              memcmp(reply.payload.data(), theirs.value, theirs.length) == 0,
              "both decoders extract byte-identical payloads");
        check_eq((long)data.vendorID, (long)reply.vendor_id, "and the same vendor id");
        check_eq((long)data.serviceNumber, (long)reply.service_number, "and the same service number");
    }
}

int run_private_transfer_oracle_tests()
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        check(false, "WSAStartup for the oracle tests");
        return 0;
    }

    test_request_matches_the_stack();
    printf("\n");
    test_reply_decodes_the_same_as_the_stack();

    WSACleanup();
    return 0;
}
