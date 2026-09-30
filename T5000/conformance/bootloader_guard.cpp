// Holds the synthetic bootloaders (testing/fake_*.h) to ISP's source, read
// as text.
//
// The fakes answer as ISP needs and judge what they hear by ISP's numbers:
// its ports, the words of its handshake, its registers and commands, and the
// block's 128 bytes with its count field equal to its byte count. Each of
// those numbers is written into the fakes; here each is found where ISP uses
// it, built from the fake's own constant, so a fake that drifts from ISP, or
// an ISP that changes, fails this check.
//
// The text is searched with its white space dropped and with each line cut
// at "//", so a line of ISP that has been commented out does not count. The
// files hold GBK text, which the comment stripper in source_text.h cannot
// always walk (see function_body there); cutting lines at "//" needs no
// knowledge of strings, and none of the lines pinned has "//" in a string.

#include <stdio.h>
#include <string.h>

#include <iterator>
#include <string>
#include <vector>

#include "../testing/check.h"
#include "../testing/fake_bootloader.h"
#include "../testing/fake_modbus_controller.h"
#include "../testing/fake_tftp_bootloader.h"
#include "source_text.h"

namespace
{
    using namespace t5000::testing;
    using t5000::conformance::read_source;

    // Each line cut at "//", then every space, tab and line end dropped.
    std::string code_only(const std::string& text)
    {
        std::string out;
        out.reserve(text.size());
        bool comment = false;
        for (size_t i = 0; i < text.size(); i++)
        {
            const char c = text[i];
            if (c == '\n')
            {
                comment = false;
                continue;
            }
            if (comment)
                continue;
            if (c == '/' && i + 1 < text.size() && text[i + 1] == '/')
            {
                comment = true;
                continue;
            }
            if (c == ' ' || c == '\t' || c == '\r')
                continue;
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

    std::string hex(unsigned v)
    {
        char b[16];
        snprintf(b, sizeof(b), "0x%x", v);
        return b;
    }

    // Found at least `at_least` times in the code, with the reason when not.
    // The pin is read as the code is, its spaces dropped: the one inside
    // "FLASH DONE" too.
    void pin(const std::string& code, const std::string& what, const char* label, int at_least = 1)
    {
        const int n = occurrences(code, code_only(what));
        if (n < at_least)
            printf("  not found %d time(s) in ISP: %s\n", at_least, what.c_str());
        check(n >= at_least, label);
    }

    void test_the_guard_reads_code_only()
    {
        section("bootloader guard: what it reads");
        const std::string text = "int a = 1; // mudbus_write_one(m_ID, 16, 0x7f)\r\n"
                                 "//if(-2==Write_One(m_ID,16,0x3f))\r\n"
                                 "  mudbus_write_one ( m_ID , 16 , 0x1f ) ;\r\n";
        const std::string code = code_only(text);
        check(code.find("0x7f") == std::string::npos, "a call after // on a line is not code");
        check(code.find("0x3f") == std::string::npos, "nor a line commented out");
        check(code.find("mudbus_write_one(m_ID,16,0x1f);") != std::string::npos, "and code is found whatever its spaces");
    }

    void test_the_network_constants()
    {
        section("bootloader guard: TFTP's ports and words (TFTPServer.h, TFTPServer.cpp, MySocket.cpp)");
        std::string header, tftp, my_socket, error;
        if (!require(read_source("ISP\\TFTPServer.h", header, error) && read_source("ISP\\TFTPServer.cpp", tftp, error) &&
                         read_source("ISP\\MySocket.cpp", my_socket, error),
                     "ISP's TFTP source is read"))
        {
            printf("  %s\n", error.c_str());
            return;
        }
        const std::string h = code_only(header);
        const std::string t = code_only(tftp);
        const std::string m = code_only(my_socket);

        pin(h, "constintFLASH_UDP_PORT=" + std::to_string(kFlashUdpPort) + ";", "the device's UDP port");
        pin(h, "constintLOCAL_UDP_PORT=" + std::to_string(kLocalUdpPort) + ";", "ISP's UDP port");
        pin(t, "memcpy_s(sendbuf," + std::to_string(strlen(kTemcoHello)) + ",\"" + kTemcoHello + "\"," +
                   std::to_string(strlen(kTemcoHello)) + ");",
            "\"Temcocontrols\", 13 bytes, starts ISP's 45");
        pin(t, "memcpy_s(Flash_Done," + std::to_string(strlen(kFlashDone)) + ",\"" + kFlashDone + "\"," +
                   std::to_string(strlen(kFlashDone)) + ");",
            "\"FLASH DONE\", 10 bytes");
        pin(m, std::string("strcmp(temp_data,\"") + kTemcoReply + "\")", "\"ReceiveDHCP\", in both of ISP's states", 2);
    }

    void test_the_modbus_constants()
    {
        section("bootloader guard: registers, commands and the block (ComWriter.cpp, common.cpp)");
        std::string writer, common, error;
        if (!require(read_source("ISP\\ComWriter.cpp", writer, error) &&
                         read_source("ModbusDllforVc\\ModbusDllforVc\\common.cpp", common, error),
                     "ISP's Modbus source is read"))
        {
            printf("  %s\n", error.c_str());
            return;
        }
        const std::string w = code_only(writer);
        const std::string c = code_only(common);

        pin(w, "Write_One(pWriter->m_szMdbIDs[i]," + std::to_string(kRegCommand) + "," + std::to_string(kCmdJump) + ");",
            "127 to register 16, the jump (the first thread)");
        pin(w, "mudbus_write_one(pWriter->m_szMdbIDs[i]," + std::to_string(kRegCommand) + "," + std::to_string(kCmdJump) + ",",
            "and the ARM thread's");
        pin(w, "mudbus_write_one(m_ID," + std::to_string(kRegCommand) + "," + hex(kCmdJump) + ")", "0x7F, init", 2);
        pin(w, "mudbus_write_one(m_ID," + std::to_string(kRegCommand) + "," + hex(kCmdErase) + ")", "0x3F, erase", 2);
        pin(w, "mudbus_write_one(m_ID," + std::to_string(kRegCommand) + "," + hex(kCmdStart) + ")", "0x1F, start", 2);
        pin(w, "mudbus_write_one(m_ID," + std::to_string(kRegCommand) + "," + std::to_string(kCmdEnd) + ")", "1, the end");
        pin(w, "mudbus_write_one(pWriter->m_szMdbIDs[i]," + std::to_string(kRegCommand) + "," + std::to_string(kCmdAfter0) + ")",
            "8, after section 0");
        pin(w, "mudbus_write_one(255," + std::to_string(kRegCommand) + ",0x0" + hex(kCmdQuiet).substr(2) + ",3)",
            "0x0455 to 255, twice", 2);
        pin(w, "mudbus_write_one(m_ID," + std::to_string(kRegSection) + ",section)", "register 12, the section");
        pin(w, "#defineREG_RESUME_OFFSET" + std::to_string(kRegPackets), "register 1991, the blocks held");
        pin(w, "modbus_read_multi(m_ID,&stored_md5[0]," + std::to_string(kRegMd5) + ",4)", "the MD5 read from 1993");
        pin(w, "mudbus_write_one(m_ID," + std::to_string(kRegMd5) + "+y,", "and written there");
        pin(w, "mudbus_read_one(m_ID," + hex(kRegStatus) + ")", "0xEE10, the update's state");
        pin(w, "update_status==" + hex(kStatusInterrupted2) + "||update_status==" + hex(kStatusInterrupted),
            "0x40 and 0x1F are interrupted");
        pin(w, "mudbus_read_one(m_ID," + std::to_string(kRegCpu) + ",3)", "register 65010");
        pin(w, "mudbus_write_single_short(m_ID,&register_data[ii],ii," + std::to_string(kBlockBytes) + ")",
            "blocks of 128 bytes", 3);

        pin(w, "typedefenum{F_INITIAL,F_START_SHUTDOWN,F_SUCCESS,F_TIMEOUT,}E_FLAG_SHUTDOWN;",
            "register 99's states, in this order");
        check(kShutdownInitial == 0 && kShutdownStart == 1 && kShutdownSuccess == 2 && kShutdownTimeout == 3,
              "and the controller's are the same numbers");
        pin(w, "mudbus_write_one(" + std::to_string(kControllerUnit) + "," + std::to_string(kRegShutdown) + ",F_START_SHUTDOWN,10)",
            "99 = 1 to the controller, unit 255", 2);
        pin(w, "intfirmware_ver=temp_register[5]*10+temp_register[4];if(firmware_ver>=" + std::to_string(kQuietFromFirmware) + ")",
            "only from firmware 63.6, register 5 * 10 + register 4");

        // The block's framing: the count field is the byte count, on the
        // line and over TCP, where the length field stays 6.
        pin(c, "data_to_write[5]=length;data_to_write[6]=length;", "on the line, the count field is the byte count");
        pin(c, "data_to_write[5]=6;data_to_write[6]=device_var;data_to_write[7]=0x10;", "over TCP, the length field is 6");
        pin(c, "data_to_write[11]=length;data_to_write[12]=length;", "and the count field the byte count");
    }

    void test_the_controllers_asked_to_quiet()
    {
        section("bootloader guard: the controllers ISP asks to quiet their bus (ComWriter.cpp, ProductModel.h)");
        std::string writer, models, error;
        if (!require(read_source("ISP\\ComWriter.cpp", writer, error) && read_source("T3000\\ProductModel.h", models, error),
                     "ISP's source and its model numbers are read"))
        {
            printf("  %s\n", error.c_str());
            return;
        }
        const std::string w = code_only(writer);
        const std::string m = code_only(models);

        // In the fake's order.
        const char* names[] = { "PM_MINIPANEL", "PM_TSTAT10", "PM_MINIPANEL_ARM", "PM_ESP32_T3_SERIES" };
        static_assert(std::size(names) == std::size(kQuietedModels), "a name for each model");
        std::string models_asked;
        for (size_t i = 0; i < std::size(names); i++)
        {
            pin(m, std::string("#define") + names[i] + std::to_string(kQuietedModels[i]),
                (std::string("model ") + std::to_string(kQuietedModels[i]) + " is " + names[i]).c_str());
            models_asked += std::string(i ? "||" : "") + "(temp_register[7]==" + names[i] + ")";
        }
        pin(w, "if(" + models_asked + ")", "those four models, and only them, register 7 of the controller");
    }
}

int run_bootloader_guard_tests()
{
    test_the_guard_reads_code_only();
    test_the_network_constants();
    test_the_modbus_constants();
    test_the_controllers_asked_to_quiet();
    return 0;
}
