// Keeps the write code apart from everything that reads, as text checks on
// T5000's own source, and pins the T3000 source the write encoder copies.
//
//   S1  the read path includes no write header: bacnet/command.*,
//       bacnet/private_transfer.*, bacnet/point_read.*, app/*_read.*,
//       app/*_plan.*, app/panel_read.*, app/find_device.*, and everything in
//       discovery/, serial/ and offline/
//   S2  nothing outside bacnet/ includes a write header, except the
//       conformance checks. Nothing can send a write yet, and nothing above
//       bacnet/ can reach the code that encodes one.
//   S4  no header in bacnet/ takes or holds a raw command byte - a uint8_t or
//       unsigned char named `command`. The typed commands are the whole
//       defence; a raw byte beside them would go round it.
//   S5  in T5000.exe's code, sendto( appears only in discovery/scanner.cpp,
//       bacnet/point_read.cpp and self-tests. A new place that sends is a
//       decision, not a detail.
//
// The T3000 pins are text, not line numbers, and whitespace-blind:
//   - WritePrivateData encodes into uint8_t test_value[480], which is what
//     kMaxWritePayload rests on
//   - total_length = 7 + (end - start + 1) * entitysize
//   - an input write carries sizeof(Str_in_point) per point, copied raw after
//     the header
//   - the Inputs grid writes the one row it changed
//
// Uses std::filesystem to list T5000's files under --source-root.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "source_text.h"
#include "../testing/check.h"

namespace
{
    namespace fs = std::filesystem;
    using namespace t5000::testing;

    const char* const kWriteHeaders[] = {
        "write_command.h",
        "private_write.h",
    };

    struct SourceFile
    {
        std::string relative;   // "bacnet/point_read.cpp", forward slashes
        std::string text;
    };

