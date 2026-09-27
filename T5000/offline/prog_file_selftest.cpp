// Tests for reading a .prog file's inputs and settings, for what an import
// keeps of each input, and for the file an export writes.
//
// The property most worth guarding is that an import keeps only what the
// operator sets. A .prog file also holds what the panel measured and set
// itself when it was saved; kept here, that would be written to the device
// as though the operator had asked for it.

#include "prog_file.h"

#include <string.h>

#include <algorithm>

#include "../testing/check.h"

namespace
{
    using namespace t5000::offline;
    using namespace t5000::testing;
    namespace wire = t5000::wire;

    bool contains(const std::string& text, const std::string& part)
    {
        return text.find(part) != std::string::npos;
    }

    bool has(const std::vector<std::string>& names, const char* name)
    {
        return std::find(names.begin(), names.end(), name) != names.end();
    }

    // A file as T3000 saves one for a panel whose inputs are as it starts
    // them, with this serial and panel type byte in its settings.
    std::vector<uint8_t> file_of(int version, uint32_t serial, uint8_t mini_type_byte)
    {
        std::vector<uint8_t> f(prog_file_length(version), 0);
        if (f.empty())
            f.resize(prog_file_length(kLastProgVersion), 0);
        f[0] = 0x55;
        f[1] = 0xFF;
        f[2] = (uint8_t)version;
        for (int i = 0; i < kProgInputs; i++)
        {
            const InputBytes p = default_input(i);
            memcpy(&f[prog_at::inputs + (size_t)i * p.size()], p.data(), p.size());
        }
        uint8_t* settings = &f[prog_at::settings];
        settings[wire::settings_at::mini_type] = mini_type_byte;
        for (int k = 0; k < 4; k++)
            settings[wire::settings_at::serial_number + k] = (uint8_t)(serial >> (8 * k));
        return f;
    }

    InputBytes& input_in(std::vector<uint8_t>& f, int index, InputBytes& scratch)
    {
        memcpy(scratch.data(), &f[prog_at::inputs + (size_t)index * scratch.size()], scratch.size());
        return scratch;
    }

    void put_input(std::vector<uint8_t>& f, int index, const InputBytes& p)
    {
        memcpy(&f[prog_at::inputs + (size_t)index * p.size()], p.data(), p.size());
    }

    void test_the_lengths_are_t3000s()
    {
        section("a .prog file is as long as the tables its version has");

        check_eq((long)prog_file_length(5), 65956, "version 5 is 65956 bytes");
        check_eq((long)prog_file_length(6), 66056, "version 6 adds the variables' units: 66056");
        check_eq((long)prog_file_length(7), 66608, "version 7 adds the multi-state values: 66608");
        check_eq((long)prog_file_length(8), 67184, "version 8 adds the schedules' flags: 67184");
        check_eq((long)prog_file_length(4), 0, "version 4 is not this format");
        check_eq((long)prog_file_length(9), 0, "nor is version 9, which T5000 does not know");

        // The settings follow the ten tables before them.
        size_t at = prog_at::inputs;
        const auto& sections = prog_sections();
        for (size_t k = 0; k < 10 && k < sections.size(); k++)
            at += (size_t)sections[k].items * sections[k].size;
        check_eq((long)at, (long)prog_at::settings, "the settings are where the ten tables before them end");
        check(sections.size() > 10 && sections[10].size == wire::kSettingsWireSize && sections[10].items == 1,
              "  and are one Str_Setting_Info");
        check(sections.front().size == wire::kInputPointWireSize && sections.front().items == kProgInputs,
              "the inputs come first, 64 Str_in_point");
    }

