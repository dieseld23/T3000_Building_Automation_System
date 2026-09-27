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

int run_prog_file_guard_tests()
{
    test_the_tables_are_t3000s();
    test_save_file_writes_them_in_order();
    test_load_file_reads_them_as_t5000_does();
    test_a_file_t3000_saved_is_read();
    return 0;
}
