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

    bool parse_u64(const std::string& text, unsigned long long& out)
    {
        // Rejected explicitly. strtoull happily wraps "-1" round to
        // 18446744073709551615, which as a handle would be a number no device
        // has rather than the error it actually is.
        if (text.find('-') != std::string::npos)
            return false;

        char* end = nullptr;
        const unsigned long long value = strtoull(text.c_str(), &end, 10);
        if (!all_consumed(text, end))
            return false;

        out = value;
        return true;
    }

    bool read_u64(const std::string& object, const char* key, unsigned long long& target)
    {
        std::string raw;
        if (!find_value(object, key, raw))
            return true;

        return parse_u64(raw, target);
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

    namespace
    {
        void skip_space(const std::string& s, size_t& i)
        {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
                i++;
        }

        bool read_hex4(const std::string& s, size_t& i, unsigned& out)
        {
            if (i + 4 > s.size())
                return false;

            out = 0;
            for (int k = 0; k < 4; k++)
            {
                const char c = s[i++];
                out <<= 4;
                if (c >= '0' && c <= '9')      out |= (unsigned)(c - '0');
                else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
                else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
                else return false;
            }
            return true;
        }

        void append_utf8(std::string& out, unsigned cp)
        {
            if (cp < 0x80)
            {
                out += (char)cp;
            }
            else if (cp < 0x800)
            {
                out += (char)(0xC0 | (cp >> 6));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else if (cp < 0x10000)
            {
                out += (char)(0xE0 | (cp >> 12));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
            else
            {
                out += (char)(0xF0 | (cp >> 18));
                out += (char)(0x80 | ((cp >> 12) & 0x3F));
                out += (char)(0x80 | ((cp >> 6) & 0x3F));
                out += (char)(0x80 | (cp & 0x3F));
            }
        }

        // s[i] is the opening quote. Leaves i after the closing one.
        bool read_quoted(const std::string& s, size_t& i, std::string& out, std::string& error)
        {
            i++;
            for (;;)
            {
                if (i >= s.size())
                {
                    error = "a string is not closed";
                    return false;
                }

                const unsigned char c = (unsigned char)s[i++];
                if (c == '"')
                    return true;
                if (c < 0x20)
                {
                    error = "a string holds a control character that is not escaped";
                    return false;
                }
                if (c != '\\')
                {
                    out += (char)c;
                    continue;
                }

                if (i >= s.size())
                {
                    error = "a string ends in the middle of an escape";
                    return false;
                }

                const char e = s[i++];
                switch (e)
                {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'n':  out += '\n'; break;
                case 'r':  out += '\r'; break;
                case 't':  out += '\t'; break;
                case 'u':
                {
                    unsigned cp = 0;
                    if (!read_hex4(s, i, cp))
                    {
                        error = "a \\u escape is not four hex digits";
                        return false;
                    }

                    // Outside the basic plane, JSON spells a character as a
                    // UTF-16 surrogate pair. Half a pair is not a character.
                    if (cp >= 0xD800 && cp <= 0xDBFF)
                    {
                        unsigned low = 0;
                        if (i + 1 >= s.size() || s[i] != '\\' || s[i + 1] != 'u')
                        {
                            error = "a \\u escape is half of a surrogate pair";
                            return false;
                        }
                        i += 2;
                        if (!read_hex4(s, i, low) || low < 0xDC00 || low > 0xDFFF)
                        {
                            error = "a \\u escape is half of a surrogate pair";
                            return false;
                        }
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (cp >= 0xDC00 && cp <= 0xDFFF)
                    {
                        error = "a \\u escape is half of a surrogate pair";
                        return false;
                    }

                    append_utf8(out, cp);
                    break;
                }
                default:
                    error = std::string("\\") + e + " is not a JSON escape";
                    return false;
                }
            }
        }
    }

    bool parse_flat_object(const std::string& text, std::map<std::string, FlatValue>& out,
                           std::string& error)
    {
        out.clear();

        size_t i = 0;
        skip_space(text, i);
        if (i >= text.size() || text[i] != '{')
        {
            error = "not a JSON object";
            return false;
        }
        i++;
        skip_space(text, i);

        if (i < text.size() && text[i] == '}')
        {
            i++;
        }
        else
        {
            for (;;)
            {
                skip_space(text, i);
                if (i >= text.size() || text[i] != '"')
                {
                    error = "a key was expected";
                    return false;
                }

                std::string key;
                if (!read_quoted(text, i, key, error))
                    return false;

                skip_space(text, i);
                if (i >= text.size() || text[i] != ':')
                {
                    error = "\"" + key + "\" has no value";
                    return false;
                }
                i++;
                skip_space(text, i);

                FlatValue value;
                if (i < text.size() && text[i] == '"')
                {
                    value.is_string = true;
                    if (!read_quoted(text, i, value.text, error))
                        return false;
                }
                else if (i < text.size() && (text[i] == '{' || text[i] == '['))
                {
                    error = "\"" + key + "\" holds an object or an array, and only flat objects are read";
                    return false;
                }
                else
                {
                    const size_t start = i;
                    while (i < text.size() && text[i] != ',' && text[i] != '}' &&
                           text[i] != ' ' && text[i] != '\t' && text[i] != '\r' && text[i] != '\n')
                        i++;
                    value.text = text.substr(start, i - start);
                    if (value.text.empty())
                    {
                        error = "\"" + key + "\" has no value";
                        return false;
                    }
                }

                if (!out.emplace(key, value).second)
                {
                    error = "\"" + key + "\" appears twice";
                    return false;
                }

                skip_space(text, i);
                if (i < text.size() && text[i] == ',')
                {
                    i++;
                    continue;
                }
                if (i < text.size() && text[i] == '}')
                {
                    i++;
                    break;
                }
                error = "a comma or a closing brace was expected after \"" + key + "\"";
                return false;
            }
        }

        skip_space(text, i);
        if (i != text.size())
        {
            error = "there is text after the object";
            return false;
        }
        return true;
    }
}
