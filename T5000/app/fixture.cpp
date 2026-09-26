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
        // digital, auto and manual, an open and a shorted sensor, a custom
        // range, and a negative calibration. Values are thousandths, as the
        // device sends them. Not an imitation of any real installation.
        struct Row
        {
            const char* description;
            const char* label;
            int32_t     value;
            uint8_t     control;        // a digital point's state
            uint8_t     auto_manual;
            uint8_t     digital_analog;
            uint8_t     decom;          // low nibble status, high nibble signal type
            uint8_t     range;
            uint8_t     filter;
            uint8_t     cal_sign;
            uint8_t     cal_h;
            uint8_t     cal_l;
        };

        static const Row rows[] = {
            { "Return Air Temp",   "RA_T",   21700, 0, 0, 1, 0x00,  3, 2, 0, 0x00, 0x00 },
            { "Supply Air Temp",   "SA_T",   13450, 0, 0, 1, 0x00,  3, 2, 1, 0x00, 0x14 },
            { "Outside Air Temp",  "OA_T",   -4250, 0, 0, 1, 0x01,  3, 4, 0, 0x00, 0x05 },
            { "Duct Pressure",     "DP",      6200, 0, 0, 1, 0x10, 13, 1, 0, 0x00, 0x00 },
            { "Occupancy Sensor",  "OCC",        0, 1, 0, 0, 0x00, 10, 0, 0, 0x00, 0x00 },
            { "Filter Status",     "FLT_S",      0, 0, 0, 0, 0x00,  5, 0, 0, 0x00, 0x00 },
            { "Fan Proving",       "FAN_PR",     0, 1, 1, 0, 0x00,  1, 0, 0, 0x00, 0x00 },
            { "Spare Analog",      "SPR_1",      0, 0, 0, 1, 0x02,  3, 0, 0, 0x00, 0x00 },
            { "",                  "",           0, 0, 0, 1, 0x00,  0, 0, 0, 0x00, 0x00 },
            { "Tank Level",        "TANK",   73500, 0, 0, 1, 0x30, 20, 3, 0, 0x01, 0x2C },
            { "Pump Mode",         "PMP_M",      0, 1, 0, 0, 0x00, 24, 0, 0, 0x00, 0x00 },
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
            p.sub_id           = 0;
            p.sub_product      = 0;
            p.sub_number       = 0;
            p.control          = r.control;

            points.push_back(p);
        }
        return points;
    }

    DeviceInfo fixture_outputs_device()
    {
        DeviceInfo d;
        d.serial_number = 500124;
        d.product_id    = 35;    // PM_MINIPANEL
        d.firmware      = 600;
        d.protocol      = 3;     // PROTOCOL_BACNET_IP
        d.is_fixture    = true;
        return d;
    }

    OutputsPanel fixture_outputs_panel()
    {
        OutputsPanel panel;
        panel.known                   = true;
        panel.product                 = device::ProductClassId::MiniPanel;
        panel.settings.mini_type_byte = 3;   // TINY_MINIPANEL: 6 digital and 2 analog switched
        panel.settings.firmware_main  = 60;
        panel.settings.firmware_sub   = 0;
        panel.settings.panel_number   = 1;
        panel.settings.serial_number  = 500124;
        set_text(panel.settings.panel_name, sizeof(panel.settings.panel_name), "Sample");

        panel.ranges.digital_known = true;
        display::DigitalRange& horn = panel.ranges.digital[0];
        horn.text       = "Quiet/Sound";
        horn.has_states = true;
        horn.off        = "Quiet";
        horn.on         = "Sound";
        return panel;
    }

    std::vector<wire::OutputPoint> fixture_outputs()
    {
        // Enough variety to exercise the page: switched outputs at auto, off
        // and hand, digital and analog, auto and manual, a custom range, a
        // decommissioned output, one on a sub-device, and an unnamed one.
        // Not an imitation of any real installation.
        struct Row
        {
            const char* description;
            const char* label;
            int32_t     value;
            uint8_t     control;
            uint8_t     auto_manual;
            uint8_t     digital_analog;
            uint8_t     hw_switch;     // 0 off, 1 auto, 2 hand
            uint8_t     range;
            uint8_t     low_voltage;   // tenths
            uint8_t     high_voltage;
            uint8_t     pwm_period;
            uint8_t     decom;
            uint8_t     sub_id;
            uint8_t     sub_product;
            uint8_t     sub_number;
        };

        static const Row rows[] = {
            { "Supply Fan",     "SF",      0,     1, 0, 0, 1, 1,  0,   0,   0, 0, 0, 0,  0 },
            { "Return Fan",     "RF",      0,     0, 0, 0, 0, 1,  0,   0,   0, 0, 0, 0,  0 },
            { "Exhaust Damper", "EXH_D",   0,     1, 0, 0, 2, 2,  0,   0,   0, 0, 0, 0,  0 },
            { "Pump Enable",    "PMP_EN",  0,     1, 1, 0, 1, 4,  0,   0,   0, 0, 0, 0,  0 },
            { "Heat Stage 1",   "HT_1",    0,     0, 0, 0, 1, 1,  0,   0,   0, 0, 0, 0,  0 },
            { "Alarm Horn",     "HORN",    0,     0, 0, 0, 1, 23, 0,   0,   0, 0, 0, 0,  0 },
            { "Cooling Valve",  "CLG_V",   7350,  0, 0, 1, 1, 1,  20,  100, 0, 0, 0, 0,  0 },
            { "Heating Valve",  "HTG_V",   45000, 0, 1, 1, 0, 2,  0,   0,   0, 0, 0, 0,  0 },
            { "Fan Speed Cmd",  "FAN_SPD", 62500, 0, 0, 1, 1, 7,  0,   0,   20, 0, 0, 0, 0 },
            { "Setpoint Out",   "SP_OUT",  12000, 0, 1, 1, 1, 6,  0,   0,   0, 1, 0, 0,  0 },
            { "Remote Relay",   "RR_1",    0,     1, 0, 0, 2, 1,  0,   0,   0, 0, 3, 44, 2 },
            { "",               "",        0,     0, 0, 1, 1, 0,  0,   0,   0, 0, 0, 0,  0 },
        };

        std::vector<wire::OutputPoint> points;
        points.reserve(sizeof(rows) / sizeof(rows[0]));

        for (const Row& r : rows)
        {
            wire::OutputPoint p{};
            set_text(p.description, wire::kOutputDescriptionLength, r.description);
            set_text(p.label,       wire::kOutputLabelLength,       r.label);

            p.value            = r.value;
            p.control          = r.control;
            p.auto_manual      = r.auto_manual;
            p.digital_analog   = r.digital_analog;
            p.hw_switch_status = r.hw_switch;
            p.range            = r.range;
            p.low_voltage      = r.low_voltage;
            p.high_voltage     = r.high_voltage;
            p.pwm_period       = r.pwm_period;
            p.decom            = r.decom;
            p.sub_id           = r.sub_id;
            p.sub_product      = r.sub_product;
            p.sub_number       = r.sub_number;

            points.push_back(p);
        }
        return points;
    }
}
