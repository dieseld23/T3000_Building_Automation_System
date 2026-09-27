// Checks how T5000 reads a .prog file (offline/prog_file.cpp) against how
// T3000 writes and reads one:
//
//   - SaveBacnetBinaryFile (global_function.cpp:12724): 55 FF and version 8,
//     then every table in T5000's order, each a count of T3000's items, and
//     nothing else written
//   - each count, from global_define.h, and each item's size, from the
//     structs in CM5/ud_str.h that wire_guard holds T5000's layouts to
//   - LoadBacnetBinaryFile (:10506): 55 FF and a version of 5 or more for
//     this format; the inputs read first, each whole; the settings found
//     after the ten tables before them; the tables read in the same order,
//     and those versions 6, 7 and 8 added read only from those versions
//   - a file T3000 saved, Documentation/BTUMeterRev22.prog, read as T5000
//     reads it
//   - an export (write_prog_file): each table T5000 does not keep built
//     from T3000's own structs as Initial_All_Point builds it, the settings
//     as Add virtual device and Initial_Virtual_Device_Setting set them, and
//     the source those defaults, the UART_ codes, and what Load File keeps
//     of the settings (which the export's warning names) are taken from
//
// Save and Load's text is compared with its spaces taken out, as the two
// space their loops differently. A failure says what was copied has
// changed, not that T5000 is wrong.

#include "cm5_header.h"

#include "../offline/prog_file.h"
#include "../testing/check.h"
#include "source_text.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <string>
#include <vector>

namespace
{
    using namespace t5000::testing;
    using t5000::conformance::function_body;
    using t5000::conformance::parse_constant;
    using t5000::conformance::read_source;
    namespace offline = t5000::offline;

    std::string no_spaces(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (const char c : s)
            if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
                out += c;
        return out;
    }

