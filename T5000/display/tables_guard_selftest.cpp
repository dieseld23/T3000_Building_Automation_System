// Checks display/tables.h against the header it was copied from.
//
// T3000's tables are CString arrays in T3000/global_define.h, which cannot be
// compiled here, so the header is read as text instead: find the array, strip
// comments, collect each _T("...") entry, and compare - count first, then
// every entry. An entry changed there fails the build here.
//
// The parser is the weak point of a check like this: one that kept commented
// entries, or passed escapes through, would agree with a copy made the same
// wrong way. So it is tested on its own input first, the header's tables are
// also pinned by hand at the places that are easy to get wrong, and anything
// it does not understand is a failure rather than a guess.

#include "tables.h"
#include "../testing/check.h"

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    using namespace t5000::display;
    using namespace t5000::testing;

    // Removes // and /* */ comments, leaving string literals alone.
    std::string strip_comments(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());

        enum { Code, String, Line, Block } state = Code;
        for (size_t i = 0; i < s.size(); i++)
        {
            const char c = s[i];
            const char next = i + 1 < s.size() ? s[i + 1] : '\0';

            switch (state)
            {
            case Code:
                if (c == '"') { state = String; out += c; }
                else if (c == '/' && next == '/') { state = Line; i++; }
                else if (c == '/' && next == '*') { state = Block; i++; }
                else out += c;
                break;
            case String:
                out += c;
                if (c == '\\' && i + 1 < s.size()) { out += s[++i]; }
                else if (c == '"') state = Code;
                break;
            case Line:
                if (c == '\n') { state = Code; out += c; }
                break;
            case Block:
                if (c == '*' && next == '/') { state = Code; i++; }
                break;
            }
        }
        return out;
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
        else
        {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    int hex_digit(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    // Parses `const CString <name>[...] = { _T("..."), ... };` out of text.
    // Only \uXXXX escapes are understood, because they are the only ones
    // these tables use; any other is an error, not something to pass along.
    bool parse_table(const std::string& text, const std::string& name,
                     std::vector<std::string>& out, std::string& error)
    {
        out.clear();
        const std::string code = strip_comments(text);

        const std::string head = "CString " + name + "[";
        const size_t at = code.find(head);
        if (at == std::string::npos)
        {
            error = name + " not found";
            return false;
        }
        const size_t open  = code.find('{', at);
        const size_t close = code.find("};", open);
        if (open == std::string::npos || close == std::string::npos)
        {
            error = name + ": no { ... }; after it";
            return false;
        }
        const std::string body = code.substr(open + 1, close - open - 1);

        size_t i = 0;
        while ((i = body.find("_T(\"", i)) != std::string::npos)
        {
            i += 4;
            std::string entry;
            for (;;)
            {
                if (i >= body.size())
                {
                    error = name + ": unterminated string";
                    return false;
                }
                const char c = body[i++];
                if (c == '"')
                    break;
                if (c != '\\')
                {
                    if ((unsigned char)c >= 0x80)
                    {
                        error = name + ": a non-ASCII byte in the source, which this parser does not decode";
                        return false;
                    }
                    entry += c;
                    continue;
                }
                if (i >= body.size() || body[i] != 'u' || i + 4 >= body.size())
                {
                    error = name + ": an escape other than \\uXXXX";
                    return false;
                }
                unsigned cp = 0;
                for (int k = 1; k <= 4; k++)
                {
                    const int d = hex_digit(body[i + k]);
                    if (d < 0)
                    {
                        error = name + ": a malformed \\u escape";
                        return false;
                    }
                    cp = cp * 16 + (unsigned)d;
                }
                i += 5;
                append_utf8(entry, cp);
            }
            out.push_back(entry);
        }
        return true;
    }

    // The indices at which the two tables differ, over the entries both have.
    // Its own function so that it can be tested on tables that DO differ -
    // against the real ones it only ever sees agreement, and a comparison
    // that had stopped comparing would look exactly the same.
    template <size_t N>
    std::vector<size_t> differences(const std::vector<std::string>& theirs,
                                    const char* const (&ours)[N])
    {
        std::vector<size_t> out;
        const size_t n = theirs.size() < N ? theirs.size() : N;
        for (size_t i = 0; i < n; i++)
        {
            if (theirs[i] != ours[i])
                out.push_back(i);
        }
        return out;
    }

    template <size_t N>
    void compare(const std::string& header, const char* t3000_name,
                 const char* const (&ours)[N], const char* what)
    {
        std::vector<std::string> theirs;
        std::string error;
        if (!require(parse_table(header, t3000_name, theirs, error), what))
        {
            printf("        %s\n", error.c_str());
            return;
        }

        check_eq((long)theirs.size(), (long)N, what);
        const std::vector<size_t> differ = differences(theirs, ours);
        check(differ.empty(), what);
        for (const size_t i : differ)
        {
            printf("        entry %u: T3000 has \"%s\", T5000 has \"%s\"\n",
                   (unsigned)i, theirs[i].c_str(), ours[i]);
        }
    }

    void test_the_comparison_on_its_own()
    {
        section("the comparison, on tables that differ");

        static const char* const ours[] = { "Off/On", "Close/Open", "Stop/Start" };
        check(differences({ "Off/On", "Close/Open", "Stop/Start" }, ours).empty(),
              "identical tables have no differences");

        const std::vector<size_t> one = differences({ "Off/On", "Closed/Open", "Stop/Start" }, ours);
        check(one.size() == 1 && one[0] == 1, "a changed entry is found, at its index");

        check(differences({ "Off/On" }, ours).empty(),
              "only shared entries are compared - the count is checked separately");
    }

    void test_the_parser_on_its_own()
    {
        section("the table parser, on input written to trip it");

        const std::string text =
            "const CString Other[] = { _T(\"x\") };\n"
            "const CString Sample[] =\n"
            "{\n"
            "\t_T(\"a\"),   // _T(\"not this\")\n"
            "\t//_T(\"nor this\"),\n"
            "\t/* _T(\"nor this\"), */\n"
            "\t_T(\"\\u00B0C\"),\n"
            "\t_T(\"half // a slash pair inside a string\"),\n"
            "    _T(\"\")\n"
            "};\n";

        std::vector<std::string> got;
        std::string error;
        if (require(parse_table(text, "Sample", got, error), "parses a table with comments in it"))
        {
            check_eq((long)got.size(), 4, "  keeps four entries, skipping every commented one");
            if (got.size() == 4)
            {
                check(got[0] == "a", "  plain text");
                check(got[1] == "\xC2\xB0" "C", "  \\u00B0 decoded to UTF-8");
                check(got[2] == "half // a slash pair inside a string",
                      "  // inside a string is text, not a comment");
                check(got[3].empty(), "  an empty entry is an entry");
            }
        }

        check(!parse_table("const CString T[] = { _T(\"a\\tb\") };", "T", got, error),
              "refuses an escape it does not decode, rather than passing it through");
        check(!parse_table("const CString T[] = { _T(\"a\") };", "Missing", got, error),
              "and a table that is not there is an error");
    }

    void test_the_tables_are_pinned_by_hand()
    {
        section("the places a copy of these tables goes wrong, pinned without the parser");

        // Three entries of Input_List_Analog_Units are commented out in the
        // source (global_define.h:915-917). Counting lines gives 43.
        check_eq((long)count(kInputAnalogUnits), 40, "Input_List_Analog_Units has 40 entries, not 43");
        check_streq(kInputAnalogUnits[19], "Volts", "  and [19] is Volts - the entry after the commented ones");
        check_streq(kInputAnalogUnits[36], "A", "  and [36] is A");
        check_eq((long)count(kInputAnalogRanges), 40, "Input_Analog_Units_Array has 40 entries");
        check_streq(kInputAnalogRanges[27], "Humidty %", "  with T3000's spelling kept");
        check_eq((long)count(kDigitalUnits), 23, "Digital_Units_Array has 23 entries");
        check_eq((long)count(kJumperStatus), 6, "JumperStatus has 6");
        check_eq((long)count(kInputStatus), 3, "Decom_Array has 3");
    }

    void test_the_tables_match_t3000()
    {
        section("display/tables.h matches T3000/global_define.h");

        const std::string path = g_source_root + "\\T3000\\global_define.h";
        std::ifstream f(path, std::ios::binary);
        if (!require((bool)f, "T3000/global_define.h can be opened"))
        {
            printf("        tried %s - pass --source-root <repository root> to --selftest\n", path.c_str());
            return;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string header = ss.str();

        compare(header, "Digital_Units_Array",      kDigitalUnits,      "Digital_Units_Array");
        compare(header, "Input_List_Analog_Units",  kInputAnalogUnits,  "Input_List_Analog_Units");
        compare(header, "Input_Analog_Units_Array", kInputAnalogRanges, "Input_Analog_Units_Array");
        compare(header, "JumperStatus",             kJumperStatus,      "JumperStatus");
        compare(header, "Decom_Array",              kInputStatus,       "Decom_Array");
    }
}

int run_tables_guard_tests()
{
    test_the_parser_on_its_own();
    test_the_comparison_on_its_own();
    test_the_tables_are_pinned_by_hand();
    test_the_tables_match_t3000();
    return 0;
}
