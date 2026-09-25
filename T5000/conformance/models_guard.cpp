// Checks device::known_models() against the two places T3000 names models.
//
// The table is T3000's Add virtual device list - init_product_list in
// T3000/global_function.cpp - entry for entry and in order, under the names
// Getminitypename in T3000/BacnetSetting.cpp gives the panel types. Neither
// can be compiled here, so both are read as text, as tables_guard.cpp reads
// global_define.h: find the function, drop what is under #if 0, and read its
// statements in order. Panel types given by name are looked up in
// global_define.h.
//
// As there, the parser is the weak point, so it is tried first on input
// written to trip it. An entry it cannot read completely is an error, not
// one to skip: T3000 reuses one struct for every entry, so a field it failed
// to read would otherwise come silently from the entry before.

#include "../device/product.h"
#include "../testing/check.h"
#include "source_text.h"

#include <stdio.h>

#include <map>
#include <regex>
#include <string>
#include <vector>

namespace
{
    using namespace t5000::device;
    using namespace t5000::testing;
    using t5000::conformance::parse_constant;
    using t5000::conformance::read_source;
    using t5000::conformance::strip_comments;

    // The body of the one function with this signature, without its braces.
    // Comments are stripped from the signature on, not from the whole file:
    // the files hold GBK text, whose second bytes can be a backslash, and a
    // string that seems to end late would throw the stripper out of step
    // before it got here. Both functions read are ASCII.
    bool function_body(const std::string& text, const std::string& signature, std::string& body,
                       std::string& error)
    {
        const size_t at = text.find(signature);
        if (at == std::string::npos || text.find(signature, at + 1) != std::string::npos)
        {
            error = signature + (at == std::string::npos ? " is not found" : " is found more than once");
            return false;
        }

        const std::string code = strip_comments(text.substr(at));
        const size_t open = code.find('{');
        if (open == std::string::npos)
        {
            error = signature + " has no body";
            return false;
        }
        int depth = 0;
        for (size_t i = open; i < code.size(); i++)
        {
            if (code[i] == '{')
            {
                depth++;
            }
            else if (code[i] == '}' && --depth == 0)
            {
                body = code.substr(open + 1, i - open - 1);
                return true;
            }
        }
        error = signature + " has no closing brace";
        return false;
    }

    // Drops each #if 0 block, #if blocks inside it counted, up to its #endif.
    // T3000's list ends with one: an INI reader that sets the same fields
    // from a file (global_function.cpp:12162-12198).
    std::string without_if_0(const std::string& body)
    {
        std::string out;
        int skipping = 0;
        size_t start = 0;
        while (start < body.size())
        {
            size_t end = body.find('\n', start);
            end = end == std::string::npos ? body.size() : end + 1;
            const std::string line = body.substr(start, end - start);
            start = end;

            const size_t first = line.find_first_not_of(" \t");
            const std::string t = first == std::string::npos ? std::string() : line.substr(first);
            if (skipping)
            {
                if (t.compare(0, 3, "#if") == 0)
                    skipping++;
                else if (t.compare(0, 6, "#endif") == 0)
                    skipping--;
            }
            else if (t.compare(0, 5, "#if 0") == 0)
            {
                skipping = 1;
            }
            else
            {
                out += line;
            }
        }
        return out;
    }

    struct ListEntry
    {
        std::string name;
        long        pid = -1;
        std::string sub_pid;   // a number, or the name of a constant
    };

    // The entries init_product_list pushes, in order: for each push_back, the
    // cs_name, pid and sub_pid set since the one before.
    bool parse_product_list(const std::string& body, std::vector<ListEntry>& out, std::string& error)
    {
        static const std::regex statement(
            R"re(temp\s*\.\s*cs_name\s*=\s*_T\s*\(\s*"([^"]*)"\s*\)\s*;)re"
            R"re(|temp\s*\.\s*pid\s*=\s*([0-9]+)\s*;)re"
            R"re(|temp\s*\.\s*sub_pid\s*=\s*([A-Za-z_][A-Za-z0-9_]*|[0-9]+)\s*;)re"
            R"re(|m_product_iocount\s*\.\s*push_back\s*\(\s*temp\s*\)\s*;)re");

        out.clear();
        const std::string code = without_if_0(body);
        ListEntry entry;
        bool named = false, has_pid = false, has_sub_pid = false;
        for (auto it = std::sregex_iterator(code.begin(), code.end(), statement); it != std::sregex_iterator(); ++it)
        {
            const std::smatch& m = *it;
            if (m[1].matched)
            {
                entry.name = m[1].str();
                named = true;
            }
            else if (m[2].matched)
            {
                entry.pid = std::stol(m[2].str());
                has_pid = true;
            }
            else if (m[3].matched)
            {
                entry.sub_pid = m[3].str();
                has_sub_pid = true;
            }
            else
            {
                if (!named || !has_pid || !has_sub_pid)
                {
                    error = "entry " + std::to_string(out.size() + 1) + " is pushed without " +
                            (!named ? "a cs_name" : !has_pid ? "a pid that is a number" : "a sub_pid") +
                            " set since the entry before";
                    return false;
                }
                out.push_back(entry);
                named = has_pid = has_sub_pid = false;
            }
        }
        if (out.empty())
        {
            error = "no entries";
            return false;
        }
        return true;
    }

