// Checks how T5000 changes an input's Value, Calibration, Sign and Signal
// Type offline (offline/input_cells.cpp, offline/input_edit.cpp) against
// T3000's input grid:
//
//   - what Fresh_Input_Item makes of a value, a calibration and a signal
//     type (BacnetInput.cpp:597-693)
//   - the clicks on Value and Sign, and the T3-PT12's Signal Type
//     (OnNMClickList1), and that the click handler runs before the list
//     control opens its editor, so a click it refuses opens none
//   - which cells Fresh_Input_List enables for an analog and a digital input
//   - the Signal Type list (Initial_List), against T5000's
//   - that T3000 builds for x86 with the compiler's default floating point,
//     so a calibration is rounded as a float, as T5000 rounds it
//
// As in input_range_guard.cpp, the text is pinned as the port was written
// from it, with each run of whitespace made one space. A failure says what
// was copied has changed, not that T5000 is wrong.

#include "../display/tables.h"
#include "../offline/input_cells.h"
#include "../testing/check.h"
#include "source_text.h"

#include <stdio.h>

#include <initializer_list>
#include <string>
#include <vector>

namespace
{
    using namespace t5000::testing;
    using t5000::conformance::function_body;
    using t5000::conformance::read_source;
    namespace offline = t5000::offline;
    namespace display = t5000::display;

    int occurrences(const std::string& text, const std::string& part)
    {
        int n = 0;
        for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
            n++;
        return n;
    }