    void test_a_file_is_read()
    {
        section("a .prog file's inputs and settings are read");

        std::vector<uint8_t> f = file_of(8, 9251, 0x45);
        InputBytes p;
        input_in(f, 63, p);
        memcpy(&p[input_at::label], "LAST", 4);
        put_input(f, 63, p);

        ProgFile file;
        std::string why;
        if (!require(read_prog_file(f.data(), f.size(), file, why), "a version 8 file is read"))
            return;
        check_eq(file.version, 8, "  as version 8");
        check_eq((long)file.settings.serial_number, 9251, "  with the serial in its settings");
        check_eq(file.settings.mini_type(), 5, "  and its panel type, without the chip's two bits");
        check_eq((long)file.inputs.size(), 64, "  and 64 inputs");
        check(file.inputs[0] == default_input(0), "  input 1 as the file has it");
        check(memcmp(&file.inputs[63][input_at::label], "LAST", 4) == 0, "  and input 64");

        for (int version = kFirstProgVersion; version < kLastProgVersion; version++)
        {
            const std::vector<uint8_t> older = file_of(version, 9251, 5);
            ProgFile o;
            check(read_prog_file(older.data(), older.size(), o, why) && o.version == version &&
                      o.settings.serial_number == 9251,
                  ("a version " + std::to_string(version) + " file is read too").c_str());
        }
    }

    void test_what_is_not_a_file_t5000_reads()
    {
        section("a file that is not a .prog file T5000 reads is refused, saying why");

        ProgFile file;
        std::string why;

        check(!read_prog_file(nullptr, 0, file, why), "an empty file is refused");
        check(contains(why, "not a .prog file"), "  as not a .prog file");

        const uint8_t two[] = { 0x55, 0xFF };
        check(!read_prog_file(two, sizeof(two), file, why), "two bytes are refused");

        std::vector<uint8_t> f = file_of(8, 9251, 5);
        f[1] = 0xFE;
        check(!read_prog_file(f.data(), f.size(), file, why), "a file not starting 55 FF is refused");
        check(contains(why, "older T3000"), "  saying an older T3000 may have saved it");

        f = file_of(5, 9251, 5);
        f[2] = 4;
        check(!read_prog_file(f.data(), f.size(), file, why), "version 4, an older T3000's, is refused");
        check(contains(why, "not a .prog file"), "  as not this format");

        f = file_of(8, 9251, 5);
        f[2] = 9;
        check(!read_prog_file(f.data(), f.size(), file, why), "version 9 is refused");
        check(contains(why, "version 9") && contains(why, "newer T3000"), "  as a newer T3000's");

        f = file_of(8, 9251, 5);
        f.pop_back();
        check(!read_prog_file(f.data(), f.size(), file, why), "a version 8 file a byte short is refused");
        check(contains(why, "67183 bytes") && contains(why, "67184"), "  saying how long it is and should be");

        f = file_of(8, 9251, 5);
        f.push_back(0);
        check(!read_prog_file(f.data(), f.size(), file, why), "a byte over is refused too");

        f = file_of(5, 9251, 5);
        f[2] = 8;
        check(!read_prog_file(f.data(), f.size(), file, why), "a version 5 file's length, labelled 8, is refused");
        check(file.inputs.empty() && file.version == 0, "a file refused gives nothing");
    }

    InputBytes every_field_set(uint8_t auto_manual)
    {
        InputBytes p = {};
        memcpy(&p[input_at::description], "AHU supply\0junk", 15);
        memcpy(&p[input_at::label], "ABCDEFGHI", 9);   // all nine bytes, no 0
        p[input_at::value + 0]      = 0x39;             // 12345
        p[input_at::value + 1]      = 0x30;
        p[input_at::filter]         = 9;
        p[input_at::decom]          = 0x3A;             // signal type 3, status 10
        p[input_at::sub_id]         = 7;
        p[input_at::sub_product]    = 8;
        p[input_at::control]        = 1;
        p[input_at::auto_manual]    = auto_manual;
        p[input_at::digital_analog] = 1;
        p[input_at::calibration_sign] = 1;
        p[input_at::sub_number]     = 9;
        p[input_at::calibration_h]  = 2;
        p[input_at::calibration_l]  = 3;
        p[input_at::range]          = 11;
        return p;
    }

