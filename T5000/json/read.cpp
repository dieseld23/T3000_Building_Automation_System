#include "read.h"

#include <ctype.h>
#include <stdlib.h>

namespace t5000::json
{
    bool find_value(const std::string& object, const std::string& key, std::string& out)
    {
        const std::string needle = "\"" + key + "\"";
        const size_t at = object.find(needle);
        if (at == std::string::npos)
            return false;

        size_t colon = object.find(':', at + needle.size());
        if (colon == std::string::npos)
            return false;

        size_t i = colon + 1;
        while (i < object.size() && isspace((unsigned char)object[i])) i++;
        if (i >= object.size())
            return false;

        if (object[i] == '"')
        {
            const size_t start = ++i;
            while (i < object.size() && object[i] != '"') i++;
            if (i >= object.size())
                return false;
            out = object.substr(start, i - start);
            return true;
        }

        const size_t start = i;
        while (i < object.size() && object[i] != ',' && object[i] != '}') i++;
        out = object.substr(start, i - start);

        while (!out.empty() && isspace((unsigned char)out.back()))
            out.pop_back();
        return !out.empty();
    }

    namespace
    {
        // strtol and friends report "nothing was converted" by leaving `end`
        // at the start, which is the difference between the key being absent
        // and the key being the word "banana". The callers here care about
        // that difference: a bad value is a 400, a missing one is a default.
        bool all_consumed(const std::string& text, const char* end)
        {
            return !text.empty() && end != text.c_str() && *end == '\0';
        }
    }

    bool read_int(const std::string& object, const char* key, int& target)
    {
        std::string raw;
        if (!find_value(object, key, raw))
            return true;   // absent is not an error

        char* end = nullptr;
        const long value = strtol(raw.c_str(), &end, 10);
        if (!all_consumed(raw, end))
            return false;

        target = (int)value;
        return true;
    }

    bool read_u64(const std::string& object, const char* key, unsigned long long& target)
    {
        std::string raw;
        if (!find_value(object, key, raw))
            return true;

        // Rejected explicitly. strtoull happily wraps "-1" round to
        // 18446744073709551615, which as a handle would be a number no device
        // has rather than the error it actually is.
        if (raw.find('-') != std::string::npos)
            return false;

        char* end = nullptr;
        const unsigned long long value = strtoull(raw.c_str(), &end, 10);
        if (!all_consumed(raw, end))
            return false;

        target = value;
        return true;
    }

    bool read_string(const std::string& object, const char* key, std::string& target)
    {
        std::string raw;
        if (!find_value(object, key, raw))
            return true;

        target = raw;
        return true;
    }

    bool read_bool(const std::string& object, const char* key, bool& target)
    {
        std::string raw;
        if (!find_value(object, key, raw))
            return true;

        if (raw == "true")  { target = true;  return true; }
        if (raw == "false") { target = false; return true; }
        return false;
    }
}