    std::vector<SourceFile> t5000_sources(std::string& error)
    {
        std::vector<SourceFile> files;
        const fs::path root = fs::path(t5000::conformance::g_source_root) / "T5000";
        std::error_code ec;
        if (!fs::is_directory(root, ec))
        {
            error = "no T5000 folder under " + t5000::conformance::g_source_root;
            return files;
        }
        for (fs::recursive_directory_iterator it(root, ec), end; it != end && !ec; it.increment(ec))
        {
            if (!it->is_regular_file())
                continue;
            const std::string ext = it->path().extension().string();
            if (ext != ".h" && ext != ".cpp")
                continue;

            SourceFile f;
            f.relative = fs::relative(it->path(), root).generic_string();
            if (f.relative.rfind("x64/", 0) == 0 || f.relative.rfind("Release/", 0) == 0 ||
                f.relative.rfind("Debug/", 0) == 0)
                continue;
            std::ifstream in(it->path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            f.text = ss.str();
            files.push_back(f);
        }
        std::sort(files.begin(), files.end(),
                  [](const SourceFile& a, const SourceFile& b) { return a.relative < b.relative; });
        return files;
    }

    bool starts_with(const std::string& s, const char* prefix)
    {
        return s.rfind(prefix, 0) == 0;
    }

    std::string base_name(const std::string& relative)
    {
        const size_t slash = relative.rfind('/');
        return slash == std::string::npos ? relative : relative.substr(slash + 1);
    }

    bool is_selftest(const std::string& relative)
    {
        return relative.size() > 13 && relative.compare(relative.size() - 13, 13, "_selftest.cpp") == 0;
    }

    // The files the read path is made of.
    bool is_read_path(const std::string& r)
    {
        const std::string name = base_name(r);
        if (starts_with(r, "discovery/") || starts_with(r, "serial/") || starts_with(r, "offline/"))
            return true;
        if (starts_with(r, "bacnet/"))
            return starts_with(name, "command.") || starts_with(name, "private_transfer") ||
                   starts_with(name, "point_read");
        if (starts_with(r, "app/"))
        {
            const bool read_or_plan = name.find("_read.") != std::string::npos ||
                                      name.find("_plan.") != std::string::npos ||
                                      name.find("_read_selftest") != std::string::npos ||
                                      name.find("_plan_selftest") != std::string::npos;
            return read_or_plan || starts_with(name, "panel_read") || starts_with(name, "find_device");
        }
        return false;
    }

    // Every header name a file includes with quotes, by base name.
    std::vector<std::string> includes(const std::string& text)
    {
        std::vector<std::string> out;
        static const std::regex include_line(R"re(#\s*include\s*"([^"]+)")re");
        for (std::sregex_iterator it(text.begin(), text.end(), include_line), end; it != end; ++it)
            out.push_back(base_name((*it)[1].str()));
        return out;
    }

    bool includes_a_write_header(const SourceFile& f, std::string& which)
    {
        for (const std::string& inc : includes(f.text))
        {
            for (const char* w : kWriteHeaders)
            {
                if (inc == w)
                {
                    which = inc;
                    return true;
                }
            }
        }
        return false;
    }

    void test_separation()
    {
        section("the write code is apart from the read path");

        std::string error;
        const std::vector<SourceFile> files = t5000_sources(error);
        if (!require(files.size() > 50, "T5000's source files are listed"))
        {
            printf("        %s\n", error.c_str());
            return;
        }

        int read_path_files = 0, s1 = 0, s2 = 0, s4 = 0, s5 = 0;
        bool saw_point_read = false, saw_write_command = false;

        static const std::regex raw_command(R"re(\b(uint8_t|unsigned\s+char)\s+command\b)re");

        for (const SourceFile& f : files)
        {
            std::string which;
            const bool writes = includes_a_write_header(f, which);
            saw_point_read    = saw_point_read || f.relative == "bacnet/point_read.cpp";
            saw_write_command = saw_write_command || f.relative == "bacnet/write_command.h";

            if (is_read_path(f.relative))
            {
                read_path_files++;
                if (writes)
                {
                    printf("        S1: %s is on the read path and includes %s\n", f.relative.c_str(), which.c_str());
                    s1++;
                }
            }

            if (writes && !starts_with(f.relative, "bacnet/") && !starts_with(f.relative, "conformance/"))
            {
                printf("        S2: %s includes %s\n", f.relative.c_str(), which.c_str());
                s2++;
            }

            if (starts_with(f.relative, "bacnet/") && base_name(f.relative).find(".h") != std::string::npos)
            {
                const std::string code = t5000::conformance::strip_comments(f.text);
                if (std::regex_search(code, raw_command))
                {
                    printf("        S4: %s has a raw command byte\n", f.relative.c_str());
                    s4++;
                }
            }

            // T5000.exe's code only: the conformance checks are another
            // program, and this file names the call it looks for.
            if (!starts_with(f.relative, "conformance/") && !is_selftest(f.relative) &&
                f.relative != "discovery/scanner.cpp" && f.relative != "bacnet/point_read.cpp" &&
                t5000::conformance::strip_comments(f.text).find("sendto(") != std::string::npos)
            {
                printf("        S5: %s calls sendto(\n", f.relative.c_str());
                s5++;
            }
        }

        check(saw_point_read && saw_write_command, "  the files these checks are about are among them");
        check(read_path_files > 20, "  and the read path is recognised");
        check_eq(s1, 0, "S1: no read-path file includes a write header");
        check_eq(s2, 0, "S2: nothing outside bacnet/ includes one, but the conformance checks");
        check_eq(s4, 0, "S4: no raw command byte in a bacnet/ header");
        check_eq(s5, 0, "S5: sendto( only in the scanner, the reader and self-tests");
    }

    // Whitespace collapsed to single spaces, so the pins survive re-indenting.
    std::string squeeze(const std::string& s)
    {
        std::string out;
        bool space = false;
        for (char c : s)
        {
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            {
                space = true;
                continue;
            }
            if (space && !out.empty())
                out += ' ';
            space = false;
            out += c;
        }
        return out;
    }

    int occurrences(const std::string& text, const std::string& what)
    {
        int n = 0;
        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + 1))
            n++;
        return n;
    }

    void test_t3000_pins()
    {
        section("the T3000 source the write encoder copies is as it was");

        std::string gf, input, error;
        if (!require(t5000::conformance::read_source("T3000\\global_function.cpp", gf, error), "global_function.cpp is read") ||
            !require(t5000::conformance::read_source("T3000\\BacnetInput.cpp", input, error), "BacnetInput.cpp is read"))
        {
            printf("        %s\n", error.c_str());
            return;
        }
        gf    = squeeze(gf);
        input = squeeze(input);

        const std::string signature = "int WritePrivateData(uint32_t deviceid,unsigned char n_command,";
        const size_t at = gf.find(signature);
        if (require(at != std::string::npos && occurrences(gf, signature) == 1, "WritePrivateData is found, once"))
        {
            const std::string head = gf.substr(at, 400);
            check(head.find("uint8_t test_value[480] = { 0 };") != std::string::npos,
                  "  it encodes into uint8_t test_value[480]: kMaxWritePayload's limit");
        }

        check_eq(occurrences(gf, "private_data_chunk.total_length = PRIVATE_HEAD_LENGTH + ((unsigned char)end_instance - "
                                 "(unsigned char)start_instance + 1)*entitysize;"),
                 1, "total_length is the header and every point after it");
        check_eq(occurrences(gf, "case WRITEINPUT_T3000: entitysize = sizeof(Str_in_point); break;"), 1,
                 "an input write's points are Str_in_point");
        check_eq(occurrences(gf, "memcpy_s(SendBuffer + i * sizeof(Str_in_point) + HEADER_LENGTH, sizeof(Str_in_point), "
                                 "&m_Input_data.at(i + start_instance), sizeof(Str_in_point));"),
                 1, "  copied raw, one after another, after the header");
        check_eq(occurrences(input, "Post_Write_Message(g_bac_instance,WRITEINPUT_T3000,Changed_Item,Changed_Item,"), 1,
                 "the Inputs grid writes the one row it changed");
    }
}

int run_write_separation_guard_tests()
{
    test_separation();
    printf("\n");
    test_t3000_pins();
    return 0;
}
