// Tests for reading points: the chunking, the matching of replies to
// requests, and every way a read can stop - driven by a scripted device, and
// once over a real loopback socket.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <deque>
#include <functional>
#include <string.h>
#include <string>
#include <thread>
#include <vector>

#include "point_read.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::bacnet;
    using namespace t5000::testing;

    using Bytes = std::vector<uint8_t>;

    constexpr uint32_t kDeviceIp = 0xC0A80132;    // 192.168.1.50
    constexpr uint32_t kOtherIp  = 0xC0A80133;    // 192.168.1.51

    Endpoint device_at(uint32_t ip = kDeviceIp, uint16_t port = 47808)
    {
        Endpoint e;
        e.ip   = ip;
        e.port = port;
        return e;
    }

    // One input point on the wire: label "IN<n>", value n*1000 + bias,
    // everything else zero - enough to tell each point from its neighbours,
    // and, with a bias, an impostor's answer from the real one.
    Bytes input_entity(int n, int bias = 0)
    {
        Bytes e(46, 0);
        const std::string label = "IN" + std::to_string(n);
        memcpy(&e[21], label.data(), label.size());
        const int32_t value = n * 1000 + bias;
        e[30] = (uint8_t)(value & 0xFF);
        e[31] = (uint8_t)((value >> 8) & 0xFF);
        e[32] = (uint8_t)((value >> 16) & 0xFF);
        e[33] = (uint8_t)((value >> 24) & 0xFF);
        return e;
    }

    // A correct ComplexACK for a request, as a device would send it.
    Bytes answer(const ReadRequest& r, uint8_t invoke, int carried = -1,
                 uint8_t reply_first = 0xFF, int bias = 0)
    {
        const int n = carried < 0 ? r.count() : carried;

        Bytes temco = { 0, 0, to_wire(r.command),
                        reply_first == 0xFF ? r.first : reply_first,
                        reply_first == 0xFF ? r.last : (uint8_t)(reply_first + r.count() - 1),
                        (uint8_t)r.entity_size, 0 };
        for (int i = 0; i < n; i++)
        {
            const Bytes e = input_entity(r.first + i, bias);
            temco.insert(temco.end(), e.begin(), e.end());
        }

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

    Bytes framed(Bytes apdu)
    {
        Bytes d = { 0x81, 0x0A, 0, 0, 0x01, 0x00 };
        d.insert(d.end(), apdu.begin(), apdu.end());
        d[2] = (uint8_t)(d.size() >> 8);
        d[3] = (uint8_t)(d.size() & 0xFF);
        return d;
    }

    // A device and a network, scripted. Every send is recorded; `respond`
    // decides what arrives in reply. An empty inbox times out at once, so
    // these tests never wait on a clock.
    class FakeTransport : public ReadTransport
    {
    public:
        struct Sent
        {
            ReadRequest request;
            uint8_t     invoke_id;
        };

        struct Incoming
        {
            Endpoint from;
            Bytes    bytes;
            int      special = 0;   // a negative receive() result instead of bytes
        };

        std::vector<Sent>    sent;
        std::deque<Incoming> inbox;
        uint16_t             port = 0;   // what local_port() reports

        // Called after each send with its index. Push onto `inbox` to reply.
        std::function<void(const Sent&, size_t, FakeTransport&)> respond;

        bool send_read(const ReadRequest& request, uint8_t invoke_id, std::string&) override
        {
            sent.push_back({ request, invoke_id });
            if (respond)
                respond(sent.back(), sent.size() - 1, *this);
            return true;
        }

        int receive(uint8_t* buffer, int capacity, int, Endpoint& from, std::string& error) override
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

    ReadSettings instant()
    {
        ReadSettings s;
        s.reply_timeout_ms          = 200;
        s.attempts                  = 2;
        s.pause_between_requests_ms = 0;
        return s;
    }

    // A device that answers everything correctly.
    void well_behaved(const FakeTransport::Sent& s, size_t, FakeTransport& t)
    {
        t.reply(answer(s.request, s.invoke_id));
    }

    // ---------------------------------------------------------------------

    void test_a_whole_read()
    {
        section("all 64 inputs, in T3000's seven requests");

        FakeTransport t;
        t.respond = well_behaved;
        uint8_t invoke = 200;

        const InputsRead r = read_inputs(t, device_at(), instant(), invoke);

        check(r.ok, "the read succeeds");
        if (!require(r.points.size() == 64, "64 points"))
            return;
        check_streq((const char*)r.points[0].label, "IN0", "point 0 is point 0");
        check_streq((const char*)r.points[37].label, "IN37", "point 37 is point 37");
        check_streq((const char*)r.points[63].label, "IN63", "point 63 is point 63");
        check_eq(r.points[63].value, 63000, "and carries its own value");

        if (!require(t.sent.size() == 7, "seven requests"))
            return;
        const int firsts[] = { 0, 10, 20, 30, 40, 50, 60 };
        const int lasts[]  = { 9, 19, 29, 39, 49, 59, 63 };
        bool ranges = true;
        for (int i = 0; i < 7; i++)
            ranges = ranges && t.sent[i].request.first == firsts[i] && t.sent[i].request.last == lasts[i] &&
                     t.sent[i].request.entity_size == 46 && t.sent[i].request.command == ReadCommand::Inputs;
        check(ranges, "0-9, 10-19 ... 50-59, 60-63, all 46-byte inputs");

        check(t.sent[0].invoke_id == 200 && t.sent[6].invoke_id == 206,
              "each request its own invoke id, in order");
        check_eq(invoke, 207, "and the counter is left past the last one used");

        // Wrapping is 255 -> 0, not a skipped or repeated id.
        FakeTransport w;
        w.respond = well_behaved;
        uint8_t near_end = 253;
        read_inputs(w, device_at(), instant(), near_end);
        check(w.sent.size() == 7 && w.sent[2].invoke_id == 255 && w.sent[3].invoke_id == 0,
              "the invoke id wraps from 255 to 0");
    }

    void test_other_traffic_is_ignored()
    {
        section("other traffic on the port is ignored, not mistaken for the answer");

        FakeTransport t;
        t.respond = [](const FakeTransport::Sent& s, size_t, FakeTransport& f) {
            // Another controller's answer with our invoke id, from its address,
            // and with different values - so accepting it is visible in the
            // points, not only in a counter.
            f.reply(answer(s.request, s.invoke_id, -1, 0xFF, 7), kOtherIp);
            // Our device, but an old invoke id - also different values.
            f.reply(answer(s.request, (uint8_t)(s.invoke_id - 1), -1, 0xFF, 3));
            // An I-Am from our device.
            f.reply(framed({ 0x10, 0x00, 0xC4, 0x02, 0x00, 0x00, 0x05 }));
            // Not BACnet at all.
            f.reply({ 0x65, 0x00, 0x40 });
            // And then the real answer.
            f.reply(answer(s.request, s.invoke_id));
        };
        uint8_t invoke = 1;

        const InputsRead r = read_inputs(t, device_at(), instant(), invoke);
        check(r.ok, "the read still succeeds");
        check_eq(r.transfer.datagrams_ignored, 7 * 4, "four strangers per request, all ignored");
        check_eq(r.transfer.replies_accepted, 7, "seven answers accepted");
        bool all_genuine = r.ok;
        for (size_t i = 0; all_genuine && i < r.points.size(); i++)
            all_genuine = r.points[i].value == (int32_t)(i * 1000);
        check(all_genuine, "and every value is the answer's, not an impostor's");
    }

    void test_a_retry_reuses_its_invoke_id()
    {
        section("a lost request is retried once, under the same invoke id");

        FakeTransport t;
        t.respond = [](const FakeTransport::Sent& s, size_t index, FakeTransport& f) {
            if (index == 2)
                return;     // the third request is lost
            f.reply(answer(s.request, s.invoke_id));
        };
        uint8_t invoke = 10;

        const InputsRead r = read_inputs(t, device_at(), instant(), invoke);
        check(r.ok, "the read succeeds after the retry");
        if (!require(t.sent.size() == 8, "eight sends: seven requests and one retry"))
            return;
        check(t.sent[2].request.first == 20 && t.sent[3].request.first == 20,
              "the retry is for the same points");
        check(t.sent[2].invoke_id == t.sent[3].invoke_id,
              "and reuses the invoke id, so a late first answer would still count");
    }

    void test_silence()
    {
        section("a device that never answers");

        FakeTransport t;
        uint8_t invoke = 1;
        const InputsRead r = read_inputs(t, device_at(), instant(), invoke);

        check(!r.ok, "the read fails");
        check(r.points.empty(), "with no points");
        check_eq((long)t.sent.size(), 2, "after exactly the configured two attempts");
        check(r.error.find("Nothing answered at all") != std::string::npos,
              "and says nothing answered, rather than just 'timeout'");
        check(r.error.find("192.168.1.50:47808") != std::string::npos,
              "naming the address that was tried");
        check(r.error.find("went out from UDP") == std::string::npos,
              "and, not knowing the local port, says nothing about it");

        FakeTransport on_47808;
        on_47808.port = 47808;
        invoke = 1;
        const InputsRead usual = read_inputs(on_47808, device_at(), instant(), invoke);
        check(usual.error.find("went out from UDP") == std::string::npos,
              "sent from 47808, the port is not a suspect");
    }

    void test_silence_from_a_fallback_port()
    {
        section("silence, when 47808 was taken and the read went out from 47809");

        // T3000 on 47808 and a device that answers the well-known port rather
        // than the sender's would look exactly like this: nothing arrives.
        // The page should not only blame the device.
        FakeTransport t;
        t.port = 47809;
        uint8_t invoke = 1;
        const InputsRead r = read_inputs(t, device_at(), instant(), invoke);

        check(!r.ok, "the read fails");
        check(r.error.find("Nothing answered at all") != std::string::npos,
              "as silence");
        check(r.error.find("went out from UDP 47809") != std::string::npos,
              "and says which port it went out from");
        check(r.error.find("close it and read again") != std::string::npos,
              "and what to do about it");

        FakeTransport partway;
        partway.port    = 47809;
        partway.respond = [](const FakeTransport::Sent& s, size_t index, FakeTransport& f) {
            if (index < 2)
                f.reply(answer(s.request, s.invoke_id));
        };
        invoke = 1;
        const InputsRead p = read_inputs(partway, device_at(), instant(), invoke);
        check(p.error.find("went out from UDP") == std::string::npos,
              "but not once the device has answered this port - it evidently replies to the sender");
    }

    void test_silence_partway()
    {
        section("a device that stops answering partway through");

        FakeTransport t;
        t.respond = [](const FakeTransport::Sent& s, size_t index, FakeTransport& f) {
            if (index < 3)
                f.reply(answer(s.request, s.invoke_id));
        };
        uint8_t invoke = 1;
        const InputsRead r = read_inputs(t, device_at(), instant(), invoke);

        check(!r.ok, "the read fails");
        check(r.points.empty() && r.transfer.entities.empty(),
              "and shows nothing - not 30 points and 34 blanks");
        check(r.error.find("points 30-39") != std::string::npos,
              "naming the range that went unanswered");
        check(r.error.find("stopped responding partway") != std::string::npos,
              "and that earlier requests were answered");
    }

    void test_the_device_says_no()
    {
        section("a refusal ends the read at once, and is reported as one");

        struct Case { const char* what; Bytes apdu; const char* expect; };
        const Case cases[] = {
            { "an Error",  { 0x50, 0, 0x12, 0x0E, 0x91, 0x02, 0x91, 0x20, 0x0F }, "error class 2 (property), code 32" },
            { "a Reject",  { 0x60, 0, 0x04 },                                    "malformed (invalid tag)" },
            { "an Abort",  { 0x71, 0, 0x09 },                                    "device aborted" },
            { "a segmented answer", { 0x38, 0, 0x00, 0x04, 0x12 },              "in segments" },
        };

        for (const Case& c : cases)
        {
            FakeTransport t;
            t.respond = [&c](const FakeTransport::Sent& s, size_t, FakeTransport& f) {
                Bytes apdu = c.apdu;
                apdu[1] = s.invoke_id;
                f.reply(framed(apdu));
            };
            uint8_t invoke = 1;
            const InputsRead r = read_inputs(t, device_at(), instant(), invoke);

            const std::string what = std::string(c.what) + ": ";
            check(!r.ok, (what + "the read fails").c_str());
            check_eq((long)t.sent.size(), 1, (what + "without a retry - the device answered").c_str());
            check(r.error.find(c.expect) != std::string::npos, (what + "says " + c.expect).c_str());
            if (r.error.find(c.expect) == std::string::npos)
                printf("        got: %s\n", r.error.c_str());
        }
    }

    void test_an_answer_that_does_not_fit()
    {
        section("an answer that does not match its request is refused, not placed");

        {
            FakeTransport t;
            t.respond = [](const FakeTransport::Sent& s, size_t, FakeTransport& f) {
                f.reply(answer(s.request, s.invoke_id, -1, (uint8_t)(s.request.first + 10)));
            };
            uint8_t invoke = 1;
            const InputsRead r = read_inputs(t, device_at(), instant(), invoke);
            check(!r.ok && r.points.empty(), "the right size, for the wrong points");
            check(r.error.find("not the 0-9") != std::string::npos, "  says which were asked for");
        }
        {
            FakeTransport t;
            t.respond = [](const FakeTransport::Sent& s, size_t, FakeTransport& f) {
                f.reply(answer(s.request, s.invoke_id, 3));
            };
            uint8_t invoke = 1;
            const InputsRead r = read_inputs(t, device_at(), instant(), invoke);
            check(!r.ok && r.points.empty(), "ten points promised, three delivered");
        }
    }

    void test_port_unreachable()
    {
        section("nothing listening on the port is its own answer");

        FakeTransport t;
        t.respond = [](const FakeTransport::Sent&, size_t, FakeTransport& f) {
            f.inbox.push_back({ Endpoint(), {}, ReadTransport::kPortUnreachable });
        };
        uint8_t invoke = 1;
        const InputsRead r = read_inputs(t, device_at(), instant(), invoke);
        check(!r.ok, "the read fails");
        check_eq((long)t.sent.size(), 1, "at once, without waiting out a retry");
        check(r.error.find("nothing is listening on UDP port 47808") != std::string::npos,
              "and says so, rather than 'no answer'");
    }

    void test_only_one_device_can_be_a_target()
    {
        section("an address that is never one device is not a target");

        Endpoint e;
        check(!parse_endpoint("0.0.0.0", 47808, e), "0.0.0.0");
        check(!parse_endpoint("255.255.255.255", 47808, e), "the limited broadcast");
        check(!parse_endpoint("239.1.2.3", 47808, e), "multicast");
        check(!parse_endpoint("192.168.1.50", 0, e), "port 0");
        check(!parse_endpoint("192.168.1.50", 70000, e), "a port past 65535");
        check(parse_endpoint("192.168.1.50", 47808, e) && e.ip == 0xC0A80132 && e.port == 47808,
              "while a controller's address parses");
        check(parse_endpoint("127.0.0.1", 47900, e), "and so does loopback, for the tests");
    }

    void test_nonsense_requests_are_refused()
    {
        section("a read the protocol cannot express is refused before anything is sent");

        FakeTransport t;
        uint8_t invoke = 1;
        const ReadOutcome o = read_entities(t, device_at(), ReadCommand::Inputs, 300, 10, 46,
                                            instant(), invoke);
        check(!o.ok && t.sent.empty(), "more points than a one-byte index can address");

        const ReadOutcome z = read_entities(t, device_at(), ReadCommand::Inputs, 64, 0, 46,
                                            instant(), invoke);
        check(!z.ok && t.sent.empty(), "a group size of zero");
    }

    // ------------------------------------------------------------- loopback

    // The real transport against a device on 127.0.0.1, answering from a
    // thread. Proves the socket half: that requests leave, that replies find
    // their way back to the bound port, and that the ICMP answer for a closed
    // port surfaces as that rather than as a timeout.
    void test_over_loopback()
    {
        section("the UDP transport, against a device on loopback");

        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);

        SOCKET dev = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a = {};
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(dev, (sockaddr*)&a, sizeof(a));
        int alen = sizeof(a);
        ::getsockname(dev, (sockaddr*)&a, &alen);
        const uint16_t dev_port = ntohs(a.sin_port);

        std::atomic<int> answered{ 0 };
        std::thread device([&]() {
            for (int i = 0; i < 7; i++)
            {
                fd_set rd;
                FD_ZERO(&rd);
                FD_SET(dev, &rd);
                timeval tv = { 3, 0 };
                if (::select(0, &rd, nullptr, nullptr, &tv) != 1)
                    return;

                uint8_t req[64];
                sockaddr_in from = {};
                int flen = sizeof(from);
                const int n = ::recvfrom(dev, (char*)req, sizeof(req), 0, (sockaddr*)&from, &flen);
                if (n != (int)kReadRequestLength)
                    return;

                // Read the request back out of the datagram the way a device
                // would: invoke id at 8, the Temco header from 18.
                ReadRequest r;
                r.command     = ReadCommand::Inputs;
                r.first       = req[21];
                r.last        = req[22];
                r.entity_size = (uint16_t)(req[23] | (req[24] << 8));

                const Bytes reply = answer(r, req[8]);
                ::sendto(dev, (const char*)reply.data(), (int)reply.size(), 0, (sockaddr*)&from, flen);
                answered++;
            }
        });

        Endpoint target;
        target.ip   = INADDR_LOOPBACK;
        target.port = dev_port;

        UdpReadTransport transport(target);
        std::string error;
        if (require(transport.open_at(INADDR_LOOPBACK, 0, error), "the transport opens on loopback"))
        {
            ReadSettings s;
            s.reply_timeout_ms          = 2000;
            s.attempts                  = 1;
            s.pause_between_requests_ms = 0;
            uint8_t invoke = 1;

            const InputsRead r = read_inputs(transport, target, s, invoke);
            check(r.ok, "all 64 inputs read over a real socket");
            if (!r.ok)
                printf("        %s\n", r.error.c_str());
            check(r.ok && r.points[42].value == 42000, "with the device's values");
        }
        device.join();
        check_eq(answered.load(), 7, "the device saw seven requests");
        ::closesocket(dev);

        // Nothing bound at this port now - Windows reports the ICMP
        // port-unreachable as a failed receive.
        UdpReadTransport closed(target);
        if (require(closed.open_at(INADDR_LOOPBACK, 0, error), "a second transport opens"))
        {
            ReadSettings s;
            s.reply_timeout_ms          = 1000;
            s.attempts                  = 1;
            s.pause_between_requests_ms = 0;
            uint8_t invoke = 1;

            const InputsRead r = read_inputs(closed, target, s, invoke);
            check(!r.ok && r.error.find("nothing is listening") != std::string::npos,
                  "a closed port is reported as a closed port");
            if (r.ok || r.error.find("nothing is listening") == std::string::npos)
                printf("        got: %s\n", r.error.c_str());
        }

        WSACleanup();
    }

    // With T3000 running, a wildcard socket on 47808 binds successfully and
    // then never sees its replies: Windows hands a datagram for a specific
    // address to the socket bound to that address. So the read socket must
    // bind the specific address itself, and move on when it is taken.
    void test_the_read_socket_binds_the_address_that_routes_to_the_device()
    {
        section("the read socket binds the routed address, and moves off a taken port");

        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);

        Endpoint device;
        device.ip   = INADDR_LOOPBACK;
        device.port = 47900;

        {
            UdpReadTransport t(device);
            std::string error;
            if (require(t.open(error), "open() succeeds for a loopback device"))
            {
                check(t.local().ip == INADDR_LOOPBACK,
                      "bound to 127.0.0.1, the address that routes to it - not 0.0.0.0");
                check(t.local().port >= 47808 && t.local().port <= 47811,
                      "on a BACnet port, 47808-47811");
            }
            else
            {
                printf("        %s\n", error.c_str());
            }
        }

        // Hold 127.0.0.1:47808 the way T3000 holds its address. If the hold
        // itself fails, something else has the port - which is the same
        // situation for the check that follows.
        SOCKET t3000 = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        sockaddr_in a = {};
        a.sin_family      = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port        = htons(47808);
        ::bind(t3000, (sockaddr*)&a, sizeof(a));

        {
            UdpReadTransport t(device);
            std::string error;
            if (require(t.open(error), "open() still succeeds with 127.0.0.1:47808 taken"))
            {
                check(t.local().port != 47808,
                      "and does not share the taken port, where its replies would go elsewhere");
                check(t.local_port() == t.local().port,
                      "and reports the port it moved to, so a silent read can say so");
            }
        }

        ::closesocket(t3000);
        WSACleanup();
    }
}

int run_point_read_tests()
{
    test_a_whole_read();
    test_other_traffic_is_ignored();
    test_a_retry_reuses_its_invoke_id();
    test_silence();
    test_silence_from_a_fallback_port();
    test_silence_partway();
    test_the_device_says_no();
    test_an_answer_that_does_not_fit();
    test_port_unreachable();
    test_only_one_device_can_be_a_target();
    test_nonsense_requests_are_refused();
    test_over_loopback();
    test_the_read_socket_binds_the_address_that_routes_to_the_device();
    return 0;
}
