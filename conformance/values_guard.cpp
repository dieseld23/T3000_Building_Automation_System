// Checks how T3000's Inputs, Outputs and Variables grids turn a point's value
// into text, which T5000's grids copy: thousandths (display/input_text.cpp,
// which display/output_text.cpp uses too) and variable_number and the
// multi-state lookup (display/variable_text.cpp).
//
//   - value / 1000, divided in double: Fresh_Input_List (BacnetInput.cpp:1092,
//     :1126), Fresh_Output_List (BacnetOutput.cpp:902, :916) and
//     Fresh_Variable_List (BacnetVariable.cpp:312-440)
//   - an input's calibration / 10, still in float (BacnetInput.cpp:1102)
//
// T3000 divided the value in float until 2026-09-28 (temcocontrols
// 57781bda), and some values show differently in the two: 45 is 0.05 in float
// and 0.04 in double. No constant names the type, so it is pinned as the text
// the port was written from. A failure here does not say T5000 is wrong -
// only that what it copied has changed, and the ports need looking at again.

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

    // One grid: the function that fills it, and how it writes the division.
    struct Grid
    {
        const char* file;
        const char* signature;
        const char* in_double;
        const char* in_float;
        int         places;
        const char* what;
    };

    void test_the_occurrence_count_on_its_own()
    {
        section("the search the checks below rely on");

        check_eq(occurrences("((double)a) / 1000 ((double)a) / 1000", "((double)a) / 1000"), 2,
                 "counts every occurrence");
        check_eq(occurrences("((float)a) / 1000", "((double)a) / 1000"), 0, "  and not the other type");
    }

    void test_values_are_divided_in_double()
    {
        section("each grid divides a value by 1000 in double, where it was float until 2026-09-28");

        const Grid grids[] = {
            { "T3000\\BacnetInput.cpp", "LRESULT CBacnetInput::Fresh_Input_List(WPARAM wParam, LPARAM lParam)",
              "((double)m_Input_data.at(i).value) / 1000", "(float)m_Input_data.at(i).value", 2,
              "Inputs: an analog value, and a digital one on range 0" },
            { "T3000\\BacnetOutput.cpp", "LRESULT CBacnetOutput::Fresh_Output_List(WPARAM wParam, LPARAM lParam)",
              "((double)m_Output_data.at(i).value) / 1000", "(float)m_Output_data.at(i).value", 2,
              "Outputs: an analog value, and a digital one on range 0" },
            { "T3000\\BacnetVariable.cpp", "LRESULT CBacnetVariable::Fresh_Variable_List(WPARAM wParam, LPARAM lParam)",
              "((double)m_Variable_data.at(i).value) / 1000", "(float)m_Variable_data.at(i).value", 7,
              "Variables: every number, and every multi-state lookup" },
        };

        for (const Grid& g : grids)
        {
            std::string text;
            std::string body;
            if (!read_or_fail(g.file, text) || !body_or_fail(text, g.signature, body))
                continue;
            check_eq(occurrences(body, g.in_double), g.places, g.what);
            check_eq(occurrences(body, g.in_float), 0, "  and none in float");
        }
    }

    void test_a_calibration_is_divided_in_float()
    {
        section("an input's calibration is still divided by 10 in float (BacnetInput.cpp:1102)");

        std::string text;
        std::string body;
        if (!read_or_fail("T3000\\BacnetInput.cpp", text) ||
            !body_or_fail(text, "LRESULT CBacnetInput::Fresh_Input_List(WPARAM wParam, LPARAM lParam)", body))
            return;
        check_eq(occurrences(body, "temp_cal.Format(_T(\"%.1f\"), ((float)temp_cal_value) / 10);"), 1,
                 "one place, as input_text.cpp copies it");
    }
}

int run_values_guard_tests()
{
    test_the_occurrence_count_on_its_own();
    test_values_are_divided_in_double();
    test_a_calibration_is_divided_in_float();
    return 0;
}
