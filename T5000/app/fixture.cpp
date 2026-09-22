#include "fixture.h"

#include <string.h>

namespace t5000::app
{
    namespace
    {
        void set_text(uint8_t* dst, size_t field, const char* text)
        {
            memset(dst, 0, field);
            const size_t n = strlen(text);
            memcpy(dst, text, n < field ? n : field - 1);
        }
    }

    std::vector<wire::InputPoint> fixture_points()
    {
        // Enough variety to exercise the page: named and unnamed, analog and
        // digital, auto and manual, a decommissioned point, and a negative
        // calibration. Not an imitation of any real installation.
        struct Row
        {
            const char* description;
            const char* label;
            int32_t     value;
            uint8_t     auto_manual;
            uint8_t     digital_analog;
            uint8_t     decom;
            uint8_t     range;
            uint8_t     filter;
            uint8_t     cal_sign;
            uint8_t     cal_h;
            uint8_t     cal_l;
        };

        static const Row rows[] = {
            { "Return Air Temp",   "RA_T",  721, 0, 1, 0, 3, 2, 0, 0x00, 0x00 },
            { "Supply Air Temp",   "SA_T",  548, 0, 1, 0, 3, 2, 1, 0x00, 0x14 },
            { "Outside Air Temp",  "OA_T",  312, 0, 1, 0, 3, 4, 0, 0x00, 0x05 },
            { "Space Humidity",    "SP_RH", 415, 0, 1, 0, 7, 1, 0, 0x00, 0x00 },
            { "Occupancy Sensor",  "OCC",     1, 0, 0, 0, 1, 0, 0, 0x00, 0x00 },
            { "Filter Status",     "FLT_S",   0, 0, 0, 0, 1, 0, 0, 0x00, 0x00 },
            { "Fan Proving",       "FAN_PR",  1, 1, 0, 0, 1, 0, 0, 0x00, 0x00 },
            { "Spare Analog",      "SPR_1",   0, 0, 1, 1, 3, 0, 0, 0x00, 0x00 },
            { "",                  "",        0, 0, 1, 0, 0, 0, 0, 0x00, 0x00 },
            { "CO2 Level",         "CO2",   612, 0, 1, 0, 9, 3, 0, 0x01, 0x2C },
        };

        std::vector<wire::InputPoint> points;
        points.reserve(sizeof(rows) / sizeof(rows[0]));

        for (const Row& r : rows)
        {
            wire::InputPoint p{};
            set_text(p.description, wire::kDescriptionLength, r.description);
            set_text(p.label,       wire::kLabelLength,       r.label);

            p.value            = r.value;
            p.auto_manual      = r.auto_manual;
            p.digital_analog   = r.digital_analog;
            p.decom            = r.decom;
            p.range            = r.range;
            p.filter           = r.filter;
            p.calibration_sign = r.cal_sign;
            p.calibration_h    = r.cal_h;
            p.calibration_l    = r.cal_l;
            p.sub_id           = 1;
            p.sub_product      = 0;
            p.sub_number       = 0;
            p.control          = 0;

            points.push_back(p);
        }
        return points;
    }
}