    std::string squash(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        bool space = false;
        for (const char c : s)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                space = true;
                continue;
            }
            if (space && !out.empty())
                out += ' ';
            space = false;
            out += c;
        }
        return out;
    }

    bool in_sequence(const std::string& text, std::initializer_list<const char*> parts)
    {
        size_t from = 0;
        for (const char* part : parts)
        {
            const size_t at = text.find(part, from);
            if (at == std::string::npos)
            {
                printf("        not found in order: %s\n", part);
                return false;
            }
            from = at + std::string(part).size();
        }
        return true;
    }

    bool read_or_fail(const char* relative, std::string& text)
    {
        std::string error;
        if (!read_source(relative, text, error))
        {
            check(false, (std::string(relative) + " can be read").c_str());
            printf("        %s\n", error.c_str());
            return false;
        }
        return true;
    }

    bool body_or_fail(const std::string& text, const char* signature, std::string& body)
    {
        std::string error;
        if (!function_body(text, signature, body, error))
        {
            check(false, (std::string(signature) + " is found").c_str());
            printf("        %s\n", error.c_str());
            return false;
        }
        body = squash(body);
        return true;
    }

    // What lies between two pieces of `text`, each found once.
    bool between(const std::string& text, const char* first, const char* last, std::string& part)
    {
        const size_t a = text.find(first);
        const size_t b = text.find(last);
        if (a == std::string::npos || b == std::string::npos || b < a || occurrences(text, first) != 1 ||
            occurrences(text, last) != 1)
        {
            check(false, (std::string("the part from \"") + first + "\" is found once").c_str());
            return false;
        }
        part = text.substr(a, b - a);
        return true;
    }

    const char* const kFreshInputItem = "LRESULT CBacnetInput::Fresh_Input_Item(WPARAM wParam,LPARAM lParam)";
    const char* const kClick          = "void CBacnetInput::OnNMClickList1(NMHDR *pNMHDR, LRESULT *pResult)";
    const char* const kFreshInputList = "LRESULT CBacnetInput::Fresh_Input_List(WPARAM wParam, LPARAM lParam)";

    void test_the_build_rounds_a_float_as_a_float()
    {
        section("T3000 and T5000 build for x86 with the compiler's default floating point");

        // Neither project, nor a property sheet T3000's imports, sets the
        // instruction set or the floating-point model. For x86 the defaults
        // are SSE2 and precise, under which a float times an int is rounded
        // to a float: what calibration_bytes does.
        const char* const files[] = {
            "T3000\\T3000_VS2019.vcxproj",
            "T3000\\config.props",
            "AppConfig.props",
            "..\\T5000.vcxproj",   // T5000's, at the root, beside T3000's tree
        };
        for (const char* file : files)
        {
            std::string text;
            if (!read_or_fail(file, text))
                continue;
            const bool none = occurrences(text, "EnableEnhancedInstructionSet") == 0 &&
                              occurrences(text, "FloatingPointModel") == 0 && occurrences(text, "/arch:") == 0 &&
                              occurrences(text, "/fp:") == 0;
            check(none, (std::string(file) + " sets neither the instruction set nor the floating-point model").c_str());
        }

        std::string sln;
        if (read_or_fail("T3000 - VS2019.sln", sln))
            check(occurrences(sln, "\"T3000\\T3000_VS2019.vcxproj\"") == 1, "T3000 is built from T3000_VS2019.vcxproj");

        std::string project;
        if (read_or_fail("T3000\\T3000_VS2019.vcxproj", project))
            check(occurrences(project, "<ProjectConfiguration Include=\"Release|Win32\">") == 1, "  for Win32, x86");
        std::string t5000;
        if (read_or_fail("..\\T5000.vcxproj", t5000))
            check(occurrences(t5000, "<ProjectConfiguration Include=\"Release|Win32\">") == 1, "  as T5000 is");
    }

    void test_a_value_typed()
    {
        section("a value typed: _wtof times 1000, truncated (Fresh_Input_Item)");

        std::string text;
        std::string body;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text) || !body_or_fail(text, kFreshInputItem, body))
            return;

        check(in_sequence(body, { "if(Changed_SubItem == INPUT_VALUE) {",
                                  "CString temp_cs = m_input_list.GetItemText(Changed_Item,Changed_SubItem);",
                                  "int temp_int = (int)(_wtof(temp_cs) * 1000);",
                                  "m_Input_data.at(Changed_Item).value = temp_int;" }),
              "the cell's text, as a double, times 1000, cast to int");
        check_eq(occurrences(body, ".value = "), 1, "  and nothing else there sets the value");
    }

    void test_a_calibration_typed()
    {
        section("a calibration typed: as a float, times 10, its size in two bytes (Fresh_Input_Item)");

        std::string text;
        std::string body;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text) || !body_or_fail(text, kFreshInputItem, body))
            return;

        std::string cal;
        if (!between(body, "if(Changed_SubItem==INPUT_CAL) {", "else if(Changed_SubItem==INPUT_FITLER)", cal))
            return;

        check(in_sequence(cal, { "float temp_value = (float)_wtof(cs_temp);",
                                 "if (temp_value < 0) { m_Input_data.at(Changed_Item).calibration_sign = 1;",
                                 "int cal_value; cal_value = (int)(temp_value * 10);",
                                 "cal_value = abs(cal_value);",
                                 "if((cal_value<0) || (cal_value >65535)) {",
                                 "MessageBox(_T(\"Please Input an value between 0.0 - 6553.6\")",
                                 "return 0; }",
                                 "m_Input_data.at(Changed_Item).calibration_l = cal_value & 0x00ff;",
                                 "m_Input_data.at(Changed_Item).calibration_h = (cal_value & 0xff00)>>8;" }),
              "a float; minus when below zero; times 10, truncated, its size; above 65535 refused; low and high bytes");
        check_eq(occurrences(cal, "temp_value * 10"), 1, "  times 10 once");
        check_eq(occurrences(cal, "temp_value * 100"), 0, "  and not the 0.01 steps, which are commented out");
        check_eq(occurrences(cal, "calibration_sign = "), 1,
                 "  the sign is only ever set minus: T5000 sets it plus for a number not below zero, as decided");
        check(cal.find("calibration_sign = 1") < cal.find("return 0;"),
              "  and it is set before the size is refused, where T5000 changes nothing");
    }

    void test_a_signal_type_chosen()
    {
        section("a signal type chosen: the last of JumperStatus's names that matches (Fresh_Input_Item)");

        std::string text;
        std::string body;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text) || !body_or_fail(text, kFreshInputItem, body))
            return;

        std::string jumper;
        if (!between(body, "if(Changed_SubItem == INPUT_JUMPER) {", "cmp_ret = memcmp", jumper))
            return;

        check(in_sequence(jumper, { "for (int z=0;z<sizeof(JumperStatus)/sizeof(JumperStatus[0]);z++) {",
                                    "if(temp_jump.CompareNoCase(JumperStatus[z]) == 0) {",
                                    "if((z == 0) || (z == 1) || (z == 2) || (z == 3) || (z == 4) || (z == 5)) "
                                    "temp_value = z;",
                                    "temp1 = m_Input_data.at(Changed_Item).decom ;",
                                    "temp1 = temp1 & 0x0f;",
                                    "temp1 = temp1 | (temp_value << 4);",
                                    "m_Input_data.at(Changed_Item).decom = temp1;" }),
              "each name compared without regard to case; the high nibble set, the low kept");
        check_eq(occurrences(jumper, "break"), 0, "  with no break, so the last that matches is stored");

        // T5000's lookup against that rule, over the names T3000 has
        // (tables_guard.cpp holds kJumperStatus to global_define.h).
        bool same = true;
        for (size_t i = 0; i < display::count(display::kJumperStatus); i++)
        {
            int last = -1;
            for (size_t z = 0; z < display::count(display::kJumperStatus); z++)
                if (offline::same_ignoring_case(display::kJumperStatus[i], display::kJumperStatus[z]))
                    last = (int)z;
            same = same && offline::signal_type_from_name(display::kJumperStatus[i]) == last;
        }
        check(same, "T5000 stores the last that matches for each name: 4 for Thermistor Dry Contact");
    }

    void test_the_signal_type_list()
    {
        section("the Signal Type list: JumperStatus without its index 4 (Initial_List)");

        std::string text;
        std::string body;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text) || !body_or_fail(text, "void CBacnetInput::Initial_List()", body))
            return;

        check(in_sequence(body, { "if(ListCtrlEx::ComboBox == m_input_list.GetColumnType(INPUT_JUMPER)) {",
                                  "for (int j=0;j<(int)sizeof(JumperStatus)/sizeof(JumperStatus[0]);j++) {",
                                  "if (j == 4) continue;",
                                  "strlist.push_back(JumperStatus[j]);",
                                  "m_input_list.SetCellStringList(i, INPUT_JUMPER, strlist);" }),
              "every name but index 4's");

        std::vector<std::string> expected;
        for (size_t j = 0; j < display::count(display::kJumperStatus); j++)
            if (j != 4)
                expected.push_back(display::kJumperStatus[j]);
        check(offline::signal_type_choices() == expected, "  which are T5000's choices, in that order");

        check(in_sequence(body, { "InsertColumn(INPUT_VALUE, _T(\"Value\"), 92, ListCtrlEx::EditBox,",
                                  "InsertColumn(INPUT_CAL, _T(\"Calibration\"), 80, ListCtrlEx::EditBox,",
                                  "InsertColumn(INPUT_CAL_OPERATION, _T(\"Sign\"), 57, ListCtrlEx::Normal,",
                                  "InsertColumn(INPUT_JUMPER, _T(\"Signal Type\"), 103, ListCtrlEx::ComboBox," }),
              "Value and Calibration are typed, Sign is clicked, Signal Type is chosen from a list");
    }

    void test_the_clicks()
    {
        section("the clicks on Value, Sign and a T3-PT12's Signal Type (OnNMClickList1)");

        std::string text;
        std::string body;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text) || !body_or_fail(text, kClick, body))
            return;

        check(body.find("m_input_list.Set_Edit(true);") < body.find("if(lCol == INPUT_VALUE)"),
              "a click lets the cell's editor open unless a branch below says not");

        std::string value;
        if (between(body, "if(lCol == INPUT_VALUE) {", "else if(lCol == INPUT_CAL_OPERATION) {", value))
        {
            check(in_sequence(value, { "if(m_Input_data.at(lRow).auto_manual == BAC_AUTO) { m_input_list.Set_Edit(false); return; }",
                                       "if(m_Input_data.at(lRow).digital_analog != BAC_UNITS_DIGITAL) return;",
                                       "if((m_Input_data.at(lRow).range < 23) &&(m_Input_data.at(lRow).range !=0)) "
                                       "temp1 = Digital_Units_Array[m_Input_data.at(lRow).range];",
                                       "else if((m_Input_data.at(lRow).range >=23) && (m_Input_data.at(lRow).range <= 30)) "
                                       "{ if(receive_custom_unit) temp1 = Custom_Digital_Range[m_Input_data.at(lRow).range - 23]; "
                                       "else { m_input_list.Set_Edit(false); return; } } else return;",
                                       "SplitCStringA(temparray,temp1,_T(\"/\"));",
                                       "if(m_Input_data.at(lRow).control == 0) { m_Input_data.at(lRow).control = 1;",
                                       "else { m_Input_data.at(lRow).control = 0;" }),
                  "Value: nothing in Auto; typed when not digital; a digital state switched on 1-22, and on 23-30 "
                  "only once their names are read");
            check_eq(occurrences(value, ".value ="), 0, "  the click switches the state, never the value");
            check_eq(occurrences(value, "Set_Edit(true)"), 0, "  and nothing there shuts the editor again");
        }

        std::string sign;
        if (between(body, "else if(lCol == INPUT_CAL_OPERATION) {", "else if(lCol == INPUT_AUTO_MANUAL)", sign))
        {
            check(in_sequence(sign, { "if(m_Input_data.at(lRow).digital_analog == BAC_UNITS_DIGITAL) return;",
                                      "notic_message.Format(_T(\"This will change the calibration of this input from %s to %s\")",
                                      "if(IDYES != MessageBox(notic_message,_T(\"Warning\"),MB_YESNOCANCEL)) return;",
                                      "if(m_Input_data.at(lRow).calibration_sign == 0) { m_Input_data.at(lRow).calibration_sign = 1;",
                                      "else { m_Input_data.at(lRow).calibration_sign = 0;" }),
                  "Sign: nothing on a digital input; asked first; then switched");
            check_eq(occurrences(sign, "calibration_l ="), 0, "  leaving the calibration's size");
            check_eq(occurrences(sign, "calibration_h ="), 0, "  both its bytes");
        }

        check_eq(occurrences(body, "else if((bacnet_device_type == PM_T3PT12) && (lCol == INPUT_JUMPER)) "
                                   "{ m_input_list.Set_Edit(false); return; }"),
                 1, "a T3-PT12's Signal Type opens no list");

        std::string list;
        if (body_or_fail(text, kFreshInputList, list))
        {
            check_eq(occurrences(list, "if ((Bacnet_Private_Device(selected_product_Node.product_class_id)) && "
                                       "Device_Basic_Setting.reg.mini_type != 0) bacnet_device_type = "
                                       "Device_Basic_Setting.reg.mini_type;"),
                     1, "  bacnet_device_type being the panel's type once the grid is shown");
        }

        std::string control;
        std::string down;
        if (read_or_fail("T3000\\CM5\\ListCtrlEx.cpp", control) &&
            body_or_fail(control, "void ListCtrlEx::CListCtrlEx::OnLButtonDown(UINT nFlags, CPoint point)", down))
        {
            check(in_sequence(down, { "CListCtrl::OnLButtonDown(nFlags, point);",
                                      "if(m_need_edit == false) return;",
                                      "if (bShouldAction && GetCellEnabled(ix.first, ix.second)) {",
                                      "case ComboBox: case EditBox: { ShowCellInPlace(ix, m_mapCol2ColType[ix.second]); }" }),
                  "the list control opens an editor after the click has been handled, unless it was told not to");
        }
    }

    void test_the_cells_enabled()
    {
        section("the cells enabled for an analog and a digital input (Fresh_Input_List)");

        std::string text;
        std::string body;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text) || !body_or_fail(text, kFreshInputList, body))
            return;

        check(in_sequence(body, { "if (m_Input_data.at(i).digital_analog == BAC_UNITS_ANALOG) { "
                                  "m_input_list.SetCellEnabled(i, INPUT_JUMPER, 0);",
                                  "m_input_list.SetCellEnabled(i, INPUT_CAL, 1); "
                                  "m_input_list.SetCellEnabled(i, INPUT_CAL_OPERATION, 1);",
                                  "if ((m_Input_data.at(i).range >= 20) && (m_Input_data.at(i).range <= 24)) {",
                                  "m_input_list.SetCellEnabled(i, INPUT_JUMPER, 1);",
                                  "else if (m_Input_data.at(i).digital_analog == BAC_UNITS_DIGITAL) { "
                                  "m_input_list.SetCellEnabled(i, INPUT_JUMPER, 0);",
                                  "m_input_list.SetCellEnabled(i, INPUT_CAL, 0); "
                                  "m_input_list.SetCellEnabled(i, INPUT_CAL_OPERATION, 0);" }),
              "analog: Calibration and Sign, and Signal Type on ranges 20-24; digital: none of them");
        check_eq(occurrences(body, "SetCellEnabled(i, INPUT_JUMPER"), 3, "  nothing else sets Signal Type's");
        check_eq(occurrences(body, "SetCellEnabled(i, INPUT_CAL,"), 2, "  nor Calibration's");
        check_eq(occurrences(body, "SetCellEnabled(i, INPUT_CAL_OPERATION"), 2, "  nor Sign's");
        check_eq(occurrences(body, "SetCellEnabled(i, INPUT_VALUE"), 0, "  and Value's is never disabled: the click decides");
    }
}

int run_input_cells_guard_tests()
{
    test_the_build_rounds_a_float_as_a_float();
    test_a_value_typed();
    test_a_calibration_typed();
    test_a_signal_type_chosen();
    test_the_signal_type_list();
    test_the_clicks();
    test_the_cells_enabled();
    return 0;
}