    void test_an_import_keeps_what_the_operator_sets()
    {
        section("an import keeps what the operator sets, and not what the panel read");

        const InputBytes manual = imported_input(4, every_field_set(1));
        check(memcmp(&manual[input_at::description], "AHU supply\0\0\0\0\0\0\0\0\0\0\0", 21) == 0,
              "the full label, to its first 0, with zeros after it");
        check(memcmp(&manual[input_at::label], "ABCDEFGHI", 9) == 0, "a label with no 0 in it, whole");
        check_eq(manual[input_at::filter], 9, "the filter");
        check_eq(manual[input_at::auto_manual], 1, "Manual");
        check_eq(manual[input_at::digital_analog], 1, "analog");
        check_eq(manual[input_at::range], 11, "the range");
        check(manual[input_at::calibration_sign] == 1 && manual[input_at::calibration_h] == 2 &&
                  manual[input_at::calibration_l] == 3,
              "the calibration and its sign");
        check_eq(manual[input_at::decom], 0x30, "the signal type, and not the status beside it");
        check(manual[input_at::value] == 0x39 && manual[input_at::value + 1] == 0x30 && manual[input_at::control] == 1,
              "in Manual, the value and the control byte");
        check(manual[input_at::sub_id] == 0 && manual[input_at::sub_product] == 0 && manual[input_at::sub_number] == 0,
              "not the external module");

        const std::vector<std::string> changed = changed_fields(default_input(4), manual);
        check(!has(changed, "status") && !has(changed, "external"),
              "an imported input is never marked as having its status or external module changed");
        check(has(changed, "value") && has(changed, "signalType") && has(changed, "fullLabel"),
              "  and is marked for what it did change");

        const InputBytes in_auto = imported_input(4, every_field_set(0));
        check(in_auto[input_at::value] == 0 && in_auto[input_at::value + 1] == 0 && in_auto[input_at::control] == 0,
              "in Auto, not the value or the control byte: the panel measured those");
        check(!has(changed_fields(default_input(4), in_auto), "value"), "  so the value is not marked changed");
        check_eq(in_auto[input_at::range], 11, "  and the rest is kept all the same");

        const InputBytes other = imported_input(4, every_field_set(2));
        check_eq(other[input_at::value], 0x39, "an Auto/Manual byte of 2 is Manual, as T3000's grid tests for Auto");

        // What the panel sets, alone, makes no change at all.
        InputBytes readings = default_input(9);
        readings[input_at::decom]       = 0x05;
        readings[input_at::sub_id]      = 1;
        readings[input_at::sub_number]  = 1;
        readings[input_at::value]       = 0x7F;
        readings[input_at::control]     = 1;
        check(imported_input(9, readings) == default_input(9),
              "an input in Auto that differs only in what the panel read is as T3000 starts it");
    }

    void test_rows_past_the_model_are_not_kept()
    {
        section("only the inputs the model shows are imported; the others are counted");

        std::vector<uint8_t> f = file_of(8, 9251, 5);
        InputBytes p;
        input_in(f, 0, p);
        memcpy(&p[input_at::label], "FIRST", 5);
        put_input(f, 0, p);
        input_in(f, 20, p);
        memcpy(&p[input_at::label], "TWENTY", 6);
        put_input(f, 20, p);
        input_in(f, 30, p);
        p[input_at::decom] = 0x04;   // the status alone
        put_input(f, 30, p);

        ProgFile file;
        std::string why;
        if (!require(read_prog_file(f.data(), f.size(), file, why), "the file is read"))
            return;

        const ImportedInputs twelve = imported_inputs(file, 12);
        check(twelve.inputs.size() == 1 && twelve.inputs[0].first == 0, "of 12 rows, input 1 is imported");
        check_eq(twelve.past, 1, "  and input 21, past them, is counted and not kept");

        const ImportedInputs all = imported_inputs(file, 64);
        check(all.inputs.size() == 2 && all.inputs[1].first == 20, "of 64, inputs 1 and 21");
        check_eq(all.past, 0, "  with none past");
        check(memcmp(&all.inputs[1].second[input_at::label], "TWENTY", 6) == 0, "  each as the file has it");
    }

