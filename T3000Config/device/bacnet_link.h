#pragma once

// Link-level proof that the BACnet stack is reachable from this process.
//
// See bacnet_link.cpp for why this is worth its own file: the plan assumed the
// stack would need decoupling from MFC, and it does not, because it ships as a
// DLL. That is load-bearing enough to be checked by the build and by a test
// rather than remembered.

namespace t3000::device
{
    // Calls into BACnet_Stack_Library and reads back what it set. False means
    // the stack is present but not behaving, which is a different and much more
    // interesting failure than "did not link".
    bool bacnet_stack_is_linked();
}