    int occurrences(const std::string& text, const std::string& part)
    {
        int n = 0;
        for (size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + 1))
            n++;
        return n;
    }

    // Each part found after the one before it. `from` is where the last
    // one ended, for the caller to go on from.
    bool in_order(const std::string& text, const std::vector<std::string>& parts, size_t& from)
    {
        for (const auto& part : parts)
        {
            const size_t at = text.find(part, from);
            if (at == std::string::npos)
            {
                printf("        not found in order: %s\n", part.c_str());
                return false;
            }
            from = at + part.size();
        }
        return true;
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

    bool body_or_fail(const std::string& text, const char* signature, std::string& body)
    {
        std::string error;
        if (!function_body(text, signature, body, error))
        {
            check(false, (std::string(signature) + " is found").c_str());
            printf("        %s\n", error.c_str());
            return false;
        }
        body = no_spaces(body);
        return true;
    }

    // The size of one of T3000's items, by the name T5000's table gives it:
    // a struct of CM5/ud_str.h, or a constant of global_define.h, or the
    // number Save File writes.
    bool item_size(const std::string& defines, const char* item, long& size)
    {
        struct Known
        {
            const char* name;
            size_t      size;
        };
        static const Known kStructs[] = {
            { "Str_in_point",             sizeof(::Str_in_point) },
            { "Str_out_point",            sizeof(::Str_out_point) },
            { "Str_variable_point",       sizeof(::Str_variable_point) },
            { "Str_program_point",        sizeof(::Str_program_point) },
            { "Str_controller_point",     sizeof(::Str_controller_point) },
            { "Control_group_point",      sizeof(::Control_group_point) },
            { "Str_label_point",          sizeof(::Str_label_point) },
            { "Str_userlogin_point",      sizeof(::Str_userlogin_point) },
            { "Str_Units_element",        sizeof(::Str_Units_element) },
            { "Str_table_point",          sizeof(::Str_table_point) },
            { "Str_Setting_Info",         sizeof(::Str_Setting_Info) },
            { "Str_weekly_routine_point", sizeof(::Str_weekly_routine_point) },
            { "Str_annual_routine_point", sizeof(::Str_annual_routine_point) },
            { "Str_monitor_point",        sizeof(::Str_monitor_point) },
            { "Str_variable_uint_point",  sizeof(::Str_variable_uint_point) },
            { "Str_MSV",                  sizeof(::Str_MSV) },
            { "Str_schedual_time_flag",   sizeof(::Str_schedual_time_flag) },
        };
        for (const auto& k : kStructs)
        {
            if (strcmp(k.name, item) == 0)
            {
                size = (long)k.size;
                return true;
            }
        }
        if (strcmp(item, "2000") == 0)
        {
            size = 2000;
            return true;
        }
        std::string error;
        if (parse_constant(defines, item, size, error))
            return true;
        printf("        %s\n", error.c_str());
        return false;
    }

    bool is_struct(const char* item)
    {
        return strncmp(item, "Str_", 4) == 0 || strcmp(item, "Control_group_point") == 0;
    }

    void test_the_tables_are_t3000s()
    {
        section(".prog: each table is as many of T3000's items as T3000 writes");

        std::string defines;
        if (!read_or_fail("T3000\\global_define.h", defines))
            return;

        size_t length = offline::prog_at::inputs;
        size_t settings_at = 0;
        for (const auto& s : offline::prog_sections())
        {
            const std::string name = std::string(s.item) + (s.count[0] ? std::string(" x ") + s.count : "");

            long size = 0;
            if (item_size(defines, s.item, size))
                check_eq((long)s.size, size, (name + ": each item is its size in T3000").c_str());
            else
                check(false, (name + ": its size is found").c_str());

            if (s.count[0])
            {
                long count = 0;
                std::string error;
                if (parse_constant(defines, s.count, count, error))
                    check_eq((long)s.items, count, (name + ": as many as global_define.h says").c_str());
                else
                {
                    check(false, (std::string(s.count) + " is found").c_str());
                    printf("        %s\n", error.c_str());
                }
            }
            else
            {
                check_eq(s.items, 1, (name + ": one").c_str());
            }

            if (strcmp(s.item, "Str_Setting_Info") == 0)
                settings_at = length;
            if (s.since == 5)
                length += (size_t)s.items * s.size;
        }
        check_eq((long)settings_at, (long)offline::prog_at::settings, "the settings are where prog_at says");
        check_eq((long)length, (long)offline::prog_file_length(5), "version 5's length is the tables it has");

        long input_count = 0;
        std::string error;
        check(parse_constant(defines, "BAC_INPUT_ITEM_COUNT", input_count, error) &&
                  input_count == offline::kProgInputs,
              "kProgInputs is BAC_INPUT_ITEM_COUNT");

        // The schedule times are written a byte at a time: 9 x 8 pairs.
        long weekly = 0;
        check(parse_constant(defines, "WEEKLY_SCHEDULE_SIZE", weekly, error) && weekly == 9 * 8 * 2,
              "WEEKLY_SCHEDULE_SIZE is the 9 x 8 minute and hour pairs Save File writes");
    }

    void test_save_file_writes_them_in_order()
    {
        section(".prog: SaveBacnetBinaryFile writes 55 FF, version 8, and the tables in T5000's order");

        std::string text, body;
        if (!read_or_fail("T3000\\global_function.cpp", text) ||
            !body_or_fail(text, "void SaveBacnetBinaryFile(CString &SaveConfigFilePath)", body))
            return;

        size_t from = 0;
        check(in_order(body, { "pBuf[0]=0x55;", "pBuf[1]=0xff;", "pBuf[2]=0x08;", "temp_point=temp_point+3;" }, from),
              "55 FF, then version 8, then the tables");
        check_eq(offline::kLastProgVersion, 8, "  the last version T5000 reads is the one T3000 writes");

        int advances = 1;   // the three bytes
        for (const auto& s : offline::prog_sections())
        {
            const std::string what = std::string("  then ") + s.item + (s.count[0] ? std::string(" x ") + s.count : "");
            std::vector<std::string> parts;
            if (s.count[0])
                parts.push_back(std::string("i<") + s.count + ";");
            if (strcmp(s.item, "Str_Setting_Info") == 0)
            {
                parts.push_back("memcpy(temp_point,&Device_Basic_Setting,sizeof(Str_Setting_Info));");
                parts.push_back("temp_point=temp_point+sizeof(Str_Setting_Info);");
                advances++;
            }
            else if (strcmp(s.item, "WEEKLY_SCHEDULE_SIZE") == 0)
            {
                parts.push_back("j<9;");
                parts.push_back("x<8;");
                parts.push_back(".time_minutes;temp_point++;");
                parts.push_back(".time_hours;temp_point++;");
            }
            else if (is_struct(s.item))
            {
                parts.push_back(std::string("sizeof(") + s.item + "));");
                parts.push_back(std::string("temp_point=temp_point+sizeof(") + s.item + ");");
                advances++;
            }
            else
            {
                parts.push_back(std::string(",") + s.item + ");");
                parts.push_back(std::string("temp_point=temp_point+") + s.item + ";");
                advances++;
            }
            check(in_order(body, parts, from), what.c_str());
        }
        check(in_order(body, { "intwrite_length=temp_point-original_point;" }, from), "  and the file ends there");

        check_eq(occurrences(body, "temp_point=temp_point+"), advances, "nothing else moves on through the file");
        check_eq(occurrences(body, "temp_point++"), 2, "  and only the schedule times, a byte at a time");
    }

    void test_load_file_reads_them_as_t5000_does()
    {
        section(".prog: LoadBacnetBinaryFile takes 55 FF and 5 up, inputs first, and each version's tables");

        std::string text, body;
        if (!read_or_fail("T3000\\global_function.cpp", text) ||
            !body_or_fail(text, "int LoadBacnetBinaryFile(int write_to_device,LPCTSTR tem_read_path)", body))
            return;

        size_t from = 0;
        check(in_order(body, { "if(((unsignedchar)temp_buffer[0]==0x55)&&((unsignedchar)temp_buffer[1]==0xff))",
                               "temp_buffer=temp_buffer+2;",
                               "if(temp_buffer[0]>=5){ntemp_version=temp_buffer[0];b_new_prg=true;}",
                               "temp_buffer++;" },
                       from),
              "this format is 55 FF and a version of 5 or more, and the tables follow the three bytes");
        check_eq(offline::kFirstProgVersion, 5, "  so T5000's first version is 5");
        check_eq(occurrences(body, "0x55"), 1, "  and there is no other test of the first byte");

        const size_t binary = body.find("char*temp_point=temp_buffer;");
        if (!require(binary != std::string::npos && occurrences(body, "char*temp_point=temp_buffer;") == 1,
                     "the format's tables are read from one place"))
            return;

        // The settings: found by adding up the ten tables before them.
        std::string sum = "cacl_panel=cacl_panel";
        const auto& sections = offline::prog_sections();
        for (size_t k = 0; k < 10; k++)
            sum += std::string("+") + sections[k].count + "*sizeof(" + sections[k].item + ")";
        sum += ";";
        check_eq(occurrences(body, sum), 1, "the settings are found after the same ten tables");
        check(strcmp(sections[10].item, "Str_Setting_Info") == 0, "  which T5000's table has next");

        from = binary;
        check(in_order(body, { "for(inti=0;i<BAC_INPUT_ITEM_COUNT;i++){memcpy(&m_Input_data.at(i),temp_point,"
                               "sizeof(Str_in_point));temp_point=temp_point+sizeof(Str_in_point);" },
                       from),
              "the inputs are read first, each whole");
        const std::string before_inputs = body.substr(binary, body.find("memcpy(&m_Input_data.at(i),temp_point", binary) - binary);
        check(occurrences(before_inputs, "temp_point=temp_point") == 0 && occurrences(before_inputs, "temp_point++") == 0,
              "  with nothing read before them");

        from = binary;
        for (const auto& s : sections)
        {
            const std::string what = std::string("  then ") + s.item + (s.since > 5 ? ", from version " + std::to_string(s.since) : "");
            std::vector<std::string> parts;
            if (s.since > 5)
                parts.push_back("if(ntemp_version>=" + std::to_string(s.since) + ")");
            if (s.count[0])
                parts.push_back(std::string("i<") + s.count + ";");
            if (is_struct(s.item))
                parts.push_back(std::string("temp_point=temp_point+sizeof(") + s.item + ");");
            else
                parts.push_back(std::string("temp_point=temp_point+") + s.item + ";");
            check(in_order(body, parts, from), what.c_str());
        }
    }

    void test_a_file_t3000_saved_is_read()
    {
        section(".prog: a file T3000 saved is read as T5000 reads it");

        std::string file;
        if (!read_or_fail("Documentation\\BTUMeterRev22.prog", file))
            return;

        offline::ProgFile prog;
        std::string why;
        if (!require(offline::read_prog_file(reinterpret_cast<const uint8_t*>(file.data()), file.size(), prog, why),
                     "Documentation/BTUMeterRev22.prog is read"))
        {
            printf("        %s\n", why.c_str());
            return;
        }
        check_eq(prog.version, 6, "  as version 6");
        check_eq((long)file.size(), (long)offline::prog_file_length(6), "  66056 bytes long");
        check_eq((long)prog.settings.serial_number, 92661, "  from serial 92661");
        check_eq(prog.settings.mini_type(), 1, "  a panel of type 1");

        const offline::InputBytes& first = prog.inputs[0];
        check(strcmp(reinterpret_cast<const char*>(&first[offline::input_at::description]), "TANK2 TOP") == 0,
              "  whose input 1 is TANK2 TOP");
        check(strcmp(reinterpret_cast<const char*>(&first[offline::input_at::label]), "T2_TOP") == 0,
              "  labelled T2_TOP");

        // In Auto, reading 1.000: a measurement the import leaves behind.
        check(first[offline::input_at::auto_manual] == 0 && first[offline::input_at::value] == 0xE8,
              "  in Auto, with the value the panel read when it was saved");
        const offline::InputBytes kept = offline::imported_input(0, first);
        check(kept[offline::input_at::value] == 0 && kept[offline::input_at::value + 1] == 0 &&
                  kept[offline::input_at::control] == 0,
              "  which an import does not keep");
        check_eq(kept[offline::input_at::decom], 0x40, "  keeping its signal type");
    }
}

