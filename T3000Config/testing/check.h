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

namespace t3000::testing
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
