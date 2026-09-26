// Tests for the Modbus RTU a serial scan speaks: the two frames it can send,
// and how it reads the replies.
//
// The CRC is pinned to two vectors that do not come from this code: the
// standard's check value, and a well-known read frame. The frames after that
// are spelled out byte by byte, so a change of order or of CRC shows up here
// as the bytes that moved.

#include "rtu.h"
#include "../testing/check.h"

#include <string.h>

#include <type_traits>

namespace
{
    using namespace t5000::serial;
    using namespace t5000::testing;
    using Bytes = std::vector<uint8_t>;

    // A ScanFrame can only be made by its two builders.
    static_assert(!std::is_default_constructible_v<ScanFrame>, "a ScanFrame cannot be made empty and filled in");
    static_assert(!std::is_constructible_v<ScanFrame, Bytes>, "a ScanFrame cannot be made from bytes");
    static_assert(!std::is_constructible_v<ScanFrame, const uint8_t*, size_t>,
                  "a ScanFrame cannot be made from bytes");

    Bytes with_crc(Bytes b)
    {
        const uint16_t crc = crc16(b.data(), b.size());
        b.push_back((uint8_t)(crc & 0xFF));
        b.push_back((uint8_t)(crc >> 8));
        return b;
    }

    RangeReply decode(const Bytes& reply, const ScanFrame& query)
    {
        return decode_range_reply(reply.data(), reply.size(), query);
    }

    // The reply to a read of registers 0-9 from `id`, with the registers given.
    Bytes identity_reply(uint8_t id, const uint16_t (&reg)[10])
    {
        Bytes b = { id, 0x03, 20 };
        for (uint16_t r : reg)
        {
            b.push_back((uint8_t)(r >> 8));
            b.push_back((uint8_t)(r & 0xFF));
        }
        return with_crc(b);
    }

    void test_the_crc_is_crc16_modbus()
    {
        section("the CRC is CRC-16/MODBUS");

        const uint8_t check_string[] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
        check_eq(crc16(check_string, sizeof(check_string)), 0x4B37, "the standard's check value, for \"123456789\"");

        const uint8_t read[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A };
        check_eq(crc16(read, sizeof(read)), 0xCDC5, "01 03 00 00 00 0A is followed by C5 CD");

        check_eq(crc16(read, 0), 0xFFFF, "nothing leaves the starting value");
    }

    void test_the_identity_read_is_registers_0_to_9()
    {
        section("the identity read is function 3, registers 0-9, CRC low byte first");

        const Bytes id1 = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xC5, 0xCD };
        const ScanFrame one = ScanFrame::identity_read(1);
        check(one.bytes() == id1, "id 1 is 01 03 00 00 00 0A C5 CD");
        check_eq(one.lo(), 1, "and asks id 1");
        check_eq(one.hi(), 1, "and only id 1");

        const Bytes id254 = { 0xFE, 0x03, 0x00, 0x00, 0x00, 0x0A, 0xD1, 0xC2 };
        check(ScanFrame::identity_read(254).bytes() == id254, "id 254 is FE 03 00 00 00 0A D1 C2");

