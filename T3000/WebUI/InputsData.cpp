#include "stdafx.h"
#include "InputsData.h"

#include "../global_variable_extern.h"
#include "../global_function.h"
#include "../global_define.h"

extern vector<Str_in_point> m_Input_data;

namespace WebUI
{
    std::string ToUtf8(const CString& text)
    {
        if (text.IsEmpty())
            return std::string();

        const int bytes = WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), NULL, 0, NULL, NULL);
        if (bytes <= 0)
            return std::string();

        std::string utf8((size_t)bytes, '\0');
        WideCharToMultiByte(CP_UTF8, 0, text, text.GetLength(), &utf8[0], bytes, NULL, NULL);
        return utf8;
    }

    Json::Value BuildInputPointsJson()
    {
        Json::Value root(Json::objectValue);
        Json::Value rows(Json::arrayValue);

        const int count = (int)m_Input_data.size();

        for (int i = 0; i < count; i++)
        {
            const Str_in_point& point = m_Input_data.at(i);

            // The Ex helpers are the same conversion the graphics screens use, so
            // ranges, custom units and the digital/analog split stay in one place
            // rather than being restated here.
            //
            // They are not the code path the MFC Inputs dialog uses: that one is
            // inlined in Fresh_Input_List and formats analog values as %.2f where
            // GetInputValueEx uses %.1f. Expect that one difference when comparing
            // the two grids side by side. Unifying them is a real cleanup, but not
            // one to make while proving out the host.
            CString value, unit, autoManual, label, fullLabel;
            int digitalValue = 0;

            GetInputValueEx(point, value, unit, autoManual, digitalValue);
            GetInputLabelEx(point, label, NULL);
            GetInputFullLabelEx(point, fullLabel, NULL);

            // Calibration is a sign bit plus a high/low byte pair, not a plain int.
            const int calibration = (point.calibration_h << 8) | point.calibration_l;

            Json::Value row(Json::objectValue);
            row["index"] = i;
            row["input"] = i + 1;
            row["panel"] = (int)point.sub_id;
            row["fullLabel"] = ToUtf8(fullLabel);
            row["label"] = ToUtf8(label);
            row["autoManual"] = point.auto_manual == 1 ? "Manual" : "Auto";
            row["value"] = ToUtf8(value);
            row["unit"] = ToUtf8(unit);
            row["range"] = (int)point.range;
            row["calibration"] = calibration;
            row["calibrationSign"] = point.calibration_sign == 1 ? "-" : "+";
            row["filter"] = (int)point.filter;
            row["status"] = point.decom == 1 ? "Decommissioned" : "OK";
            row["digitalAnalog"] = point.digital_analog == BAC_UNITS_ANALOG ? "Analog" : "Digital";

            rows.append(row);
        }

        root["serialNumber"] = (int)Device_Basic_Setting.reg.n_serial_number;
        root["count"] = count;
        root["inputs"] = rows;

        return root;
    }
}
