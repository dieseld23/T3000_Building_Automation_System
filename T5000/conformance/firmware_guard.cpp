// Holds firmware/ to ISP's source, read as text, and to the repository's own
// firmware files.
//
// ISP cannot be compiled here, so, as models_guard.cpp reads T3000, its
// functions are found by their signatures, their comments dropped and their
// white space squeezed, and read three ways:
//
//   tables   GetFirmwareUpdateName and GetProductName are parsed case by
//            case, their ids looked up in ProductModel.h, and compared with
//            firmware_name and product_name for every product 0 to 1000
//   chains   the alias chains of UpdataDeviceInformation and
//            UpdataDeviceInformation_ex are parsed branch by branch, and
//            compared with the *_names_match functions for every pair of
//            names either knows; the network's names ISP takes for any
//            device, which T5000 does not, are parsed and pinned
//   rules    the file's bootloader flags (FlashByEthernet, FlashByCom) and
//            check_bootloader_and_frimware's thresholds are parsed and run,
//            in ISP's float arithmetic where ISP uses it, and compared with
//            file_needs_new_bootloader and bootloader_call over every version
//
// The buffers, their fills and where the header is are parsed too, and
// T5000 is made to read files against them. Everything else is pinned as
// text: which serial thread checks which file, the chip size checks, Bin_Info,
// the reader's rules, and the ISP code T5000 is stricter than. A parse that
// does not account for every branch it finds is an error, not a partial
// result.
//
// Last, the repository's own .hex files (T3000\ResourceFile\HexFile) are read
// as ISP reads them, and a .bin that is not firmware is refused.

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <filesystem>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../firmware/firmware_check.h"
#include "../firmware/firmware_file.h"
#include "../testing/check.h"
#include "source_text.h"

namespace
{
    namespace fs = std::filesystem;
    using namespace t5000::firmware;
    using namespace t5000::testing;
    using t5000::conformance::parse_constant;
    using t5000::conformance::read_source;
    using t5000::conformance::strip_comments;

    // White space collapsed to single spaces, so the pins survive
    // re-indenting.
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

    std::string upper(std::string s)
    {
        for (char& c : s)
            c = (char)toupper((unsigned char)c);
        return s;
    }

    // The squeezed body of the one definition with this signature: the
    // signature followed by its brace, so a declaration of the same function
    // does not count. Comments are stripped from there on, as function_body
    // in source_text.h does and for its reason.
    bool definition(const std::string& text, const std::string& signature, std::string& body, std::string& error)
    {
        size_t found = std::string::npos;
        int n = 0;
        for (size_t at = text.find(signature); at != std::string::npos; at = text.find(signature, at + 1))
        {
            // White space and comments, then the brace.
            size_t i = at + signature.size();
            for (;;)
            {
                while (i < text.size() && isspace((unsigned char)text[i]))
                    i++;
                if (text.compare(i, 2, "//") == 0)
                    i = text.find('\n', i);
                else if (text.compare(i, 2, "/*") == 0)
                    i = text.find("*/", i) + 2;
                else
                    break;
                if (i == std::string::npos || i < 2)
                    break;
            }
            if (i < text.size() && text[i] == '{')
            {
                found = at;
                n++;
            }
        }
        if (n != 1)
        {
            error = signature + (n == 0 ? " is not defined" : " is defined more than once");
            return false;
        }
        const std::string code = strip_comments(text.substr(found));
        const size_t open = code.find('{');
        int depth = 0;
        for (size_t i = open; i < code.size(); i++)
        {
            if (code[i] == '{')
                depth++;
            else if (code[i] == '}' && --depth == 0)
            {
                body = squeeze(code.substr(open + 1, i - open - 1));
                return true;
            }
        }
        error = signature + " has no closing brace";
        return false;
    }

    // The ISP source, read once.
    struct Isp
    {
        std::string model;   // T3000\ProductModel.h, which ISP includes
        std::string global_function, com_writer, my_socket, isp_dlg, hex_parser, bin_parser, global_struct, tftp;
    };

    bool read_isp(Isp& isp, std::string& error)
    {
        return read_source("T3000\\ProductModel.h", isp.model, error) &&
               read_source("ISP\\global_function.cpp", isp.global_function, error) &&
               read_source("ISP\\ComWriter.cpp", isp.com_writer, error) &&
               read_source("ISP\\MySocket.cpp", isp.my_socket, error) &&
               read_source("ISP\\ISPDlg.cpp", isp.isp_dlg, error) &&
               read_source("ISP\\HexFileParser.cpp", isp.hex_parser, error) &&
               read_source("ISP\\BinFileParser.cpp", isp.bin_parser, error) &&
               read_source("ISP\\Global_Struct.h", isp.global_struct, error) &&
               read_source("ISP\\TFTPServer.cpp", isp.tftp, error);
    }

    // A function's body, or a failed check saying why not.
    bool body_of(const std::string& text, const std::string& signature, std::string& body)
    {
        std::string error;
        if (definition(text, signature, body, error))
            return true;
        check(false, ("ISP's " + signature + " is found").c_str());
        printf("        %s\n", error.c_str());
        return false;
    }

    void pin(const std::string& body, const std::string& text, const char* what)
    {
        const int n = occurrences(body, text);
        check(n == 1, what);
        if (n != 1)
            printf("        found %d times: %s\n", n, text.c_str());
    }

    void absent(const std::string& body, const std::string& text, const char* what)
    {
        check(body.find(text) == std::string::npos, what);
    }

    // ---- tables ----

