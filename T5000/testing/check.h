#pragma once

// A deliberately tiny test harness. No framework, because pulling one in would
// be the largest dependency this project has, and the tests here are assertions
// about byte layouts and branch conditions rather than anything that needs
// fixtures or mocking.
//
// Every check reports what it got and what it expected. A test that only says
// "failed" wastes the run it took to produce.

#include <stdio.h>
#include <string.h>

namespace t5000::testing
{
    inline int g_failures = 0;
    inline int g_checks   = 0;

    inline void check(bool condition, const char* what)
    {
        g_checks++;
        if (!condition)
        {
            printf("  FAIL  %s\n", what);
            g_failures++;
        }
    }

    // A check whose result the caller can act on, for when continuing past a
    // failure would crash rather than report.
    //
    // check() deliberately does not abort - one failing assertion should not
    // cost the other seven hundred. But that means
    //
    //     check(reg.selected() != nullptr, "selected");
    //     check_eq(reg.selected()->serial_number, 8002, "the right one");
    //
    // dereferences null on the very next line when the first check fails, and
    // the run dies with an access violation instead of a list of failures.
    // That happened: a mutation test killed its target by crashing the suite,
    // which proved the mutation was caught but hid everything else.
    inline bool require(bool condition, const char* what)
    {
        check(condition, what);
        return condition;
    }

    inline void check_eq(long actual, long expected, const char* what)
    {
        g_checks++;
        if (actual != expected)
        {
            printf("  FAIL  %s: got %ld, expected %ld\n", what, actual, expected);
            g_failures++;
        }
    }

    inline void check_streq(const char* actual, const char* expected, const char* what)
    {
        g_checks++;
        const bool same = (actual != nullptr) && (expected != nullptr) &&
                          (strcmp(actual, expected) == 0);
        if (!same)
        {
            printf("  FAIL  %s: got \"%s\", expected \"%s\"\n",
                   what, actual ? actual : "(null)", expected ? expected : "(null)");
            g_failures++;
        }
    }

    inline void section(const char* name)
    {
        printf("%s\n", name);
    }
}
