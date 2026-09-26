// Checks the rules T5000 copies from T3000 to change the inputs of a device
// added by hand, before any scan has found it (offline/input_edit.cpp). Few
// of them are a constant; most are the way T3000's grid takes a change:
//
//   - what every point is before a panel is read, from Initial_All_Point
//     (global_function.cpp:17693): an input's bytes before any change, and
//     the names a new full label may not repeat
//   - how Fresh_Input_Item takes a new Full Label, Label, Auto/Manual or
//     Filter (BacnetInput.cpp:451-706), and how a click flips Auto/Manual
//     (OnNMClickList1, :1421)
//   - what Check_FullLabel_Exsit and Check_Label_Exsit compare a new name
//     with (global_function.cpp:3151-3330)
//   - which columns the grid lets a product change (Inial_Product_Input_map,
//     :7477), and that the products it narrows are none T5000 configures
//     offline
//   - that T3000 never sets a locale, so MakeUpper puts a-z in capitals and
//     nothing else
//
// As in variables_guard.cpp, each is pinned as the text the port was written
// from. A failure does not say T5000 is wrong - only that what it copied has
// changed, and offline/input_edit.cpp needs looking at again.

#include "../device/product.h"
#include "../offline/input_edit.h"
#include "../testing/check.h"
#include "source_text.h"

#include <stdio.h>

#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>

namespace
{
    using namespace t5000::testing;
    using t5000::conformance::function_body;
    using t5000::conformance::parse_constant;
    using t5000::conformance::read_source;

    int occurrences(const std::string& text, const std::string& part)
    {
        int n = 0;
        for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
            n++;
        return n;
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
        return true;
    }

    bool constant_or_fail(const std::string& text, const char* name, long& value)
    {
        std::string error;
        if (!parse_constant(text, name, value, error))
        {
            check(false, (std::string(name) + " is found").c_str());
            printf("        %s\n", error.c_str());
            return false;
        }
        return true;
    }

    void has(const std::string& text, const std::string& part, const char* what)
    {
        check_eq(occurrences(text, part), 1, what);
    }

    // True when each part is found once, and in the order given.
    bool in_order(const std::string& text, std::initializer_list<const char*> parts)
    {
        size_t from = 0;
        for (const char* part : parts)
        {
            if (occurrences(text, part) != 1)
                return false;
            const size_t at = text.find(part);
            if (at < from)
                return false;
            from = at;
        }
        return true;
    }

    void test_the_searches_on_their_own()
    {
        section("the searches the checks below rely on");

        check(in_order("a b c", { "a", "b", "c" }), "parts in order are in order");
        check(!in_order("a c b", { "a", "b", "c" }), "  and out of order are not");
        check(!in_order("a b b c", { "a", "b", "c" }), "  nor is a part found twice");
        check(!in_order("a c", { "a", "b", "c" }), "  nor one not found");
    }

    void test_the_constants()
    {
        section("the counts and columns the rules use (global_define.h)");

        std::string text;
        if (!read_or_fail("T3000\\global_define.h", text))
            return;

        namespace counts = t5000::offline::t3000_counts;
        long value = 0;
        if (constant_or_fail(text, "BAC_INPUT_ITEM_COUNT", value))
            check_eq(value, (long)counts::kInputs, "BAC_INPUT_ITEM_COUNT, the inputs a full label is compared with");
        if (constant_or_fail(text, "BAC_OUTPUT_ITEM_COUNT", value))
            check_eq(value, (long)counts::kOutputs, "BAC_OUTPUT_ITEM_COUNT, the OUTn names");
        if (constant_or_fail(text, "BAC_VARIABLE_ITEM_COUNT", value))
            check_eq(value, (long)counts::kVariables, "BAC_VARIABLE_ITEM_COUNT, the VARn names");
        if (constant_or_fail(text, "BAC_PVAR_ITEM_COUNT", value))
            check_eq(value, (long)counts::kPvars, "BAC_PVAR_ITEM_COUNT, the PVARn names");
        if (constant_or_fail(text, "BAC_PROGRAM_ITEM_COUNT", value))
            check_eq(value, (long)counts::kPrograms, "BAC_PROGRAM_ITEM_COUNT, the PRGn names");

        // What edit_auto_manual writes for "Auto" and "Manual".
        if (constant_or_fail(text, "BAC_AUTO", value))
            check_eq(value, 0L, "BAC_AUTO is 0");
        if (constant_or_fail(text, "BAC_MANUAL", value))
            check_eq(value, 1L, "BAC_MANUAL is 1");

        // The default map lets every product change columns 0-15 (see
        // test_the_columns_a_product_may_change), so each column T5000 lets
        // be changed has to be one of those.
        const char* const columns[] = { "INPUT_FULL_LABLE", "INPUT_AUTO_MANUAL", "INPUT_FITLER", "INPUT_LABLE" };
        for (const char* column : columns)
        {
            if (constant_or_fail(text, column, value))
                check(value >= 0 && value < 16, (std::string(column) + " is a column the default map lets be changed").c_str());
        }
    }