        check(ScanFrame::identity_read(0).empty(), "id 0 is refused: it is not a device");
        check(ScanFrame::identity_read(255).empty(), "id 255 is refused: every device would answer");
        check_eq(ScanFrame::identity_read(0).lo(), 0, "a refused frame asks no id");
    }

    void test_the_range_query_is_hi_before_lo()
    {
        section("the range query is FF 19, then hi before lo");

        const Bytes all = { 0xFF, 0x19, 0xFE, 0x01, 0x60, 0x57 };
        const ScanFrame q = ScanFrame::range_query(1, 254);
        check(q.bytes() == all, "1-254 is FF 19 FE 01 60 57");
        check_eq(q.lo(), 1, "and asks from 1");
        check_eq(q.hi(), 254, "to 254");

        const Bytes twelve = { 0xFF, 0x19, 0x0C, 0x0C, 0xE4, 0xF2 };
        check(ScanFrame::range_query(12, 12).bytes() == twelve, "12-12 is FF 19 0C 0C E4 F2");

        check(ScanFrame::range_query(0, 5).empty(), "a range from 0 is refused");
        check(ScanFrame::range_query(1, 255).empty(), "a range to 255 is refused");
        check(ScanFrame::range_query(9, 8).empty(), "a range upside down is refused");
        check(!ScanFrame::range_query(254, 254).empty(), "254 alone is allowed");
    }

    void test_nobody_answering()
    {
        section("nothing, zeros, or the query echoed back, is nobody");

        const ScanFrame q = ScanFrame::range_query(1, 254);
        check(decode({}, q).answer == RangeAnswer::Nobody, "no bytes");
        check(decode(Bytes(13, 0), q).answer == RangeAnswer::Nobody, "thirteen zeros");
        check(decode(q.bytes(), q).answer == RangeAnswer::Nobody,
              "the query itself, as a port with nothing on it can hand back");
    }

    void test_one_device_answering()
    {
        section("one device answers in five bytes or nine");

        const ScanFrame q = ScanFrame::range_query(1, 254);

        const Bytes five = { 0xFF, 0x19, 0x0C, 0x4A, 0x65 };
        RangeReply r = decode(five, q);
        check(r.answer == RangeAnswer::One, "FF 19 0C and its CRC, from older firmware");
        check_eq(r.id, 12, "is id 12");

        const Bytes nine = { 0xFF, 0x19, 0x0C, 0x39, 0x30, 0x00, 0x00, 0x0A, 0x94 };
        r = decode(nine, q);
        check(r.answer == RangeAnswer::One, "FF 19 0C, four more bytes, and a CRC over seven");
        check_eq(r.id, 12, "is id 12");

        r = decode(nine, ScanFrame::range_query(12, 12));
        check(r.answer == RangeAnswer::One, "an id at both ends of the range is in it");
        check_eq(r.id, 12, "and is the id");
    }

    void test_replies_that_do_not_check_out()
    {
        section("a reply that does not check out is garbled");

        const ScanFrame q = ScanFrame::range_query(1, 254);

        Bytes nine = { 0xFF, 0x19, 0x0C, 0x39, 0x30, 0x00, 0x00, 0x0A, 0x94 };
        nine[8] ^= 0x01;
        check(decode(nine, q).answer == RangeAnswer::Garbled, "a nine-byte reply with its CRC wrong");

        Bytes five = { 0xFF, 0x19, 0x0C, 0x4A, 0x65 };
        five[2] = 0x0D;
        check(decode(five, q).answer == RangeAnswer::Garbled, "a five-byte reply with its id changed");

        check(decode(with_crc({ 0xFF, 0x18, 0x0C }), q).answer == RangeAnswer::Garbled,
              "a reply to some other function");
        check(decode(with_crc({ 0xFE, 0x19, 0x0C }), q).answer == RangeAnswer::Garbled,
              "a reply from some other address");
        check(decode({ 0xFF, 0x19, 0x0C }, q).answer == RangeAnswer::Garbled, "a reply cut short");

        // No device with an id outside the range should have answered.
        const RangeReply r = decode(with_crc({ 0xFF, 0x19, 0x0C }), ScanFrame::range_query(1, 10));
        check(r.answer == RangeAnswer::Garbled, "id 12 answering a query for 1-10");
        check_eq(r.id, 0, "and gives no id");
        check(decode(with_crc({ 0xFF, 0x19, 0x0C }), ScanFrame::range_query(13, 20)).answer == RangeAnswer::Garbled,
              "id 12 answering a query for 13-20");
    }

    void test_more_than_one_device_answering()
    {
        section("more than one reply's worth is several devices");

        const ScanFrame q = ScanFrame::range_query(1, 254);
        const Bytes a = with_crc({ 0xFF, 0x19, 0x0C, 1, 2, 3, 4 });
        const Bytes b = with_crc({ 0xFF, 0x19, 0x0D, 5, 6, 7, 8 });

        Bytes two = a;
        two.insert(two.end(), b.begin(), b.end());
        check(decode(two, q).answer == RangeAnswer::Several, "two nine-byte replies, one after the other");

        const Bytes a5 = with_crc({ 0xFF, 0x19, 0x0C });
        const Bytes b5 = with_crc({ 0xFF, 0x19, 0x0D });
        Bytes two5 = a5;
        two5.insert(two5.end(), b5.begin(), b5.end());
        check(decode(two5, q).answer == RangeAnswer::Several, "two five-byte replies, one after the other");

        Bytes ten = a;
        ten.push_back(0x33);
        check(decode(ten, q).answer == RangeAnswer::Several, "a byte past a nine-byte reply");

        Bytes six = a5;
        six.push_back(0x33);
        check(decode(six, q).answer == RangeAnswer::Several, "a byte past a five-byte reply");
        Bytes seven = a5;
        seven.push_back(0x00);
        seven.push_back(0x33);
        check(decode(seven, q).answer == RangeAnswer::Several, "or two");

        // T3000 judges the first 13 bytes. Anything after them is more than
        // one device sends, however clean the start looks.
        Bytes fourteen = a;
        fourteen.resize(13, 0);
        fourteen.push_back(0x33);
        check(decode(fourteen, q).answer == RangeAnswer::Several, "a byte after thirteen");
        Bytes thirteen = a;
        thirteen.resize(13, 0);
        check(decode(thirteen, q).answer == RangeAnswer::One, "where thirteen, padded with zeros, is one");

        check(decode(Bytes(14, 0x11), q).answer == RangeAnswer::Several, "fourteen bytes of anything");
    }

    void test_mstp_frames()
    {
        section("MS/TP preambles eight bytes apart");

        const Bytes tokens = { 0x55, 0xFF, 0x00, 0x02, 0x01, 0x00, 0x00, 0x7C,
                               0x55, 0xFF, 0x00, 0x03, 0x02, 0x00, 0x00, 0x3B };
        check(has_mstp_preambles(tokens.data(), tokens.size()), "two token frames");
        check(has_mstp_preambles(tokens.data(), 10), "a preamble, and another eight bytes on");
        check(!has_mstp_preambles(tokens.data(), 9), "not when the second is cut short");
        check(!has_mstp_preambles(tokens.data(), 8), "not one frame alone");

        Bytes late(3, 0);
        late.insert(late.end(), tokens.begin(), tokens.end());
        check(has_mstp_preambles(late.data(), late.size()), "anywhere in what arrived");

        Bytes seven = { 0x55, 0xFF, 0, 0, 0, 0, 0, 0x55, 0xFF, 0, 0 };
        check(!has_mstp_preambles(seven.data(), seven.size()), "not seven bytes apart");

        check(decode(tokens, ScanFrame::range_query(1, 254)).answer == RangeAnswer::Mstp,
              "a reply holding them is MS/TP");
    }

    void test_reading_the_identity()
    {
        section("registers 0-9 of one device");

        // Serial 123456 is 0x0001E240, a byte per register, low first.
        const uint16_t reg[10] = { 0x40, 0xE2, 0x01, 0x00, 26, 2, 7, 88, 26, 0 };
        const Bytes reply = identity_reply(7, reg);
        check_eq((long)reply.size(), 25, "a reply is 25 bytes");

        DeviceIdentity d;
        std::string error;
        check(decode_identity_reply(reply.data(), reply.size(), 7, d, error), "is read");
        check_eq((long)d.serial, 123456, "serial from registers 0-3");
        check_eq(d.firmware, 538, "firmware from registers 5 and 4");
        check_eq(d.modbus_id, 7, "id from register 6");
        check_eq(d.product, 88, "product from register 7");
        check_eq(d.hardware, 26, "hardware from register 8");

        // T3000 adds the registers, whatever each holds.
        const uint16_t wide[10] = { 0x0102, 0, 0, 0, 26, 2, 7, 88, 26, 0 };
        const Bytes w = identity_reply(7, wide);
        check(decode_identity_reply(w.data(), w.size(), 7, d, error), "a register above 255 is read");
        check_eq((long)d.serial, 0x0102, "and added whole, as T3000 adds it");

        const uint16_t old_tstat[10] = { 1, 0, 0, 0, 245, 7, 7, 3, 0, 0 };
        const Bytes o = identity_reply(7, old_tstat);
        check(decode_identity_reply(o.data(), o.size(), 7, d, error), "an old Tstat is read");
        check_eq(d.firmware, 245, "with register 4 alone as its version, when it is 240-249");

        const uint16_t just_over[10] = { 1, 0, 0, 0, 250, 7, 7, 3, 0, 0 };
        const Bytes j = identity_reply(7, just_over);
        check(decode_identity_reply(j.data(), j.size(), 7, d, error), "register 4 of 250 is read");
        check_eq(d.firmware, 7 * 256 + 250, "with registers 5 and 4 together");
    }

    void test_identity_replies_that_are_refused()
    {
        section("an identity reply that is not one is refused, and changes nothing");

        const uint16_t reg[10] = { 0x40, 0xE2, 0x01, 0x00, 26, 2, 7, 88, 26, 0 };
        const Bytes good = identity_reply(7, reg);

        DeviceIdentity d;
        d.serial = 77;
        std::string error;

        const Bytes exception = with_crc({ 7, 0x83, 0x02 });
        check(!decode_identity_reply(exception.data(), exception.size(), 7, d, error), "a Modbus exception");
        check(error.find("exception 2") != std::string::npos, "says which exception");

        error.clear();
        check(!decode_identity_reply(good.data(), good.size() - 1, 7, d, error), "a reply cut short");
        check(error.find("24 bytes") != std::string::npos, "says how long it was");

        error.clear();
        check(!decode_identity_reply(good.data(), good.size(), 8, d, error), "a reply from another id");
        check(error.find("not an answer") != std::string::npos, "says it is not an answer to the read");

        Bytes wrong_count = good;
        wrong_count[2] = 18;
        wrong_count.resize(23);
        wrong_count = with_crc(wrong_count);
        error.clear();
        check(!decode_identity_reply(wrong_count.data(), wrong_count.size(), 7, d, error),
              "a reply giving the wrong byte count");
        check(error.find("not an answer") != std::string::npos, "says it is not an answer to the read");

        Bytes bad_crc = good;
        bad_crc[24] ^= 0x01;
        error.clear();
        check(!decode_identity_reply(bad_crc.data(), bad_crc.size(), 7, d, error), "a reply with its CRC wrong");
        check(error.find("CRC") != std::string::npos, "says the CRC does not match");

        check_eq((long)d.serial, 77, "none of them changed what was passed in");
    }
}

int run_rtu_tests()
{
    test_the_crc_is_crc16_modbus();
    test_the_identity_read_is_registers_0_to_9();
    test_the_range_query_is_hi_before_lo();
    test_nobody_answering();
    test_one_device_answering();
    test_replies_that_do_not_check_out();
    test_more_than_one_device_answering();
    test_mstp_frames();
    test_reading_the_identity();
    test_identity_replies_that_are_refused();
    return 0;
}
