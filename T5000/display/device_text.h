#pragma once

// Text as a device stores it, turned into text a page can show.
//
// Device strings are CP_ACP bytes - GBK on these controllers - and T3000
// turns them into UTF-16 with MultiByteToWideChar(CP_ACP, ...) before doing
// anything else with them. Where T3000 then measures or splits a string, it
// does so in UTF-16, so the helpers here work in UTF-16 too and convert to
// UTF-8 only at the end: a rule like "12 or more characters" counts UTF-16
// units, not bytes.

#include <stddef.h>
#include <stdint.h>

#include <string>
#include <vector>

namespace t5000::display
{
    // The bytes before the first NUL, at most max_length of them.
    size_t length_to_nul(const uint8_t* text, size_t max_length);

    // MultiByteToWideChar(CP_ACP) over the bytes before the first NUL, at
    // most max_length of them.
    std::wstring acp_to_wide(const uint8_t* text, size_t max_length);

    std::string wide_to_utf8(const std::wstring& wide);
    std::wstring utf8_to_wide(const std::string& utf8);

    std::string acp_to_utf8(const uint8_t* text, size_t max_length);

    // T3000's SplitCStringA (global_function.cpp:7250-7284), which is not a
    // plain split:
    //
    //   - No token at all: the whole string, untrimmed, as one part.
    //   - Otherwise each part is trimmed of whitespace, and a token at the
    //     start of what remains is skipped - so "a//b" and "/a/b" are two
    //     parts, and "a/" is one.
    //
    // T3000 shows a digital input's state only when this gives exactly two
    // parts, so which strings give two matters.
    std::vector<std::wstring> split_like_t3000(const std::wstring& source, wchar_t token);
}