    void test_what_a_point_starts_as()
    {
        section("what every point is before a panel is read (Initial_All_Point, global_function.cpp:17693)");

        std::string text;
        if (!read_or_fail("T3000\\global_function.cpp", text))
            return;

        std::string body;
        if (!body_or_fail(text, "void Initial_All_Point()", body))
            return;

        has(body, "for (int i = 0; i < BAC_INPUT_ITEM_COUNT; i++)", "an input for each of BAC_INPUT_ITEM_COUNT");
        has(body, "Str_in_point temp_in = {0};", "  all zeros");
        has(body, "temp_in.filter = 5;", "  but a filter of 5");
        has(body, "sprintf((char *)temp_in.description, \"IN%d\", i + 1);", "  and a full label of IN1, IN2 and on");
        check_eq(occurrences(body, "temp_in."), 4, "  and nothing else of it set: its full label and label cleared, its filter, its name");

        has(body, "for (int i = 0; i < BAC_OUTPUT_ITEM_COUNT; i++)", "an output for each of BAC_OUTPUT_ITEM_COUNT");
        has(body, "sprintf((char*)temp_out.description, \"OUT%d\", i + 1);", "  named OUT1 and on");
        has(body, "for (int i = 0; i < BAC_VARIABLE_ITEM_COUNT; i++)", "a variable for each of BAC_VARIABLE_ITEM_COUNT");
        has(body, "sprintf((char*)temp_variable.description, \"VAR%d\", i + 1);", "  named VAR1 and on");
        has(body, "for (int i = 0; i < BAC_PVAR_ITEM_COUNT; i++)", "a PVAR for each of BAC_PVAR_ITEM_COUNT");
        has(body, "sprintf((char*)temp_variable.description, \"PVAR%d\", i + 1);", "  named PVAR1 and on");
        has(body, "for (int i = 0; i < BAC_PROGRAM_ITEM_COUNT; i++)", "a program for each of BAC_PROGRAM_ITEM_COUNT");
        has(body, "sprintf((char*)temp_program.description, \"PRG%d\", i + 1);", "  named PRG1 and on");
        check_eq(occurrences(body, "sprintf("), 5,
                 "and nothing else named: every screen, schedule and holiday starts without a full label, and every point without a label");
    }

