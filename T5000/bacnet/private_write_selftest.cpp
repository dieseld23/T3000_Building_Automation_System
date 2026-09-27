// Tests for the private-transfer write codec.
//
// The request bytes are written out by hand from T3000's source, as the read
// tests' are, and conformance/private_transfer_oracle.cpp checks the same
// encoder against what T3000's own stack sends. These are the readable half,
// and the cases the oracle cannot reach: what is refused, and every reply a
// panel might send.

#include <string.h>
#include <string>
#include <vector>

#include "private_transfer.h"
#include "private_write.h"
#include "../testing/check.h"

namespace
{
    using namespace t5000::bacnet;
    using namespace t5000::testing;

    using Bytes = std::vector<uint8_t>;

    // `count` inputs of patterned bytes, each different, so a point copied one
    // byte early or in the wrong order shows.
    Bytes input_points(int count)
    {
        Bytes b((size_t)count * 46);
        for (size_t i = 0; i < b.size(); i++)
            b[i] = (uint8_t)(i * 7 + 1);
        return b;
    }

    WriteRequest inputs(uint8_t first, uint8_t last, const Bytes& entities)
    {
        WriteRequest r;
        r.command  = WriteCommand::Inputs;
        r.first    = first;
        r.last     = last;
        r.entities = entities;
        return r;
    }

    Bytes encode(const WriteRequest& r, uint8_t invoke)
    {
        Bytes out(1500);
        const size_t n = encode_write_request(r, invoke, out.data(), out.size());
        out.resize(n);
        return out;
    }

    // A reply datagram around an APDU: BVLC unicast, NPDU with no reply
    // expected, as a panel on the local network sends it.
    Bytes reply(const Bytes& apdu)
    {
        Bytes d = { 0x81, 0x0A, 0, 0, 0x01, 0x00 };
        d.insert(d.end(), apdu.begin(), apdu.end());
        d[2] = (uint8_t)(d.size() >> 8);
        d[3] = (uint8_t)(d.size() & 0xFF);
        return d;
    }

    bool decodes(const Bytes& d, WriteReply& out)
    {
        std::string why;
        return decode_write_reply(d.data(), d.size(), out, why);
    }

    void test_the_list()
    {
        section("the write whitelist");

        check_eq((long)(sizeof(kAllWriteCommands) / sizeof(kAllWriteCommands[0])), 1,
                 "one write is admitted");
        check(kAllWriteCommands[0] == WriteCommand::Inputs, "  and it is Inputs");
        check_eq(to_wire(WriteCommand::Inputs), 102, "WRITEINPUT_T3000 is 102");
        check(read_back_for(WriteCommand::Inputs) == ReadCommand::Inputs, "read back by READINPUT_T3000");
        check_eq((long)entity_size(WriteCommand::Inputs), 46, "46 bytes per input");
        check_streq(to_string(WriteCommand::Inputs), "inputs", "named for messages");
    }

    void test_one_input()
    {
        section("one input: IN3, invoke 0x2A, Filter 8");

        Bytes point = input_points(1);
        point[34] = 8;
        const Bytes d = encode(inputs(2, 2, point), 0x2A);

        Bytes expected = {
            0x81, 0x0A, 0x00, 0x48,                 // BVLC unicast, 72 bytes
            0x01, 0x04,                             // NPDU, reply expected
            0x00, 0x05, 0x2A, 0x12,                 // confirmed request, invoke, service 18
            0x0A, 0x01, 0x04,                       // [0] vendor 260
            0x19, 0x01,                             // [1] service number 1
            0x2E,                                   // [2] open
            0x65, 0x35,                             // octet string, 53 bytes
            0x35, 0x00, 0x66, 0x02, 0x02, 0x2E, 0x00,   // total 53, 102, 2-2, size 46
        };
        expected.insert(expected.end(), point.begin(), point.end());
        expected.push_back(0x2F);

        check_eq((long)d.size(), 72, "72 bytes");
        check(d == expected, "byte for byte as WritePrivateData builds it");
        check_eq(d.size() > 59 ? d[59] : -1, 8, "  the filter is datagram byte 59");
    }

