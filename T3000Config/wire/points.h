#pragma once

// The on-the-wire layout of a device's configuration points.
//
// This is a deliberate copy of the structs in T3000/CM5/ud_str.h rather than an
// include of them, because that header cannot compile outside an MFC
// translation unit: it uses `byte`, which only exists once the Windows headers
// have been pulled in, and puts a `public:` inside Point_T3000.
//
// Copying a wire format is normally a mistake. It is done here because the
// alternative is worse, and because the copy is checked rather than trusted -
// wire_guard.cpp includes BOTH this header and the real one and fails the build
// if any field moves. Treat that guard as part of this file.
//
// DO NOT "fix" this by including BacNetDllforVc/include/ud_str.h instead. That
// header defines structs with these same names and it is STALE: it still has
// sen_on/sen_off where the device has sub_id/sub_product, and a single
// calibration byte where the device has calibration_h/calibration_l. It is also
// the copy that looks clean - no MFC anywhere near it - which is exactly what
// makes it dangerous. Building on it produces a tool that compiles, looks
// right, and misreads every point on real hardware while writing to it.
//
// The live layout is the one T3000/global_variable.h:5 includes: CM5\ud_str.h.

#include <stdint.h>

namespace t3000::wire
{
    // Byte-packed to match the device. CM5/ud_str.h wraps these in
    // #pragma pack(1) and the structs are read straight off the wire, so any
    // padding introduced here is silent corruption.
#pragma pack(push, 1)

    enum : int
    {
        kDescriptionLength = 21,   // STR_IN_DESCRIPTION_LENGTH
        kLabelLength       = 9,    // STR_IN_LABEL
    };

    struct InputPoint
    {
        uint8_t  description[kDescriptionLength];
        uint8_t  label[kLabelLength];
        int32_t  value;

        uint8_t  filter;
        uint8_t  decom;              // 0 = ok, 1 = decommissioned
        uint8_t  sub_id;
        uint8_t  sub_product;
        uint8_t  control;
        uint8_t  auto_manual;        // 0 = auto, 1 = manual
        uint8_t  digital_analog;     // 1 = analog, 0 = digital

        // Calibration is a sign plus a high/low byte pair, not a plain int.
        // The stale header has one `calibration` byte here instead, which is
        // the single most likely way for a copy of this struct to go wrong.
        uint8_t  calibration_sign;   // 0 = positive, 1 = negative
        uint8_t  sub_number;
        uint8_t  calibration_h;
        uint8_t  calibration_l;

        uint8_t  range;              // input_range_equate
    };

#pragma pack(pop)

    // 21 + 9 + 4 + 12 = 46. global_define.h:441 records the same number next to
    // BAC_INPUT_ITEM_COUNT. Size agreement is necessary but NOT sufficient - the
    // stale layout also totals 46 - so wire_guard.cpp checks every offset.
    static_assert(sizeof(InputPoint) == 46, "InputPoint must stay 46 bytes on the wire");

    inline int calibration(const InputPoint& p)
    {
        const int magnitude = (p.calibration_h << 8) | p.calibration_l;
        return p.calibration_sign == 1 ? -magnitude : magnitude;
    }
}
