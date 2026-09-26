// Self-tests for the wire decoder.
//
// These need no device. That is the point: the decoder can be wrong in ways that
// look entirely plausible against real hardware - a swapped field pair reads as
// two sensible-looking numbers - so it is pinned against a buffer whose every
// byte is known here, before anything is pointed at a live controller.
//
// Each field in the fixture below gets a distinct value, so a decoder that reads
// the right number of bytes in the wrong order still fails.

#include "decode.h"
#include "../testing/check.h"

#include <string.h>

namespace
{
    using namespace t5000::wire;
    using namespace t5000::testing;

    // A full 46-byte point. Values are deliberately all different and none are
    // 0 or 1, so an off-by-one walk through the trailing byte fields cannot
    // coincidentally produce the expected answer.
    void build_fixture(uint8_t (&buf)[kInputPointWireSize])
    {
        memset(buf, 0, sizeof(buf));

        memcpy(buf + 0, "Return Air Temp", 15);   // description, NUL-padded
        memcpy(buf + 21, "RA-T.1", 6);            // label, with both separators

        // value = -1234, little-endian two's complement: 0x FF FF FB 2E
        buf[30] = 0x2E;
        buf[31] = 0xFB;
        buf[32] = 0xFF;
        buf[33] = 0xFF;

        buf[34] = 3;    // filter
        buf[35] = 0;    // decom          (0 = ok)
        buf[36] = 7;    // sub_id
        buf[37] = 9;    // sub_product
        buf[38] = 2;    // control
        buf[39] = 1;    // auto_manual    (1 = manual)
        buf[40] = 1;    // digital_analog (1 = analog)
        buf[41] = 1;    // calibration_sign (1 = negative)
        buf[42] = 4;    // sub_number
        buf[43] = 0x01; // calibration_h
        buf[44] = 0x2C; // calibration_l  -> 0x012C = 300, signed -300
        buf[45] = 6;    // range
    }

    void test_decodes_every_field()
    {
        section("decodes every field in wire order");

        uint8_t buf[kInputPointWireSize];
        build_fixture(buf);

        InputPoint p;
        memset(&p, 0xAA, sizeof(p));   // poison, so "not written" != "zero"

        check(decode_input_point(buf, sizeof(buf), p), "decode returned true");

        check(memcmp(p.description, "Return Air Temp", 15) == 0, "description");

        // '-' and '.' both folded to '_'
        check(memcmp(p.label, "RA_T_1", 6) == 0, "label separators sanitized");

        check_eq(p.value, -1234, "value (little-endian, negative)");
        check_eq(p.filter, 3, "filter");
        check_eq(p.decom, 0, "decom");
        check_eq(p.sub_id, 7, "sub_id");
        check_eq(p.sub_product, 9, "sub_product");
        check_eq(p.control, 2, "control");
        check_eq(p.auto_manual, 1, "auto_manual");
        check_eq(p.digital_analog, 1, "digital_analog");
        check_eq(p.calibration_sign, 1, "calibration_sign");
        check_eq(p.sub_number, 4, "sub_number");
        check_eq(p.calibration_h, 0x01, "calibration_h");
        check_eq(p.calibration_l, 0x2C, "calibration_l");
        check_eq(p.range, 6, "range");

        // The accessor is where a caller would get calibration wrong, since the
        // sign lives in a separate field from the magnitude.
        check_eq(calibration(p), -300, "calibration() combines h/l and applies sign");
    }

    void test_rejects_short_buffer()
    {
        section("refuses to read past a short buffer");

        uint8_t buf[kInputPointWireSize];
        build_fixture(buf);

        InputPoint p;
        for (size_t len = 0; len < kInputPointWireSize; len++)
            check(!decode_input_point(buf, len, p), "rejected a buffer one byte too short");

        check(decode_input_point(buf, kInputPointWireSize, p), "accepted an exact-length buffer");
        check(!decode_input_point(nullptr, kInputPointWireSize, p), "rejected a null buffer");
    }

    void test_blanks_unterminated_text()
    {
        section("blanks text fields that do not fit");

        uint8_t buf[kInputPointWireSize];
        build_fixture(buf);

        // A description with no room for a terminator. fill_in_input blanks this
        // rather than truncating, so a malformed response is visible instead of
        // looking like a real, slightly-wrong name.
        memset(buf + 0, 'X', kDescriptionLength);

        InputPoint p;
        check(decode_input_point(buf, sizeof(buf), p), "decode still succeeded");

        bool all_zero = true;
        for (int i = 0; i < kDescriptionLength; i++)
            if (p.description[i] != 0) all_zero = false;
        check(all_zero, "unterminated description was blanked, not truncated");

        // The fields after it must still land correctly - a blanked string must
        // not shift the walk.
        check_eq(p.range, 6, "range still correct after a blanked description");
    }

