// Runs every self-test in the project and returns non-zero if any failed.
//
// Reached via "T5000.exe --selftest", which the post-build event invokes,
// so the build fails when a check fails. Compiling tests without running them
// proves only that they parse.
//
// Everything here tests T5000 on its own terms, with nothing of T3000's
// present. The checks against T3000 - its headers, its tables, its BACnet
// stack - are in conformance/, which T3000's solution builds and runs.

#include "testing/check.h"

int run_wire_tests();
int run_panel_wire_tests();
int run_read_path_tests();
int run_connection_tests();
int run_product_tests();
int run_registry_tests();
int run_scan_response_tests();
int run_scanner_tests();
int run_serial_ports_tests();
int run_rtu_tests();
int run_serial_scan_tests();
int run_json_read_tests();
int run_scan_json_tests();
int run_private_transfer_tests();
int run_point_read_tests();
int run_inputs_plan_tests();
int run_input_text_tests();
int run_output_text_tests();
int run_custom_ranges_tests();
int run_input_rows_tests();
int run_output_rows_tests();
int run_inputs_read_tests();
int run_outputs_read_tests();
int run_device_db_tests();
int run_device_list_tests();
int run_http_server_tests();

int run_selftests(int, char**)
{
    printf("T5000 self-test\n\n");

    run_json_read_tests();
    printf("\n");
    run_wire_tests();
    printf("\n");
    run_panel_wire_tests();
    printf("\n");
    run_read_path_tests();
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
    run_serial_ports_tests();
    printf("\n");
    run_rtu_tests();
    printf("\n");
    run_serial_scan_tests();
    printf("\n");
    run_scan_json_tests();
    printf("\n");
    run_private_transfer_tests();
    printf("\n");
    run_point_read_tests();
    printf("\n");
    run_inputs_plan_tests();
    printf("\n");
    run_input_text_tests();
    printf("\n");
    run_output_text_tests();
    printf("\n");
    run_custom_ranges_tests();
    printf("\n");
    run_input_rows_tests();
    printf("\n");
    run_output_rows_tests();
    printf("\n");
    run_inputs_read_tests();
    printf("\n");
    run_outputs_read_tests();
    printf("\n");
    run_device_db_tests();
    printf("\n");
    run_device_list_tests();
    printf("\n");
    run_http_server_tests();

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
