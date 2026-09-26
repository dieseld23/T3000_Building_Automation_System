#include "decode.h"

#include <string.h>

namespace t5000::wire
{
    void sanitize_label(uint8_t* label, size_t length)
    {
        for (size_t i = 0; i < length; i++)
        {
            if (label[i] == '-' || label[i] == '.')
                label[i] = '_';
        }
    }

    bool decode_input_point(const uint8_t* buffer, size_t length, InputPoint& out)
    {
        if (buffer == nullptr || length < kInputPointWireSize)
            return false;

        const uint8_t* p = buffer;

        // fill_in_input guards each string with
        //     if (strlen(src) > FIELD_LENGTH) memset(dst, 0, FIELD_LENGTH);
        //     else                            memcpy(dst, src, FIELD_LENGTH);
        //
        // which reads as "blank the field if the text does not fit". Keep that
        // behaviour: a device returning an unterminated field is reporting
        // something wrong, and a blank label is a visible symptom, where a
        // truncated one silently looks like a real name.
        //
        // "Does not fit" is decided by strlen over the wire, not the field:
        // "> 21" means the 21 description bytes AND the byte after them - the
        // label's first - are all non-NUL. So a description that fills its
        // field is kept when the label is empty and blanked when it is not,
        // and a 9-character label is kept or blanked by the value's first
        // byte. That is what T3000 shows, so it is what this does; a test in
        // decode_selftest.cpp pins each edge. Neither test can read past the
        // point: the byte after each field is in it.
        if (strnlen((const char*)p, kDescriptionLength + 1) > kDescriptionLength)
            memset(out.description, 0, kDescriptionLength);
        else
            memcpy(out.description, p, kDescriptionLength);
        p += kDescriptionLength;

        if (strnlen((const char*)p, kLabelLength + 1) > kLabelLength)
            memset(out.label, 0, kLabelLength);
        else
            memcpy(out.label, p, kLabelLength);
        p += kLabelLength;

        // Labels are used as identifiers elsewhere in the product, so separators
        // that would break that are folded to underscore.
        sanitize_label(out.label, kLabelLength);

        // Little-endian, decoded explicitly rather than by casting the pointer:
        // the buffer has no alignment guarantee, and an unaligned int32 read is
        // undefined behaviour even where the hardware tolerates it.
        out.value = (int32_t)(((uint32_t)p[3] << 24) |
                              ((uint32_t)p[2] << 16) |
                              ((uint32_t)p[1] << 8)  |
                              ((uint32_t)p[0]));
        p += 4;

        out.filter           = *p++;
        out.decom            = *p++;
        out.sub_id           = *p++;
        out.sub_product      = *p++;
        out.control          = *p++;
        out.auto_manual      = *p++;
        out.digital_analog   = *p++;
        out.calibration_sign = *p++;
        out.sub_number       = *p++;
        out.calibration_h    = *p++;
        out.calibration_l    = *p++;
        out.range            = *p++;

        return (size_t)(p - buffer) == kInputPointWireSize;
    }

    bool decode_output_point(const uint8_t* buffer, size_t length, OutputPoint& out)
    {
        if (buffer == nullptr || length < kOutputPointWireSize)
            return false;

        const uint8_t* p = buffer;

        // fill_in_output guards its two strings differently from each other,
        // and the description differently from fill_in_input:
        //
        //   description  if (strlen(src) > 19) { copy 19; description[18] = 0; }
        //                else copy 19                        (:3405-3411)
        //   label        if (strlen(src) > 9) blank it; else copy 9  (:3423-3426)
        //
        // So a description that does not fit is cut to 18 characters, not
        // blanked. And strlen runs over the wire, not the field: "> 19" means
        // the 19 description bytes AND the byte after them - the low voltage -
        // are all non-NUL. A 19-character description is kept whole when the
        // low voltage is 0 and cut to 18 when it is not; a 9-character label
        // is kept when the value's first byte is 0 and blanked when it is not.
        // That is what T3000 shows, so it is what this does. Neither test can
        // read past the point: the bytes after each field are in it.
        memcpy(out.description, p, kOutputDescriptionLength);
        if (strnlen((const char*)p, kOutputDescriptionLength + 1) > kOutputDescriptionLength)
            out.description[kOutputDescriptionLength - 1] = 0;
        p += kOutputDescriptionLength;

        // Tenths of a volt. T3000 zeroes either one above 120 - 12.0 V -
        // (:3415-3420), which it then shows as blank.
        out.low_voltage  = *p++;
        out.high_voltage = *p++;
        if (out.low_voltage > 120)
            out.low_voltage = 0;
        if (out.high_voltage > 120)
            out.high_voltage = 0;

        if (strnlen((const char*)p, kOutputLabelLength + 1) > kOutputLabelLength)
            memset(out.label, 0, kOutputLabelLength);
        else
            memcpy(out.label, p, kOutputLabelLength);
        p += kOutputLabelLength;

        // The same folding as an input's label (:3430-3443).
        sanitize_label(out.label, kOutputLabelLength);

        out.value = (int32_t)(((uint32_t)p[3] << 24) |
                              ((uint32_t)p[2] << 16) |
                              ((uint32_t)p[1] << 8)  |
                              ((uint32_t)p[0]));
        p += 4;

        out.auto_manual      = *p++;
        out.digital_analog   = *p++;
        out.hw_switch_status = *p++;
        out.control          = *p++;
        out.digital_control  = *p++;
        out.decom            = *p++;
        out.range            = *p++;
        out.sub_id           = *p++;
        out.sub_product      = *p++;
        out.sub_number       = *p++;
        out.pwm_period       = *p++;

        return (size_t)(p - buffer) == kOutputPointWireSize;
    }
}
