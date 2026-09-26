#pragma once

// Reading T3000's source as text, for the checks that cannot include it:
// comments stripped, and integer constants found by their one definition.
// Shared by tables_guard.cpp, which tests the parser on input written to
// trip it, and models_guard.cpp.

#include <fstream>
#include <sstream>
#include <string>

#include "source_root.h"

namespace t5000::conformance
{
    // Removes // and /* */ comments, leaving string literals alone.
    inline std::string strip_comments(const std::string& s)
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

    inline bool is_identifier_char(char c)
    {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
    }

    // Finds the one definition of an integer constant, in any of the three
    // forms global_define.h uses: `const int NAME = 8;`, an enumerator
    // `NAME = 29,`, and `#define NAME 637`. Every other mention of the name
    // is a use and is skipped - including `NAME == 3`. None, or more than
    // one, is an error: a constant defined twice under #if is exactly the
    // case where picking one would be a guess.
    inline bool parse_constant(const std::string& text, const std::string& name, long& value,
                        std::string& error)
    {
        const std::string s = strip_comments(text);
        int found = 0;

        for (size_t at = s.find(name); at != std::string::npos; at = s.find(name, at + 1))
        {
            const size_t end = at + name.size();
            if ((at > 0 && is_identifier_char(s[at - 1])) || (end < s.size() && is_identifier_char(s[end])))
                continue;

            size_t i = end;
            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                i++;

            bool definition = false;
            if (i < s.size() && s[i] == '=' && (i + 1 >= s.size() || s[i + 1] != '='))
            {
                definition = true;
                i++;
            }
            else
            {
                // `#define NAME value`: look back over the whitespace for the
                // directive.
                size_t b = at;
                while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '\t'))
                    b--;
                const std::string directive = "#define";
                definition = b >= directive.size() && s.compare(b - directive.size(), directive.size(), directive) == 0;
            }
            if (!definition)
                continue;

            while (i < s.size() && (s[i] == ' ' || s[i] == '\t'))
                i++;
            const bool negative = i < s.size() && s[i] == '-';
            if (negative)
                i++;
            if (i >= s.size() || s[i] < '0' || s[i] > '9')
            {
                error = name + " is defined, but not as a plain integer";
                return false;
            }
            long v = 0;
            while (i < s.size() && s[i] >= '0' && s[i] <= '9')
                v = v * 10 + (s[i++] - '0');
            value = negative ? -v : v;
            found++;
        }

        if (found != 1)
        {
            error = found == 0 ? name + " is not defined" : name + " is defined " + std::to_string(found) + " times";
            return false;
        }
        return true;
    }

    // Reads a file under the repository root - T3000\global_define.h, say -
    // into `text`. False, with the path tried, when it cannot be opened.
    inline bool read_source(const std::string& relative, std::string& text, std::string& error)
    {
        const std::string path = g_source_root + "\\" + relative;
        std::ifstream f(path, std::ios::binary);
        if (!f)
        {
            error = "cannot open " + path + " - pass --source-root <repository root> to T5000Conformance.exe";
            return false;
        }
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
        return true;
    }
}
