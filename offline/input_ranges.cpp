#include "input_ranges.h"

#include <algorithm>

#include "../display/tables.h"

namespace t5000::offline
{
    namespace
    {
        using device::MiniType;
        using device::ProductClassId;

        // The dialog's box numbers for the buttons its panel-type chain
        // turns on and off.
        constexpr int kSlowPulse   = 45;   // IDC_RADIO69, analog 15
        constexpr int kFastPulse   = 55;   // IDC_RADIO87, analog 25
        constexpr int kRpm         = 59;   // IDC_RADIO103, analog 29
        constexpr int kPt1kC       = 39;   // IDC_RADIO63, analog 9
        constexpr int kPt1kF       = 40;   // IDC_RADIO64, analog 10
        constexpr int k10kType2C   = 33;   // IDC_RADIO57, analog 3
        constexpr int k10kType2F   = 34;   // IDC_RADIO58, analog 4

        constexpr const char* kDegC = "\xC2\xB0" "C";

        std::vector<InputRangeChoice> make_choices()
        {
            std::vector<InputRangeChoice> out;
            out.push_back({ 0, "Unused", "" });

            // IDC_RADIO36-46 and IDC_RADIO89-99, "1. Off/On" to
            // "22. High/Low": Digital_Units_Array's names.
            for (int n = 1; n <= 22; n++)
                out.push_back({ n, display::kDigitalUnits[n], "" });

            // IDC_RADIO55-72 are analog 1-18, IDC_RADIO81-88 19-26 and
            // IDC_RADIO101-113 27-39 (BacnetRange.cpp:1387-1471), each
            // numbered 30 above. The even ones of 32-40 are hidden, and
            // reached by the °F button beside the one before.
            const InputRangeChoice analog[] = {
                { 31, "PT100 -40 to 1000", kDegC },
                { 32, "PT100 -40 to 1800 Deg.F", "" },
                { 33, "10K Type2", kDegC },
                { 34, "Deg.F 10K Type2", "" },
                { 35, "PT1000 -200 to 600", kDegC },
                { 36, "PT1000 -328 to 1112 Deg.F", "" },
                { 37, "10K Type3", kDegC },
                { 38, "10K-40 to 250 Deg.F(Type3)", "" },
                { 39, "PT 1K -200 to 300", kDegC },
                { 40, "PT 1K -200 to 570 Deg.F", "" },
                { 41, "0.0 to 5.0 Volts", "" },
                { 42, "0.0 to 100 Amps", "" },
                { 43, "4.0 to 20 ma", "" },
                { 44, "0.0 to 20 psi", "" },
                { 45, "Pulse Count (Slow 1Hz)", "" },
                { 46, "0 to 100 %(0-10V)", "" },
                { 47, "0 to 100 %(0-5V)", "" },
                { 48, "0 to 100 %(4-20ma)", "" },
                { 49, "0.0 to 10.0 Volts", "" },
                { 50, "Table 1", "" },
                { 51, "Table 2", "" },
                { 52, "Table 3", "" },
                { 53, "Table 4", "" },
                { 54, "Table 5", "" },
                { 55, "Pulse Count (Fast 100Hz)", "" },
                { 56, "Hz", "" },
                { 57, "Humidty %", "" },
                { 58, "CO2 PPM", "" },
                { 59, "Revolutions Per Minute", "" },
                { 60, "TVOC PPB", "" },
                { 61, "ug/m3", "" },
                { 62, "#/cm3", "" },
                { 63, "dB", "" },
                { 64, "Lux", "" },
                { 66, "AHKC-AC DC", "" },
            };
            for (const auto& c : analog)
                out.push_back(c);
            return out;
        }

        bool in(int row, int first, int last)
        {
            return row >= first && row <= last;
        }
    }

    const std::vector<InputRangeChoice>& input_range_choices()
    {
        static const std::vector<InputRangeChoice> choices = make_choices();
        return choices;
    }

    const InputRangeChoice* find_input_range(int number)
    {
        for (const auto& c : input_range_choices())
            if (c.number == number)
                return &c;
        return nullptr;
    }

    std::string input_range_name(const InputRangeChoice& choice)
    {
        std::string name = choice.caption;
        if (choice.scale[0] != '\0')
            name += std::string(" ") + choice.scale;
        return name;
    }

    InputRangeBytes input_range_bytes(int number)
    {
        if (number > 30)
            return { 1, (uint8_t)(number - 30) };
        if (number == 0)
            return { 1, 0 };
        return { 0, (uint8_t)number };
    }

