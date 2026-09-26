// Checks the rules T5000's Variables grid copies from T3000 that no constant
// names: numbers and text written where T3000 uses them, which neither the
// constants check nor the tables check in tables_guard.cpp can see.
//
//   - which multi-state tables T3000 reads on which firmware
//     (BacnetView.cpp:6483-6547), and how it reads the custom units (:6571)
//   - that Get_Msv_Item_Name looks in tables 0-2 only, and how
//     Get_Msv_Table_Name joins names (global_function.cpp:16855, :17025)
//   - how Fresh_Variable_List shows a time and a number, and where its
//     ranges split (BacnetVariable.cpp:209-478), and how intervaltotextfull
//     builds the time (global_function.cpp:7856)
//
// Each is pinned as the text the port was written from. A failure here does
// not say T5000 is wrong - only that what it copied has changed, and the
// port in display/variable_text.cpp, display/variable_ranges.cpp and
// app/variables_read.cpp needs looking at again.
//
// BacnetView.cpp is searched whole rather than by function: it holds GBK
// string literals, whose second bytes can be a backslash, and the comment
// stripper that finds a function's body would lose its place in them.

#include "../testing/check.h"
#include "source_text.h"

#include <stdio.h>

#include <string>

namespace
{
    using namespace t5000::testing;
    using t5000::conformance::function_body;
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

    void has(const std::string& text, const std::string& part, const char* what)
    {
        check_eq(occurrences(text, part), 1, what);
    }

    void test_the_occurrence_count_on_its_own()
    {
        section("the search the checks below rely on");

        check_eq(occurrences("a <= 607) b <= 607)", "<= 607)"), 2, "counts every occurrence");
        check_eq(occurrences("aaa", "aa"), 2, "  overlapping ones too");
        check_eq(occurrences("abc", "x"), 0, "  and none is none");
    }

    void test_what_is_read()
    {
        section("the multi-state tables and custom units T3000 reads (BacnetView.cpp:6483-6571)");

        std::string text;
        if (!read_or_fail("T3000\\BacnetView.cpp", text))
            return;

        has(text, "firmware0_rev_sub <= 607)",
            "firmware 60.7 and older is the one split, and 60.7 itself reads the old way");
        has(text, "READ_MSV_COMMAND, 0, 1, sizeof(Str_MSV)", "  which reads tables 0-1");
        has(text, "READ_MSV_COMMAND, 2, 2, sizeof(Str_MSV)", "  and then table 2 alone");
        has(text, "read_msv_table = read_ret[0] && read_ret[1] && read_ret[2];",
            "  and has them all when those three came back");
        has(text, "READ_MSV_COMMAND, 2 * x, 2*x + 1, sizeof(Str_MSV)", "newer firmware reads 0-1 and 2-3");
        has(text, "read_msv_table = read_ret[0] && read_ret[1] && read_ret[2] && read_ret[3];",
            "  and has them all when all four came back");
        has(text, "READVARUNIT_T3000,0,4,sizeof(Str_variable_uint_point)", "the custom units are read as 0-4");
    }

    void test_how_names_are_found()
    {
        section("how T3000 finds a multi-state name, and names a table (global_function.cpp)");

        std::string text;
        if (!read_or_fail("T3000\\global_function.cpp", text))
            return;

        std::string body;
        if (body_or_fail(text, "int Get_Msv_Item_Name(int ntable, int nitemvalue,CString &csItemString)", body))
        {
            has(body, "if (ntable > 2)", "a name is looked up in tables 0-2 only");
            has(body, ".status == 1", "  among the items whose status is 1");
            has(body, "if (nitemvalue == m_msv_data.at(ntable).msv_data[i].msv_value)", "  by value");
            has(body, "return 1;", "  taking the first that matches");
        }

        if (body_or_fail(text, "int Get_Msv_Table_Name(int x)", body))
        {
            has(body, "if (index >= 3)", "a table's name is its first three names");
            has(body, "_T(\" /...\")", "  and \" /...\" when there is another item");
            has(body, "temp_cs.Trim();", "  each trimmed");
            has(body, "if (temp_cs.IsEmpty())", "  and a blank one skipped");
            has(body, "_T(\" / \") + temp_cs", "  joined by \" / \"");
        }

        if (body_or_fail(text, "char * intervaltotextfull(char *textbuf, long seconds , unsigned minutes , unsigned hours, char *c)", body))
        {
            has(body, "hours += seconds/3600;", "a time is hours");
            has(body, "minutes += (unsigned)(seconds%3600)/60;", "  minutes");
            has(body, "seconds = (unsigned)(seconds%3600)%60;", "  and seconds");
            check_eq(occurrences(body, "< 10 )"), 3, "  each with a leading zero below 10");
        }
    }

    void test_how_the_grid_shows_a_variable()
    {
        section("how Fresh_Variable_List shows a variable (BacnetVariable.cpp:209-478)");

        std::string text;
        if (!read_or_fail("T3000\\BacnetVariable.cpp", text))
            return;

        std::string body;
        if (!body_or_fail(text, "LRESULT CBacnetVariable::Fresh_Variable_List(WPARAM wParam, LPARAM lParam)", body))
            return;

        has(body, "if (m_Variable_data.at(i).digital_analog == BAC_UNITS_DIGITAL)", "digital is one value");
        has(body, "if ((m_Variable_data.at(i).range == 0) || ((m_Variable_data.at(i).range > 30) && (m_Variable_data.at(i).range < 100)))",
            "a digital variable on 0 or 31-99 is a number");
        has(body, "if ((m_Variable_data.at(i).range < 23) && (m_Variable_data.at(i).range != 0))",
            "  on 1-22 a fixed state pair");
        has(body, "else if ((m_Variable_data.at(i).range >= 23) && (m_Variable_data.at(i).range <= 30))",
            "  on 23-30 the device's own");
        has(body, "if (m_Variable_data.at(i).range == 20)", "an analog variable on 20 is a time");
        has(body, "int time_seconds = m_Variable_data.at(i).value / 1000;", "  of whole seconds");
        has(body, "intervaltotextfull(temp_char, time_seconds, 0, 0);", "  as intervaltotextfull shows it");
        has(body, "else if (m_Variable_data.at(i).range < sizeof(Variable_Analog_Units_Array) / sizeof(Variable_Analog_Units_Array[0]))",
            "  below the table's count a fixed unit");
        has(body, "else if ((m_Variable_data.at(i).range >= 34) && (m_Variable_data.at(i).range <= 38))",
            "  on 34-38 the device's own");
        check_eq(occurrences(body, "(m_Variable_data.at(i).range >= 101) && (m_Variable_data.at(i).range <= 104)"), 3,
                 "101-104 are multi-state: once for digital, twice for analog, the second never reached");
        check_eq(occurrences(body, "Get_Msv_Item_Name(m_Variable_data.at(i).range - 101, (int)temp_float_value1, cstemp_value2);"), 3,
                 "  each looking the name up by the whole part of value / 1000");
        check_eq(occurrences(body, "if (read_msv_table)"), 3, "  and writing Units only once every table is read");
        check_eq(occurrences(body, "Format(_T(\"%.3f\")"), 7, "every number has three decimals");
    }
}

int run_variables_guard_tests()
{
    test_the_occurrence_count_on_its_own();
    test_what_is_read();
    test_how_names_are_found();
    test_how_the_grid_shows_a_variable();
    return 0;
}
