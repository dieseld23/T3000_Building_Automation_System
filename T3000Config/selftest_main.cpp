// Runs every self-test in the project and returns non-zero if any failed.
//
// Reached via "T3000Config.exe --selftest", which the post-build event invokes,
// so the build fails when a check fails. Compiling tests without running them
// proves only that they parse.

#include "testing/check.h"

int run_wire_tests();
int run_read_path_tests();
int run_bacnet_link_tests();
int run_connection_tests();

int run_selftests()
{
    printf("T3000Config self-test\n\n");

    run_wire_tests();
    printf("\n");
    run_read_path_tests();
    printf("\n");
    run_bacnet_link_tests();
    printf("\n");
    run_connection_tests();

    const int failures = t3000::testing::g_failures;
    const int checks   = t3000::testing::g_checks;

    printf("\n%d checks, %d failures - %s\n",
           checks, failures,
           failures == 0 ? "all checks passed" : "FAILURES PRESENT");

    // A run that asserts nothing is a failure too: it means the tests were
    // dropped from the build or an early return skipped them, which would
    // otherwise look identical to success.
    if (checks == 0)
    {
        printf("no checks ran at all - the tests are not wired up\n");
        return 1;
    }

    return failures == 0 ? 0 : 1;
}
