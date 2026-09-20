#include "stdafx.h"
#include "InputsData.h"

#include "../global_variable_extern.h"
#include "../global_function.h"
#include "../global_define.h"

#include <map>

extern vector<Str_in_point> m_Input_data;

namespace WebUI
{
    // Post_Write_Message takes the row as int8_t, so row 128 would arrive as -128.
    // The MFC dialog never hits this because its row count is bounded by what it
    // displays, but an index arriving from a page is not bounded by anything.
    static const int kMaxWritableIndex = 127;

    // Pre-write snapshots, mirroring m_temp_Input_data in CBacnetInput. The device
    // write is asynchronous and can fail, and the point has already been changed in
    // memory by then, so the old value has to be held until the outcome is known.
    static std::map<int, Str_in_point> g_pendingWrites;

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

    UpdateResult UpdateInputFullLabel(int index,
                                      const CString& newLabel,
                                      const CString& expectedCurrentLabel,
                                      HWND completionTarget)
    {
        UpdateResult result = { false, _T("") };

        if (index < 0 || index >= (int)m_Input_data.size())
        {
            result.message = _T("That input does not exist on the current device.");
            return result;
        }

        if (index > kMaxWritableIndex)
        {
            result.message.Format(_T("Input %d cannot be written: the device write ")
                                  _T("command addresses rows as a signed byte and ")
                                  _T("stops at %d."), index + 1, kMaxWritableIndex + 1);
            return result;
        }

        // Refuse rather than write to a row the caller may not have meant. The read
        // path is what produced this index; if its ordering ever drifts from
        // m_Input_data, this is what stops a live point being renamed by mistake.
        CString currentLabel;
        GetInputFullLabelEx(m_Input_data.at(index), currentLabel, NULL);
        if (currentLabel != expectedCurrentLabel)
        {
            result.message = _T("This input changed since the page last read it. ")
                             _T("Refresh and try again.");
            return result;
        }

        CString trimmed = newLabel;
        trimmed.Trim();

        if (trimmed.GetLength() >= STR_IN_DESCRIPTION_LENGTH)
        {
            result.message.Format(_T("Full Label must be shorter than %d characters."),
                                  STR_IN_DESCRIPTION_LENGTH);
            return result;
        }

        // Same gate the MFC dialog applies: some columns are not writable on some
        // products, and the device rejects those writes anyway.
        if (Get_Product_Input_Map(g_selected_product_id, INPUT_FULL_LABLE) == false)
        {
            result.message = _T("This device does not allow the Full Label to be changed.");
            return result;
        }

        if (Check_FullLabel_Exsit(trimmed))
        {
            result.message = _T("Another point already uses that Full Label.");
            return result;
        }

        g_pendingWrites[index] = m_Input_data.at(index);

        char ansi[STR_IN_DESCRIPTION_LENGTH * 2];
        memset(ansi, 0, sizeof(ansi));
        WideCharToMultiByte(CP_ACP, 0, trimmed, -1, ansi, sizeof(ansi) - 1, NULL, NULL);

        memset(m_Input_data.at(index).description, 0, STR_IN_DESCRIPTION_LENGTH);
        memcpy_s(m_Input_data.at(index).description, STR_IN_DESCRIPTION_LENGTH,
                 ansi, STR_IN_DESCRIPTION_LENGTH);

        // Deliberately not mirroring the PM_THIRD_PARTY_DEVICE branch in
        // Fresh_Input_Item. It issues an extra BACnet PROP_DESCRIPTION write and
        // leaks the BACNET_APPLICATION_DATA_VALUE it allocates. Third-party devices
        // are out of scope here; copying a leak to match behaviour is not worth it.

        CString taskInfo;
        taskInfo.Format(_T("Write Input %d Full Label to \"%s\" "), index + 1, (LPCTSTR)trimmed);

        if (!Post_Write_Message(g_bac_instance, WRITEINPUT_T3000,
                                (int8_t)index, (int8_t)index, sizeof(Str_in_point),
                                completionTarget, taskInfo, index, INPUT_FULL_LABLE))
        {
            ReportUpdateOutcome(index, false);
            result.message = _T("Could not queue the write. Is the device still connected?");
            return result;
        }

        result.queued = true;
        return result;
    }

    void ReportUpdateOutcome(int index, bool succeeded)
    {
        std::map<int, Str_in_point>::iterator it = g_pendingWrites.find(index);
        if (it == g_pendingWrites.end())
            return;

        if (!succeeded && index >= 0 && index < (int)m_Input_data.size())
        {
            // The point was already changed in memory before the write went out,
            // so a rejection has to put the old value back or the page and the
            // device disagree from here on.
            memcpy_s(&m_Input_data.at(index), sizeof(Str_in_point),
                     &it->second, sizeof(Str_in_point));
        }

        g_pendingWrites.erase(it);
    }
}