    int input_range_number(uint8_t digital_analog, uint8_t range)
    {
        if (digital_analog == 1 && range != 0)
            return range + 30;
        return range;
    }

    bool input_range_fixed(ProductClassId product, MiniType type, int row)
    {
        // :1658-1660.
        if (product == ProductClassId::TstatAq || product == ProductClassId::AirlabEsp32)
            return true;

        // :1661-1682. T3000 tests the product first, so a TSTAT11 read as
        // an ESP32 T3 panel, as T3000's product list pairs it, is not held
        // here.
        if (product == ProductClassId::Tstat10)
        {
            if (type == MiniType::Oem && in(row, 13, 17))
                return true;
            if (type == MiniType::Oem12I && in(row, 17, 21))
                return true;
            if ((type == MiniType::Tstat10 || type == MiniType::Tstat11) && in(row, 9, 12))
                return true;
        }

        // :1683-1721. The second block's "(lRow == 32) && (lRow <= 47)"
        // holds only row 32, which the first already holds.
        if (product == ProductClassId::Esp32T3Series)
        {
            if (type == MiniType::Rmc1232 && (in(row, 8, 11) || in(row, 32, 47)))
                return true;
            if (type == MiniType::Bms && in(row, 32, 47))
                return true;
            if (type == MiniType::EspRmc && in(row, 16, 17))
                return true;
            if (type == MiniType::Ng3 && in(row, 24, 29))
                return true;
        }
        return false;
    }

    std::vector<int> input_ranges_offered(ProductClassId product, MiniType type, int row)
    {
        std::vector<int> out;
        if (input_range_fixed(product, type, row))
            return out;

        // T3000.rc disables none of the dialog's buttons. The chain at
        // BacnetRange.cpp:917-1016 then sets three of them, or, on a few rows,
        // disables every button (SetAllRadioButton, whose default is
        // RANGE_RADIO_DISABLE) and enables two. A row it names no branch
        // for keeps all three on, as the T3-OEM's other rows do.
        //
        // Some branches test bacnet_device_type rather than mini_type. The
        // grid has set it to the panel's type by the time the dialog can be
        // opened from it (BacnetInput.cpp:1305-1306), for any type but 0.
        bool slow = true, fast = true, rpm = true;
        std::vector<int> only;
        bool only_these = false;

        if ((type == MiniType::BigMiniPanel || type == MiniType::MiniPanelArm) && in(row, 26, 31))
            slow = false;
        else if ((type == MiniType::SmallMiniPanel || type == MiniType::MiniPanelArmLb) && in(row, 10, 16))
            slow = false;
        else if (type == MiniType::TinyMiniPanel && in(row, 5, 11))
            slow = false;
        else if (type == MiniType::TinyExMiniPanel && in(row, 0, 7))
            slow = false;
        else if (type == MiniType::MiniPanelArmTb && in(row, 0, 7))
            fast = rpm = false;
        else if (type == MiniType::Tb11I && in(row, 0, 10))
            fast = rpm = false;
        else if (type == MiniType::T322AI && in(row, 0, 10))
            slow = false;
        else if (type == MiniType::Oem || type == MiniType::Oem12I)
        {
            // The T3-OEM-12I's rows are the T3-OEM's four on.
            const int first = type == MiniType::Oem ? 8 : 12;
            if (in(row, first, first + 3))
            {
                only_these = true;
                only = { kFastPulse, kRpm };
            }
            else if (row == first + 4)
            {
                only_these = true;
                only = { k10kType2C, k10kType2F };
            }
        }
        else if (type == MiniType::FanModule && row == 4)
        {
            // RPM enabled; the other two as the resource leaves them.
        }
        else
        {
            fast = rpm = false;
        }

        // :1017-1026: PT 1K's two buttons follow special_flag's bit 0, which
        // is a setting read from the panel, and 0 on one not read.
        const bool pt1k = false;

        for (const auto& c : input_range_choices())
        {
            const int n = c.number;
            if (only_these)
            {
                if (std::find(only.begin(), only.end(), n) != only.end())
                    out.push_back(n);
                continue;
            }
            if ((n == kSlowPulse && !slow) || (n == kFastPulse && !fast) || (n == kRpm && !rpm))
                continue;
            if ((n == kPt1kC || n == kPt1kF) && !pt1k)
                continue;
            out.push_back(n);
        }
        return out;
    }
}