    void test_base64()
    {
        section("the file is read from base64, strictly");

        std::vector<uint8_t> out;
        check(base64_decode("", out) && out.empty(), "nothing is nothing");
        check(base64_decode("QQ==", out) && out == std::vector<uint8_t>{ 'A' }, "QQ== is A");
        check(base64_decode("QUI=", out) && out == std::vector<uint8_t>{ 'A', 'B' }, "QUI= is AB");
        check(base64_decode("QUJD", out) && out == std::vector<uint8_t>{ 'A', 'B', 'C' }, "QUJD is ABC");
        check(base64_decode("VQ+/", out) && out == std::vector<uint8_t>{ 0x55, 0x0F, 0xBF }, "+ and / are 62 and 63");

        check(!base64_decode("QQ=", out), "a length that is not a multiple of 4 is refused");
        check(!base64_decode("Q===", out), "three = are refused");
        check(!base64_decode("QQ=A", out), "a digit after = is refused");
        check(!base64_decode("QQ==QUJD", out), "= before the end is refused");
        check(!base64_decode("QU J", out) && !base64_decode("QUJ\n", out), "whitespace is refused");
        check(!base64_decode("QU-_", out), "the URL alphabet's - and _ are refused");
    }

    void test_base64_encoding()
    {
        section("base64 out, as the page decodes it");

        check(base64_encode({}).empty(), "nothing is nothing");
        check(base64_encode({ 'M' }) == "TQ==", "one byte, padded twice");
        check(base64_encode({ 'M', 'a' }) == "TWE=", "two, padded once");
        check(base64_encode({ 'M', 'a', 'n' }) == "TWFu", "three, not padded");
        check(base64_encode({ 0xFB, 0xFF, 0xBF }) == "+/+/", "+ and / for 62 and 63");

        std::vector<uint8_t> all(256);
        for (int i = 0; i < 256; i++)
            all[(size_t)i] = (uint8_t)i;
        bool round_trip = true;
        for (size_t n = 0; n <= all.size(); n++)
        {
            const std::vector<uint8_t> part(all.begin(), all.begin() + (long)n);
            std::vector<uint8_t> back;
            round_trip = round_trip && base64_decode(base64_encode(part), back) && back == part;
        }
        check(round_trip, "and every length from 0 to 256 comes back as it was");
    }

    std::vector<InputBytes> inputs_as_t3000_starts_them()
    {
        std::vector<InputBytes> inputs;
        for (int i = 0; i < kProgInputs; i++)
            inputs.push_back(default_input(i));
        return inputs;
    }

    ProgExport a_device(uint32_t serial, uint8_t mini_type)
    {
        ProgExport d;
        d.serial    = serial;
        d.mini_type = mini_type;
        d.inputs    = inputs_as_t3000_starts_them();
        return d;
    }

    bool all_are(const std::vector<uint8_t>& f, size_t from, size_t n, uint8_t b)
    {
        return std::all_of(f.begin() + (long)from, f.begin() + (long)(from + n), [&](uint8_t x) { return x == b; });
    }

    std::string text_at(const std::vector<uint8_t>& f, size_t at)
    {
        return std::string(reinterpret_cast<const char*>(&f[at]));
    }

    void test_an_export_is_a_file_t5000_reads()
    {
        section("an export is a version 8 file, which T5000 reads back as it was written");

        check_eq((long)prog_table_at(prog_table::settings), (long)prog_at::settings, "the settings are where the reader looks");
        check_eq((long)prog_table_at(prog_table::inputs), (long)prog_at::inputs, "  and so are the inputs");

        ProgExport d = a_device(9401, 6);
        memcpy(&d.inputs[1][input_at::label], "SAT", 4);
        d.inputs[2][input_at::auto_manual] = 1;
        d.inputs[2][input_at::value]       = 0xFC;
        d.inputs[2][input_at::value + 1]   = 0x53;

        const std::vector<uint8_t> f = write_prog_file(d);
        check_eq((long)f.size(), (long)prog_file_length(8), "67184 bytes");
        check(f[0] == 0x55 && f[1] == 0xFF && f[2] == 8, "  starting 55 FF 08");

        ProgFile back;
        std::string why;
        if (!require(read_prog_file(f.data(), f.size(), back, why), "T5000 reads it"))
            return;
        check_eq((long)back.settings.serial_number, 9401, "  as saved from serial 9401");
        check_eq(back.settings.mini_type(), 6, "  a T3-LB");
        bool same = back.inputs.size() == d.inputs.size();
        for (size_t i = 0; same && i < d.inputs.size(); i++)
            same = back.inputs[i] == d.inputs[i];
        check(same, "  with every input as written, the changes to inputs 2 and 3 among them");
    }