    // The cases of a switch that sets strProductName, by product id, and its
    // default "PID%d".
    bool name_table(const std::string& body, const std::string& model, std::map<long, std::string>& out,
                    std::string& error)
    {
        static const std::regex statement(R"re(case\s+(\w+)\s*:)re"
                                          R"re(|strProductName\s*=\s*(?:_T\s*\(\s*|L)?"([^"]*)")re"
                                          R"re(|\bbreak\s*;)re"
                                          R"re(|default\s*:)re");
        out.clear();
        std::vector<std::string> pending;
        bool in_default = false;
        for (auto it = std::sregex_iterator(body.begin(), body.end(), statement); it != std::sregex_iterator(); ++it)
        {
            const std::smatch& m = *it;
            if (m[1].matched)
            {
                if (in_default)
                {
                    error = "a case after default";
                    return false;
                }
                pending.push_back(m[1].str());
            }
            else if (m[2].matched)
            {
                if (pending.empty())
                {
                    error = "the name \"" + m[2].str() + "\" is set with no case before it";
                    return false;
                }
                for (const std::string& label : pending)
                {
                    long id = 0;
                    if (!parse_constant(model, label, id, error))
                        return false;
                    if (!out.emplace(id, m[2].str()).second)
                    {
                        error = label + " (" + std::to_string(id) + ") is named twice";
                        return false;
                    }
                }
                pending.clear();
            }
            else if (m[0].str().compare(0, 7, "default") == 0)
            {
                in_default = true;
            }
            else if (!pending.empty())
            {
                error = "case " + pending.front() + " breaks with no name";
                return false;
            }
        }
        if (!pending.empty() || out.size() < 50)
        {
            error = "the switch is not read to its end, or holds fewer than 50 names";
            return false;
        }
        const std::string tail = "default: { strProductName.Format(_T(\"PID%d\"), ModelID); } break; } return strProductName;";
        if (occurrences(body, tail) != 1)
        {
            error = "its default is not PID and the product's number";
            return false;
        }
        return true;
    }

    void compare_table(const std::string& body, const std::string& model, std::string (*t5000)(int), const char* which,
                       std::map<long, std::string>& table)
    {
        std::string error;
        if (!require(name_table(body, model, table, error), (std::string(which) + " is read, case by case").c_str()))
        {
            printf("        %s\n", error.c_str());
            return;
        }
        int wrong = 0;
        for (int p = 0; p <= 1000; p++)
        {
            const auto it = table.find(p);
            const std::string isp = it != table.end() ? it->second : "PID" + std::to_string(p);
            const std::string ours = t5000(p);
            if (isp != ours)
            {
                if (wrong++ < 5)
                    printf("        product %d: ISP \"%s\", T5000 \"%s\"\n", p, isp.c_str(), ours.c_str());
            }
        }
        check_eq(wrong, 0, (std::string("  T5000 names every product 0 to 1000 as ") + which + " does").c_str());
    }

    // ---- chains ----

    struct Branch
    {
        std::vector<std::string> devices;   // raised
        std::vector<std::string> files;
    };

    // The else-if branches between `first` and `last`, each "any of these
    // device names and any of these file names".
    bool alias_chain(const std::string& body, const std::string& first, const std::string& last, std::vector<Branch>& out,
                     std::string& error)
    {
        out.clear();
        const size_t from = body.find(first);
        const size_t to   = body.find(last);
        if (from == std::string::npos || to == std::string::npos || to < from || occurrences(body, first) != 1 ||
            occurrences(body, last) != 1)
        {
            error = "the chain's first or last branch is not found, once";
            return false;
        }
        const std::string chain = body.substr(from + first.size(), to - from - first.size());
        static const std::regex name(R"re((prodcutname|hexproductname)\.CompareNoCase\(_T\("([^"]*)"\)\))re");

        std::vector<std::string> pieces;
        size_t start = 0;
        for (size_t at = chain.find("else if"); at != std::string::npos; at = chain.find("else if", start))
        {
            pieces.push_back(chain.substr(start, at - start));
            start = at + 7;
        }
        pieces.push_back(chain.substr(start));

        for (size_t i = 0; i < pieces.size(); i++)
        {
            const std::string& p = pieces[i];
            if (p.find("TRUE") == std::string::npos || p.find("FALSE") != std::string::npos)
            {
                error = "branch " + std::to_string(i) + " does not take the file";
                return false;
            }
            if (i == 0)
                continue;   // the same name
            Branch b;
            int named = 0;
            for (auto it = std::sregex_iterator(p.begin(), p.end(), name); it != std::sregex_iterator(); ++it)
            {
                ((*it)[1].str() == "prodcutname" ? b.devices : b.files).push_back(upper((*it)[2].str()));
                named++;
            }
            if (occurrences(p, "&&") != 1 || b.devices.empty() || b.files.empty() || named != occurrences(p, "CompareNoCase("))
            {
                error = "branch " + std::to_string(i) + " is not \"any of these devices and any of these files\": " + p;
                return false;
            }
            out.push_back(b);
        }
        return true;
    }

    bool chain_takes(const std::vector<Branch>& chain, const std::string& d, const std::string& f)
    {
        if (d == f)
            return true;
        for (const Branch& b : chain)
        {
            bool dev = false, file = false;
            for (const std::string& x : b.devices)
                dev = dev || x == d;
            for (const std::string& x : b.files)
                file = file || x == f;
            if (dev && file)
                return true;
        }
        return false;
    }

    // MySocket::OnReceive's pairs, and its names it takes for any device.
    struct NetworkChecks
    {
        std::vector<std::pair<std::string, std::string>> pairs;
        std::vector<std::string> any_device;
    };

    bool network_checks(const std::string& body, std::vector<NetworkChecks>& out, std::string& error)
    {
        static const std::regex pair(
            R"re(\(\(DeviceProductName\.CompareNoCase\(_T\("([^"]*)"\)\) == 0\) && \(FileProductName\.CompareNoCase\(_T\("([^"]*)"\)\) == 0 ?\)\))re");
        static const std::regex any(
            R"re(if ?\(\(FileProductName\.CompareNoCase\(_T\("([^"]*)"\)\) == 0\) \|\| \(FileProductName\.CompareNoCase\(_T\("([^"]*)"\)\) == 0\) \|\| \(FileProductName\.CompareNoCase\(_T\("([^"]*)"\)\) == 0\)\))re");
        out.clear();
        const std::string start = "CString DeviceProductName ,FileProductName;";
        for (size_t at = body.find(start); at != std::string::npos; at = body.find(start, at + 1))
        {
            const size_t end = body.find("\"Your device is %s,", at);
            if (end == std::string::npos)
            {
                error = "a check has no refusal after it";
                return false;
            }
            const std::string region = body.substr(at, end - at);
            NetworkChecks c;
            for (auto it = std::sregex_iterator(region.begin(), region.end(), pair); it != std::sregex_iterator(); ++it)
                c.pairs.emplace_back(upper((*it)[1].str()), upper((*it)[2].str()));
            std::smatch m;
            if (!std::regex_search(region, m, any))
            {
                error = "a check has no names it takes for any device";
                return false;
            }
            for (int i = 1; i <= 3; i++)
                c.any_device.push_back(upper(m[i].str()));
            if (occurrences(region, "CompareNoCase(_T(") != (int)(2 * c.pairs.size() + 3))
            {
                error = "a check compares a name T5000 does not read";
                return false;
            }
            out.push_back(c);
        }
        if (out.size() != 2)
        {
            error = "OnReceive has " + std::to_string(out.size()) + " name checks, not 2";
            return false;
        }
        return true;
    }

    void test_names_and_aliases(const Isp& isp)
    {
        section("firmware: ISP's names and aliases");

        std::string fw_body, pn_body, upd, ex, sock;
        if (!body_of(isp.global_function, "CString GetFirmwareUpdateName(int ModelID)", fw_body) ||
            !body_of(isp.global_function, "CString GetProductName(int ModelID)", pn_body) ||
            !body_of(isp.com_writer, "int CComWriter::UpdataDeviceInformation(int& ID)", upd) ||
            !body_of(isp.com_writer, "BOOL CComWriter::UpdataDeviceInformation_ex(unsigned short device_productID)", ex) ||
            !body_of(isp.my_socket, "void MySocket::OnReceive(int nErrorCode)", sock))
            return;

        std::map<long, std::string> fw_table, pn_table;
        compare_table(fw_body, isp.model, &firmware_name, "GetFirmwareUpdateName", fw_table);
        compare_table(pn_body, isp.model, &product_name, "GetProductName", pn_table);

        // How each route reads the names.
        pin(upd, "CString prodcutname= GetFirmwareUpdateName(Device_infor[7]);",
            "the extended route names the device by GetFirmwareUpdateName");
        pin(upd, "MultiByteToWideChar(CP_ACP, 0, (char *)global_fileInfor.product_name, "
                 "(int)strlen(global_fileInfor.product_name) + 1, hexproductname.GetBuffer(MAX_PATH), MAX_PATH); "
                 "hexproductname.ReleaseBuffer(); hexproductname.Left(10);",
            "  the file by its name read with strlen, Left(10)'s result thrown away");
        pin(upd, "hexproductname.MakeLower(); if (hexproductname.CompareNoCase(L\"mini_arm\") == 0) { hexproductname = "
                 "L\"Minipanel\"; } prodcutname.MakeUpper(); hexproductname.MakeUpper(); hexproductname.TrimLeft(); "
                 "hexproductname.TrimRight();",
            "  lowered, mini_arm made Minipanel, raised, then trimmed");
        pin(ex, "CString prodcutname=GetProductName(device_productID);", "the data route names the device by GetProductName");
        pin(ex, "for (int i=0; i<10; i++) { hexproductname.AppendFormat(_T(\"%c\"),global_fileInfor.product_name[i]); }",
            "  the file by its first 10 bytes");
        absent(ex, "Trim", "  not trimmed");
        absent(ex, "mini_arm", "  mini_arm not made Minipanel");
        std::vector<Branch> upd_chain, ex_chain;
        std::string error;
        const std::string first = "if(hexproductname.CompareNoCase(prodcutname)==0)";
        const std::string last  = "else { strtips.Format(_T(\"Your device is %s but the hex file is for %s \")";
        if (!require(alias_chain(upd, first, last, upd_chain, error), "UpdataDeviceInformation's aliases are read") ||
            !require(alias_chain(ex, first, last, ex_chain, error), "UpdataDeviceInformation_ex's aliases are read"))
        {
            printf("        %s\n", error.c_str());
            return;
        }
        std::vector<NetworkChecks> net;
        if (!require(network_checks(sock, net, error), "OnReceive's two name checks are read"))
        {
            printf("        %s\n", error.c_str());
            return;
        }
        check(net[0].pairs == net[1].pairs && net[0].any_device == net[1].any_device, "  and are the same");

        // Every name either side knows, and some neither does.
        std::set<std::string> names = { "MINI", "MINIPANEL", "MINI_ARM", "XYZ", "" };
        for (const auto& t : { fw_table, pn_table })
            for (const auto& row : t)
                names.insert(upper(row.second));
        for (const auto& chain : { upd_chain, ex_chain })
            for (const Branch& b : chain)
            {
                names.insert(b.devices.begin(), b.devices.end());
                names.insert(b.files.begin(), b.files.end());
            }

        int upd_wrong = 0, ex_wrong = 0;
        for (const std::string& d : names)
        {
            for (const std::string& f : names)
            {
                if (extended_names_match(d, f) != chain_takes(upd_chain, d, f) && upd_wrong++ < 3)
                    printf("        extended route, device %s, file %s\n", d.c_str(), f.c_str());
                if (data_names_match(d, f) != chain_takes(ex_chain, d, f) && ex_wrong++ < 3)
                    printf("        data route, device %s, file %s\n", d.c_str(), f.c_str());
            }
        }
        printf("        %zu names, every pair\n", names.size());
        check_eq(upd_wrong, 0, "  the extended route takes what UpdataDeviceInformation takes");
        check_eq(ex_wrong, 0, "  the data route takes what UpdataDeviceInformation_ex takes");

        // Stricter: what ISP takes for any device on the network. T5000
        // checks by product, with the extended route's names.
        check(net[0].any_device == std::vector<std::string>({ "HUMNET", "CO2NET", "PSNET" }),
              "ISP takes HUMNET, CO2NET and PSNET files for any device on the network");
        std::string start;
        if (body_of(isp.tftp, "BOOL TFTPServer::StartServer()", start))
            pin(start, "else if((m_StrProductName.CompareNoCase(_T(\"HUMNET\")) == 0) || "
                       "(m_StrProductName.CompareNoCase(_T(\"CO2NET\")) == 0) || "
                       "(m_StrProductName.CompareNoCase(_T(\"CO2\")) == 0) || "
                       "(m_StrProductName.CompareNoCase(_T(\"PSNET\")) == 0)) { Sleep(1); }",
                "  and any file for a device naming itself HUMNET, CO2NET, CO2 or PSNET, on a broadcast");
        const auto network = [](const char* name, int product) {
            FirmwareFile f;
            f.path   = Path::Network;
            f.format = HexFormat::LinearAddress;
            memcpy(f.header.bytes, "TEMCO", 5);
            memcpy(&f.header.bytes[header_at::product_name], name, strlen(name));
            DeviceFacts d;
            d.product = product;
            return check_firmware(f, d).ok;
        };
        check(!network("HUMNET", 9) && !network("CO2NET", 9) && !network("PSNET", 9),
              "  T5000 refuses such a file for a TStat8");
        check(!network("TSTAT7", 212) && !network("TSTAT7", 210) && !network("TSTAT7", 33) && !network("TSTAT7", 214),
              "  and a TSTAT7 file for a HUMNET, CO2NET, CO2 or PSNET");
    }

    // ---- rules ----

    struct Flag
    {
        std::string name;
        bool        divided = false;   // by 100
        bool        has_threshold = false;
        std::string op, constant;
    };

    bool brace_block(const std::string& s, size_t open, std::string& block)
    {
        int depth = 0;
        for (size_t i = open; i < s.size(); i++)
        {
            if (s[i] == '{')
                depth++;
            else if (s[i] == '}' && --depth == 0)
            {
                block = s.substr(open, i - open + 1);
                return true;
            }
        }
        return false;
    }

    bool file_flags(const std::string& body, bool network, std::vector<Flag>& out, std::string& error)
    {
        out.clear();
        static const std::regex branch(
            R"re(temp_hex_pid_name\.CompareNoCase\(_T\("([A-Za-z0-9_]+)"\)\) == 0\)( && \(new_bootload == 0\)\))? \{)re");
        static const std::regex formula(
            R"re(hex_version = \(\(float\)\(global_fileInfor\.software_high \* 256 \+ global_fileInfor\.software_low\)\)( ?/ ?100)?;)re");
        static const std::regex threshold(
            R"re(if \(hex_version (>=|>) ([0-9]+(?:\.[0-9]+)?)\) \{ firmware_must_use_new_bootloader = 1;)re");

        if (occurrences(body, "temp_hex_pid_name.ReleaseBuffer(); temp_hex_pid_name.Trim(); temp_hex_pid_name.MakeUpper();") != 1)
        {
            error = "the name is not trimmed and raised, once";
            return false;
        }
        bool divided_before = false;
        const size_t first_branch = body.find("temp_hex_pid_name.CompareNoCase(");
        if (network)
        {
            std::smatch m;
            const std::string before = body.substr(0, first_branch);
            if (!std::regex_search(before, m, formula) || !m[1].matched)
            {
                error = "the version is not divided by 100 before the branches";
                return false;
            }
            divided_before = true;
        }
        int thresholds = 0;
        for (auto it = std::sregex_iterator(body.begin(), body.end(), branch); it != std::sregex_iterator(); ++it)
        {
            Flag f;
            f.name = (*it)[1].str();
            std::string block;
            if (!brace_block(body, it->position() + it->length() - 1, block))
            {
                error = f.name + "'s branch does not close";
                return false;
            }
            std::smatch m;
            if (std::regex_search(block, m, formula))
                f.divided = m[1].matched;
            else if (network)
                f.divided = divided_before;
            else
            {
                error = f.name + "'s branch does not compute the version";
                return false;
            }
            if (std::regex_search(block, m, threshold))
            {
                f.has_threshold = true;
                f.op            = m[1].str();
                f.constant      = m[2].str();
                thresholds++;
            }
            out.push_back(f);
        }
        if (out.size() != (size_t)occurrences(body, "temp_hex_pid_name.CompareNoCase(") ||
            thresholds != occurrences(body, "firmware_must_use_new_bootloader = 1"))
        {
            error = "a branch or a threshold is not read";
            return false;
        }
        return true;
    }

    bool isp_marks(const Flag& f, int v)
    {
        if (!f.has_threshold)
            return false;
        float hv = (float)v;
        if (f.divided)
            hv = hv / 100;
        if (f.constant.find('.') != std::string::npos)
        {
            const double c = std::stod(f.constant);
            return f.op == ">=" ? hv >= c : hv > c;
        }
        const int c = std::stoi(f.constant);
        return f.op == ">=" ? hv >= c : hv > c;   // c made a float, as in ISP
    }

    Header header_named(const std::string& name, int version)
    {
        Header h;
        memcpy(&h.bytes[header_at::company], "TEMCO", 5);
        memcpy(&h.bytes[header_at::product_name], name.data(), std::min(name.size(), (size_t)10));
        h.bytes[header_at::software_low]  = (uint8_t)(version & 0xFF);
        h.bytes[header_at::software_high] = (uint8_t)(version >> 8);
        return h;
    }

    void compare_flags(const std::string& body, Path path, const char* which)
    {
        std::vector<Flag> flags;
        std::string error;
        if (!require(file_flags(body, path == Path::Network, flags, error), (std::string(which) + "'s file flags are read").c_str()))
        {
            printf("        %s\n", error.c_str());
            return;
        }
        std::string read;
        for (const Flag& f : flags)
            read += " " + f.name + (f.has_threshold ? " " + f.op + " " + f.constant + (f.divided ? " (/100)" : "") : " (none)");
        printf("       %s\n", read.c_str());

        int wrong = 0;
        for (const Flag& f : flags)
        {
            for (int v = 0; v <= 0xFFFF; v++)
            {
                if (file_needs_new_bootloader(path, header_named(f.name, v)) != isp_marks(f, v) && wrong++ < 3)
                    printf("        %s version %d\n", f.name.c_str(), v);
            }
        }
        check_eq(wrong, 0, (std::string("  T5000 marks each of them at every version as ") + which + " does").c_str());

        int others = 0;
        for (const char* name : { "TSTAT7", "MINIPANEL", "PID59", "CO2NET", "TSTAT10", "MINI" })
        {
            bool branch = false;
            for (const Flag& f : flags)
                branch = branch || f.name == name;
            if (branch)
                continue;
            for (const int v : { 0, 58, 59, 100, 101, 5109, 6000, 0xFFFF })
                if (file_needs_new_bootloader(path, header_named(name, v)) && others++ < 3)
                    printf("        %s version %d is marked\n", name, v);
        }
        check_eq(others, 0, "  and no other name");
    }

    struct BootRule
    {
        char kind = 0;   // A: <= and not 0; B: < with the serial refusal; C: <; D: a range, <
        long product = 0, from = 0, to = 0, also = 0;
        long below = 0;
    };

    struct BootRules
    {
        long early_from = 0, early_to = 0, early_also = 0, early_firmware = 0;
        std::set<long> checked;
        std::vector<BootRule> rules;
    };

    bool boot_rules(const std::string& body, const std::string& model, BootRules& r, std::string& error)
    {
        const auto id = [&](const std::string& name, long& v) { return parse_constant(model, name, v, error); };

        static const std::regex early(
            R"re(if \(\(\(\(npid >= (\w+)\) && \(npid <= (\w+)\)\) \|\| \(npid == (\w+)\)\) && app_already_version >= (\d+)\) \{ c2_update_boot = false; return Ret_Result; \})re");
        std::smatch m;
        if (!std::regex_search(body, m, early) || !id(m[1].str(), r.early_from) || !id(m[2].str(), r.early_to) ||
            !id(m[3].str(), r.early_also))
        {
            if (error.empty())
                error = "the early return for a sensor's firmware is not read";
            return false;
        }
        r.early_firmware = std::stol(m[4].str());

        const std::string open  = "if (com_port_flash_status == 0) { if (";
        const std::string close = ") { if (firmware_must_use_new_bootloader == 0) c2_update_boot = false; else {";
        const size_t a = body.find(open), b = body.find(close);
        if (a == std::string::npos || b == std::string::npos || b < a)
        {
            error = "the products checked are not found";
            return false;
        }
        const std::string products = body.substr(a + open.size(), b - a - open.size());
        static const std::regex product(R"re(\(Device_infor\[7\] == (\w+)\))re");
        int named = 0;
        for (auto it = std::sregex_iterator(products.begin(), products.end(), product); it != std::sregex_iterator(); ++it)
        {
            long v = 0;
            if (!id((*it)[1].str(), v))
                return false;
            r.checked.insert(v);
            named++;
        }
        if (named != occurrences(products, "Device_infor[7]") || named != occurrences(products, "||") + 1)
        {
            error = "the products checked are not all read";
            return false;
        }

        const std::string rules_open  = "temp_bootloader_version = isp_max(Device_infor[11], Device_infor[14]); if ";
        const std::string rules_close = " else c2_update_boot = false; } update_value = c2_update_boot;";
        const size_t c = body.find(rules_open), d = body.find(rules_close);
        if (c == std::string::npos || d == std::string::npos || d < c || occurrences(body, rules_open) != 1)
        {
            error = "the rules are not found";
            return false;
        }
        const std::string chain = body.substr(c + rules_open.size(), d - c - rules_open.size());
        std::vector<std::string> pieces;
        size_t start = 0;
        for (size_t at = chain.find(" else if "); at != std::string::npos; at = chain.find(" else if ", start))
        {
            pieces.push_back(chain.substr(start, at - start));
            start = at + 9;
        }
        pieces.push_back(chain.substr(start));

        static const std::regex A(
            R"re(\(\(Device_infor\[7\] == (\w+)\) && \(\(temp_bootloader_version <= (\d+)\) && \(temp_bootloader_version != 0\)\)\) \{ c2_update_boot = true; \})re");
        static const std::regex B(
            R"re(\(\(Device_infor\[7\] == (\w+)\) && \(temp_bootloader_version < (\d+)\)\) \{ if \(comport == 0\) \{ c2_update_boot = false; Ret_Result = -1; \} else c2_update_boot = true; \})re");
        static const std::regex C(
            R"re(\(\(Device_infor\[7\] == (\w+)\) && \(temp_bootloader_version < (\d+)\)\) \{ c2_update_boot = true; \})re");
        static const std::regex D(
            R"re(\(\(\(\(Device_infor\[7\] >= (\w+)\) && \(Device_infor\[7\] <= (\w+)\)\) \|\| \(Device_infor\[7\] == (\w+)\)\) && \(temp_bootloader_version < (\d+)\)\) \{ c2_update_boot = true; \})re");
        for (const std::string& p : pieces)
        {
            BootRule rule;
            std::smatch pm;
            if (std::regex_match(p, pm, A) || std::regex_match(p, pm, B) || std::regex_match(p, pm, C))
            {
                rule.kind = std::regex_match(p, A) ? 'A' : std::regex_match(p, B) ? 'B' : 'C';
                if (!id(pm[1].str(), rule.product))
                    return false;
                rule.below = std::stol(pm[2].str());
            }
            else if (std::regex_match(p, pm, D))
            {
                rule.kind = 'D';
                if (!id(pm[1].str(), rule.from) || !id(pm[2].str(), rule.to) || !id(pm[3].str(), rule.also))
                    return false;
                rule.below = std::stol(pm[4].str());
            }
            else
            {
                error = "a rule T5000 does not know: " + p;
                return false;
            }
            r.rules.push_back(rule);
        }
        return true;
    }

    BootloaderCall isp_call(const BootRules& r, int product, Path path, int bootloader, int firmware_low)
    {
        if (((product >= r.early_from && product <= r.early_to) || product == r.early_also) && firmware_low >= r.early_firmware)
            return BootloaderCall::Fine;
        if (!r.checked.count(product))
            return BootloaderCall::Fine;
        for (const BootRule& rule : r.rules)
        {
            switch (rule.kind)
            {
            case 'A':
                if (product == rule.product && bootloader <= rule.below && bootloader != 0)
                    return BootloaderCall::Update;
                break;
            case 'B':
                if (product == rule.product && bootloader < rule.below)
                    return path == Path::Network ? BootloaderCall::Update : BootloaderCall::RefusedSerial;
                break;
            case 'C':
                if (product == rule.product && bootloader < rule.below)
                    return BootloaderCall::Update;
                break;
            case 'D':
                if (((product >= rule.from && product <= rule.to) || product == rule.also) && bootloader < rule.below)
                    return BootloaderCall::Update;
                break;
            }
        }
        return BootloaderCall::Fine;
    }

    void test_bootloader_rules(const Isp& isp)
    {
        section("firmware: ISP's bootloader rules");

        std::string eth, com, boot;
        if (!body_of(isp.isp_dlg, "void CISPDlg::FlashByEthernet()", eth) ||
            !body_of(isp.isp_dlg, "void CISPDlg::FlashByCom()", com) ||
            !body_of(isp.global_function,
                     "int check_bootloader_and_frimware(int npid ,int comport , unsigned short reg_11 , unsigned short "
                     "reg_14, int &update_value, unsigned char app_already_version)",
                     boot))
            return;

        compare_flags(eth, Path::Network, "FlashByEthernet");
        compare_flags(com, Path::Serial, "FlashByCom");

        BootRules rules;
        std::string error;
        if (!require(boot_rules(boot, isp.model, rules, error), "check_bootloader_and_frimware's rules are read"))
        {
            printf("        %s\n", error.c_str());
            return;
        }
        printf("        %zu products checked, %zu rules\n", rules.checked.size(), rules.rules.size());
        pin(boot, "if (com_port_flash_status == 0) {", "  decided only in the normal flash, which is T5000's");

        int checked_wrong = 0;
        for (int p = 0; p <= 1000; p++)
            if (bootloader_is_checked(p) != (rules.checked.count(p) != 0))
                checked_wrong++;
        check_eq(checked_wrong, 0, "  T5000 checks the products ISP checks, 0 to 1000");

        int wrong = 0;
        std::vector<int> versions;
        for (int v = 0; v <= 300; v++)
            versions.push_back(v);
        versions.push_back(1000);
        versions.push_back(0xFFFF);
        for (int p = 0; p <= 300; p++)
            for (const int v : versions)
                for (const int low : { 0, (int)rules.early_firmware - 1, (int)rules.early_firmware, 255 })
                    for (const Path path : { Path::Serial, Path::Network, Path::Controller })
                        if (bootloader_call(p, path, v, low) != isp_call(rules, p, path, v, low) && wrong++ < 3)
                            printf("        product %d, version %d, firmware %d, %s\n", p, v, low, to_string(path));
        check_eq(wrong, 0, "  T5000 makes the call ISP makes for every product 0 to 300 and version");

        // Who calls it, and with which comport.
        check_eq(occurrences(squeeze(strip_comments(isp.com_writer)),
                             "check_bootloader_and_frimware(Device_infor[7], 0, Device_infor[11], Device_infor[14], "
                             "c2_update_boot, Device_infor[4]);"),
                 1, "ComWriter calls it once, with comport 0, on serial and through a controller");
        check_eq(occurrences(squeeze(isp.tftp), "check_bootloader_and_frimware(read_reg[7], 1, read_reg[11], read_reg[14], "
                                                "c2_update_boot, read_reg[4]);"),
                 2, "TFTPServer calls it twice, with comport 1, on the network");
    }

    // ---- routes ----

    void test_serial_routes(const Isp& isp)
    {
        section("firmware: which serial thread checks which file");

        std::string begin, ext, ext_ram, modbus, thread, thread_ram, com, upd;
        if (!body_of(isp.com_writer, "int CComWriter::BeginWirteByCom()", begin) ||
            !body_of(isp.com_writer, "int CComWriter::WirteExtendHexFileByCom()", ext) ||
            !body_of(isp.com_writer, "int CComWriter::WirteExtendHexFileByCom_RAM()", ext_ram) ||
            !body_of(isp.com_writer, "UINT Flash_Modebus_Device(LPVOID pParam)", modbus) ||
            !body_of(isp.com_writer, "UINT flashThread_ForExtendFormatHexfile(LPVOID pParam)", thread) ||
            !body_of(isp.com_writer, "UINT flashThread_ForExtendFormatHexfile_RAM(LPVOID pParam)", thread_ram) ||
            !body_of(isp.isp_dlg, "void CISPDlg::FlashByCom()", com) ||
            !body_of(isp.com_writer, "int CComWriter::UpdataDeviceInformation(int& ID)", upd))
            return;

        pin(com, "if(com_bin) m_pComWriter->SetHexFileType(HEXFILE_LINERADDR); else "
                 "m_pComWriter->SetHexFileType(pHexFile->GetHexFileFormatType()); m_pComWriter->Is_Ram = pHexFile->GetFileType();",
            "FlashByCom gives a .bin the linear type, and Is_Ram the .hex's chip");

        const size_t data = begin.find("if (m_nHexFileType == 0) {");
        const size_t data_thread = begin.find("m_pWorkThread=AfxBeginThread(Flash_Modebus_Device, this);");
        const size_t linear = begin.find("} else if(m_nHexFileType == 2) {");
        const size_t linear_writers = begin.find("if (Is_Ram) { WirteExtendHexFileByCom_RAM(); } else { WirteExtendHexFileByCom(); } } return 1;");
        const bool all_found = data != std::string::npos && data_thread != std::string::npos &&
                               linear != std::string::npos && linear_writers != std::string::npos;
        check(all_found && data < data_thread && data_thread < linear && linear < linear_writers,
              "BeginWirteByCom sends a data .hex to Flash_Modebus_Device, and the linear type to the extended writers");
        check_eq(occurrences(begin, "m_nHexFileType"), 2, "  and has no branch for a segment .hex, which it does not flash");
        pin(ext, "m_pWorkThread=AfxBeginThread(flashThread_ForExtendFormatHexfile, this);",
            "WirteExtendHexFileByCom starts flashThread_ForExtendFormatHexfile");
        pin(ext_ram, "m_pWorkThread=AfxBeginThread(flashThread_ForExtendFormatHexfile_RAM, this);",
            "WirteExtendHexFileByCom_RAM starts flashThread_ForExtendFormatHexfile_RAM");

        pin(modbus, "UpdataDeviceInformation", "Flash_Modebus_Device checks the file once");
        pin(modbus, "UpdataDeviceInformation_ex(temp_read_reg[7])", "  with UpdataDeviceInformation_ex");
        pin(thread, "UpdataDeviceInformation", "flashThread_ForExtendFormatHexfile checks it once");
        pin(thread, "UpdataDeviceInformation(pWriter->m_szMdbIDs[i])", "  with UpdataDeviceInformation");
        pin(thread_ram, "UpdataDeviceInformation", "flashThread_ForExtendFormatHexfile_RAM checks it once");
        pin(thread_ram, "UpdataDeviceInformation(pWriter->m_szMdbIDs[i])", "  with UpdataDeviceInformation");
        pin(upd, "Ret_Result = check_bootloader_and_frimware(", "UpdataDeviceInformation checks the bootloader");
        std::string ex;
        if (body_of(isp.com_writer, "BOOL CComWriter::UpdataDeviceInformation_ex(unsigned short device_productID)", ex))
            absent(ex, "bootloader", "UpdataDeviceInformation_ex does not, which T5000 is stricter than");

        // The chip size checks: for a TStat6, TStat7 or TStat5i, register 11
        // below 37 is the 64K chip, which takes only a data .hex.
        pin(modbus, "if (ModelID==6||ModelID==7||ModelID==8) { Chipsize_6 = temp_read_reg[11]; if (Chipsize_6<37) { if "
                    "(pWriter->m_nHexFileType==0) { strTips = _T(\"|hex file matches the device CPU!\");",
            "Flash_Modebus_Device takes a data .hex for a TStat6, 7 or 5i only on its 64K chip");
        pin(thread, "if (ModelID==6||ModelID==7||ModelID==8) { int Chipsize= mudbus_read_one(pWriter->m_szMdbIDs[i],11,5); "
                    "if(Chipsize < 0) { nFlashRet = false; goto end_isp_flash; } if (Chipsize<37) { if "
                    "(pWriter->m_nHexFileType==0) { strTips = _T(\"|hex file matches with the chip!\");",
            "flashThread_ForExtendFormatHexfile takes the rest only on its 128K chip");
        absent(thread_ram, "Chipsize", "flashThread_ForExtendFormatHexfile_RAM does not check the chip");

        // Where each reads register 11: Flash_Modebus_Device from the
        // running device, before the jump to the bootloader; the other in the
        // bootloader.
        const size_t read_first = modbus.find("temp_mu_ret = read_multi_tap(pWriter->m_szMdbIDs[i],temp_read_reg,0,40);");
        const size_t jump       = modbus.find("int nRet = Write_One(pWriter->m_szMdbIDs[i],16,127);");
        const size_t chip       = modbus.find("Chipsize_6 = temp_read_reg[11];");
        check(read_first != std::string::npos && jump != std::string::npos && chip != std::string::npos &&
                  read_first < jump && jump < chip && occurrences(modbus, "temp_read_reg,0,40)") == 1,
              "Flash_Modebus_Device reads register 11 before the jump, and uses that");
        const size_t thread_jump = thread.find("int nRet = mudbus_write_one(pWriter->m_szMdbIDs[i],16,127);");
        const size_t thread_chip = thread.find("int Chipsize= mudbus_read_one(pWriter->m_szMdbIDs[i],11,5);");
        check(thread_jump != std::string::npos && thread_chip != std::string::npos && thread_jump < thread_chip,
              "flashThread_ForExtendFormatHexfile reads it after the jump, in the bootloader");

        // What each makes of the check's answer. -1 (an old MiniPanel
        // bootloader on serial) and 2 (a bootloader to update) stop the _RAM
        // thread, which goes on only on 1, but not the other.
        pin(thread, "if (pWriter->UpdataDeviceInformation(pWriter->m_szMdbIDs[i])) {",
            "flashThread_ForExtendFormatHexfile goes on whatever the check returns but 0");
        pin(thread_ram, "int nret_device_info = pWriter->UpdataDeviceInformation(pWriter->m_szMdbIDs[i]); int temp_ret3 = "
                        "pWriter->Fix_Tstat10_76800_baudrate(); if (nret_device_info == 1) {",
            "flashThread_ForExtendFormatHexfile_RAM only when it returns 1");
    }

    // ---- reader ----

    std::string record(int type, unsigned address, const std::vector<uint8_t>& data)
    {
        std::vector<uint8_t> b = { (uint8_t)data.size(), (uint8_t)(address >> 8), (uint8_t)address, (uint8_t)type };
        b.insert(b.end(), data.begin(), data.end());
        uint8_t sum = 0;
        for (const uint8_t x : b)
            sum = (uint8_t)(sum + x);
        b.push_back((uint8_t)(0x100 - sum));
        std::string line = ":";
        char two[3];
        for (const uint8_t x : b)
        {
            snprintf(two, sizeof two, "%02X", x);
            line += two;
        }
        return line + "\r\n";
    }

    bool read_text(const std::string& name, const std::string& text, Path path, FirmwareFile& f)
    {
        std::string why;
        return read_firmware(name, (const uint8_t*)text.data(), text.size(), path, f, why);
    }

    void test_reader(const Isp& isp)
    {
        section("firmware: ISP's buffers, fills and header");

        const std::string gs = squeeze(strip_comments(isp.global_struct));
        static const std::regex length(R"re(const DWORD (c_nHexFileBufLen|c_nBinFileBufLen) = (0x[0-9A-Fa-f]+);)re");
        std::map<std::string, unsigned long> lengths;
        for (auto it = std::sregex_iterator(gs.begin(), gs.end(), length); it != std::sregex_iterator(); ++it)
            lengths[(*it)[1].str()] = std::stoul((*it)[2].str(), nullptr, 16);
        check_eq((long)lengths["c_nHexFileBufLen"], (long)kHexBufferLength, "a .hex buffer is c_nHexFileBufLen");
        check_eq((long)lengths["c_nBinFileBufLen"], (long)kBinBufferLength, "a .bin buffer is c_nBinFileBufLen");

        pin(gs, "typedef struct { char company[5]; char product_name[10]; unsigned char software_low; unsigned char "
                "software_high; char reserved[3]; }Bin_Info;",
            "Bin_Info is 5 bytes of company, 10 of name, the version's two, 3 reserved");
        check(header_at::company == 0 && header_at::product_name == 5 && header_at::software_low == 15 &&
                  header_at::software_high == 16 && header_at::reserved == 17 && header_at::size == 20,
              "  as header_at has it");

        // The buffer and fill each path reads each kind of file into, and
        // T5000's image for a small file of each.
        std::string com, eth, sub;
        if (!body_of(isp.isp_dlg, "void CISPDlg::FlashByCom()", com) ||
            !body_of(isp.isp_dlg, "void CISPDlg::FlashByEthernet()", eth) ||
            !body_of(isp.isp_dlg, "void CISPDlg::OnFlashSubID()", sub))
            return;
        struct Fill
        {
            const std::string* body;
            const char*        text;   // ISP's allocation and fill
            Path               path;
            bool               bin;
            size_t             length;
            uint8_t            fill;
            const char*        what;
        };
        const Fill fills[] = {
            { &com, "m_pFileBuffer = new char[c_nBinFileBufLen]; memset(m_pFileBuffer, 0xFF, c_nBinFileBufLen); nDataSize = "
                    "pBinFile->GetBinFileBuffer(",
              Path::Serial, true, kBinBufferLength, 0xFF, "serial reads a .bin into its buffer filled with 0xFF" },
            { &com, "m_pFileBuffer = new char[c_nHexFileBufLen]; memset(m_pFileBuffer, 0x00, c_nHexFileBufLen); nDataSize = "
                    "pHexFile->GetHexFileBuffer(",
              Path::Serial, false, kHexBufferLength, 0x00, "serial reads a .hex into its buffer filled with 0x00" },
            { &eth, "m_pFileBuffer=new char[c_nBinFileBufLen]; memset(m_pFileBuffer, 0xFF, c_nBinFileBufLen); "
                    "nDataSize=pBinFile->GetBinFileBuffer(",
              Path::Network, true, kBinBufferLength, 0xFF, "the network reads a .bin into the .bin buffer filled with 0xFF" },
            { &eth, "m_pFileBuffer = new char[c_nBinFileBufLen]; memset(m_pFileBuffer, 0x00, c_nBinFileBufLen); nDataSize = "
                    "pHexFile->GetHexFileBuffer(",
              Path::Network, false, kBinBufferLength, 0x00, "the network reads a .hex into the .bin buffer filled with 0x00" },
            { &sub, "m_pFileBuf = new char[c_nHexFileBufLen]; memset(m_pFileBuf, 0xFF, c_nHexFileBufLen); int nDataSize = "
                    "pHexFile->GetHexFileBuffer(",
              Path::Controller, false, kHexBufferLength, 0xFF, "through a controller, a .hex into its buffer filled with 0xFF" },
        };
        const std::string hex = record(0, 0x10, { 1 }) + ":00000001FF\r\n";
        std::string bin = "ASIX";
        bin.resize(0x300, 'Z');
        for (const Fill& f : fills)
        {
            pin(*f.body, f.text, f.what);
            FirmwareFile file;
            const bool read = read_text(f.bin ? "a.bin" : "a.hex", f.bin ? bin : hex, f.path, file);
            check(read && file.image.size() == f.length && file.image.back() == f.fill, "  as T5000 reads it");
        }
        pin(com, "temp_name = m_strHexFileName.Right(4); temp_name.MakeUpper(); if (temp_name.CompareNoCase(_T(\".BIN\")) == 0)",
            "serial reads a name ending .BIN as a .bin, and the rest as a .hex");
        pin(eth, "if(BinFileValidation(m_strHexFileName))", "the network reads a name ending bin as a .bin");
        pin(eth, "if(HexFileValidation (m_strHexFileName))", "  and one ending hex as a .hex");

        // Where the header is, by the chip the first line names.
        std::string hexbuf, chip, high, type, line, normal, extend, linear, bin_body;
        if (!body_of(isp.hex_parser, "int  CHexFileParser::GetHexFileBuffer(char* pBuf, int nLen)", hexbuf) ||
            !body_of(isp.hex_parser, "int CHexFileParser::GetFileTypeFromLine(const CString& strLine)", chip) ||
            !body_of(isp.hex_parser, "WORD CHexFileParser::GetHighAddrFromFile(const CString& strLine)", high) ||
            !body_of(isp.hex_parser, "HEXFILE_FORMAT\tCHexFileParser::GetHexFileType(CFile& hexFile)", type) ||
            !body_of(isp.hex_parser, "BOOL CHexFileParser::ReadLineFromFile(CFile& file, char* pBuffer)", line) ||
            !body_of(isp.hex_parser, "int CHexFileParser::ReadNormalHexFile( CFile& hexFile,  char* pBuf, int nBufLen)", normal) ||
            !body_of(isp.hex_parser, "int CHexFileParser::ReadExtendHexFile(CFile& hexFile, char* pBuf, int nBufLen)", extend) ||
            !body_of(isp.hex_parser, "BOOL CHexFileParser::ReadExtLinearHexFile(CFile& hexfile, char* pBuf, int nBufLen)", linear) ||
            !body_of(isp.bin_parser, "int CBinFileParser::GetBinFileBuffer(char* pFileBuf, int nFileBufLen)", bin_body))
            return;

        static const std::regex place(
            R"re(if \(m_file_type == HEX_TYPE_ARM_32K\) \{ memcpy\(&global_fileInfor, ?&pBuf\[(0x[0-9A-Fa-f]+)\],sizeof\(Bin_Info\)\); \} else if ?\(m_file_type == HEX_TYPE_ARM_64K\) \{ memcpy\(&global_fileInfor, ?&pBuf\[(0x[0-9A-Fa-f]+)\], ?sizeof\(Bin_Info\)\); \} else \{ memcpy\(&global_fileInfor, ?&pBuf\[(0x[0-9A-Fa-f]+)\], ?sizeof\(Bin_Info\)\); \})re");
        std::smatch m;
        if (require(std::regex_search(hexbuf, m, place), "GetHexFileBuffer takes the header from a place for each chip"))
        {
            const unsigned long at32 = std::stoul(m[1].str(), nullptr, 16), at64 = std::stoul(m[2].str(), nullptr, 16),
                                other = std::stoul(m[3].str(), nullptr, 16);
            printf("        32K ARM at 0x%lX, 64K ARM at 0x%lX, others at 0x%lX\n", at32, at64, other);
            const std::vector<uint8_t> name = { 'T', 'E', 'M', 'C', 'O', 'P', 'L', 'A', 'C', 'E' };
            struct Chipped
            {
                unsigned      first;   // the first line's linear address
                unsigned long at;
                const char*   what;
            };
            for (const Chipped& c : { Chipped{ 0x0800, at32, "  T5000 reads a 32K ARM file's header there" },
                                      Chipped{ 0x0801, at64, "  a 64K ARM file's" },
                                      Chipped{ 0x0000, other, "  and another linear file's" } })
            {
                // The first record names the chip and sets the high address
                // the header is written under.
                const unsigned long high = (unsigned long)(c.first >= 0x800 ? c.first - 0x800 : c.first) << 16;
                if (!require(c.at >= high && c.at - high <= 0xFFFF, "  the place can be written under the chip's first record"))
                    continue;
                const std::string text = record(4, 0, { (uint8_t)(c.first >> 8), (uint8_t)c.first }) +
                                         record(0, (unsigned)(c.at - high), name) + ":00000001FF\r\n";
                FirmwareFile f;
                check(read_text("p.hex", text, Path::Serial, f) && f.header_at == c.at && f.header.company() == "TEMCO", c.what);
            }
        }
        pin(hexbuf, "if(Temco_logo.CompareNoCase(_T(\"TEMCO\")) != 0&&Temco_logo.Find(L\"CO2\")==-1) { nBufLen = -1; }",
            "a .hex's company is TEMCO or holds CO2");
        pin(type, "hexFile.Read(chBuf, 12); if (chBuf[8] == '0') { } else if (chBuf[8] == '2') { nRet = 1; } else if "
                  "(chBuf[8] == '4') { nRet = 2; }",
            "the ninth character picks the reader");
        pin(chip, "dwTemp = szBuf[4] * 256 + szBuf[5]; if (dwTemp == 0x0800) { temp_type = HEX_TYPE_ARM_32K; } else if "
                  "(dwTemp >= 0x0801) { temp_type = HEX_TYPE_ARM_64K; }",
            "the first line's first two data bytes pick the chip");
        pin(high, "dwTemp = szBuf[4] * 256 + szBuf[5]; if (dwTemp>=0x0800) { dwTemp-=0x800; }",
            "a high address of 0x800 or more is less 0x800");
        pin(line, "if (linecharnum<256) { file.Read(&c, 1); } else { if(!auto_flash_mode) AfxMessageBox(_T(\"The Hex File is "
                  "broken\")); return FALSE; }",
            "a line's 256th character that is not its CR is broken");
        check_eq((long)kLongestHexLine, 255, "  so the longest line is 255");
        for (const std::string* reader : { &normal, &extend })
        {
            pin(*reader, "if(get_hex[3]==1) break;", "the address readers stop at the end-of-file record");
            pin(*reader, "if(get_hex[1]==0 && get_hex[2]==0) get_hex[4]=255;", "  make the byte at 0000 255");
            pin(*reader, "if((UINT)nBufCount<(ltemp+get_hex[0])) { nBufCount=ltemp+get_hex[0]-1; }",
                "  and count the highest address less one");
        }
        pin(linear, "if( a[8] == '4') { CString strTemp(a); dwHiAddr = GetHighAddrFromFile(strTemp); dwHiAddr <<= 16; if "
                    "(m_file_type == HEX_TYPE_ARM_64K) { if (nBufCount == 0) nBufCount = 0x10000; "
                    "m_szFlags.push_back(nBufCount); } else { if (nBufCount != 0) { m_szFlags.push_back(nBufCount); } } "
                    "continue; } else if ((a[8] == '5')) { continue; }",
            "the linear reader marks sections as T5000 does, and skips a start address");
        pin(linear, "if (ltemp > nBufLen) { return 0; }", "  refuses an address past its buffer");
        pin(linear, "if((UINT)nBufCount<(ltemp+get_hex[0])) nBufCount=ltemp+get_hex[0]; ZeroMemory(a, 256); } "
                    "m_szFlags.push_back(nBufCount);",
            "  counts the highest address, and marks the end");
        absent(linear, "get_hex[4]=255", "  and does not make the byte at 0000 255");

        pin(bin_body, "if (m_strASIX.CompareNoCase(_T(\"ASIX\") ) == 0)", "a .bin may start ASIX");
        pin(bin_body, "MultiByteToWideChar( CP_ACP, 0, (char *)(&pBuf[512]),20,temp_identify.GetBuffer(MAX_PATH), MAX_PATH ); "
                      "temp_identify.ReleaseBuffer(); if (temp_identify.Find(_T(\"Temco\")) != -1)",
            "  or say Temco in its bytes 512 to 531");
        pin(bin_body, "if(i==0) memcpy(&global_fileInfor,&pFileBuf[0x100],sizeof(Bin_Info)); else if(i == 1) "
                      "memcpy(&global_fileInfor,&pFileBuf[0x200],sizeof(Bin_Info));",
            "  its header is at 0x100, else 0x200");
        pin(bin_body, "if(Temco_logo.CompareNoCase(_T(\"TEMCO\")) != 0&&Temco_logo.CompareNoCase(L\"CO2\")!=0) { "
                      "ret_find_in_0x100 = -1; } else return nFileBufSize; } return nFileBufSize;",
            "  when its company there is TEMCO or CO2, and is not refused otherwise, which T5000 is stricter than");
    }

    void test_what_t5000_is_stricter_than(const Isp& isp)
    {
        section("firmware: the ISP code T5000 is stricter than");

        std::string upd, ex, hexbuf, sock;
        if (!body_of(isp.com_writer, "int CComWriter::UpdataDeviceInformation(int& ID)", upd) ||
            !body_of(isp.com_writer, "BOOL CComWriter::UpdataDeviceInformation_ex(unsigned short device_productID)", ex) ||
            !body_of(isp.hex_parser, "int  CHexFileParser::GetHexFileBuffer(char* pBuf, int nLen)", hexbuf) ||
            !body_of(isp.my_socket, "void MySocket::OnReceive(int nErrorCode)", sock))
            return;

        pin(upd, "if (n_check_temco_firmware == 0) return 1;", "Check_Temco_Firmware=0 skips the extended route's check");
        pin(ex, "if (n_check_temco_firmware == 0) return 1;", "  the data route's");
        pin(hexbuf, "if (n_check_temco_firmware == 0) return nBufLen;", "  a .hex's company check");
        pin(sock, "else if (n_check_temco_firmware == 0) { Sleep(10); }", "  and the network's name check");
        pin(upd, "if (Device_infor[7] == 255 || Device_infor[7] == 0) { return TRUE; }",
            "a device reporting product 0 or 255 is not checked on the extended route");
        pin(ex, "if (device_productID == 255 || device_productID == 0) { return TRUE; }", "  nor on the data route");
        pin(hexbuf, "if(strlen(global_fileInfor.product_name) > 200) nBufLen = -1;",
            "ISP refuses only a name running past 200 characters");

        FirmwareFile f;
        f.header.bytes[header_at::company] = 'X';
        memcpy(&f.header.bytes[header_at::product_name], "TSTAT8", 6);
        DeviceFacts d;
        d.product = 9;
        check(!check_firmware(f, d).ok, "T5000 refuses a company that is not Temco's");
        memcpy(f.header.bytes, "TEMCO", 5);
        d.product = 0;
        check(!check_firmware(f, d).ok, "  and a device reporting product 0");
    }

    // ---- files ----

    void test_the_repository_files()
    {
        section("firmware: the repository's own files");

        const fs::path dir = fs::path(t5000::conformance::g_source_root) / "T3000" / "ResourceFile" / "HexFile";
        std::error_code ec;
        int files = 0, read = 0;
        for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec))
        {
            if (it->path().extension() != ".hex")
                continue;
            files++;
            std::ifstream in(it->path(), std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            const std::string text = ss.str();
            const std::string name = it->path().filename().string();
            bool all = true;
            for (const Path path : { Path::Serial, Path::Network, Path::Controller })
            {
                FirmwareFile f;
                std::string why;
                const bool ok = read_firmware(name, (const uint8_t*)text.data(), text.size(), path, f, why);
                const bool linear = ok && f.format == HexFormat::LinearAddress && !f.sections.empty() &&
                                    f.sections.back() == f.data_size;
                if (!ok || !linear || upper(f.header.company()) != "TEMCO")
                {
                    printf("        %s on %s: %s\n", name.c_str(), to_string(path), ok ? "not as expected" : why.c_str());
                    all = false;
                }
            }
            if (all)
                read++;
        }
        check(files >= 6, "the repository's .hex files are found");
        check_eq(read, files, "  and each is read, on every path, as a Temco file of linear address records");

        std::string bin, error;
        if (require(read_source("T3000\\res\\monitor_.bin", bin, error), "T3000\\res\\monitor_.bin is read"))
        {
            FirmwareFile f;
            std::string why;
            check(!read_firmware("monitor_.bin", (const uint8_t*)bin.data(), bin.size(), Path::Serial, f, why) &&
                      why.find("neither starts with ASIX") != std::string::npos,
                  "  and, being no firmware, refused");
        }
    }
}

int run_firmware_guard_tests()
{
    Isp isp;
    std::string error;
    if (!require(read_isp(isp, error), "ISP's source is read"))
    {
        printf("        %s\n", error.c_str());
        return 0;
    }
    test_names_and_aliases(isp);
    printf("\n");
    test_bootloader_rules(isp);
    printf("\n");
    test_serial_routes(isp);
    printf("\n");
    test_reader(isp);
    printf("\n");
    test_what_t5000_is_stricter_than(isp);
    printf("\n");
    test_the_repository_files();
    return 0;
}
