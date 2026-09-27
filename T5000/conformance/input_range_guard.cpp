// Checks the ranges T5000 lets an input configured offline be given
// (offline/input_ranges.cpp) against T3000's Range dialog and input grid:
//
//   - the dialog's buttons, their captions in T3000.rc, and the range each
//     gives (BacnetRange::OnTimer): T5000's list of ranges is compared with
//     the resource itself, caption by caption
//   - the number its box holds, and how OK reads it (OnOK)
//   - which buttons it enables for a panel's type and row (Initial_static)
//   - the rows whose Range cell the grid ignores (OnNMClickList1)
//   - what a range chosen changes in the input, and what it leaves
//   - that the grid enables an input's filter only when it is analog
//     (Fresh_Input_List), and that its list control honours that
//
// As in offline_guard.cpp, the text is pinned as the port was written from
// it, here with each run of spaces, tabs and line breaks made one space. A
// failure says what was copied has changed, not that T5000 is wrong.

#include "../device/product.h"
#include "../display/tables.h"
#include "../offline/input_ranges.h"
#include "../testing/check.h"
#include "source_text.h"

#include <stdio.h>

#include <initializer_list>
#include <map>
#include <string>

namespace
{
    using namespace t5000::testing;
    using t5000::conformance::function_body;
    using t5000::conformance::parse_constant;
    using t5000::conformance::read_source;
    namespace offline = t5000::offline;

    int occurrences(const std::string& text, const std::string& part)
    {
        int n = 0;
        for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
            n++;
        return n;
    }

    // Each run of whitespace as one space.
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