    void test_an_export_holds_t3000s_defaults()
    {
        section("an export holds, for every table T5000 does not keep, what T3000 gives a new virtual device");

        const std::vector<uint8_t> f = write_prog_file(a_device(9402, 5));
        const auto& sections = prog_sections();

        const size_t outputs = prog_table_at(prog_table::outputs);
        const size_t out_size = sections[prog_table::outputs].size;
        check(text_at(f, outputs) == "OUT1" && text_at(f, outputs + 63 * out_size) == "OUT64",
              "the outputs are OUT1 to OUT64");
        bool outputs_ok = true;
        for (int i = 0; i < 64; i++)
        {
            std::vector<uint8_t> expected(out_size, 0);
            const std::string name = "OUT" + std::to_string(i + 1);
            memcpy(expected.data(), name.data(), name.size());
            expected[offsetof(wire::OutputPoint, hw_switch_status)] = 1;
            outputs_ok = outputs_ok && std::equal(expected.begin(), expected.end(), f.begin() + (long)(outputs + i * out_size));
        }
        check(outputs_ok, "  each with its hand switch at Auto, and nothing else");

        const size_t variables = prog_table_at(prog_table::variables);
        const size_t var_size = sections[prog_table::variables].size;
        check(text_at(f, variables) == "VAR1" && text_at(f, variables + 127 * var_size) == "VAR128",
              "the variables are VAR1 to VAR128");
        check(all_are(f, variables + 4, var_size - 4, 0), "  and nothing else");

        const size_t programs = prog_table_at(prog_table::programs);
        const size_t prg_size = sections[prog_table::programs].size;
        check(text_at(f, programs) == "PRG1" && text_at(f, programs + 15 * prg_size) == "PRG16",
              "the programs are PRG1 to PRG16");
        check(all_are(f, programs + 5, prg_size - 5, 0), "  with nothing else, their length 0 among it");

        bool zeros = true;
        for (size_t t = 4; t < sections.size(); t++)
            if (t != prog_table::settings && t != prog_table::schedule_flags)
                zeros = zeros && all_are(f, prog_table_at(t), (size_t)sections[t].items * sections[t].size, 0);
        check(zeros, "every other table is 0, the program code, holidays' codes and graphic labels among them");

        const ProgSection& flags = sections[prog_table::schedule_flags];
        check(all_are(f, prog_table_at(prog_table::schedule_flags), (size_t)flags.items * flags.size, 0xFF),
              "the schedules' time flags are all 0xFF");

        const size_t s = prog_table_at(prog_table::settings);
        namespace at = wire::settings_at;
        std::vector<uint8_t> expected(wire::kSettingsWireSize, 0);
        expected[at::mini_type]         = 5;
        expected[at::serial_number]     = 9402 & 0xFF;
        expected[at::serial_number + 1] = 9402 >> 8;
        expected[at::com_baudrate0]     = 9;
        expected[at::com_baudrate2]     = 9;
        expected[at::ip_addr]           = 192;
        expected[at::ip_addr + 1]       = 168;
        expected[at::ip_addr + 3]       = 3;
        expected[at::modbus_port]       = 502 & 0xFF;
        expected[at::modbus_port + 1]   = 502 >> 8;
        check(std::equal(expected.begin(), expected.end(), f.begin() + (long)s),
              "the settings are 0 but the serial, the panel type, ports 0 and 2 at 115200, 192.168.0.3 and port 502");
        check_eq(uart_rate(kUart115200), 115200, "code 9 is 115200 baud");
        check(uart_rate(0) == 1200 && uart_rate(10) == 921600 && uart_rate(11) == 0, "  0 is 1200, 10 is 921600, 11 none");
    }