namespace
{
    // ------------------------------------------------------------ export

    template <typename T>
    bool item_is(const std::vector<uint8_t>& f, size_t table, int i, const T& expected)
    {
        return memcmp(&f[offline::prog_table_at(table) + (size_t)i * sizeof(T)], &expected, sizeof(T)) == 0;
    }

    void test_an_export_is_a_new_panel_of_t3000s()
    {
        section("an export's tables are built as T3000 builds a new virtual device's, in its own structs");

        offline::ProgExport device;
        device.serial    = 123456;
        device.mini_type = 5;
        for (int i = 0; i < offline::kProgInputs; i++)
            device.inputs.push_back(offline::default_input(i));
        const std::vector<uint8_t> f = offline::write_prog_file(device);
        if (!require(f.size() == offline::prog_file_length(8), "an export is a version 8 file"))
            return;
        const auto& sections = offline::prog_sections();

        // As Initial_All_Point makes each (global_function.cpp:17693).
        bool inputs = true, outputs = true, variables = true, programs = true, flags = true;
        for (int i = 0; i < sections[offline::prog_table::inputs].items; i++)
        {
            ::Str_in_point in;
            memset(&in, 0, sizeof in);
            in.filter = 5;
            snprintf((char*)in.description, sizeof in.description, "IN%d", i + 1);
            inputs = inputs && item_is(f, offline::prog_table::inputs, i, in);
        }
        for (int i = 0; i < sections[offline::prog_table::outputs].items; i++)
        {
            ::Str_out_point out;
            memset(&out, 0, sizeof out);
            snprintf((char*)out.description, sizeof out.description, "OUT%d", i + 1);
            out.hw_switch_status = 1;
            outputs = outputs && item_is(f, offline::prog_table::outputs, i, out);
        }
        for (int i = 0; i < sections[offline::prog_table::variables].items; i++)
        {
            ::Str_variable_point v;
            memset(&v, 0, sizeof v);
            snprintf((char*)v.description, sizeof v.description, "VAR%d", i + 1);
            variables = variables && item_is(f, offline::prog_table::variables, i, v);
        }
        for (int i = 0; i < sections[offline::prog_table::programs].items; i++)
        {
            ::Str_program_point p;
            memset(&p, 0, sizeof p);
            snprintf((char*)p.description, sizeof p.description, "PRG%d", i + 1);
            p.bytes = 0;
            programs = programs && item_is(f, offline::prog_table::programs, i, p);
        }
        for (int i = 0; i < sections[offline::prog_table::schedule_flags].items; i++)
        {
            ::Str_schedual_time_flag flag;
            memset(&flag, 255, sizeof flag);
            flags = flags && item_is(f, offline::prog_table::schedule_flags, i, flag);
        }
        check(inputs, "inputs as T3000 starts them are written as Str_in_point holds them");
        check(outputs, "the outputs are Initial_All_Point's: OUTn, the hand switch at Auto");
        check(variables, "the variables are VARn");
        check(programs, "the programs are PRGn, of no length");
        check(flags, "the schedules' time flags are memset to 255");

        bool zeros = true;
        for (size_t t = 0; t < sections.size(); t++)
        {
            if (t <= offline::prog_table::programs || t == offline::prog_table::settings ||
                t == offline::prog_table::schedule_flags)
                continue;
            const size_t n = (size_t)sections[t].items * sections[t].size;
            const uint8_t* p = &f[offline::prog_table_at(t)];
            zeros = zeros && std::all_of(p, p + n, [](uint8_t b) { return b == 0; });
        }
        check(zeros, "every other table is 0, as ClearBacnetData and Initial_All_Point leave it");

        // Initial_All_Point's memset, then Add virtual device's panel type
        // and serial, then Initial_Virtual_Device_Setting
        // (BacnetAddVirtualDevice.cpp:201-225, global_function.cpp:17621).
        ::Str_Setting_Info s;
        memset(&s, 0, sizeof s);
        s.reg.mini_type       = 5;
        s.reg.n_serial_number = 123456;
        s.reg.com_baudrate0   = offline::kUart115200;
        s.reg.com_baudrate2   = offline::kUart115200;
        s.reg.ip_addr[0]      = 192;
        s.reg.ip_addr[1]      = 168;
        s.reg.ip_addr[2]      = 0;
        s.reg.ip_addr[3]      = 3;
        s.reg.modbus_port     = 502;
        check(item_is(f, offline::prog_table::settings, 0, s),
              "the settings are Str_Setting_Info as Add virtual device leaves them, but for the Modbus id, object "
              "instance and name T5000 does not have");
    }