    // Getminitypename's name for each case label: the _T("...") assigned
    // after one or more `case X:`, up to the break. The default, and the
    // "(Asix)" added after the switch, are not a label's.
    bool parse_type_names(const std::string& body, std::map<std::string, std::string>& out, std::string& error)
    {
        static const std::regex token(
            R"re(case\s+([A-Za-z_][A-Za-z0-9_]*)\s*:)re"
            R"re(|ret_name\s*=\s*_T\s*\(\s*"([^"]*)"\s*\)\s*;)re"
            R"re(|\bbreak\s*;)re"
            R"re(|\bdefault\s*:)re");

        out.clear();
        const std::string code = without_if_0(body);
        std::vector<std::string> labels;
        for (auto it = std::sregex_iterator(code.begin(), code.end(), token); it != std::sregex_iterator(); ++it)
        {
            const std::smatch& m = *it;
            if (m[1].matched)
            {
                labels.push_back(m[1].str());
            }
            else if (m[2].matched)
            {
                for (const auto& label : labels)
                {
                    if (!out.emplace(label, m[2].str()).second)
                    {
                        error = label + " is named twice";
                        return false;
                    }
                }
            }
            else
            {
                labels.clear();
            }
        }
        if (out.empty())
        {
            error = "no names";
            return false;
        }
        return true;
    }

    void test_the_parsers_on_their_own()
    {
        section("the list and name parsers, on input written to trip them");

        const std::string list =
            "Str_product_io_count temp = { 0 };\n"
            "temp.cs_name = _T(\"First\");  temp.pid = 74;\n"
            "temp.sub_pid = MINIPANELARM;\n"
            "m_product_iocount.push_back(temp);\n"
            "temp.cs_name = _T(\"Second\");\n"
            "temp.pid = 10;\n"
            "temp.sub_pid = 11;\n"
            "m_product_iocount.push_back(temp);\n"
            "#if 0\n"
            "#ifdef NESTED\n"
            "#endif\n"
            "temp.cs_name = _T(\"Hidden\"); temp.pid = 1; temp.sub_pid = 1;\n"
            "m_product_iocount.push_back(temp);\n"
            "#endif\n";
        std::vector<ListEntry> entries;
        std::string error;
        const bool read = parse_product_list(strip_comments("// temp.cs_name = _T(\"Commented\");\n" + list), entries,
                                             error);
        check(read, "a list is read");
        if (read && require(entries.size() == 2, "  two entries: not the commented one, nor the one under #if 0"))
        {
            check(entries[0].name == "First" && entries[0].pid == 74 && entries[0].sub_pid == "MINIPANELARM",
                  "  the first, with its sub_pid by name");
            check(entries[1].name == "Second" && entries[1].pid == 10 && entries[1].sub_pid == "11",
                  "  the second, with its sub_pid a number");
        }

        const std::string inherited =
            "temp.cs_name = _T(\"A\"); temp.pid = 74; temp.sub_pid = 5; m_product_iocount.push_back(temp);\n"
            "temp.cs_name = _T(\"B\"); temp.sub_pid = 6; m_product_iocount.push_back(temp);\n";
        check(!parse_product_list(inherited, entries, error), "an entry that would inherit a pid is an error");
        const std::string symbolic =
            "temp.cs_name = _T(\"A\"); temp.pid = PM_TSTAT10; temp.sub_pid = 5; m_product_iocount.push_back(temp);\n";
        check(!parse_product_list(symbolic, entries, error), "  as is a pid that is not a number");

        const std::string names =
            "switch (n) {\n"
            "case ONE:\ncase TWO:\n ret_name = _T(\"Pair\");\n break;\n"
            "case THREE:\n ret_name = _T(\"Third\"); break;\n"
            "default:\n ret_name = _T(\" \"); break;\n}\n"
            "if (n < 5) ret_name = ret_name + _T(\"(Asix)\");\n";
        std::map<std::string, std::string> named;
        check(parse_type_names(names, named, error), "a switch of names is read");
        check(named.size() == 3, "  one name per label, and none for the default");
        check(named["ONE"] == "Pair" && named["TWO"] == "Pair", "  labels that fall through share the name");
        check(named["THREE"] == "Third", "  and a label on its own has its own");
        check(!parse_type_names("case ONE: ret_name = _T(\"A\"); break; case ONE: ret_name = _T(\"B\"); break;",
                                named, error),
              "a label named twice is an error");
    }

