#include "device_text.h"

#include <windows.h>

#include <string.h>
#include <wctype.h>

namespace t5000::display
{
    namespace
    {
        // CString::Trim(): whitespace off both ends, as iswspace decides it
        // (ATL's ChTraitsCRT<wchar_t>::IsSpace).
        std::wstring trim(const std::wstring& s)
        {
            size_t first = 0;
            while (first < s.size() && iswspace(s[first]))
                first++;
            size_t last = s.size();
            while (last > first && iswspace(s[last - 1]))
                last--;
            return s.substr(first, last - first);
        }
    }

    size_t length_to_nul(const uint8_t* text, size_t max_length)
    {
        return text == nullptr ? 0 : strnlen((const char*)text, max_length);
    }

    std::wstring acp_to_wide(const uint8_t* text, size_t max_length)
    {
        const int bytes = (int)length_to_nul(text, max_length);
        if (bytes <= 0)
            return std::wstring();

        const int wide_len = MultiByteToWideChar(CP_ACP, 0, (const char*)text, bytes, nullptr, 0);
        if (wide_len <= 0)
            return std::wstring();

        std::wstring wide((size_t)wide_len, L'\0');
        MultiByteToWideChar(CP_ACP, 0, (const char*)text, bytes, &wide[0], wide_len);
        return wide;
    }

    std::string wide_to_utf8(const std::wstring& wide)
    {
        if (wide.empty())
            return std::string();

        const int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(),
                                                 nullptr, 0, nullptr, nullptr);
        if (utf8_len <= 0)
            return std::string();

        std::string utf8((size_t)utf8_len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, wide.data(), (int)wide.size(), &utf8[0], utf8_len, nullptr, nullptr);
        return utf8;
    }

    std::wstring utf8_to_wide(const std::string& utf8)
    {
        if (utf8.empty())
            return std::wstring();

        const int wide_len = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
        if (wide_len <= 0)
            return std::wstring();

        std::wstring wide((size_t)wide_len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), &wide[0], wide_len);
        return wide;
    }

    std::string acp_to_utf8(const uint8_t* text, size_t max_length)
    {
        return wide_to_utf8(acp_to_wide(text, max_length));
    }

    std::vector<std::wstring> split_like_t3000(const std::wstring& source, wchar_t token)
    {
        std::vector<std::wstring> parts;
        std::wstring rest = source;

        if (rest.find(token) == std::wstring::npos)
        {
            parts.push_back(rest);
            return parts;
        }

        while (!rest.empty())
        {
            const size_t at = rest.find(token);
            if (at == std::wstring::npos)
            {
                parts.push_back(trim(rest));
                break;
            }
            if (at == 0)
            {
                rest = rest.substr(1);
                continue;
            }
            parts.push_back(trim(rest.substr(0, at)));
            rest = rest.substr(at + 1);
        }
        return parts;
    }
}