    void test_lengths()
    {
        section("both octet-string length forms");

        struct Case
        {
            int     count;
            size_t  datagram;
            Bytes   tag;
        };
        const Case cases[] = {
            { 5, 256, { 0x65, 0xED } },
            { 6, 304, { 0x65, 0xFE, 0x01, 0x1B } },
            { 10, 488, { 0x65, 0xFE, 0x01, 0xD3 } },
        };
        for (const Case& c : cases)
        {
            char what[96];
            const Bytes points = input_points(c.count);
            const Bytes d = encode(inputs(0, (uint8_t)(c.count - 1), points), 7);

            snprintf(what, sizeof(what), "%d inputs: %zu bytes", c.count, c.datagram);
            if (!require(d.size() == c.datagram, what))
                continue;

            check(d[2] == (uint8_t)(c.datagram >> 8) && d[3] == (uint8_t)(c.datagram & 0xFF),
                  "  BVLC length is the datagram");
            check(Bytes(d.begin() + 16, d.begin() + 16 + (long)c.tag.size()) == c.tag,
                  "  the octet string's tag and length");

            const size_t header = 16 + c.tag.size();
            const size_t payload = 7 + points.size();
            check(d[header] == (uint8_t)(payload & 0xFF) && d[header + 1] == (uint8_t)(payload >> 8),
                  "  total_length little-endian, header and points");
            check(d[header + 2] == 102 && d[header + 3] == 0 && d[header + 4] == c.count - 1,
                  "  command and range");
            check(d[header + 5] == 46 && d[header + 6] == 0, "  entitysize 46, little-endian");
            check(Bytes(d.begin() + (long)header + 7, d.end() - 1) == points, "  the points, in order");
            check(d.back() == 0x2F, "  [2] closed");
        }

        uint8_t tag[4] = {};
        check_eq((long)detail::put_octet_string_header(253, tag), 2, "253 bytes: short form");
        check(tag[0] == 0x65 && tag[1] == 0xFD, "  65 FD");
        check_eq((long)detail::put_octet_string_header(254, tag), 4, "254 bytes: long form");
        check(tag[0] == 0x65 && tag[1] == 0xFE && tag[2] == 0x00 && tag[3] == 0xFE, "  65 FE 00 FE");
        check_eq((long)detail::put_octet_string_header(4, tag), 0, "under 5: not a form a write needs");
    }

    void test_refusals()
    {
        section("what is not encoded");

        uint8_t buf[1500];

        check_eq((long)encode_write_request(inputs(0, 10, input_points(11)), 1, buf, sizeof(buf)), 0,
                 "11 inputs: 513 bytes, past T3000's 480-byte buffer");
        check_eq((long)encode_write_request(inputs(3, 2, input_points(2)), 1, buf, sizeof(buf)), 0,
                 "a backwards range");
        check_eq((long)encode_write_request(inputs(2, 2, Bytes(45)), 1, buf, sizeof(buf)), 0,
                 "45 bytes for one input");
        check_eq((long)encode_write_request(inputs(2, 3, input_points(1)), 1, buf, sizeof(buf)), 0,
                 "one input's bytes for two");
        check_eq((long)encode_write_request(inputs(2, 2, Bytes()), 1, buf, sizeof(buf)), 0, "no bytes");
        check_eq((long)encode_write_request(inputs(3, 2, Bytes()), 1, buf, sizeof(buf)), 0,
                 "a backwards range with no points, whose size agrees");
        check_eq((long)encode_write_request(inputs(2, 2, input_points(1)), 1, nullptr, 100), 0,
                 "no buffer");

        // One byte short, with canaries: nothing is written at all.
        uint8_t small[72];
        memset(small, 0xCC, sizeof(small));
        check_eq((long)encode_write_request(inputs(2, 2, input_points(1)), 1, small, 71), 0,
                 "capacity 71 for a 72-byte write");
        bool untouched = true;
        for (uint8_t b : small)
            untouched = untouched && b == 0xCC;
        check(untouched, "  and nothing was written into it");

        check_eq((long)encode_write_request(inputs(0, 9, input_points(10)), 1, buf, 488), 488,
                 "ten inputs fit exactly in 488");
    }