    void test_full_width_input_text_depends_on_the_next_byte()
    {
        section("input text that fills its field is kept or blanked by the byte after it, as in T3000");

        uint8_t buf[kInputPointWireSize];
        InputPoint p;

        // 21 characters and an empty label: fill_in_input's strlen is 21,
        // which is not more than 21, so the description is kept.
        build_fixture(buf);
        memset(buf + 0, 'X', kDescriptionLength);
        memset(buf + 21, 0, kLabelLength);
        decode_input_point(buf, sizeof(buf), p);
        check(memcmp(p.description, "XXXXXXXXXXXXXXXXXXXXX", kDescriptionLength) == 0,
              "21 characters, then an empty label: kept");

        // A 9-character label, and a value whose first byte is 0: kept.
        build_fixture(buf);
        memcpy(buf + 21, "ABCDEFGHI", kLabelLength);
        buf[30] = 0x00;
        buf[31] = 0x10;
        buf[32] = 0x00;
        buf[33] = 0x00;
        decode_input_point(buf, sizeof(buf), p);
        check(memcmp(p.label, "ABCDEFGHI", kLabelLength) == 0, "9 characters, value 4096: kept");
        check_eq(p.value, 4096, "  and the value read");

        // The same label, and a value whose first byte is not 0: blanked.
        buf[30] = 0x08;
        buf[31] = 0x00;
        decode_input_point(buf, sizeof(buf), p);
        bool blank = true;
        for (int i = 0; i < kLabelLength; i++)
            blank = blank && p.label[i] == 0;
        check(blank, "9 characters, value 8: blanked");
        check_eq(p.value, 8, "  and the value still read");
    }

    // ------------------------------------------------------------- outputs

    // A full 45-byte output point, every field distinct, as the input fixture
    // is. Offsets: description 0-18, low and high voltage 19-20, label
    // 21-29, value 30-33, then eleven single bytes.
    void build_output(uint8_t (&buf)[kOutputPointWireSize])
    {
        memset(buf, 0, sizeof(buf));

        memcpy(buf + 0, "Supply Fan", 10);     // description, NUL-padded
        buf[19] = 25;                          // low_voltage, 2.5 V
        buf[20] = 95;                          // high_voltage, 9.5 V
        memcpy(buf + 21, "SF-1.A", 6);         // label, with both separators

        // value = 73500, little-endian: 0x00011F1C
        buf[30] = 0x1C;
        buf[31] = 0x1F;
        buf[32] = 0x01;
        buf[33] = 0x00;

        buf[34] = 1;    // auto_manual      (1 = manual)
        buf[35] = 1;    // digital_analog   (1 = analog)
        buf[36] = 2;    // hw_switch_status (2 = hand)
        buf[37] = 3;    // control
        buf[38] = 4;    // digital_control
        buf[39] = 5;    // decom
        buf[40] = 6;    // range
        buf[41] = 7;    // sub_id
        buf[42] = 8;    // sub_product
        buf[43] = 9;    // sub_number
        buf[44] = 10;   // pwm_period
    }

    void test_decodes_every_output_field()
    {
        section("decodes every output field in wire order");

        uint8_t buf[kOutputPointWireSize];
        build_output(buf);

        OutputPoint p;
        memset(&p, 0xAA, sizeof(p));   // poison, so "not written" != "zero"

        check(decode_output_point(buf, sizeof(buf), p), "decode returned true");
        check(memcmp(p.description, "Supply Fan\0\0\0\0\0\0\0\0\0", kOutputDescriptionLength) == 0,
              "description, with its padding");
        check_eq(p.low_voltage, 25, "low_voltage");
        check_eq(p.high_voltage, 95, "high_voltage");
        check(memcmp(p.label, "SF_1_A\0\0\0", kOutputLabelLength) == 0, "label separators sanitized");
        check_eq(p.value, 73500, "value (little-endian)");
        check_eq(p.auto_manual, 1, "auto_manual");
        check_eq(p.digital_analog, 1, "digital_analog");
        check_eq(p.hw_switch_status, 2, "hw_switch_status");
        check_eq(p.control, 3, "control");
        check_eq(p.digital_control, 4, "digital_control");
        check_eq(p.decom, 5, "decom");
        check_eq(p.range, 6, "range");
        check_eq(p.sub_id, 7, "sub_id");
        check_eq(p.sub_product, 8, "sub_product");
        check_eq(p.sub_number, 9, "sub_number");
        check_eq(p.pwm_period, 10, "pwm_period");

        // A negative value, so the sign comes from the top byte.
        buf[30] = 0x2E;
        buf[31] = 0xFB;
        buf[32] = 0xFF;
        buf[33] = 0xFF;
        check(decode_output_point(buf, sizeof(buf), p) && p.value == -1234, "a negative value");
    }

    void test_rejects_short_output_buffer()
    {
        section("refuses to read past a short output buffer");

        uint8_t buf[kOutputPointWireSize];
        build_output(buf);

        OutputPoint p;
        bool all_refused = true;
        for (size_t len = 0; len < kOutputPointWireSize; len++)
            all_refused = all_refused && !decode_output_point(buf, len, p);
        check(all_refused, "every buffer shorter than 45 bytes is refused");
        check(decode_output_point(buf, kOutputPointWireSize, p), "an exact-length buffer is accepted");
        check(!decode_output_point(nullptr, kOutputPointWireSize, p), "a null buffer is refused");
    }

