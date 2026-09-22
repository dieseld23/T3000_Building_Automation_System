// Proves the BACnet stack can be linked and called from this process.
//
// The plan assumed the stack would have to be decoupled from MFC before a
// non-MFC tool could use it, because BacNetDllforVc's project sets
// UseOfMfc=Static. That setting turns out not to matter here: the project builds
// a DLL, so whatever MFC it uses is resolved inside BACnet_Stack_Library.dll,
// and this process links the import library and calls C-decorated exports.
//
// The exports confirm it - dumpbin shows _Device_Init, _Set_transfer_length,
// _Send_ConfirmedPrivateTransfer, _address_get_by_device, _tsm_invoke_id_free
// and the bacapp_* codec, all cdecl with no C++ mangling.
//
// This file exists to make that claim fail loudly at build time if it stops
// being true, rather than being an assertion in a commit message.

#include "bacnet_link.h"

extern "C"
{
    // Declared here rather than by including the stack's headers. bacdef.h and
    // its dependents pull in a large tree of the stack's own configuration
    // macros, and this file only needs to prove the ABI: that these symbols
    // resolve at link time and are callable with plain C types.
    //
    // The real read path will include the headers properly. If any signature
    // below disagrees with the stack's, the linker still resolves it - which is
    // why nothing here passes a non-trivial argument or uses a return value in
    // a way that would depend on the signature being right.
    void Set_transfer_length(int len);
    int  Get_transfer_length(void);
}

namespace t5000::device
{
    bool bacnet_stack_is_linked()
    {
        // A round-trip through the DLL's own state. If the import library were
        // not linked this would not build; if the DLL were missing at runtime
        // the process would fail to start, which is itself the answer.
        const int probe = 4711;
        Set_transfer_length(probe);
        return Get_transfer_length() == probe;
    }
}

// ---------------------------------------------------------------- self-test

#include "../testing/check.h"

int run_bacnet_link_tests()
{
    using namespace t5000::testing;

    section("the BACnet stack links and responds");

    // If the import library were missing this would not build, so reaching here
    // already proves the link. What this checks is that the DLL is actually
    // loaded and holding state - that the call went somewhere real.
    check(t5000::device::bacnet_stack_is_linked(),
          "Set_transfer_length/Get_transfer_length round-trip through the DLL");

    return 0;
}