    bool read_or_fail(const char* relative, std::string& text)
    {
        std::string error;
        if (!read_source(relative, text, error))
        {
            check(false, (std::string(relative) + " can be read").c_str());
            printf("        %s\n", error.c_str());
            return false;
        }
        return true;
    }

    // A sub_pid or case label: a number, or a constant in global_define.h.
    bool value_of(const std::string& header, const std::string& token, long& value)
    {
        if (!token.empty() && token.find_first_not_of("0123456789") == std::string::npos)
        {
            value = std::stol(token);
            return true;
        }
        std::string error;
        if (parse_constant(header, token, value, error))
            return true;
        check(false, (token + " is a constant in global_define.h").c_str());
        printf("        %s\n", error.c_str());
        return false;
    }

    void test_the_models_are_t3000s_list()
    {
        section("device/product.cpp's models are T3000's Add virtual device list, in order");

        std::string source, header, body, error;
        if (!read_or_fail("T3000\\global_function.cpp", source) || !read_or_fail("T3000\\global_define.h", header))
            return;
        std::vector<ListEntry> entries;
        if (!function_body(source, "void init_product_list()", body, error) ||
            !parse_product_list(body, entries, error))
        {
            check(false, "init_product_list is read");
            printf("        %s\n", error.c_str());
            return;
        }

        // Custom Device is a third-party device, which cannot be added by hand.
        std::vector<ListEntry> listed;
        int custom = 0;
        for (const auto& e : entries)
        {
            if (e.pid == static_cast<long>(ProductClassId::ThirdPartyDevice))
                custom++;
            else
                listed.push_back(e);
        }
        check_eq(custom, 1, "the list has one entry for a third-party device, which is left out");

        const ModelTable models = known_models();
        check_eq(models.count, (long)listed.size(), "one model for each of the list's other entries");

        for (int i = 0; i < models.count && i < (int)listed.size(); i++)
        {
            const Model& m = models.entries[i];
            const ListEntry& e = listed[i];
            const std::string what = std::string(m.name) + " is the list's " + e.name;

            check_eq((long)static_cast<uint8_t>(m.product), e.pid, (what + ": its pid is the product").c_str());

            if (e.name == "T3_3IIC")
            {
                // The list's slip, which the table corrects. When this fails,
                // T3000 has fixed it, and the note on T3-3IIC can go.
                check(e.sub_pid == "T3_NG3",
                      "T3000's list still gives T3_3IIC the sub_pid T3_NG3, which the table corrects");
                long iic = 0;
                if (value_of(header, "T3_3IIC", iic))
                    check_eq((long)m.type, iic, (what + ": its panel type is T3_3IIC").c_str());
                continue;
            }

            long sub_pid = 0;
            if (value_of(header, e.sub_pid, sub_pid))
                check_eq((long)m.type, sub_pid, (what + ": its sub_pid, " + e.sub_pid + ", is the panel type").c_str());
        }
    }

    void test_the_models_are_named_as_t3000_names_them()
    {
        section("each model has the name T3000's Settings page gives its panel type");

        std::string source, header, body, error;
        if (!read_or_fail("T3000\\BacnetSetting.cpp", source) || !read_or_fail("T3000\\global_define.h", header))
            return;
        std::map<std::string, std::string> by_label;
        if (!function_body(source, "void Getminitypename(", body, error) || !parse_type_names(body, by_label, error))
        {
            check(false, "Getminitypename is read");
            printf("        %s\n", error.c_str());
            return;
        }

        std::map<long, std::string> by_value;
        for (const auto& [label, name] : by_label)
        {
            long value = 0;
            if (!value_of(header, label, value))
                continue;
            const auto [it, added] = by_value.emplace(value, name);
            if (!added && it->second != name)
                check(false, (label + " shares its value with a label of another name").c_str());
        }

        const ModelTable models = known_models();
        for (int i = 0; i < models.count; i++)
        {
            const Model& m = models.entries[i];
            const auto it = by_value.find(static_cast<long>(m.type));
            const std::string what = std::string(m.name) + ": Getminitypename names panel type " +
                                     std::to_string(static_cast<int>(m.type)) +
                                     (it == by_value.end() ? " nothing" : " " + it->second);
            check(it != by_value.end() && it->second == m.name, what.c_str());
        }
    }
}

int run_models_guard_tests()
{
    test_the_parsers_on_their_own();
    test_the_models_are_t3000s_list();
    test_the_models_are_named_as_t3000_names_them();
    return 0;
}
