#pragma once

// The repository root, for the checks that read the T3000 source a T5000
// copy was taken from. Set from --source-root by conformance/main.cpp.
//
// Here rather than in testing/check.h, which T5000's own self-test shares:
// nothing in T5000 itself reads T3000's source.

#include <string>

namespace t5000::conformance
{
    inline std::string g_source_root;
}
