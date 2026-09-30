// Keeps the write code apart from everything that reads, as text checks on
// T5000's own source, and pins the T3000 source the write encoder copies.
//
//   S1  the read path includes no write header: bacnet/command.*,
//       bacnet/private_transfer.*, bacnet/point_read.*, app/*_read.*,
//       app/*_plan.*, app/panel_read.*, app/find_device.*, and everything in
//       discovery/, serial/, offline/ and firmware/
//   S2  nothing outside bacnet/ includes a write header, except the
//       conformance checks. Nothing can send a write yet, and nothing above
//       bacnet/ can reach the code that encodes one.
//   S4  no header in bacnet/ takes or holds a raw command byte - a uint8_t or
//       unsigned char named `command`. The typed commands are the whole
//       defence; a raw byte beside them would go round it.
//   S5  in T5000.exe's code, sendto( appears only in discovery/scanner.cpp,
//       bacnet/point_read.cpp and self-tests. A new place that sends is a
//       decision, not a detail.
//   S6  firmware/ reads the bytes it is given and nothing else: it includes
//       only its own headers, the test harness and the standard library,
//       and names no socket, serial port or file. Until T5000 flashes, a
//       firmware file goes nowhere.
//   S7  app/firmware_page.*, which checks a file for the Firmware page, can
//       reach no transport: nothing it includes, or what that includes in
//       turn, is in bacnet/, serial/, discovery/, net/, http/ or store/, or
//       is an OS or file header, and it names no socket, serial port or
//       file. In main.cpp, the route that checks a file calls only
//       check_firmware_json, and is the only route that takes a firmware
//       file's size; each points page keeps the bootloader's version its
//       read gave.
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

    std::string lowered(std::string s)
    {
        for (char& c : s)
            c = (char)tolower((unsigned char)c);
        return s;
    }

    // Windows opens App/Firmware_Read.h as app/firmware_read.h.
    bool same_file(const std::string& a, const std::string& b)
    {
        return lowered(a) == lowered(b);
    }

    bool is_selftest(const std::string& relative)
    {
        return relative.size() > 13 && relative.compare(relative.size() - 13, 13, "_selftest.cpp") == 0;
    }

    // The files the read path is made of.
    bool is_read_path(const std::string& r)
    {
        const std::string name = base_name(r);
        if (starts_with(r, "discovery/") || starts_with(r, "serial/") || starts_with(r, "offline/") ||
            starts_with(r, "firmware/"))
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

    // Headers that reach the OS: sockets, serial ports, files.
    const char* const kOsHeaders[] = { "windows.h", "winsock.h", "winsock2.h", "ws2tcpip.h", "iphlpapi.h",
                                       "io.h",      "fcntl.h",   "filesystem", "fstream",    "cstdio" };

    // Calls that send, or open a port or a file.
    const char* const kReachingCalls[] = { "socket(",   "sendto(", "send(",   "CreateFile", "WriteFile(",
                                           "ReadFile(", "fopen",   "_open(",  "ifstream",   "ofstream" };

    bool includes_an_os_header(const SourceFile& f, std::string& what)
    {
        // In quotes too: "winsock2.h" finds the system's when T5000 has no
        // file of that name. And in any case, as Windows opens it.
        static const std::regex angled(R"re(#\s*include\s*<([^>]+)>)re");
        static const std::regex quoted(R"re(#\s*include\s*"([^"]+)")re");
        for (const std::regex* form : { &angled, &quoted })
        {
            for (std::sregex_iterator it(f.text.begin(), f.text.end(), *form), end; it != end; ++it)
            {
                std::string name = (*it)[1].str();
                std::replace(name.begin(), name.end(), '\\', '/');
                const std::string inc = lowered(base_name(name));
                for (const char* os : kOsHeaders)
                {
                    if (inc == os)
                    {
                        what = form == &angled ? "includes <" + (*it)[1].str() + ">"
                                               : "includes \"" + (*it)[1].str() + "\"";
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool names_a_reaching_call(const SourceFile& f, std::string& what)
    {
        const std::string code = t5000::conformance::strip_comments(f.text);
        for (const char* call : kReachingCalls)
        {
            if (code.find(call) != std::string::npos)
            {
                what = std::string("calls ") + call;
                return true;
            }
        }
        return false;
    }

    // S6: what a firmware/ file may not include or call.
    bool firmware_reaches_out(const SourceFile& f, std::string& what)
    {
        static const std::regex quoted(R"re(#\s*include\s*"([^"]+)")re");
        static const std::regex angled(R"re(#\s*include\s*<([^>]+)>)re");
        for (std::sregex_iterator it(f.text.begin(), f.text.end(), quoted), end; it != end; ++it)
        {
            const std::string inc = (*it)[1].str();
            if (inc.find('/') != std::string::npos && inc != "../testing/check.h")
            {
                what = "includes " + inc;
                return true;
            }
        }
        return includes_an_os_header(f, what) || names_a_reaching_call(f, what);
    }

    // A quoted include, as a path under T5000/: "../json/read.h" from
    // "app/firmware_page.cpp" is "json/read.h". Empty when it climbs out.
    std::string resolve(const std::string& from, const std::string& inc)
    {
        std::vector<std::string> parts;
        const size_t slash = from.rfind('/');
        std::string joined = (slash == std::string::npos ? std::string() : from.substr(0, slash + 1)) + inc;
        std::replace(joined.begin(), joined.end(), '\\', '/');   // the compiler takes either
        size_t start = 0;
        while (start <= joined.size())
        {
            size_t next = joined.find('/', start);
            if (next == std::string::npos)
                next = joined.size();
            const std::string part = joined.substr(start, next - start);
            if (part == "..")
            {
                if (parts.empty())
                    return std::string();
                parts.pop_back();
            }
            else if (!part.empty() && part != ".")
                parts.push_back(part);
            start = next + 1;
        }
        std::string out;
        for (const std::string& p : parts)
            out += (out.empty() ? "" : "/") + p;
        return out;
    }

    // Every T5000 file a file includes, and what those include in turn,
    // itself first. A quoted include T5000 does not have is kept by name,
    // so the check below sees it.
    std::vector<std::string> include_closure(const std::vector<SourceFile>& files, const std::string& start)
    {
        static const std::regex quoted(R"re(#\s*include\s*"([^"]+)")re");
        std::vector<std::string> seen = { start };
        for (size_t i = 0; i < seen.size(); i++)
        {
            const auto f = std::find_if(files.begin(), files.end(),
                                        [&](const SourceFile& s) { return same_file(s.relative, seen[i]); });
            if (f == files.end())
                continue;
            for (std::sregex_iterator it(f->text.begin(), f->text.end(), quoted), end; it != end; ++it)
            {
                std::string next = resolve(f->relative, (*it)[1].str());
                if (next.empty())
                    next = "(outside T5000) " + (*it)[1].str();
                if (std::none_of(seen.begin(), seen.end(), [&](const std::string& s) { return same_file(s, next); }))
                    seen.push_back(next);
            }
        }
        return seen;
    }

    // S7: whether a Firmware-page file reaches a transport, by what it
    // includes at any depth or by what it names itself.
    bool firmware_page_reaches_out(const std::vector<SourceFile>& files, const SourceFile& f, std::string& what)
    {
        for (const std::string& r : include_closure(files, f.relative))
        {
            // Windows opens Bacnet/ as bacnet/.
            const std::string lower = lowered(r);
            for (const char* dir : { "bacnet/", "serial/", "discovery/", "net/", "http/", "store/", "(outside" })
            {
                if (starts_with(lower, dir))
                {
                    what = "reaches " + r;
                    return true;
                }
            }
            const auto g = std::find_if(files.begin(), files.end(),
                                        [&](const SourceFile& s) { return same_file(s.relative, r); });
            std::string how;
            if (g != files.end() && includes_an_os_header(*g, how))
            {
                what = "reaches " + r + ", which " + how;
                return true;
            }
        }
        return names_a_reaching_call(f, what);
    }

    // The text of a main.cpp route's registration, from its path to the
    // `});` or `);` that ends it. Empty when the path is not registered
    // once.
    std::string route_text(const std::string& main, const std::string& path)
    {
        const std::string quoted = "\"" + path + "\",";
        const size_t at = main.find(quoted);
        if (at == std::string::npos || main.find(quoted, at + 1) != std::string::npos)
            return std::string();
        const size_t next = main.find("server.route(", at);
        return main.substr(at, (next == std::string::npos ? main.size() : next) - at);
    }

    int occurrences(const std::string& text, const std::string& what);
    std::string squeeze(const std::string& s);

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

        int read_path_files = 0, s1 = 0, s2 = 0, s4 = 0, s5 = 0, s6 = 0, firmware_files = 0, s7 = 0,
            firmware_page_files = 0;
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

            if (starts_with(f.relative, "firmware/"))
            {
                firmware_files++;
                std::string what;
                if (firmware_reaches_out(f, what))
                {
                    printf("        S6: %s %s\n", f.relative.c_str(), what.c_str());
                    s6++;
                }
            }

            if (f.relative == "app/firmware_page.h" || f.relative == "app/firmware_page.cpp")
            {
                firmware_page_files++;
                std::string what;
                if (firmware_page_reaches_out(files, f, what))
                {
                    printf("        S7: %s %s\n", f.relative.c_str(), what.c_str());
                    s7++;
                }
            }
        }

        check(saw_point_read && saw_write_command, "  the files these checks are about are among them");
        check(read_path_files > 20, "  and the read path is recognised");
        check_eq(s1, 0, "S1: no read-path file includes a write header");
        check_eq(s2, 0, "S2: nothing outside bacnet/ includes one, but the conformance checks");
        check_eq(s4, 0, "S4: no raw command byte in a bacnet/ header");
        check_eq(s5, 0, "S5: sendto( only in the scanner, the reader and self-tests");
        check(firmware_files >= 6, "  firmware/'s files are among them");
        check_eq(s6, 0, "S6: firmware/ reads what it is given, and reaches nothing else");
        check_eq(firmware_page_files, 2, "  app/firmware_page.h and .cpp are among them");
        check_eq(s7, 0, "S7: the Firmware page's check reaches no transport, at any depth");
        const std::vector<std::string> reached = include_closure(files, "app/firmware_page.cpp");
        const auto has = [&](const char* r) { return std::find(reached.begin(), reached.end(), r) != reached.end(); };
        check(has("app/firmware_page.h") && has("json/read.h") && has("wire/panel.h") && has("device/connection.h"),
              "  its includes are followed, down to the headers of the headers it includes");

        std::string what;
        std::vector<SourceFile> planted = { { "app/firmware_page.cpp", "#include \"points_json.h\"\n" },
                                            { "app/points_json.h", "#include \"../wire/panel.h\"\n" },
                                            { "wire/panel.h", "#include \"../bacnet/point_read.h\"\n" },
                                            { "bacnet/point_read.h", "" } };
        check(firmware_page_reaches_out(planted, planted[0], what) && what == "reaches bacnet/point_read.h",
              "  a transport three includes down is seen");
        planted[2].text = "#include <winsock2.h>\n";
        check(firmware_page_reaches_out(planted, planted[0], what) &&
                  what == "reaches wire/panel.h, which includes <winsock2.h>",
              "  and so is an OS header");
        planted[2].text = "";
        planted[0].text += "void f() { sendto(0, 0, 0, 0, 0, 0); }\n";
        check(firmware_page_reaches_out(planted, planted[0], what) && what == "calls sendto(",
              "  and a call it names itself");
        planted[0].text = "#include \"../../elsewhere.h\"\n";
        check(firmware_page_reaches_out(planted, planted[0], what), "  and a header from outside T5000");
        planted[0].text = "#include \"..\\bacnet\\point_read.h\"\n";
        check(firmware_page_reaches_out(planted, planted[0], what) && what == "reaches bacnet/point_read.h",
              "  and one spelled with backslashes");
        planted[0].text = "#include \"../Bacnet/point_read.h\"\n";
        check(firmware_page_reaches_out(planted, planted[0], what), "  or in another case");
        planted[2].text = "#include <WinSock2.h>\n";
        planted[0].text = "#include \"points_json.h\"\n";
        check(firmware_page_reaches_out(planted, planted[0], what), "  and an OS header in another case");
        planted[2].text = "";
        planted[0].text = "#include \"winsock2.h\"\n";
        check(firmware_page_reaches_out(planted, planted[0], what) && what == "reaches app/firmware_page.cpp, which includes \"winsock2.h\"",
              "  and an OS header in quotes");
        std::vector<SourceFile> cased = { { "app/firmware_page.cpp", "#include \"../App/Firmware_Read.h\"\n" },
                                          { "app/firmware_read.h", "#include \"../bacnet/point_read.h\"\n" },
                                          { "bacnet/point_read.h", "" } };
        check(firmware_page_reaches_out(cased, cased[0], what) && what == "reaches bacnet/point_read.h",
              "  and a transport reached through a header named in another case");
        planted[0].text = "#include \"points_json.h\"\n";
        check(!firmware_page_reaches_out(planted, planted[0], what), "  while what reaches nothing passes");

        section("the Firmware routes in main.cpp");

        const auto main = std::find_if(files.begin(), files.end(),
                                       [](const SourceFile& s) { return s.relative == "main.cpp"; });
        if (!require(main != files.end(), "main.cpp is read"))
            return;
        const std::string code  = t5000::conformance::strip_comments(main->text);
        const std::string check_route = route_text(code, "/api/firmware/check");
        if (require(!check_route.empty(), "/api/firmware/check is registered, once"))
        {
            check(check_route.find("app::check_firmware_json(g_registry, request, req.body)") != std::string::npos,
                  "  it answers with check_firmware_json");
            bool reaches = false;
            for (const char* word : { "ransport", "read_bootloader", "sendto(", "Udp", "Serial", "scan", "fopen",
                                      "stream", "CreateFile" })
                reaches = reaches || check_route.find(word) != std::string::npos;
            check(!reaches, "  and names no transport, read or file");
            check(check_route.find("app::kLargestFirmwareRequest);") != std::string::npos,
                  "  it takes up to kLargestFirmwareRequest");
        }
        check_eq(occurrences(code, "kLargestFirmwareRequest"), 1, "  and no other route does");

        for (const char* page : { "Inputs", "Outputs", "Variables" })
        {
            std::string lower = page;
            lower[0] = (char)tolower((unsigned char)lower[0]);
            const std::string read = "app::read_planned_" + lower +
                                     "(d, plan, transport, bacnet::ReadSettings(), g_next_invoke_id, &panel); "
                                     "app::keep_bootloader(g_registry, d.handle, panel, \"the " + page + " page\");";
            check_eq(occurrences(squeeze(code), read), 1,
                     (std::string("the ") + page + " page keeps the bootloader's version its read gave").c_str());
        }
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
