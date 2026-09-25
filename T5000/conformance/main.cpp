// T5000Conformance.exe - the checks that hold T5000 to T3000.
//
// T5000 copies what it needs from T3000 rather than building on it: the wire
// layouts, the command codes, the product codes, the display tables. Its own
// self-test (T5000.exe --selftest) checks that T5000 does what it means to.
// This checks that what it means to is still what T3000 does:
//
//   compile time, against T3000's headers
//     wire_guard.cpp     the point structs in wire/points.h vs CM5/ud_str.h
//     panel_guard.cpp    the settings offsets in wire/panel.h vs CM5/ud_str.h
//     command_guard.cpp  every command T5000 may send is a CM5 read, never a write
//     product_guard.cpp  every product code in device/product.h vs ProductModel.h
//
//   run time
//     tables_guard.cpp             display/tables.h and the copied constants,
//                                  read against T3000/global_define.h as text
//     private_transfer_oracle.cpp  a read request, byte for byte, against the
//                                  one T3000's BACnet stack DLL sends
//
// Built by "T3000 - VS2019.sln", which also builds the stack, and run by its
// post-build step, so a change on either side that breaks the other fails
// that build. T5000/T5000.sln does not build this: T5000 has to build, run and
// pass its own tests with nothing of T3000's present.
//
// Usage: T5000Conformance.exe --source-root <repository root>

#include <stdio.h>
#include <string.h>

#include <string>

#include "../testing/check.h"
#include "source_root.h"

int run_tables_guard_tests();
int run_private_transfer_oracle_tests();

int main(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; i++)
    {
        if (strcmp(argv[i], "--source-root") == 0)
            t5000::conformance::g_source_root = argv[i + 1];
    }
    if (t5000::conformance::g_source_root.empty())
    {
        printf("usage: T5000Conformance.exe --source-root <repository root>\n");
        return 2;
    }

    printf("T5000 conformance with T3000\n\n");

    run_tables_guard_tests();
    printf("\n");
    run_private_transfer_oracle_tests();

    const int failures = t5000::testing::g_failures;
    const int checks   = t5000::testing::g_checks;

    printf("\n%d checks, %d failures - %s\n",
           checks, failures,
           failures == 0 ? "all checks passed" : "FAILURES PRESENT");

    // As in T5000's self-test: a run that asserts nothing means the checks
    // were dropped from the build, which would otherwise look like success.
    if (checks == 0)
    {
        printf("no checks ran at all - the checks are not wired up\n");
        return 1;
    }

    return failures == 0 ? 0 : 1;
}