    void test_replies()
    {
        section("replies a panel might send to a write");

        WriteReply r;

        // The header echoed, as the Modbus tunnel's write does.
        const Bytes echo = { 0x35, 0x00, 0x66, 0x02, 0x02, 0x2E, 0x00 };
        Bytes apdu = { 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E, 0x65, 0x07 };
        apdu.insert(apdu.end(), echo.begin(), echo.end());
        apdu.push_back(0x2F);
        if (require(decodes(reply(apdu), r), "ComplexACK with the header echoed"))
        {
            check(r.kind == WriteAck::ComplexAck, "  a ComplexAck");
            check_eq(r.invoke_id, 0x2A, "  invoke id");
            check_eq((long)r.vendor_id, 260, "  vendor id");
            check_eq((long)r.service_number, 1, "  service number");
            check(r.body == echo && r.body_is_octet_string, "  the echo kept");
        }

        check(decodes(reply({ 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E, 0x2F }), r) &&
                  r.kind == WriteAck::ComplexAck && r.body.empty() && !r.body_is_octet_string,
              "ComplexACK with an empty [2]");
        check(decodes(reply({ 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01 }), r) &&
                  r.kind == WriteAck::ComplexAckNoResult,
              "ComplexACK with no [2] at all");
        check(decodes(reply({ 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E, 0x21, 0x05, 0x2F }), r) &&
                  r.kind == WriteAck::ComplexAck && !r.body_is_octet_string &&
                  r.body == Bytes({ 0x21, 0x05 }),
              "ComplexACK whose [2] holds something else: kept as it came");
        check(!decodes(reply({ 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E, 0x65, 0x07 }), r),
              "a [2] block that does not close: not an answer");
        check(!decodes(reply({ 0x30, 0x2A, 0x0C, 0x0C, 0x02, 0x00, 0x00, 0x01 }), r),
              "a ComplexACK for ReadProperty: not ours");
        check(!decodes(reply({ 0x30, 0x2A, 0x0F, 0x0A, 0x01, 0x04, 0x19, 0x01 }), r),
              "a ComplexACK for WriteProperty, shaped like ours: not ours");
        check(!decodes(reply({ 0x30, 0x2A, 0x12, 0x19, 0x01 }), r), "no [0] vendorID: not an answer");
        check(!decodes(reply({ 0x30, 0x2A, 0x12, 0xFF, 0x19, 0x01 }), r),
              "a stray byte where [0] vendorID goes: not an answer");
        check(!decodes(reply({ 0x30, 0x2A, 0x12, 0x19, 0x01, 0x19, 0x01 }), r),
              "[1] where [0] goes: the tag number is checked");
        check(!decodes(reply({ 0x30, 0x2A, 0x12, 0x22, 0x01, 0x04, 0x19, 0x01 }), r),
              "an application tag where [0] goes: the context bit is checked");

        check(decodes(reply({ 0x20, 0x2A, 0x12 }), r) && r.kind == WriteAck::SimpleAck && r.invoke_id == 0x2A,
              "SimpleACK for a private transfer");
        check(!decodes(reply({ 0x20, 0x2A, 0x0F }), r), "SimpleACK for WriteProperty: not ours");

        if (require(decodes(reply({ 0x50, 0x2A, 0x12, 0x0E, 0x91, 0x02, 0x91, 0x20, 0x0F }), r),
                    "Error for a private transfer"))
        {
            check(r.kind == WriteAck::Error && r.has_error_codes, "  with its codes");
            check(r.error_class == 2 && r.error_code == 32, "  class 2, code 32");
        }
        check(!decodes(reply({ 0x50, 0x2A, 0x0F, 0x91, 0x02, 0x91, 0x20 }), r),
              "Error for WriteProperty: not ours");
        check(decodes(reply({ 0x50, 0x2A, 0x12, 0x95, 0x05, 1, 2, 3, 4, 5, 0x91, 0x20 }), r) &&
                  r.kind == WriteAck::Error && !r.has_error_codes,
              "Error whose class is five bytes: still an Error, with no codes read");

        check(decodes(reply({ 0x60, 0x2A, 0x04 }), r) && r.kind == WriteAck::Reject && r.reason == 4,
              "Reject, with its reason");
        check(decodes(reply({ 0x71, 0x2A, 0x09 }), r) && r.kind == WriteAck::Abort && r.abort_from_server &&
                  r.reason == 9,
              "Abort from the panel");
        check(decodes(reply({ 0x70, 0x2A, 0x00 }), r) && r.kind == WriteAck::Abort && !r.abort_from_server,
              "Abort from the client side");
        check(decodes(reply({ 0x38, 0x2A, 0x00, 0x01, 0x12 }), r) && r.kind == WriteAck::Segmented,
              "a segmented ComplexACK is named");

        // Our own request, reflected back: not a reply.
        const Bytes request = encode(inputs(2, 2, input_points(1)), 0x2A);
        check(!decodes(request, r), "a write request is not an answer to one");

        // A panel behind a router: SNET 5, SLEN 1, SADR 7.
        Bytes routed = { 0x81, 0x0A, 0, 0, 0x01, 0x08, 0x00, 0x05, 0x01, 0x07, 0x20, 0x2A, 0x12 };
        routed[3] = (uint8_t)routed.size();
        check(decodes(routed, r) && r.kind == WriteAck::SimpleAck, "a routed SimpleACK");

        // Bytes past the BVLC length are not part of the frame.
        Bytes padded = reply({ 0x20, 0x2A, 0x12 });
        padded.push_back(0x99);
        check(decodes(padded, r) && r.kind == WriteAck::SimpleAck, "bytes after the frame are ignored");
    }