    void test_the_export_defaults_are_t3000s()
    {
        section("an export's defaults, and what its warning says Load File keeps, are T3000's source");

        std::string text, body;
        if (!read_or_fail("T3000\\global_function.cpp", text))
            return;

        if (body_or_fail(text, "void Initial_All_Point()", body))
        {
            size_t from = 0;
            check(in_order(body,
                           { "temp_in.filter=5;", "sprintf((char*)temp_in.description,\"IN%d\",i+1);",
                             "sprintf((char*)temp_out.description,\"OUT%d\",i+1);", "temp_out.hw_switch_status=1;",
                             "sprintf((char*)temp_variable.description,\"VAR%d\",i+1);",
                             "sprintf((char*)temp_program.description,\"PRG%d\",i+1);", "temp_program.bytes=0;",
                             "memset(&temp_time_flag,255,sizeof(Str_schedual_time_flag));",
                             "memset(&Device_Basic_Setting,0,sizeof(Str_Setting_Info));" },
                           from),
                  "Initial_All_Point names the points, sets the filter, the hand switches and the time flags, and "
                  "zeroes the settings, last");
            check(occurrences(body, "=1;") == 1 && occurrences(body, "=5;") == 1 && occurrences(body, "=255;") == 2,
                  "  and sets nothing else to anything but 0, bar the Tstats' and graphic items' 255, in no .prog");
        }

        if (body_or_fail(text, "void Initial_Virtual_Device_Setting()", body))
            check(body == "Device_Basic_Setting.reg.com_baudrate0=UART_115200;"
                          "Device_Basic_Setting.reg.com_baudrate2=UART_115200;"
                          "Device_Basic_Setting.reg.ip_addr[0]=192;Device_Basic_Setting.reg.ip_addr[1]=168;"
                          "Device_Basic_Setting.reg.ip_addr[2]=0;Device_Basic_Setting.reg.ip_addr[3]=3;"
                          "Device_Basic_Setting.reg.modbus_port=502;",
                  "Initial_Virtual_Device_Setting sets ports 0 and 2 to 115200, 192.168.0.3 and port 502, and "
                  "nothing else");

        if (body_or_fail(text, "void ClearBacnetData()", body))
        {
            size_t from = 0;
            check(in_order(body,
                           { "memset(&m_graphic_label_data.at(i),0,sizeof(Str_label_point));",
                             "memset(&m_analog_custmer_range.at(i),0,sizeof(Str_table_point));",
                             "memset(g_DayState[i],0,ANNUAL_CODE_SIZE);", "memset(program_code[i],0,2000);",
                             "memset(&m_variable_analog_unite.at(i),0,sizeof(Str_variable_uint_point));" },
                           from),
                  "ClearBacnetData zeroes the tables Initial_All_Point does not: the graphic labels, the range "
                  "tables, the holidays' codes, the program code and the variable units");
        }

        // What Load File keeps of the panel's settings, which the warning
        // lists: whatever it writes after taking the file's settings whole.
        if (body_or_fail(text, "int LoadBacnetBinaryFile(int write_to_device,LPCTSTR tem_read_path)", body))
        {
            const std::string take = "memcpy(&Device_Basic_Setting,cacl_panel,sizeof(Str_Setting_Info));";
            const size_t at = body.find(take + "Device_Basic_Setting.reg.n_serial_number=temp_device_serial;");
            const size_t end = at == std::string::npos
                                   ? std::string::npos
                                   : body.find("memcpy(&GetPrgSetting,cacl_panel,sizeof(Str_Setting_Info));", at);
            check(end != std::string::npos &&
                      body.substr(at + take.size(), end - at - take.size()) ==
                          "Device_Basic_Setting.reg.n_serial_number=temp_device_serial;"
                          "Device_Basic_Setting.reg.reset_default=0;"
                          "memcpy(Device_Basic_Setting.reg.panel_name,temp_panel_name,20);"
                          "Device_Basic_Setting.reg.object_instance=temp_object_instance;"
                          "Device_Basic_Setting.reg.panel_number=temp_panel_number;"
                          "Device_Basic_Setting.reg.modbus_id=temp_modbus_id;"
                          "memcpy(Device_Basic_Setting.reg.ip_addr,temp_ip_addr,4);"
                          "memcpy(Device_Basic_Setting.reg.subnet,temp_subnet,4);"
                          "memcpy(Device_Basic_Setting.reg.gate_addr,temp_gate_addr,4);"
                          "memcpy(Device_Basic_Setting.reg.mac_addr,temp_mac_addr,6);",
                  "Load File keeps the serial, name, object instance, panel number, Modbus id, IP, subnet, gateway "
                  "and MAC, sets reset_default to 0, and takes every other setting from the file");
        }

        std::string dialog;
        if (read_or_fail("T3000\\BacnetAddVirtualDevice.cpp", dialog) &&
            body_or_fail(dialog, "void CBacnetAddVirtualDevice::OnBnClickedButtonVirtualOk()", body))
        {
            size_t from = 0;
            check(in_order(body,
                           { "ClearBacnetData();", "Initial_All_Point();", "Device_Basic_Setting.reg.mini_type=",
                             "Device_Basic_Setting.reg.n_serial_number=", "Initial_Virtual_Device_Setting();",
                             "SaveBacnetBinaryFile(offline_prg_path);" },
                           from),
                  "Add virtual device clears, starts the points, sets the panel type and serial, then the settings, "
                  "and saves");
        }

        // The codes each port's rate is saved as: UART_1200 = 0, in order.
        std::string defines;
        if (read_or_fail("T3000\\global_define.h", defines))
        {
            std::string expected = "enum{";
            for (uint8_t code = 0; offline::uart_rate(code) != 0; code++)
                expected += "UART_" + std::to_string(offline::uart_rate(code)) + (code == 0 ? "=0," : ",");
            expected.back() = '}';
            check(no_spaces(defines).find(expected) != std::string::npos,
                  "the UART_ codes are T3000's, 1200 to 921600, so 9 is 115200");
            check_eq(offline::uart_rate(offline::kUart115200), 115200, "  and T5000's code for 115200 is 9");
        }

        std::string structs;
        if (read_or_fail("T3000\\CM5\\ud_str.h", structs))
            check(no_spaces(structs).find("enum{NOUSE,BACNET_MSTP,") != std::string::npos,
                  "a port mode of 0 is NOUSE, which the warning calls not used");
    }
}

int run_prog_file_guard_tests()
{
    test_the_tables_are_t3000s();
    test_save_file_writes_them_in_order();
    test_load_file_reads_them_as_t5000_does();
    test_a_file_t3000_saved_is_read();
    test_an_export_is_a_new_panel_of_t3000s();
    test_the_export_defaults_are_t3000s();
    return 0;
}