    void test_the_warning_reads_the_file()
    {
        section("what the page is told Load File would do is read back from the file");

        std::vector<uint8_t> f = write_prog_file(a_device(9403, 6));
        const std::string text = describe_prog_export(f, "T3-LB");
        check(contains(text, "holds every table of a panel, not only its inputs"), "it says the file is every table");
        check(contains(text, "the outputs as OUT1 to OUT64, their hand switches at Auto"), "  the outputs");
        check(contains(text, "the variables as VAR1 to VAR128"), "  the variables");
        check(contains(text, "the programs as PRG1 to PRG16, with no code"), "  the programs");
        check(contains(text, "and no PID loops, screens, graphic labels, logins, custom units, range tables, "
                             "schedules, holidays, trend logs, schedule times, holiday codes, program code, "
                             "variable units and multi-state values."),
              "  and the tables it empties");
        check(contains(text, "Load File keeps the panel's serial, name, panel number, Modbus id, object instance, "
                             "IP address, subnet, gateway and MAC"),
              "  what Load File keeps");
        check(contains(text, "panel type T3-LB, so load it only onto a T3-LB"), "  the panel type");
        check(contains(text, "DHCP, so a panel with a static address takes one from DHCP"), "  DHCP");
        check(contains(text, "serial ports 0, 1 and 2 not used, at 115200, 1200 and 115200 baud"), "  the serial ports");
        check(contains(text, "Modbus TCP port 502"), "  the Modbus TCP port");
        check(contains(text, "MS/TP network 0 and max master 0"), "  MS/TP");
        check(contains(text, "its product field 0; and every other setting 0."), "  and every other setting");
        check(contains(text, "The file says serial 9403."), "  and the serial in the file");

        // Changed bytes change the words: the text is the file's, not a
        // copy of what the writer meant to put there.
        const size_t s = prog_table_at(prog_table::settings);
        namespace at = wire::settings_at;
        f[s + at::tcp_type]      = 1;
        f[s + at::com1_config]   = 2;
        f[s + at::com_baudrate1] = 5;
        f[s + at::max_master]    = 127;
        f[s + 170]               = 1;   // en_dyndns, named nowhere
        f[prog_table_at(6)]      = 1;   // a graphic label
        f[prog_table_at(prog_table::outputs) + offsetof(wire::OutputPoint, hw_switch_status)] = 2;
        const std::string changed = describe_prog_export(f, "T3-LB");
        check(contains(changed, "a static address"), "a static address is said");
        check(contains(changed, "serial port 0 not used at 115200 baud, serial port 1 in mode 2 at 9600 baud and "
                                "serial port 2 not used at 115200 baud"),
              "  ports that differ, each");
        check(contains(changed, "max master 127"), "  the max master");
        check(contains(changed, "every other setting as the file has it"), "  a setting named nowhere");
        check(contains(changed, "and the graphic labels the file holds"), "  a table not empty");
        check(!contains(changed, "hand switches at Auto"), "  and an output's hand switch not at Auto");

        check(describe_prog_export(std::vector<uint8_t>(100, 0), "T3-LB").empty(), "a file of another length says nothing");
    }
}

int run_prog_file_tests()
{
    test_the_lengths_are_t3000s();
    test_a_file_is_read();
    test_what_is_not_a_file_t5000_reads();
    test_an_import_keeps_what_the_operator_sets();
    test_rows_past_the_model_are_not_kept();
    test_base64();
    test_base64_encoding();
    test_an_export_is_a_file_t5000_reads();
    test_an_export_holds_t3000s_defaults();
    test_the_warning_reads_the_file();
    return 0;
}
