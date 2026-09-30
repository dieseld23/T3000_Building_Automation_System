#pragma once

// T3000's tree, <repository>\T3000, for the checks that read the T3000
// source a T5000 copy was taken from. Set from --source-root by
// conformance/main.cpp. Paths under it are T3000's own, as in T3000's
// repository: T3000\global_define.h, ISP\ComWriter.cpp.
//
// Here rather than in testing/check.h, which T5000's own self-test shares:
// nothing in T5000 itself reads T3000's source.

#include <filesystem>
#include <string>

namespace t5000::conformance
{
    inline std::string g_source_root;

    // T5000's tree: the repository root, the folder that holds T3000's.
    inline std::string t5000_root()
    {
        return (std::filesystem::path(g_source_root) / "..").lexically_normal().string();
    }
}
