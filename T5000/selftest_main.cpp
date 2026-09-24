// Runs every self-test in the project and returns non-zero if any failed.
//
// Reached via "T5000.exe --selftest", which the post-build event invokes,
// so the build fails when a check fails. Compiling tests without running them
// proves only that they parse.

#include "testing/check.h"

#include <windows.h>

int run_wire_tests();
int run_panel_wire_tests();
int run_read_path_tests();
int run_bacnet_link_tests();
int run_connection_tests();
int run_product_tests();
int run_registry_tests();
int run_scan_response_tests();
int run_scanner_tests();
int run_json_read_tests();
int run_scan_json_tests();
int run_private_transfer_tests();
int run_private_transfer_oracle_tests();
int run_point_read_tests();
int run_inputs_plan_tests();
int run_tables_guard_tests();
int run_input_text_tests();
int run_custom_ranges_tests();
int run_input_rows_tests();
int run_inputs_read_tests();

namespace
{
    // Where the T3000 source is, for the tests that check a copy against it.
    //
    // The post-build step passes it (--source-root "$(ProjectDir).."), which
    // is right wherever the tree is checked out. Run by hand, the exe sits at
    // <root>\T3000 Output\<configuration>\T5000.exe, so two levels up is the
    // fallback - and a test that still cannot find its file says which path
    // it tried.
    std::string find_source_root(int argc, char** argv)
    {
        for (int i = 1; i + 1 < argc; i++)
        {
            if (strcmp(argv[i], "--source-root") == 0)
                return argv[i + 1];
        }

        char exe[MAX_PATH] = {};
        const DWORD n = GetModuleFileNameA(nullptr, exe, MAX_PATH);
        std::string path(exe, n);
        for (int up = 0; up < 3; up++)   // the file name, then two directories
        {
            const size_t slash = path.find_last_of("\\/");
            if (slash == std::string::npos)
                return std::string();
            path.resize(slash);
        }
        return path;
    }
}

int run_selftests(int argc, char** argv)
{
    t5000::testing::g_source_root = find_source_root(argc, argv);

    printf("T5000 self-test\n\n");

    run_json_read_tests();
    printf("\n");
    run_wire_tests();
    printf("\n");
    run_panel_wire_tests();
    printf("\n");
    run_read_path_tests();
    printf("\n");
    run_bacnet_link_tests();
    printf("\n");
    run_connection_tests();
    printf("\n");
    run_product_tests();
    printf("\n");
    run_registry_tests();
    printf("\n");
    run_scan_response_tests();
    printf("\n");
    run_scanner_tests();
    printf("\n");
    run_scan_json_tests();
    printf("\n");
    run_private_transfer_tests();
    printf("\n");
    run_private_transfer_oracle_tests();
    printf("\n");
    run_point_read_tests();
    printf("\n");
    run_inputs_plan_tests();
    printf("\n");
    run_tables_guard_tests();
    printf("\n");
    run_input_text_tests();
    printf("\n");
    run_custom_ranges_tests();
    printf("\n");
    run_input_rows_tests();
    printf("\n");
    run_inputs_read_tests();

    const int failures = t5000::testing::g_failures;
    const int checks   = t5000::testing::g_checks;

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