    void test_how_a_change_is_taken()
    {
        section("how the grid takes a change to an input (Fresh_Input_Item, BacnetInput.cpp:451)");

        std::string text;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text))
            return;

        std::string body;
        if (!body_or_fail(text, "LRESULT CBacnetInput::Fresh_Input_Item(WPARAM wParam,LPARAM lParam)", body))
            return;

        has(body, "if(Changed_Item>= INPUT_LIMITE_ITEM_COUNT)", "a row past the model's inputs is not changed");
        has(body, "if (Get_Product_Input_Map(g_selected_product_id, Changed_SubItem) == false)",
            "  nor a column the product's map shuts");

        has(body, "if(cs_temp.GetLength()>= STR_IN_LABEL)", "a label of STR_IN_LABEL characters or more is refused");
        check(in_order(body, { "cs_temp.Replace(_T(\"-\"),_T(\"_\"));", "cs_temp.MakeUpper();", "if(Check_Label_Exsit(cs_temp))" }),
              "  otherwise '-' becomes '_', then it is put in capitals, then looked for among the labels");
        has(body, "memcpy_s(m_Input_data.at(Changed_Item).label,STR_IN_LABEL,cTemp1,STR_IN_LABEL);",
            "  and STR_IN_LABEL bytes of it kept");

        has(body, "if(cs_temp.GetLength()>= STR_IN_DESCRIPTION_LENGTH)",
            "a full label of STR_IN_DESCRIPTION_LENGTH characters or more is refused");
        has(body, "if(Check_FullLabel_Exsit(cs_temp))", "  otherwise it is looked for among the full labels as it was typed");
        has(body, "memcpy_s(m_Input_data.at(Changed_Item).description,STR_IN_DESCRIPTION_LENGTH,cTemp1,STR_IN_DESCRIPTION_LENGTH);",
            "  and STR_IN_DESCRIPTION_LENGTH bytes of it kept");

        check_eq(occurrences(body, "WideCharToMultiByte( CP_ACP, 0, cs_temp.GetBuffer(), -1, cTemp1, 255, NULL, NULL );"), 2,
                 "both are kept in the ANSI code page");
        check_eq(occurrences(body, "MakeUpper"), 1, "only the label is put in capitals");
        check_eq(occurrences(body, "Trim"), 0, "  and neither is trimmed");

        has(body, "if(temp_cs.CompareNoCase(_T(\"Auto\"))==0 || temp_cs.CompareNoCase(_T(\"False\")) == 0)",
            "Auto/Manual typed as Auto or False, in any case, is Auto");
        has(body, "m_Input_data.at(Changed_Item).auto_manual = BAC_AUTO ;", "  which is BAC_AUTO");
        has(body, "m_Input_data.at(Changed_Item).auto_manual = BAC_MANUAL ;", "  and anything else is BAC_MANUAL");

        has(body, "int  temp2 = _wtoi(cs_temp);", "a filter is read with _wtoi");
        has(body, "if((temp2<0) || (temp2 >255))", "  and one outside 0-255 is refused");
        has(body, "m_Input_data.at(Changed_Item).filter = (unsigned char)temp2;", "  otherwise kept as it is");

        has(body, "cmp_ret = memcmp(&m_temp_Input_data[Changed_Item],&m_Input_data.at(Changed_Item),sizeof(Str_in_point));",
            "an input is compared, whole, with what it was");
        has(body, "if(cmp_ret!=0)", "  and written only when it differs");
    }

    void test_the_click()
    {
        section("how a click flips Auto/Manual (OnNMClickList1, BacnetInput.cpp:1421)");

        std::string text;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text))
            return;

        std::string body;
        if (!body_or_fail(text, "void CBacnetInput::OnNMClickList1(NMHDR *pNMHDR, LRESULT *pResult)", body))
            return;

        has(body, "if(lRow>= INPUT_LIMITE_ITEM_COUNT)", "a click past the model's inputs does nothing");
        has(body, "if (Get_Product_Input_Map(g_selected_product_id, lCol) == false)", "  nor one on a column the map shuts");
        has(body, "else if(lCol == INPUT_AUTO_MANUAL)", "a click on Auto/Manual");
        check_eq(occurrences(body, "if (m_Input_data.at(lRow).auto_manual == 0)"), 2,
                 "  makes 0 into 1 and anything else into 0 (once for a third-party device, once for a panel)");
        check_eq(occurrences(body, "m_Input_data.at(lRow).auto_manual = 1;"), 2, "  writing 1");
        check_eq(occurrences(body, "m_Input_data.at(lRow).auto_manual = 0;"), 2, "  and 0");
        check_eq(occurrences(body, "auto_manual = "), 4, "  and nothing else is written to it");
        has(body, "m_input_list.SetItemText(lRow, INPUT_AUTO_MANUAL, _T(\"Manual\"));", "  shown as Manual");
        has(body, "m_input_list.SetItemText(lRow, INPUT_AUTO_MANUAL, _T(\"Auto\"));", "  and Auto");
    }

    void test_what_a_name_is_compared_with()
    {
        section("what a new name is compared with (global_function.cpp:3151-3330)");

        std::string text;
        if (!read_or_fail("T3000\\global_function.cpp", text))
            return;

        const char* const convert = "WideCharToMultiByte( CP_ACP, 0, new_string.GetBuffer(), -1, cTemp1, 255, NULL, NULL );";

        std::string body;
        if (body_or_fail(text, "bool Check_FullLabel_Exsit(LPCTSTR m_new_fulllabel)", body))
        {
            has(body, "if(new_string.IsEmpty())", "an empty full label is never a repeat");
            has(body, convert, "  a full label is compared in the ANSI code page");
            has(body, "for (int i=0; i<BAC_INPUT_ITEM_COUNT; i++)", "  with the first BAC_INPUT_ITEM_COUNT inputs");
            has(body, "for (int i=0; i<BAC_OUTPUT_ITEM_COUNT; i++)", "  the outputs");
            has(body, "for (int i=0; i<BAC_VARIABLE_ITEM_COUNT; i++)", "  the variables");
            has(body, "for (int i = 0; i < BAC_PVAR_ITEM_COUNT; i++)", "  the PVARs");
            has(body, "for (int i=0; i<BAC_PROGRAM_ITEM_COUNT; i++)", "  the programs");
            has(body, "for (int i=0; i<BAC_SCREEN_COUNT; i++)", "  the screens");
            has(body, "for (int i=0; i<BAC_SCHEDULE_COUNT; i++)", "  the schedules");
            has(body, "for (int i=0; i<BAC_HOLIDAY_COUNT; i++)", "  and the holidays");
            check_eq(occurrences(body, "for ("), 8, "  and nothing else");
            check_eq(occurrences(body, ".description) == 0"), 8, "  each by strcmp with its full label: case and all");
            check_eq(occurrences(body, "strcmp("), 8, "  and by nothing else");
            check_eq(occurrences(body, "if(strcmp(cTemp1,(char *)m_") + occurrences(body, "if (strcmp(cTemp1, (char*)m_"), 8,
                     "  each comparison the whole of its test");
        }

        if (body_or_fail(text, "bool Check_Label_Exsit(LPCTSTR m_new_label)", body))
        {
            has(body, "if(new_string.IsEmpty())", "an empty label is never a repeat");
            has(body, convert, "  a label is compared in the ANSI code page");
            has(body, "for (int i=0; i< m_Input_data.size(); i++)", "  with every input");
            has(body, "for (int i=0; i< m_Output_data.size(); i++)", "  every output");
            has(body, "for (int i=0; i< m_Variable_data.size(); i++)", "  every variable");
            has(body, "for (int i = 0; i < m_pvar_data.size(); i++)", "  every PVAR");
            has(body, "for (int i=0; i<BAC_PROGRAM_ITEM_COUNT; i++)", "  the programs");
            has(body, "for (int i=0; i<BAC_SCREEN_COUNT; i++)", "  the screens");
            has(body, "for (int i=0; i<BAC_SCHEDULE_COUNT; i++)", "  the schedules");
            has(body, "for (int i=0; i<BAC_HOLIDAY_COUNT; i++)", "  the holidays");
            has(body, "for (int i=0; i<BAC_MONITOR_COUNT; i++)", "  and the monitors");
            check_eq(occurrences(body, "for ("), 9, "  and nothing else");
            check_eq(occurrences(body, ".label) == 0"), 9, "  each by strcmp with its label");
            check_eq(occurrences(body, "strcmp("), 9, "  and by nothing else");
            check_eq(occurrences(body, "if(strcmp(cTemp1,(char *)m_") + occurrences(body, "if (strcmp(cTemp1, (char*)m_"), 9,
                     "  each comparison the whole of its test");
            check_eq(occurrences(body, "MakeUpper"), 0, "  as the caller made it: in capitals, with '_' for '-'");
        }
    }

    void test_the_columns_a_product_may_change()
    {
        section("the columns the grid lets a product change (Inial_Product_Input_map, global_function.cpp:7477)");

        std::string text;
        if (!read_or_fail("T3000\\global_function.cpp", text))
            return;

        std::string body;
        if (body_or_fail(text, "void Inial_Product_Input_map()", body))
        {
            has(body, "unsigned char default_input[20] = { 1,1,1,1  ,1,1,1,1 ,1,1,1,1   ,1 ,1,1,1   ,0,0,0,0 };",
                "every product may change columns 0-15");
            check_eq(occurrences(body, "case "), 3, "  but three");
            has(body, "case PM_MULTI_SENSOR:", "  the multi-sensor");
            has(body, "case PM_TSTAT_AQ:", "  the Tstat AQ");
            has(body, "case PM_AIRLAB_ESP32:", "  and the Airlab ESP32");
        }

        if (body_or_fail(text, "int Get_Product_Input_Map(unsigned char product_tpye, int input_item)", body))
            has(body, "return product_input[product_tpye][input_item];", "a column's rule is looked up by the product alone");

        // Their codes are held to ProductModel.h by product_guard.cpp. None
        // is read by private transfer, and only a product that is can be
        // configured offline, so the default map is the one that applies.
        using t5000::device::DataPath;
        using t5000::device::ProductClassId;
        using t5000::device::capabilities;
        check(capabilities(ProductClassId::MultiSensor).path != DataPath::BacnetPrivateData,
              "T5000 does not read the multi-sensor by private transfer, so does not configure it offline");
        check(capabilities(ProductClassId::TstatAq).path != DataPath::BacnetPrivateData, "  nor the Tstat AQ");
        check(capabilities(ProductClassId::AirlabEsp32).path != DataPath::BacnetPrivateData, "  nor the Airlab ESP32");
    }

    // MakeUpper is the C runtime's, which in the "C" locale T3000 starts in
    // puts a-z in capitals and nothing else. A setlocale anywhere in T3000
    // would change that for every label after it. ISP\ConfigFileHandler.cpp
    // sets one, but it is ISP.exe's, a program of its own, which T3000 does
    // not build.
    void test_no_locale_is_set()
    {
        section("T3000 never sets a locale, so a label's capitals are a-z alone");

        namespace fs = std::filesystem;
        const fs::path root = fs::path(t5000::conformance::g_source_root) / "T3000";

        int files = 0;
        int calls = 0;
        std::error_code ec;
        for (fs::recursive_directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec))
                continue;
            const std::string ext = it->path().extension().string();
            if (ext != ".cpp" && ext != ".h" && ext != ".c")
                continue;

            std::ifstream f(it->path(), std::ios::binary);
            if (!f)
                continue;
            files++;

            std::string line;
            int number = 0;
            while (std::getline(f, line))
            {
                number++;
                if (line.find("setlocale") == std::string::npos && line.find("locale::global") == std::string::npos)
                    continue;
                const size_t first = line.find_first_not_of(" \t");
                if (first != std::string::npos && line.compare(first, 2, "//") == 0)
                    continue;
                calls++;
                printf("        %s:%d sets a locale\n", it->path().string().c_str(), number);
            }
        }

        check(!ec, "T3000's source can be walked");
        check(files > 400, ("  and holds the files expected of it: " + std::to_string(files) + " read").c_str());
        check_eq(calls, 0, "no line of it sets a locale that is not commented out");
    }
}

int run_offline_guard_tests()
{
    test_the_searches_on_their_own();
    test_the_constants();
    test_what_a_point_starts_as();
    test_how_a_change_is_taken();
    test_the_click();
    test_what_a_name_is_compared_with();
    test_the_columns_a_product_may_change();
    test_no_locale_is_set();
    return 0;
}