    void test_output_voltages()
    {
        section("output voltages above 12.0 V are zeroed, as fill_in_output does");

        uint8_t buf[kOutputPointWireSize];
        build_output(buf);
        OutputPoint p;

        buf[19] = 120;
        buf[20] = 121;
        decode_output_point(buf, sizeof(buf), p);
        check_eq(p.low_voltage, 120, "120 - 12.0 V - is kept");
        check_eq(p.high_voltage, 0, "121 is zeroed");

        buf[19] = 255;
        buf[20] = 0;
        decode_output_point(buf, sizeof(buf), p);
        check_eq(p.low_voltage, 0, "255 is zeroed");
        check_eq(p.high_voltage, 0, "0 stays 0");
    }

    void test_output_description_that_does_not_fit()
    {
        section("an output description that does not fit is cut to 18, not blanked");

        uint8_t buf[kOutputPointWireSize];
        OutputPoint p;

        // 19 characters, and a low voltage of 0 after them: strlen is 19,
        // which is not more than 19, so all 19 are kept.
        build_output(buf);
        memset(buf + 0, 'D', kOutputDescriptionLength);
        buf[19] = 0;
        decode_output_point(buf, sizeof(buf), p);
        check(memcmp(p.description, "DDDDDDDDDDDDDDDDDDD", kOutputDescriptionLength) == 0,
              "19 characters, then a zero low voltage: all 19 kept");

        // The same 19, and a low voltage after them: strlen runs on into it,
        // so the description is cut to 18 - and the voltage is still read.
        buf[19] = 25;
        decode_output_point(buf, sizeof(buf), p);
        check(memcmp(p.description, "DDDDDDDDDDDDDDDDDD\0", kOutputDescriptionLength) == 0,
              "19 characters, then a non-zero low voltage: cut to 18");
        check_eq(p.low_voltage, 25, "  and the low voltage is still read");

        // The test is on the wire byte, before the voltage is zeroed: 200 is
        // non-zero on the wire, so the description is cut, and then the
        // voltage becomes 0.
        buf[19] = 200;
        decode_output_point(buf, sizeof(buf), p);
        check(p.description[kOutputDescriptionLength - 1] == 0 && p.description[17] == 'D',
              "a low voltage of 200 cuts it too, though it is then zeroed");
        check_eq(p.low_voltage, 0, "  and the voltage is zeroed");

        // 18 characters fit with room for their terminator.
        build_output(buf);
        memset(buf + 0, 'E', kOutputDescriptionLength - 1);
        buf[18] = 0;
        decode_output_point(buf, sizeof(buf), p);
        check(memcmp(p.description, "EEEEEEEEEEEEEEEEEE\0", kOutputDescriptionLength) == 0, "18 characters kept");
        check_eq(p.range, 6, "the fields after it still land correctly");
    }

    void test_output_label_that_does_not_fit()
    {
        section("an output label that does not fit is blanked, depending on the byte after it");

        uint8_t buf[kOutputPointWireSize];
        OutputPoint p;

        // Nine characters, and a value whose first byte is 0 after them:
        // strlen is 9, so the label is kept.
        build_output(buf);
        memset(buf + 21, 'L', kOutputLabelLength);
        buf[30] = 0x00;
        buf[31] = 0x10;   // value 4096: first byte 0
        buf[32] = 0x00;
        buf[33] = 0x00;
        decode_output_point(buf, sizeof(buf), p);
        check(memcmp(p.label, "LLLLLLLLL", kOutputLabelLength) == 0, "9 characters, value 4096: kept");
        check_eq(p.value, 4096, "  and the value is read");

        // The same, with a value whose first byte is not 0: blanked.
        buf[30] = 0x08;
        buf[31] = 0x00;
        decode_output_point(buf, sizeof(buf), p);
        bool blank = true;
        for (int i = 0; i < kOutputLabelLength; i++)
            blank = blank && p.label[i] == 0;
        check(blank, "9 characters, value 8: blanked");
        check_eq(p.value, 8, "  and the value is still read");

        // Eight characters always fit.
        build_output(buf);
        memcpy(buf + 21, "ABCDEFGH", 8);
        buf[29] = 0;
        decode_output_point(buf, sizeof(buf), p);
        check(memcmp(p.label, "ABCDEFGH", 9) == 0, "8 characters kept");
    }
}

int run_wire_tests()
{
    test_decodes_every_field();
    test_rejects_short_buffer();
    test_blanks_unterminated_text();
    test_full_width_input_text_depends_on_the_next_byte();
    test_decodes_every_output_field();
    test_rejects_short_output_buffer();
    test_output_voltages();
    test_output_description_that_does_not_fit();
    test_output_label_that_does_not_fit();
    return 0;
}
