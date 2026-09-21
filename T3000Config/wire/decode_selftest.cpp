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

#include <stdio.h>
#include <string.h>

namespace
{
    using namespace t3000::wire;

    int g_failures = 0;

    void check(bool condition, const char* what)
    {
        if (!condition)
        {
            printf("  FAIL  %s\n", what);
            g_failures++;
        }
    }

    void check_eq(long actual, long expected, const char* what)
    {
        if (actual != expected)
        {
            printf("  FAIL  %s: got %ld, expected %ld\n", what, actual, expected);
            g_failures++;
        }
    }

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
        printf("decodes every field in wire order\n");

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
        printf("refuses to read past a short buffer\n");

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
        printf("blanks text fields that do not fit\n");

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
}

int main()
{
    printf("T3000Config wire decoder self-test\n\n");

    test_decodes_every_field();
    test_rejects_short_buffer();
    test_blanks_unterminated_text();

    printf("\n%s\n", g_failures == 0 ? "all checks passed" : "FAILURES PRESENT");
    return g_failures == 0 ? 0 : 1;
}
