// Tests for the private-transfer codec.
//
// The request bytes below are written out by hand from the stack's source.
// private_transfer_oracle.cpp is what makes them trustworthy: it checks the
// same encoder against the bytes T3000's own stack emits. These tests are the
// readable half - what each byte is - and the cases the oracle cannot reach:
// every way a reply can be malformed.

#include <string.h>
#include <string>
#include <vector>

#include "private_transfer.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::bacnet;
    using namespace t5000::testing;

    using Bytes = std::vector<uint8_t>;

    ReadRequest inputs(uint8_t first, uint8_t last)
    {
        ReadRequest r;
        r.command     = ReadCommand::Inputs;
        r.first       = first;
        r.last        = last;
        r.entity_size = 46;
        return r;
    }

    // A Temco payload for a request: header, then `count` entities of
    // `entity_size` patterned bytes.
    Bytes temco_payload(uint8_t command, uint8_t first, uint8_t last, int count, int entity_size)
    {
        Bytes p(7 + (size_t)count * entity_size);
        p[0] = (uint8_t)(p.size() & 0xFF);
        p[1] = (uint8_t)(p.size() >> 8);
        p[2] = command;
        p[3] = first;
        p[4] = last;
        p[5] = (uint8_t)entity_size;
        p[6] = 0;
        for (size_t i = 7; i < p.size(); i++)
            p[i] = (uint8_t)(i * 13 + 1);
        return p;
    }

    // The octet string's tag and length in whichever form the length needs.
    Bytes octet_string(const Bytes& contents)
    {
        Bytes out;
        const size_t n = contents.size();
        if (n <= 4)
        {
            out.push_back((uint8_t)(0x60 | n));
        }
        else if (n <= 253)
        {
            out.push_back(0x65);
            out.push_back((uint8_t)n);
        }
        else if (n <= 65535)
        {
            out.push_back(0x65);
            out.push_back(254);
            out.push_back((uint8_t)(n >> 8));
            out.push_back((uint8_t)(n & 0xFF));
        }
        else
        {
            out.push_back(0x65);
            out.push_back(255);
            out.push_back((uint8_t)(n >> 24));
            out.push_back((uint8_t)(n >> 16));
            out.push_back((uint8_t)(n >> 8));
            out.push_back((uint8_t)(n & 0xFF));
        }
        out.insert(out.end(), contents.begin(), contents.end());
        return out;
    }

    // BVLC + NPDU around an APDU, with the BVLC length filled in.
    Bytes frame(const Bytes& apdu, const Bytes& npdu = { 0x01, 0x00 }, uint8_t function = 0x0A,
                const Bytes& forwarded_from = {})
    {
        Bytes d = { 0x81, function, 0, 0 };
        d.insert(d.end(), forwarded_from.begin(), forwarded_from.end());
        d.insert(d.end(), npdu.begin(), npdu.end());
        d.insert(d.end(), apdu.begin(), apdu.end());
        d[2] = (uint8_t)(d.size() >> 8);
        d[3] = (uint8_t)(d.size() & 0xFF);
        return d;
    }

    Bytes complex_ack(uint8_t invoke, const Bytes& payload)
    {
        Bytes a = { 0x30, invoke, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E };
        const Bytes os = octet_string(payload);
        a.insert(a.end(), os.begin(), os.end());
        a.push_back(0x2F);
        return a;
    }

    bool decodes(const Bytes& d, Reply& r)
    {
        std::string why;
        return decode_reply(d.data(), d.size(), r, why);
    }

    // ---------------------------------------------------------------- request

    void test_request_bytes()
    {
        section("the read request, byte by byte");

        uint8_t out[64];
        memset(out, 0xEE, sizeof(out));
        const size_t n = encode_read_request(inputs(0, 9), 0x42, out, sizeof(out));

        const uint8_t expected[] = {
            0x81, 0x0A, 0x00, 0x1A,     // BVLC: BACnet/IP, unicast, 26 bytes
            0x01, 0x04,                 // NPDU: v1, expecting reply
            0x00, 0x05, 0x42, 0x12,     // confirmed, 1476/unsegmented, invoke, PT
            0x0A, 0x01, 0x04,           // [0] vendor 260
            0x19, 0x01,                 // [1] service 1
            0x2E,                       // [2] open
            0x65, 0x07,                 // octet string, 7
            0x07, 0x00,                 //   total_length 7, little-endian
            0x02,                       //   READINPUT_T3000
            0x00, 0x09,                 //   points 0-9
            0x2E, 0x00,                 //   46 bytes each, little-endian
            0x2F,                       // [2] close
        };

        check_eq((long)n, (long)sizeof(expected), "26 bytes");
        check(memcmp(out, expected, sizeof(expected)) == 0, "every byte as traced");
        check(out[sizeof(expected)] == 0xEE, "and nothing written past them");
        check_eq((long)kReadRequestLength, (long)sizeof(expected), "kReadRequestLength agrees");
    }

    void test_request_refusals()
    {
        section("the encoder refuses what it cannot encode honestly");

        uint8_t out[64];
        check_eq((long)encode_read_request(inputs(0, 9), 1, out, kReadRequestLength - 1), 0,
                 "a buffer one byte short");
        check_eq((long)encode_read_request(inputs(9, 0), 1, out, sizeof(out)), 0,
                 "a backwards range");
        check_eq((long)encode_read_request(inputs(0, 9), 1, nullptr, sizeof(out)), 0,
                 "no buffer");
        check_eq((long)encode_read_request(inputs(7, 7), 1, out, sizeof(out)),
                 (long)kReadRequestLength, "a single point is a valid range");
    }

    // ------------------------------------------------------------------ reply

    void test_complex_ack_length_forms()
    {
        section("a ComplexACK in each octet-string length form");

        // 3 bytes (short form); 50 and 253 (one-byte extended, and its last
        // value); 254 (the first that needs the two-byte form); 467 (what every
        // real ten-point Inputs reply is); 1400 (near the APDU limit). The
        // four-byte form is for lengths no BACnet/IP datagram can carry - its
        // BVLC length field is 16 bits - so it is not exercised.
        const size_t sizes[] = { 3, 50, 253, 254, 467, 1400 };
        for (size_t size : sizes)
        {
            Bytes payload(size);
            for (size_t i = 0; i < size; i++)
                payload[i] = (uint8_t)(i ^ 0x5A);

            Reply r;
            const bool ok = decodes(frame(complex_ack(0x33, payload)), r);
            const std::string what = "a " + std::to_string(size) + "-byte payload";
            check(ok, (what + " decodes").c_str());
            check(r.kind == ReplyKind::ComplexAck, (what + ": is an answer").c_str());
            check_eq(r.invoke_id, 0x33, (what + ": invoke id").c_str());
            check_eq((long)r.vendor_id, 260, (what + ": vendor id").c_str());
            check_eq((long)r.service_number, 1, (what + ": service number").c_str());
            check(r.payload == payload, (what + ": payload intact").c_str());
        }
    }

    void test_reply_framings()
    {
        section("replies arrive in more than one framing");

        const Bytes payload = temco_payload(2, 0, 0, 1, 46);

        Reply r;
        check(decodes(frame(complex_ack(1, payload), { 0x01, 0x00 }, 0x04, { 10, 0, 0, 5, 0xBA, 0xC0 }), r) &&
              r.payload == payload,
              "forwarded through a BBMD (BVLC 0x04, six extra bytes)");

        // From behind a router: SNET 0x0005, SLEN 1, SADR 0x17.
        check(decodes(frame(complex_ack(1, payload), { 0x01, 0x08, 0x00, 0x05, 0x01, 0x17 }), r) &&
              r.payload == payload,
              "with a source network and address in the NPDU");

        // Addressed through a router: DNET, DLEN 0, then the hop count.
        check(decodes(frame(complex_ack(1, payload), { 0x01, 0x20, 0xFF, 0xFF, 0x00, 0xFF }), r) &&
              r.payload == payload,
              "with a destination network and hop count in the NPDU");

        // A BVLC length shorter than the datagram: trailing bytes are not part
        // of the frame.
        Bytes padded = frame(complex_ack(1, payload));
        padded.push_back(0x00);
        padded.push_back(0x00);
        check(decodes(padded, r) && r.payload == payload, "with padding after the frame");
    }

    void test_not_replies()
    {
        section("traffic that is not a reply is turned away, with a reason");

        const Bytes good = frame(complex_ack(1, temco_payload(2, 0, 0, 1, 46)));

        struct Case { const char* what; Bytes bytes; };
        std::vector<Case> cases;

        Bytes b = good; b[0] = 0x82;
        cases.push_back({ "not BACnet/IP", b });

        b = good; b[1] = 0x00;
        cases.push_back({ "a BVLC-Result, which carries no NPDU", b });

        b = good; b[3] = (uint8_t)(b[3] + 1);
        cases.push_back({ "a BVLC length longer than what arrived", b });

        b = good; b[4] = 0x02;
        cases.push_back({ "NPDU version 2", b });

        b = good; b[5] = 0x80;
        cases.push_back({ "a network-layer message", b });

        // An I-Am: unconfirmed request, service 0. Port 47808 is full of them.
        cases.push_back({ "an I-Am", frame({ 0x10, 0x00, 0xC4, 0x02, 0x00, 0x00, 0x05 }) });

        // A confirmed request - somebody else asking somebody something.
        cases.push_back({ "a confirmed request", frame({ 0x00, 0x05, 0x01, 0x0C }) });

        b = good; b.resize(b.size() - 20); b[2] = 0; b[3] = (uint8_t)b.size();
        cases.push_back({ "an octet string cut short", b });

        b = good; b.back() = 0x3F; // not the [2] closing tag
        cases.push_back({ "no closing tag", b });

        b = good; b.push_back(0x00); b[3] = (uint8_t)(b[3] + 1);
        cases.push_back({ "bytes after the closing tag, inside the frame", b });

        b = good; b[9 + 6] = 0x45;  // tag 4, not an octet string
        cases.push_back({ "a result block that is not an octet string", b });

        cases.push_back({ "three bytes", { 0x81, 0x0A, 0x00 } });
        cases.push_back({ "nothing", {} });

        for (const Case& c : cases)
        {
            Reply r;
            std::string why;
            const bool accepted = decode_reply(c.bytes.data(), c.bytes.size(), r, why);
            check(!accepted, (std::string("rejects ") + c.what).c_str());
            check(accepted || !why.empty(), (std::string("  and says why: ") + c.what).c_str());
        }
    }

    void test_refusals_from_the_device()
    {
        section("a device saying no is decoded as a no, not ignored");

        Reply r;

        // The private-transfer form: class and code inside [0].
        check(decodes(frame({ 0x50, 0x07, 0x12, 0x0E, 0x91, 0x02, 0x91, 0x20, 0x0F,
                              0x19, 0x01, 0x29, 0x01 }), r),
              "a ConfirmedPrivateTransfer-Error decodes");
        check(r.kind == ReplyKind::Error && r.has_error_codes &&
              r.error_class == 2 && r.error_code == 32 && r.invoke_id == 7,
              "  as error class 2, code 32, invoke 7");

        // The plain form, as any other service would send it.
        check(decodes(frame({ 0x50, 0x08, 0x12, 0x91, 0x05, 0x91, 0x05 }), r) &&
              r.kind == ReplyKind::Error && r.error_class == 5 && r.error_code == 5,
              "a plain Error PDU decodes with its class and code");

        // An error whose codes are unreadable is still an error.
        check(decodes(frame({ 0x50, 0x09, 0x12 }), r) && r.kind == ReplyKind::Error &&
              !r.has_error_codes,
              "an Error with no readable codes is still reported as an error");

        check(decodes(frame({ 0x60, 0x0A, 0x09 }), r) && r.kind == ReplyKind::Reject &&
              r.invoke_id == 0x0A && r.reason == 9,
              "a Reject, with its reason");

        check(decodes(frame({ 0x71, 0x0B, 0x04 }), r) && r.kind == ReplyKind::Abort &&
              r.abort_from_server && r.reason == 4,
              "an Abort from the server, with its reason");

        check(decodes(frame({ 0x70, 0x0C, 0x00 }), r) && r.kind == ReplyKind::Abort &&
              !r.abort_from_server,
              "an Abort not from the server");

        check(decodes(frame({ 0x38, 0x0D, 0x00, 0x04, 0x12 }), r) &&
              r.kind == ReplyKind::SegmentedAck && r.invoke_id == 0x0D,
              "a segmented ComplexACK is recognised, not mistaken for an answer");
    }

    // ---------------------------------------------------------------- payload

    void test_payload_must_answer_the_request()
    {
        section("a payload must answer exactly the request that was sent");

        const ReadRequest req = inputs(10, 19);
        const uint8_t* entities = nullptr;
        std::string why;

        Bytes p = temco_payload(2, 10, 19, 10, 46);
        check(check_read_payload(req, p, entities, why), "the right answer is accepted");
        check(entities == p.data() + 7, "  and the entities start after the 7-byte header");

        // T3000 accepts every one of these (global_function.cpp:4165-4211).
        p = temco_payload(1, 10, 19, 10, 46);
        check(!check_read_payload(req, p, entities, why), "another command's answer");

        p = temco_payload(2, 0, 9, 10, 46);
        check(!check_read_payload(req, p, entities, why),
              "the right size, but for points 0-9 - T3000 would write these onto 0-9");

        p = temco_payload(2, 10, 19, 3, 46);
        check(!check_read_payload(req, p, entities, why),
              "ten points claimed, three carried - T3000 would read past the end");

        p = temco_payload(2, 10, 19, 11, 46);
        check(!check_read_payload(req, p, entities, why), "one point too many");

        p = temco_payload(2, 10, 19, 10, 45);
        check(!check_read_payload(req, p, entities, why), "entities one byte short");

        p = Bytes{ 0x07, 0x00, 0x02, 0x0A, 0x13, 0x2E };
        check(!check_read_payload(req, p, entities, why), "shorter than the header");
        check(entities == nullptr, "  and no entity pointer on failure");

        // Bytes 0-1 and 5-6 are not checked, because T3000 never reads them.
        p = temco_payload(2, 10, 19, 10, 46);
        p[0] = p[1] = p[5] = p[6] = 0xAB;
        check(check_read_payload(req, p, entities, why),
              "header fields T3000 ignores are ignored here too");
    }
}

int run_private_transfer_tests()
{
    test_request_bytes();
    test_request_refusals();
    test_complex_ack_length_forms();
    test_reply_framings();
    test_not_replies();
    test_refusals_from_the_device();
    test_payload_must_answer_the_request();
    return 0;
}
