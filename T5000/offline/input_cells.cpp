#include "input_cells.h"

#include <stdlib.h>

#include <charconv>

#include "../display/tables.h"

namespace t5000::offline
{
    namespace
    {
        bool is_digit(char c)
        {
            return c >= '0' && c <= '9';
        }

        char lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
        }
    }

    bool typed_number(const std::string& text, double& value)
    {
        const size_t first = text.find_first_not_of(" \t");
        if (first == std::string::npos)
            return false;
        const size_t last = text.find_last_not_of(" \t");
        const std::string s = text.substr(first, last - first + 1);

        // [+-] digits [. digits] [e [+-] digits], with a digit before or
        // after the point.
        size_t i = 0;
        if (s[i] == '+' || s[i] == '-')
            i++;
        const size_t number = i;
        size_t digits = 0;
        while (i < s.size() && is_digit(s[i]))
        {
            i++;
            digits++;
        }
        if (i < s.size() && s[i] == '.')
        {
            i++;
            while (i < s.size() && is_digit(s[i]))
            {
                i++;
                digits++;
            }
        }
        if (digits == 0)
            return false;
        if (i < s.size() && (s[i] == 'e' || s[i] == 'E'))
        {
            i++;
            if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                i++;
            size_t exponent = 0;
            while (i < s.size() && is_digit(s[i]))
            {
                i++;
                exponent++;
            }
            if (exponent == 0)
                return false;
        }
        if (i != s.size())
            return false;

        // from_chars reads as the C locale does, whatever this process's
        // is, and rounds correctly, as the UCRT's _wtof does. It takes a
        // "-" but not a "+".
        const char* begin = s.data() + (s[0] == '+' ? number : 0);
        const char* end   = s.data() + s.size();
        const auto r = std::from_chars(begin, end, value);
        if (r.ptr != end)
            return false;
        // An exponent too large for a double: _wtof gives HUGE_VAL, which
        // no cell takes. from_chars leaves `value` unset and says so.
        return r.ec == std::errc();
    }

    bool value_thousandths(double typed, int32_t& value)
    {
        const double scaled = typed * 1000;

        // (int) truncates towards zero, so it is defined for anything above
        // INT_MIN - 1 and below INT_MAX + 1.
        if (!(scaled > -2147483649.0 && scaled < 2147483648.0))
            return false;
        value = (int32_t)scaled;
        return true;
    }

    bool calibration_bytes(double typed, CalibrationBytes& bytes)
    {
        // (float)_wtof(cs_temp) (:634). A double too large for a float has
        // no float, and such a number is refused below anyway; stopping
        // here keeps the cast defined.
        if (!(typed > -1.0e7 && typed < 1.0e7))
            return false;
        const float as_float = (float)typed;

        // cal_value = (int)(temp_value * 10) (:648): a float times an int is
        // a float, rounded to one before it is truncated. Held in a float
        // here so the rounding is the same however T5000 is compiled.
        const float scaled = as_float * 10;

        // abs(cal_value) above 65535 is refused (:649-655). Past 65536 the
        // truncation is still defined, but nothing there is taken.
        if (!(scaled > -65536.0f && scaled < 65536.0f))
            return false;
        const int tenths = abs((int)scaled);

        bytes.sign = as_float < 0 ? 1 : 0;
        bytes.high = (uint8_t)((tenths & 0xff00) >> 8);   // :660
        bytes.low  = (uint8_t)(tenths & 0x00ff);          // :659
        return true;
    }

    std::vector<std::string> signal_type_choices()
    {
        std::vector<std::string> out;
        for (size_t j = 0; j < display::count(display::kJumperStatus); j++)
        {
            if (j == 4)
                continue;
            out.push_back(display::kJumperStatus[j]);
        }
        return out;
    }

    int signal_type_from_name(const std::string& name)
    {
        int found = -1;
        for (size_t z = 0; z < display::count(display::kJumperStatus); z++)
        {
            if (same_ignoring_case(name, display::kJumperStatus[z]))
                found = (int)z;
        }
        return found;
    }

    bool digital_states(int range, std::string& off, std::string& on)
    {
        if (range < 1 || (size_t)range >= display::count(display::kDigitalUnits))
            return false;

        // SplitCStringA(temparray, temp1, "/"), then GetAt(0) and GetAt(1)
        // (:1537-1550). Each of the table's entries 1-22 has one "/", which
        // T5000Conformance checks.
        const std::string pair = display::kDigitalUnits[range];
        const size_t slash = pair.find('/');
        if (slash == std::string::npos || pair.find('/', slash + 1) != std::string::npos)
            return false;
        off = pair.substr(0, slash);
        on  = pair.substr(slash + 1);
        return true;
    }

    bool same_ignoring_case(const std::string& a, const std::string& b)
    {
        if (a.size() != b.size())
            return false;
        for (size_t i = 0; i < a.size(); i++)
        {
            if (lower(a[i]) != lower(b[i]))
                return false;
        }
        return true;
    }
}