    void test_truncation()
    {
        section("every truncated reply is refused, not read past");

        const Bytes whole[] = {
            reply({ 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01, 0x2E, 0x65, 0x07,
                    1, 2, 3, 4, 5, 6, 7, 0x2F }),
            reply({ 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01 }),
            reply({ 0x20, 0x2A, 0x12 }),
            reply({ 0x50, 0x2A, 0x12, 0x0E, 0x91, 0x02, 0x91, 0x20, 0x0F }),
            reply({ 0x60, 0x2A, 0x04 }),
            reply({ 0x71, 0x2A, 0x09 }),
        };

        int refused = 0, tried = 0;
        for (const Bytes& d : whole)
        {
            for (size_t n = 0; n < d.size(); n++)
            {
                WriteReply r;
                std::string why;
                tried++;
                if (!decode_write_reply(n == 0 ? nullptr : d.data(), n, r, why))
                    refused++;
            }
        }
        check_eq(refused, tried, "each cut-short copy is refused");

        // And a frame whose BVLC says it is short, with the rest cut off
        // inside the APDU.
        for (size_t cut = 7; cut < 9; cut++)
        {
            Bytes d = reply({ 0x20, 0x2A, 0x12 });
            d.resize(cut);
            d[3] = (uint8_t)cut;
            WriteReply r;
            std::string why;
            check(!decode_write_reply(d.data(), d.size(), r, why), "a SimpleACK cut inside its APDU");
        }
    }

    void test_reads_stay_strict()
    {
        section("the read decoder is unchanged by the write one");

        Reply r;
        std::string why;

        const Bytes simple = reply({ 0x20, 0x2A, 0x12 });
        check(!decode_reply(simple.data(), simple.size(), r, why), "a read still refuses a SimpleACK");

        const Bytes bare = reply({ 0x30, 0x2A, 0x12, 0x0A, 0x01, 0x04, 0x19, 0x01 });
        check(!decode_reply(bare.data(), bare.size(), r, why), "  and a ComplexACK with no [2]");
        check_streq(why.c_str(), "ComplexACK without the [2] opening tag", "  for the reason it gave before");

        size_t at = 0, end = 0;
        const Bytes local = reply({ 0x20, 0x2A, 0x12 });
        check(locate_apdu(local.data(), local.size(), at, end, why) && at == 6 && end == local.size(),
              "locate_apdu: a local reply's APDU starts at byte 6");

        Bytes forwarded = { 0x81, 0x04, 0, 0, 10, 0, 0, 1, 0xBA, 0xC0, 0x01, 0x00, 0x20, 0x2A, 0x12 };
        forwarded[3] = (uint8_t)forwarded.size();
        check(locate_apdu(forwarded.data(), forwarded.size(), at, end, why) && at == 12,
              "  a forwarded one's at byte 12, past the original source");

        const Bytes network = { 0x81, 0x0A, 0x00, 0x08, 0x01, 0x80, 0x00, 0x00 };
        check(!locate_apdu(network.data(), network.size(), at, end, why), "  a network message has none");
    }
}

int run_private_write_tests()
{
    test_the_list();
    printf("\n");
    test_one_input();
    printf("\n");
    test_lengths();
    printf("\n");
    test_refusals();
    printf("\n");
    test_replies();
    printf("\n");
    test_truncation();
    printf("\n");
    test_reads_stay_strict();
    return 0;
}
