#include "decode.h"

#include <string.h>

namespace t3000::wire
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
        // Note this is strlen over the wire bytes, so it stops at the first NUL
        // and cannot run past the field unless the field is entirely non-zero.
        if (strnlen((const char*)p, kDescriptionLength) >= kDescriptionLength)
            memset(out.description, 0, kDescriptionLength);
        else
            memcpy(out.description, p, kDescriptionLength);
        p += kDescriptionLength;

        if (strnlen((const char*)p, kLabelLength) >= kLabelLength)
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
}