    // True when each part is found after the one before it. A part may be
    // found elsewhere too: the order is what is held. The first part not
    // found is printed, unless `quiet`.
    bool in_sequence(const std::string& text, std::initializer_list<const char*> parts, bool quiet = false)
    {
        size_t from = 0;
        for (const char* part : parts)
        {
            const size_t at = text.find(part, from);
            if (at == std::string::npos)
            {
                if (!quiet)
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

    // The squashed body of the function with this signature.
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

    void test_the_searches_on_their_own()
    {
        section("the searches the checks below rely on");

        check(squash(" a \t b\r\n\r\nc ") == "a b c", "whitespace squashes to single spaces, none at the ends");
        check(in_sequence("a b a c", { "a", "c" }), "parts in order are in sequence");
        check(in_sequence("a b a c", { "b", "a" }), "  a part may be found again later");
        check(!in_sequence("a c b", { "b", "c" }, true), "  and out of order is not");
    }

    void test_the_button_ids_run_in_order()
    {
        section("the dialog's buttons are numbered in runs, as its loops count them (resource.h)");

        std::string text;
        if (!read_or_fail("T3000\\resource.h", text))
            return;

        struct Run
        {
            int first;
            int last;
        };
        // IDC_RADIO35-46 and 89-99 are digital 0-22, 54-72 analog 0-18,
        // 81-88 analog 19-26, 101-113 analog 27-39.
        const Run runs[] = { { 35, 46 }, { 89, 99 }, { 54, 72 }, { 81, 88 }, { 101, 113 } };
        for (const Run& r : runs)
        {
            long start = 0;
            std::string error;
            const std::string first = "IDC_RADIO" + std::to_string(r.first);
            if (!parse_constant(text, first, start, error))
            {
                check(false, (first + " is found").c_str());
                continue;
            }
            bool run = true;
            for (int i = r.first + 1; i <= r.last; i++)
            {
                long v = 0;
                run = run && parse_constant(text, "IDC_RADIO" + std::to_string(i), v, error) && v == start + (i - r.first);
            }
            check(run, (first + " to IDC_RADIO" + std::to_string(r.last) + " are numbered one after another").c_str());
        }
    }

    // A button of the Range dialog, as T3000.rc gives it.
    struct Button
    {
        std::string caption;
        bool        visible = true;
    };

    // The caption without T3000's "41." before it, and with each run of
    // spaces one space. `number` is the number it had, or -1.
    std::string plain_caption(const std::string& caption, int& number)
    {
        number = -1;
        size_t i = 0;
        while (i < caption.size() && caption[i] == ' ')
            i++;
        size_t digits = i;
        while (digits < caption.size() && caption[digits] >= '0' && caption[digits] <= '9')
            digits++;
        std::string rest = caption;
        if (digits > i && digits < caption.size() && caption[digits] == '.')
        {
            number = std::stoi(caption.substr(i, digits - i));
            rest   = caption.substr(digits + 1);
        }
        return squash(rest);
    }

    bool read_buttons(std::map<std::string, Button>& buttons)
    {
        std::string rc;
        if (!read_or_fail("T3000\\T3000.rc", rc))
            return false;

        std::string dialog;
        const size_t at = rc.find("IDD_DIALOG_BACNET_RANGES DIALOGEX");
        const size_t end = at == std::string::npos ? at : rc.find("\nEND", at);
        if (!require(at != std::string::npos && end != std::string::npos, "T3000.rc has the Range dialog"))
            return false;
        dialog = rc.substr(at, end - at);

        check_eq(occurrences(dialog, "WS_DISABLED"), 0, "  and disables none of its buttons");

        size_t line_start = 0;
        while (line_start < dialog.size())
        {
            size_t line_end = dialog.find('\n', line_start);
            if (line_end == std::string::npos)
                line_end = dialog.size();
            const std::string line = dialog.substr(line_start, line_end - line_start);
            line_start = line_end + 1;

            const size_t control = line.find("CONTROL");
            const size_t open = line.find('"');
            if (control == std::string::npos || open == std::string::npos)
                continue;
            const size_t close = line.find('"', open + 1);
            const size_t id = line.find("IDC_RADIO", close);
            if (close == std::string::npos || id == std::string::npos)
                continue;
            size_t id_end = id;
            while (id_end < line.size() && t5000::conformance::is_identifier_char(line[id_end]))
                id_end++;

            Button b;
            b.caption = line.substr(open + 1, close - open - 1);
            b.visible = line.find("NOT WS_VISIBLE") == std::string::npos;
            buttons[line.substr(id, id_end - id)] = b;
        }
        return true;
    }

    void test_the_captions_are_t3000s()
    {
        section("each range offered is a button of T3000's Range dialog, captioned as T5000 names it (T3000.rc)");

        std::map<std::string, Button> buttons;
        if (!read_buttons(buttons))
            return;

        auto button = [&](int id) -> const Button* {
            const auto it = buttons.find("IDC_RADIO" + std::to_string(id));
            return it == buttons.end() ? nullptr : &it->second;
        };

        // The digital buttons: "0. No Units", then "1. Off/On" to
        // "22. High/Low", the names of Digital_Units_Array.
        bool digital = true;
        for (int n = 0; n <= 22; n++)
        {
            const Button* b = button(n <= 11 ? 35 + n : 89 + n - 12);
            int number = -1;
            const std::string caption = b ? plain_caption(b->caption, number) : std::string();
            const std::string want = n == 0 ? "No Units" : t5000::display::kDigitalUnits[n];
            if (!b || number != n || caption != want)
            {
                printf("        digital %d: \"%s\"\n", n, b ? b->caption.c_str() : "(no button)");
                digital = false;
            }
        }
        check(digital, "the digital buttons are 0-22, named as Digital_Units_Array names them");
        check(offline::find_input_range(0) != nullptr, "  0, No Units, is offered, as Unused, what OK makes it");

        // The analog buttons, by the range each gives.
        int named = 0;
        bool same = true;
        bool numbered = true;
        bool blanks_left_out = true;
        for (int range = 1; range <= 39; range++)
        {
            const int id = range <= 18 ? 54 + range : range <= 26 ? 81 + range - 19 : 101 + range - 27;
            const Button* b = button(id);
            if (!b)
            {
                printf("        no button IDC_RADIO%d for analog range %d\n", id, range);
                same = false;
                continue;
            }
            int number = -1;
            const std::string caption = plain_caption(b->caption, number);
            if (number != -1 && number != range + 30)
            {
                printf("        IDC_RADIO%d is captioned %d, for range %d\n", id, number, range);
                numbered = false;
            }

            const offline::InputRangeChoice* c = offline::find_input_range(range + 30);
            if (caption.empty())
            {
                blanks_left_out = blanks_left_out && c == nullptr;
                continue;
            }
            named++;
            if (!c || caption != c->caption)
            {
                printf("        %d: T3000 \"%s\", T5000 \"%s\"\n", range + 30, caption.c_str(), c ? c->caption : "(none)");
                same = false;
            }
        }
        check(same, "every analog button with a name is offered, by that name");
        check(numbered, "  each numbered, where its caption numbers it, 30 above the range it gives");
        check(blanks_left_out, "  and the buttons with no name are not offered");

        int analog_offered = 0;
        for (const auto& c : offline::input_range_choices())
            analog_offered += c.number > 30;
        check_eq(analog_offered, named, "T5000 offers no analog range the dialog has no button for");

        // The first of each temperature pair is visible and names no scale;
        // the second is hidden, and the °F button picks it.
        bool pairs = true;
        for (int range = 1; range <= 9; range += 2)
        {
            const Button* c = button(54 + range);
            const Button* f = button(54 + range + 1);
            const offline::InputRangeChoice* first = offline::find_input_range(range + 30);
            pairs = pairs && c && f && c->visible && !f->visible && first &&
                    std::string(first->scale) == "\xC2\xB0" "C";
        }
        check(pairs, "the five temperature pairs: the first shown and named in \xC2\xB0" "C, the second hidden");

        bool no_other_scale = true;
        for (const auto& c : offline::input_range_choices())
        {
            const bool first_of_pair = c.number >= 31 && c.number <= 39 && c.number % 2 == 1;
            no_other_scale = no_other_scale && (c.scale[0] != '\0') == first_of_pair;
        }
        check(no_other_scale, "  and no other range names a scale");
    }

    void test_the_buttons_give_these_ranges()
    {
        section("the range each button gives, and the number in the box (BacnetRange::OnTimer, OnOK)");

        std::string text;
        if (!read_or_fail("T3000\\BacnetRange.cpp", text))
            return;

        std::string body;
        if (body_or_fail(text, "void BacnetRange::OnTimer(UINT_PTR nIDEvent)", body))
        {
            check(in_sequence(body, { "m_digital_select = i - IDC_RADIO35;", "m_digital_select = i - IDC_RADIO89 + 12;",
                                      "m_digital_select = i - IDC_RADIO73 + 23;" }),
                  "digital: IDC_RADIO35 and on 0-11, IDC_RADIO89 and on 12-22, IDC_RADIO73 and on the custom 23-30");
            check(in_sequence(body, { "if(initial_dialog == 2) { for (int i=IDC_RADIO55;i<=IDC_RADIO72;i++)",
                                      "m_input_Analog_select = i - IDC_RADIO54;" }),
                  "an input's analog: IDC_RADIO55 to 72 give ranges 1 to 18");
            check(in_sequence(body, { "int temp_degf = ((CButton*)GetDlgItem(IDC_RADIO_DEGF))->GetCheck(); if (temp_degf)",
                                      "m_input_Analog_select = m_input_Analog_select + 1;" }),
                  "  with the \xC2\xB0" "F button, the first of a pair gives the next range");
            check(in_sequence(body, { "for (int i = IDC_RADIO101;i <= IDC_RADIO113;i++)",
                                      "m_input_Analog_select = i - IDC_RADIO101 + 27;" }),
                  "  IDC_RADIO101 to 113 give 27 to 39");
            check(in_sequence(body, { "(IDC_RADIO81))->GetCheck()) { m_input_Analog_select = 19; }",
                                      "(IDC_RADIO82))->GetCheck()) { m_input_Analog_select = 20; }",
                                      "(IDC_RADIO83))->GetCheck()) { m_input_Analog_select = 21; }",
                                      "(IDC_RADIO84))->GetCheck()) { m_input_Analog_select = 22; }",
                                      "(IDC_RADIO85))->GetCheck()) { m_input_Analog_select = 23; }",
                                      "(IDC_RADIO86))->GetCheck()) { m_input_Analog_select = 24; }",
                                      "(IDC_RADIO87))->GetCheck()) { m_input_Analog_select = 25; }",
                                      "(IDC_RADIO88))->GetCheck()) { m_input_Analog_select = 26; }" }),
                  "  and IDC_RADIO81 to 88 give 19 to 26");
        }

        if (body_or_fail(text, "void BacnetRange::OnOK()", body))
        {
            check(in_sequence(body, { "GetDlgItemText(IDC_EDIT_RANGE_SELECT,temp);", "int temp_value = _wtoi(temp);",
                                      "if(temp_value > 30) { temp_value = _wtoi(temp) - 30;",
                                      "else if(initial_dialog == 2) { bac_ranges_type = INPUT_RANGE_ANALOG_TYPE; }",
                                      "else if(initial_dialog == 2) { bac_ranges_type = INPUT_RANGE_DIGITAL_TYPE; }",
                                      "bac_range_number_choose = temp_value;" }),
                  "OK reads the box: above 30 is the analog range 30 below, anything else digital");
            check_eq((long)offline::input_range_bytes(41).digital_analog, 1L, "  as T5000 reads 41: analog");
            check_eq((long)offline::input_range_bytes(41).range, 11L, "  range 11");
            check_eq((long)offline::input_range_bytes(30).digital_analog, 0L, "  and 30 digital");
        }

        if (body_or_fail(text, "void BacnetRange::Initial_static()", body))
        {
            check(in_sequence(body, { "else if((bac_ranges_type == INPUT_RANGE_ANALOG_TYPE) || (initial_dialog == 2))",
                                      "if(bac_range_number_choose == 0) { temp_cs.Format(_T(\"%d\"),bac_range_number_choose);",
                                      "else { temp_cs.Format(_T(\"%d\"),bac_range_number_choose + 30); }",
                                      "temp_cs.Format(_T(\"%d\"),bac_range_number_choose); }",
                                      "GetDlgItem(IDC_EDIT_RANGE_SELECT)->SetWindowTextW(temp_cs);" }),
                  "the dialog opens with an analog input's range plus 30 in the box, but 0, and a digital one's as it is");
        }
    }

    void test_the_buttons_the_dialog_enables()
    {
        section("which buttons the dialog enables for a panel's type and row (BacnetRange::Initial_static)");

        std::string header;
        if (read_or_fail("T3000\\BacnetRange.h", header))
        {
            check(occurrences(squash(header), "void SetAllRadioButton(int button_index = 2);") == 1,
                  "SetAllRadioButton() with no argument is SetAllRadioButton(2)");
            long v = 0;
            std::string error;
            check(parse_constant(header, "RANGE_RADIO_DISABLE", v, error) && v == 2, "  which is RANGE_RADIO_DISABLE");
        }

        std::string text;
        if (!read_or_fail("T3000\\BacnetRange.cpp", text))
            return;

        std::string body;
        if (body_or_fail(text, "void BacnetRange::SetAllRadioButton(int button_index )", body))
        {
            const char* const loops[] = {
                "for (int i = IDC_RADIO35;i <= IDC_RADIO46;i++) { if (button_index == RANGE_RADIO_DISABLE) ((CButton *)GetDlgItem(i))->EnableWindow(0);",
                "for (int i = IDC_RADIO89;i <= IDC_RADIO99;i++) { if (button_index == RANGE_RADIO_DISABLE) ((CButton *)GetDlgItem(i))->EnableWindow(0);",
                "for (int i = IDC_RADIO54;i <= IDC_RADIO72;i++) { if (button_index == RANGE_RADIO_DISABLE) ((CButton *)GetDlgItem(i))->EnableWindow(0);",
                "for (int i = IDC_RADIO101;i <= IDC_RADIO113;i++) { if (button_index == RANGE_RADIO_DISABLE) ((CButton *)GetDlgItem(i))->EnableWindow(0);",
                "for (int i = IDC_RADIO81;i <= IDC_RADIO88;i++) { if (button_index == RANGE_RADIO_DISABLE) ((CButton *)GetDlgItem(i))->EnableWindow(0);",
            };
            bool all = true;
            for (const char* loop : loops)
                all = all && occurrences(body, loop) == 1;
            check(all, "  and it disables every digital and analog button of an input's dialog");
        }

        if (!body_or_fail(text, "void BacnetRange::Initial_static()", body))
            return;

        std::string chain;
        if (!between(body, "if((Device_Basic_Setting.reg.mini_type == BIG_MINIPANEL || bacnet_device_type == MINIPANELARM)",
                     "GetDlgItem(IDC_RADIO54)->ShowWindow(false);", chain))
            return;

        check(in_sequence(chain, {
                  "if((Device_Basic_Setting.reg.mini_type == BIG_MINIPANEL || bacnet_device_type == MINIPANELARM) && (input_list_line >=26) && (input_list_line <=31))",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO87)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO103)->EnableWindow(TRUE); }",
                  "else if((Device_Basic_Setting.reg.mini_type == SMALL_MINIPANEL || bacnet_device_type == MINIPANELARM_LB) && (input_list_line >=10) && (input_list_line <=16))",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO87)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO103)->EnableWindow(TRUE); }",
                  "else if((Device_Basic_Setting.reg.mini_type == TINY_MINIPANEL) && (input_list_line >=5) && (input_list_line <=11))",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO87)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO103)->EnableWindow(TRUE); }",
                  "else if ((Device_Basic_Setting.reg.mini_type == TINY_EX_MINIPANEL ) && (input_list_line >= 0) && (input_list_line <= 7))",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO87)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO103)->EnableWindow(TRUE); }",
                  "else if ((Device_Basic_Setting.reg.mini_type == MINIPANELARM_TB) && (input_list_line >= 0) && (input_list_line <= 7))",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO87)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO103)->EnableWindow(FALSE); }",
                  "else if ((Device_Basic_Setting.reg.mini_type == T3_TB_11I) && (input_list_line >= 0) && (input_list_line <= 10))",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO87)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO103)->EnableWindow(FALSE); }",
                  "else if((bacnet_device_type == PID_T322AI) && (input_list_line >= 0) && (input_list_line <=10))",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO87)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO103)->EnableWindow(TRUE); }",
                  "else if ((Device_Basic_Setting.reg.mini_type == T3_OEM)) { if ((input_list_line >= 8) && (input_list_line <= 11)) { SetAllRadioButton(); GetDlgItem(IDC_RADIO87)->EnableWindow(1); GetDlgItem(IDC_RADIO103)->EnableWindow(1);",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(FALSE); } else if (input_list_line == 12) { SetAllRadioButton();",
                  "GetDlgItem(IDC_RADIO57)->EnableWindow(1); GetDlgItem(IDC_RADIO58)->EnableWindow(1); } }",
                  "else if ((Device_Basic_Setting.reg.mini_type == T3_OEM_12I)) { if ((input_list_line >= 12) && (input_list_line <= 15)) { SetAllRadioButton(); GetDlgItem(IDC_RADIO87)->EnableWindow(1); GetDlgItem(IDC_RADIO103)->EnableWindow(1);",
                  "GetDlgItem(IDC_RADIO69)->EnableWindow(FALSE); } else if (input_list_line == 16) { SetAllRadioButton();",
                  "GetDlgItem(IDC_RADIO57)->EnableWindow(1); GetDlgItem(IDC_RADIO58)->EnableWindow(1); } }",
                  "else if ((Device_Basic_Setting.reg.mini_type == T3_FAN_MODULE) && (input_list_line == 4)) { GetDlgItem(IDC_RADIO103)->EnableWindow(TRUE); }",
                  "else { GetDlgItem(IDC_RADIO69)->EnableWindow(TRUE); GetDlgItem(IDC_RADIO87)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO103)->EnableWindow(FALSE); }",
                  "if ((Device_Basic_Setting.reg.special_flag & 0x01) == 0x01) { GetDlgItem(IDC_RADIO63)->EnableWindow(true); GetDlgItem(IDC_RADIO64)->EnableWindow(true); }",
                  "else { GetDlgItem(IDC_RADIO63)->EnableWindow(FALSE); GetDlgItem(IDC_RADIO64)->EnableWindow(FALSE); }",
              }),
              "the chain, branch by branch, as input_ranges_offered follows it");
        check_eq(occurrences(chain, "EnableWindow("), 39, "  and no button enabled or disabled but those");
        check_eq(occurrences(chain, "SetAllRadioButton("), 4, "  nor every button disabled but on those four rows");
    }

    void test_the_rows_whose_range_is_fixed()
    {
        section("the rows whose Range cell the grid ignores (CBacnetInput::OnNMClickList1)");

        std::string text;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text))
            return;

        std::string body;
        if (!body_or_fail(text, "void CBacnetInput::OnNMClickList1(NMHDR *pNMHDR, LRESULT *pResult)", body))
            return;

        std::string gates;
        if (!between(body, "else if(lCol == INPUT_RANGE)", "m_dialog_signal_type = 0xff; BacnetRange dlg;", gates))
            return;

        check(in_sequence(gates, {
                  "if ((g_selected_product_id == PM_TSTAT_AQ) || (g_selected_product_id == PM_AIRLAB_ESP32)) return;;",
                  "if (g_selected_product_id == PM_TSTAT10) { if (Device_Basic_Setting.reg.mini_type == T3_OEM) { if((lRow >= 13) && (lRow <= 17)) return; }",
                  "if (Device_Basic_Setting.reg.mini_type == T3_OEM_12I) { if ((lRow >= 17) && (lRow <= 21)) return; }",
                  "if (Device_Basic_Setting.reg.mini_type == T3_TSTAT10) { if ((lRow >= 9) && (lRow <= 12)) return; }",
                  "if (Device_Basic_Setting.reg.mini_type == T3_TSTAT11) { if ((lRow >= 9) && (lRow <= 12)) return; } }",
                  "if (g_selected_product_id == PM_ESP32_T3_SERIES) { if (Device_Basic_Setting.reg.mini_type == T3_RMC1232) { if ((lRow >= 8) && (lRow <= 11)) return; if ((lRow >= 32) && (lRow <= 47)) return; }",
                  "if (Device_Basic_Setting.reg.mini_type == T3_BMS) { if ((lRow >= 32) && (lRow <= 47)) return; } }",
                  "if (PM_ESP32_T3_SERIES == g_selected_product_id) { if (Device_Basic_Setting.reg.mini_type == T3_ESP_RMC) { if ((lRow >= 16) && (lRow <= 17)) return; }",
                  "if (Device_Basic_Setting.reg.mini_type == T3_BMS) { if ((lRow == 32) && (lRow <= 47)) return; }",
                  "if (Device_Basic_Setting.reg.mini_type == T3_RMC1232) { if ((lRow == 32) && (lRow <= 47)) return; }",
                  "else if (Device_Basic_Setting.reg.mini_type == T3_NG3) { if ((lRow >= 24) && (lRow <= 29)) return; } }",
              }),
              "the gates, as input_range_fixed follows them");
        check_eq(occurrences(gates, "return;"), 12, "  and no other row ignored");
    }

    void test_what_a_range_changes()
    {
        section("what the Range dialog's answer changes in an input (CBacnetInput::OnNMClickList1)");

        std::string text;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text))
            return;

        std::string body;
        if (!body_or_fail(text, "void CBacnetInput::OnNMClickList1(NMHDR *pNMHDR, LRESULT *pResult)", body))
            return;

        std::string answer;
        if (!between(body, "if(bac_range_number_choose == 0)",
                     "else if((bacnet_device_type == PM_T3PT12) && (lCol == INPUT_JUMPER))", answer))
            return;

        check(in_sequence(answer, { "if(bac_range_number_choose == 0) { m_Input_data.at(lRow).digital_analog = BAC_UNITS_ANALOG; bac_ranges_type = INPUT_RANGE_ANALOG_TYPE; }" }),
              "0 makes the input analog");
        check(in_sequence(answer, { "if(bac_ranges_type == INPUT_RANGE_ANALOG_TYPE) { m_Input_data.at(lRow).digital_analog = BAC_UNITS_ANALOG; m_Input_data.at(lRow).range = bac_range_number_choose;",
                                    "m_Input_data.at(lRow).digital_analog = BAC_UNITS_DIGITAL; m_Input_data.at(lRow).range = bac_range_number_choose;" }),
              "an analog range sets analog and the range, a digital one digital and the range");
        check_eq(occurrences(answer, ".value = "), 0, "  and neither changes the value");
        check_eq(occurrences(answer, ".control = "), 0, "  the state");
        check_eq(occurrences(answer, ".calibration_h = ") + occurrences(answer, ".calibration_l = ") +
                     occurrences(answer, ".calibration_sign = "),
                 0, "  or the calibration");

        // T5000 leaves the signal type alone when a table is chosen: T3000
        // copies m_dialog_signal_type, which the custom-table dialog sets
        // and which is 0xff when that dialog was not opened. It is changed
        // in the Signal Type column instead.
        check(in_sequence(answer, { "if ((bac_range_number_choose >= 20) && (bac_range_number_choose <= 24))",
                                    "m_Input_data.at(lRow).decom = m_dialog_signal_type;" }),
              "a table copies the custom-table dialog's signal type, which T5000 does not open");
        check_eq(occurrences(answer, "decom = "), 2, "  and nothing else writes the signal type");

        check(in_sequence(answer, { "temp1 = Digital_Units_Array[bac_range_number_choose];" }),
              "a digital range past 30 would be looked up in Digital_Units_Array, so 101-104 are not offered");
        check_eq((long)(sizeof(t5000::display::kDigitalUnits) / sizeof(t5000::display::kDigitalUnits[0])), 23L,
                 "  which has 23 entries");

        std::string defines;
        long v = 0;
        std::string error;
        if (read_or_fail("T3000\\global_define.h", defines))
        {
            check(parse_constant(defines, "BAC_UNITS_ANALOG", v, error) && v == 1, "BAC_UNITS_ANALOG is 1");
            check(parse_constant(defines, "BAC_UNITS_DIGITAL", v, error) && v == 0, "BAC_UNITS_DIGITAL is 0");
        }
    }

    void test_the_filter_follows_analog()
    {
        section("the grid enables an input's filter when it is analog, and not when digital");

        std::string text;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text))
            return;

        std::string body;
        if (body_or_fail(text, "LRESULT CBacnetInput::Fresh_Input_List(WPARAM wParam, LPARAM lParam)", body))
        {
            check(in_sequence(body, { "if (m_Input_data.at(i).digital_analog == BAC_UNITS_ANALOG) {",
                                      "m_input_list.SetCellEnabled(i, INPUT_FITLER, 1);",
                                      "else if (m_Input_data.at(i).digital_analog == BAC_UNITS_DIGITAL) {",
                                      "m_input_list.SetCellEnabled(i, INPUT_FITLER, 0);" }),
                  "Fresh_Input_List: analog enables Filter, digital disables it");
            check_eq(occurrences(body, "SetCellEnabled(i, INPUT_FITLER"), 2, "  and nothing else sets it");
        }

        std::string list;
        if (read_or_fail("T3000\\CM5\\ListCtrlEx.cpp", list))
        {
            check_eq(occurrences(list, "if (bShouldAction && GetCellEnabled(ix.first, ix.second))"), 2,
                     "the list control opens a cell's editor, on a click or a double click, only when it is enabled");
            check_eq(occurrences(list, "if (!GetCellEnabled(pDispInfo->item.iItem, pDispInfo->item.iSubItem))"), 1,
                     "  and refuses to edit its label otherwise");
        }
    }
}

int run_input_range_guard_tests()
{
    test_the_searches_on_their_own();
    test_the_button_ids_run_in_order();
    test_the_captions_are_t3000s();
    test_the_buttons_give_these_ranges();
    test_the_buttons_the_dialog_enables();
    test_the_rows_whose_range_is_fixed();
    test_what_a_range_changes();
    test_the_filter_follows_analog();
    return 0;
}
