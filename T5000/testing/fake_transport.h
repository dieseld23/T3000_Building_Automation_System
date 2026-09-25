#pragma once

// A device and a network, scripted, for tests of anything that reads over a
// ReadTransport. Every send is recorded; `respond` decides what arrives in
// reply. An empty inbox times out at once, so tests never wait on a clock.

#include <string.h>

#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "../bacnet/point_read.h"

namespace t5000::testing
{
    using Bytes = std::vector<uint8_t>;

    inline constexpr uint32_t kDeviceIp = 0xC0A80132;    // 192.168.1.50
    inline constexpr uint32_t kOtherIp  = 0xC0A80133;    // 192.168.1.51

    inline bacnet::Endpoint device_at(uint32_t ip = kDeviceIp, uint16_t port = 47808)
    {
        bacnet::Endpoint e;
        e.ip   = ip;
        e.port = port;
        return e;
    }

    // A ComplexACK for a request, carrying `entities` after the Temco header.
    // The header names the range the request asked for unless reply_first
    // says otherwise, and gives the entity size in both of its bytes,
    // little-endian - 400 is 90 01.
    inline Bytes ack(const bacnet::ReadRequest& r, uint8_t invoke, const Bytes& entities,
                     uint8_t reply_first = 0xFF)
    {
        Bytes temco = { 0, 0, bacnet::to_wire(r.command),
                        reply_first == 0xFF ? r.first : reply_first,
                        reply_first == 0xFF ? r.last : (uint8_t)(reply_first + r.count() - 1),
                        (uint8_t)(r.entity_size & 0xFF), (uint8_t)(r.entity_size >> 8) };
        temco.insert(temco.end(), entities.begin(), entities.end());

        Bytes d = { 0x81, 0x0A, 0, 0, 0x01, 0x00, 0x30, invoke, 0x12,
                    0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E, 0x65 };
        if (temco.size() <= 253)
        {
            d.push_back((uint8_t)temco.size());
        }
        else
        {
            d.push_back(254);
            d.push_back((uint8_t)(temco.size() >> 8));
            d.push_back((uint8_t)(temco.size() & 0xFF));
        }
        d.insert(d.end(), temco.begin(), temco.end());
        d.push_back(0x2F);
        d[2] = (uint8_t)(d.size() >> 8);
        d[3] = (uint8_t)(d.size() & 0xFF);
        return d;
    }

    // An APDU with the BVLC and NPDU in front of it.
    inline Bytes framed(Bytes apdu)
    {
        Bytes d = { 0x81, 0x0A, 0, 0, 0x01, 0x00 };
        d.insert(d.end(), apdu.begin(), apdu.end());
        d[2] = (uint8_t)(d.size() >> 8);
        d[3] = (uint8_t)(d.size() & 0xFF);
        return d;
    }

    // A BACnet Error PDU refusing a private transfer: class 2 (property),
    // code 32 (unknown-property), inside the [0] errorType a
    // ConfirmedPrivateTransfer-Error carries - what a device might say to a
    // read it does not support.
    inline Bytes refusal(uint8_t invoke)
    {
        return framed({ 0x50, invoke, 0x12, 0x0E, 0x91, 0x02, 0x91, 0x20, 0x0F });
    }

    class FakeTransport : public bacnet::ReadTransport
    {
    public:
        struct Sent
        {
            bacnet::ReadRequest request;
            uint8_t             invoke_id;
        };

        struct Incoming
        {
            bacnet::Endpoint from;
            Bytes            bytes;
            int              special = 0;   // a negative receive() result instead of bytes
        };

        std::vector<Sent>    sent;
        std::deque<Incoming> inbox;
        uint16_t             port = 0;   // what local_port() reports

        // Called after each send with its index. Push onto `inbox` to reply.
        std::function<void(const Sent&, size_t, FakeTransport&)> respond;

        bool send_read(const bacnet::ReadRequest& request, uint8_t invoke_id, std::string&) override
        {
            sent.push_back({ request, invoke_id });
            if (respond)
                respond(sent.back(), sent.size() - 1, *this);
            return true;
        }

        int receive(uint8_t* buffer, int capacity, int, bacnet::Endpoint& from, std::string& error) override
        {
            if (inbox.empty())
                return 0;

            Incoming in = inbox.front();
            inbox.pop_front();
            if (in.special < 0)
            {
                error = "scripted failure";
                return in.special;
            }
            from = in.from;
            const int n = (int)in.bytes.size() < capacity ? (int)in.bytes.size() : capacity;
            memcpy(buffer, in.bytes.data(), (size_t)n);
            return n;
        }

        void reply(const Bytes& bytes, uint32_t ip = kDeviceIp)
        {
            inbox.push_back({ device_at(ip), bytes, 0 });
        }

        uint16_t local_port() const override { return port; }
    };

    // Short timeouts and no pacing, so a scripted read runs at once.
    inline bacnet::ReadSettings instant()
    {
        bacnet::ReadSettings s;
        s.reply_timeout_ms          = 200;
        s.attempts                  = 2;
        s.pause_between_requests_ms = 0;
        return s;
    }
}
