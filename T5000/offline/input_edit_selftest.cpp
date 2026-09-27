// Tests for changing an input offline.
//
// Each rule is T3000's, from Fresh_Input_Item and the grid's click handler,
// or a stated difference from it. The text rules are tested under two named
// code pages, 1252 and 936, rather than this computer's, so a label that
// fits in characters but not in bytes is tested wherever the tests run.

#include "input_edit.h"

#include <string.h>

#include "../testing/check.h"

namespace
{
    using namespace t5000::offline;
    using namespace t5000::testing;
    using t5000::device::MiniType;
    using t5000::device::ProductClassId;

    constexpr unsigned kWestern = 1252;
    constexpr unsigned kChinese = 936;

    InputPanel panel_of(ProductClassId product, MiniType type, int rows)
    {
        InputPanel p;
        p.product = product;
        p.type    = type;
        p.rows    = rows;
        return p;
    }

    // A T3-BB, which fixes no row's range: every input the tests make can
    // be changed.
    InputPanel bb(int rows)
    {
        return panel_of(ProductClassId::MiniPanelArm, MiniType::MiniPanelArm, rows);
    }

    std::vector<InputBytes> fresh_panel(int count = 64)
    {
        std::vector<InputBytes> inputs;
        for (int i = 0; i < count; i++)
            inputs.push_back(default_input(i));
        return inputs;
    }

    std::string text_at(const InputBytes& p, size_t at, size_t length)
    {
        return std::string((const char*)&p[at], strnlen((const char*)&p[at], length));
    }

    std::string full_label(const InputBytes& p) { return text_at(p, input_at::description, 21); }
    std::string label(const InputBytes& p) { return text_at(p, input_at::label, 9); }

    // One edit on input `index`, returning whether it was taken.
    bool edit(std::vector<InputBytes>& inputs, int index, InputField field, const std::string& text,
              unsigned code_page = kWestern, bool* changed_out = nullptr, std::string* message_out = nullptr)
    {
        bool changed = false;
        std::string message;
        const bool ok = apply_input_edit(inputs, bb((int)inputs.size()), index, field, text, changed, message, code_page);
        if (changed_out)
            *changed_out = changed;
        if (message_out)
            *message_out = message;
        return ok;
    }

    void test_a_new_panels_inputs_are_t3000s()
    {
        section("an input T3000 has not read is named IN1, IN2..., with filter 5 and nothing else set");

        const InputBytes first = default_input(0);
        check(full_label(first) == "IN1", "input 1 is IN1");
        check_eq(first[input_at::filter], 5, "  with filter 5");
        bool rest_zero = true;
        for (size_t i = 0; i < first.size(); i++)
        {
            if (i < 3 || i == input_at::filter)
                continue;
            rest_zero = rest_zero && first[i] == 0;
        }
        check(rest_zero, "  and every other byte 0: no label, Auto, digital, range 0");

        check(full_label(default_input(63)) == "IN64", "input 64 is IN64");
        check(full_label(default_input(254)) == "IN255", "the last input a panel can have is IN255");
    }

    void test_the_field_names_are_the_payloads()
    {
        section("the fields are named as the payload names its columns");

        const char* const names[] = { "fullLabel", "label", "autoManual", "range", "filter" };
        for (const char* name : names)
        {
            InputField f;
            check(input_field_from_name(name, f) && std::string(input_field_name(f)) == name, name);
        }

        InputField f;
        check(!input_field_from_name("value", f), "Value cannot be changed yet");
        check(!input_field_from_name("calibration", f), "  nor Calibration");
        check(!input_field_from_name("FullLabel", f), "  and a name is matched exactly");
        check_eq((long)input_fields().size(), 5, "five fields can be changed");
    }

    void test_a_label_is_changed_as_t3000_changes_one()
    {
        section("a label: at most 8 characters, '-' as '_', a to z in capitals");

        auto inputs = fresh_panel();
        check(edit(inputs, 0, InputField::Label, "ahu-1"), "\"ahu-1\" is taken");
        check(label(inputs[0]) == "AHU_1", "  as AHU_1");

        check(edit(inputs, 1, InputField::Label, "abcdefgh"), "8 characters are taken");
        std::string message;
        check(!edit(inputs, 2, InputField::Label, "abcdefghi", kWestern, nullptr, &message), "9 are not");
        check(message.find("at most 8 characters") != std::string::npos,
              "  as too many characters, as T3000 says, before a byte is counted");
        check(label(inputs[2]).empty(), "  and the input is left as it was");

        // MakeUpper in the "C" locale: a to z only. "é" is one byte in 1252.
        check(edit(inputs, 3, InputField::Label, "caf\xC3\xA9"), "\"caf\xC3\xA9\" is taken in code page 1252");
        check(label(inputs[3]) == "CAF\xE9", "  with only a to z in capitals");

        // The bytes after the text are zeros, as memcpy_s from a zeroed
        // buffer leaves them.
        check(edit(inputs, 1, InputField::Label, "x"), "a shorter label replaces a longer one");
        bool zeros = true;
        for (size_t i = 1; i < 9; i++)
            zeros = zeros && inputs[1][input_at::label + i] == 0;
        check(label(inputs[1]) == "X" && zeros, "  and leaves zeros after it, not the old text");

        check(edit(inputs, 1, InputField::Label, ""), "an empty label is taken");
        check(label(inputs[1]).empty(), "  and clears it");
        check(edit(inputs, 4, InputField::Label, ""), "  and two inputs may both have none");
    }

    void test_a_label_no_other_input_has()
    {
        section("a label another input already has is refused, as T3000 holds labels after a read");

        auto inputs = fresh_panel(70);
        check(edit(inputs, 1, InputField::Label, "AHU"), "input 2 is labelled AHU");

        std::string message;
        check(!edit(inputs, 2, InputField::Label, "ahu", kWestern, nullptr, &message),
              "\"ahu\" on input 3 is refused: it is AHU once in capitals");
        check(message.find("input 2") != std::string::npos, "  and the message says which input has it");

        // decode_input_point folds '.' and '-' to '_', so T3000, holding the
        // panel as read, has A_B for a label stored as A.B.
        memcpy(&inputs[5][input_at::label], "A.B", 3);
        check(!edit(inputs, 6, InputField::Label, "a_b"), "A_B is refused beside a label stored as A.B");

        // Check_Label_Exsit walks every input held, not only the first 64.
        memcpy(&inputs[66][input_at::label], "FAR", 3);
        check(!edit(inputs, 0, InputField::Label, "far"), "a label on input 67 counts too");

        bool changed = true;
        check(edit(inputs, 1, InputField::Label, "ahu", kWestern, &changed),
              "input 2 given its own label again is not refused");
        check(!changed, "  and nothing changes");
    }

    void test_a_label_must_fit_in_bytes_too()
    {
        section("a label must fit in its 9 bytes, not only in 8 characters");

        // 中文标签: four characters, eight bytes in GBK.
        auto inputs = fresh_panel();
        check(edit(inputs, 0, InputField::Label, "\xE4\xB8\xAD\xE6\x96\x87\xE6\xA0\x87\xE7\xAD\xBE", kChinese),
              "four Chinese characters, eight bytes in code page 936, are taken");
        check_eq((long)label(inputs[0]).size(), 8, "  as eight bytes");

        // Five characters pass T3000's check and are ten bytes: T3000 copies
        // nine of them, with no terminator, and reads the label back empty.
        std::string message;
        check(!edit(inputs, 1, InputField::Label,
                    "\xE4\xB8\xAD\xE6\x96\x87\xE6\xA0\x87\xE7\xAD\xBE\xE5\x90\x8D", kChinese, nullptr, &message),
              "five, ten bytes, are refused");
        check(message.find("10 bytes") != std::string::npos, "  and the message gives the bytes it would take");
        check(label(inputs[1]).empty(), "  and the input is as it was");

        // Four and a letter: five characters, nine bytes, the ninth of them
        // where the terminator goes.
        check(!edit(inputs, 2, InputField::Label, "\xE4\xB8\xAD\xE6\x96\x87\xE6\xA0\x87\xE7\xAD\xBE" "a", kChinese,
                    nullptr, &message),
              "four and a letter, nine bytes, are refused too");
        check(message.find("9 bytes") != std::string::npos, "  as nine bytes");
    }

    void test_text_the_code_page_cannot_hold_is_refused()
    {
        section("text the code page cannot hold is refused, where T3000 would store something else");

        auto inputs = fresh_panel();
        std::string message;
        check(!edit(inputs, 0, InputField::FullLabel, "Pump \xE4\xB8\xAD", kWestern, nullptr, &message),
              "a Chinese character is refused in code page 1252, where T3000 stores '?'");
        check(message.find("1252") != std::string::npos, "  and the message names the code page");

        // Windows maps A with a macron to a plain A in 1252 when asked for no
        // flags, as T3000 asks. Nothing reports it; converting back does.
        check(!edit(inputs, 0, InputField::Label, "\xC4\x80"), "a best-fit look-alike is refused too");

        check(!edit(inputs, 0, InputField::FullLabel, "\xF0\x9F\x98\x80", kChinese), "an emoji in 936 is refused");
        check(full_label(inputs[0]) == "IN1", "  and the input keeps its full label");
    }

    void test_text_that_is_not_one_line_is_refused()
    {
        section("text with a control character, or that is not UTF-8, is refused");

        auto inputs = fresh_panel();
        check(!edit(inputs, 0, InputField::FullLabel, "Pump\t2"), "a tab in a full label is refused");
        check(!edit(inputs, 0, InputField::FullLabel, std::string("Pu\0mp", 5)),
              "a NUL, which T3000's conversion would stop at, is refused");
        check(!edit(inputs, 0, InputField::Label, "A\x7F"), "DEL in a label is refused");
        check(!edit(inputs, 0, InputField::Label, "\xC3"), "a broken UTF-8 sequence is refused");
        check(!edit(inputs, 0, InputField::FullLabel, "\xC0\xAF"), "  and an overlong one");
        check(full_label(inputs[0]) == "IN1" && label(inputs[0]).empty(), "  and nothing changes");
    }

    void test_a_full_label_is_changed_as_t3000_changes_one()
    {
        section("a full label: at most 20 characters, kept as typed");

        auto inputs = fresh_panel();
        check(edit(inputs, 0, InputField::FullLabel, "Supply air temp-1.b"), "a full label is taken");
        check(full_label(inputs[0]) == "Supply air temp-1.b", "  as typed: no capitals, no '-' changed");

        check(edit(inputs, 1, InputField::FullLabel, "12345678901234567890"), "20 characters are taken");
        std::string message;
        check(!edit(inputs, 2, InputField::FullLabel, "123456789012345678901", kWestern, nullptr, &message), "21 are not");
        check(message.find("at most 20 characters") != std::string::npos,
              "  as too many characters, before a byte is counted");
        check(full_label(inputs[2]) == "IN3", "  and the input keeps its name");

        // Eleven characters, 22 bytes in GBK.
        std::string eleven;
        for (int i = 0; i < 11; i++)
            eleven += "\xE4\xB8\xAD";
        check(!edit(inputs, 3, InputField::FullLabel, eleven, kChinese), "eleven Chinese characters are 22 bytes, refused");

        // Ten and a letter: 21 bytes, the last where the terminator goes.
        const std::string ten = eleven.substr(0, 30);
        check(!edit(inputs, 3, InputField::FullLabel, ten + "a", kChinese, nullptr, &message),
              "ten and a letter, 21 bytes, are refused");
        check(message.find("21 bytes") != std::string::npos, "  as 21 bytes");
        check(edit(inputs, 3, InputField::FullLabel, ten, kChinese), "ten, 20 bytes, are taken");
        check_eq((long)full_label(inputs[3]).size(), 20, "  as 20 bytes");

        check(edit(inputs, 4, InputField::FullLabel, ""), "an empty full label is taken");
        check(full_label(inputs[4]).empty(), "  and clears it");
    }

    void test_a_full_label_no_other_point_has()
    {
        section("a full label another point has is refused, the other kinds' names as T3000 starts them");

        auto inputs = fresh_panel(70);
        std::string message;
        check(!edit(inputs, 2, InputField::FullLabel, "IN5", kWestern, nullptr, &message),
              "IN5 on input 3 is refused: it is input 5's");
        check(message.find("input 5") != std::string::npos, "  and the message says so");
        check(edit(inputs, 2, InputField::FullLabel, "in5"), "in5 is not: the check is case-sensitive");

        check(!edit(inputs, 0, InputField::FullLabel, "OUT64"), "OUT64, the 64th output's name, is refused");
        check(edit(inputs, 0, InputField::FullLabel, "OUT65"), "  OUT65 is not: there are 64 outputs");
        check(!edit(inputs, 0, InputField::FullLabel, "VAR128"), "VAR128 is refused");
        check(edit(inputs, 0, InputField::FullLabel, "VAR129"), "  VAR129 is not");
        check(!edit(inputs, 0, InputField::FullLabel, "PVAR48"), "PVAR48 is refused");
        check(edit(inputs, 0, InputField::FullLabel, "PVAR49"), "  PVAR49 is not");
        check(!edit(inputs, 0, InputField::FullLabel, "PRG16"), "PRG16 is refused");
        check(edit(inputs, 0, InputField::FullLabel, "PRG17"), "  PRG17 is not");
        check(!edit(inputs, 0, InputField::FullLabel, "OUT1"), "OUT1 is refused");
        check(edit(inputs, 0, InputField::FullLabel, "OUT01"), "  OUT01 is not: \"%d\" has no leading zero");

        // Check_FullLabel_Exsit walks BAC_INPUT_ITEM_COUNT inputs, 64, even
        // on a panel with more.
        check(!edit(inputs, 1, InputField::FullLabel, "IN64"), "input 64's name counts");
        check(edit(inputs, 1, InputField::FullLabel, "IN65"), "  input 65's does not");

        bool changed = true;
        check(edit(inputs, 3, InputField::FullLabel, "IN4", kWestern, &changed), "input 4 given its own name is taken");
        check(!changed, "  and nothing changes");
    }

    void test_auto_manual()
    {
        section("Auto/Manual is set to the state chosen");

        auto inputs = fresh_panel();
        bool changed = false;
        check(edit(inputs, 0, InputField::AutoManual, "Manual", kWestern, &changed) && changed, "Manual is taken");
        check_eq(inputs[0][input_at::auto_manual], 1, "  as 1, BAC_MANUAL");
        check(edit(inputs, 0, InputField::AutoManual, "auto"), "\"auto\" is taken");
        check_eq(inputs[0][input_at::auto_manual], 0, "  as 0, BAC_AUTO");

        // A panel may hold any byte there. The grid shows anything but 0 as
        // Manual, and a click takes it to Auto; choosing Auto does the same.
        inputs[1][input_at::auto_manual] = 2;
        check(edit(inputs, 1, InputField::AutoManual, "Auto"), "Auto from 2 is taken");
        check_eq(inputs[1][input_at::auto_manual], 0, "  as 0");

        check(!edit(inputs, 0, InputField::AutoManual, "True"), "anything else is refused");
    }

    void test_the_filter()
    {
        section("the filter: a whole number from 0 to 255");

        auto inputs = fresh_panel();
        check(edit(inputs, 0, InputField::Range, "41"), "input 1 is made analog");
        check(edit(inputs, 0, InputField::Filter, "0"), "0 is taken");
        check_eq(inputs[0][input_at::filter], 0, "  as 0");
        check(edit(inputs, 0, InputField::Filter, "255"), "255 is taken");
        check_eq(inputs[0][input_at::filter], 255, "  as 255");
        check(edit(inputs, 0, InputField::Filter, " 7 "), "spaces around it are ignored");
        check_eq(inputs[0][input_at::filter], 7, "  as 7");

        check(!edit(inputs, 0, InputField::Filter, "256"), "256 is refused");
        check(!edit(inputs, 0, InputField::Filter, "-1"), "-1 is refused");
        check(!edit(inputs, 0, InputField::Filter, "12abc"), "12abc is refused, where T3000's _wtoi takes 12");
        check(!edit(inputs, 0, InputField::Filter, "abc"), "abc is refused, where _wtoi takes 0");
        check(!edit(inputs, 0, InputField::Filter, ""), "nothing is refused");
        check(!edit(inputs, 0, InputField::Filter, "99999999999"), "a number too long for an int is refused");
        check_eq(inputs[0][input_at::filter], 7, "  and the filter stays 7");
    }

    void test_only_the_rows_t3000_shows_can_be_changed()
    {
        section("only the inputs T3000 shows for the model can be changed");

        auto inputs = fresh_panel();
        bool changed = false;
        std::string message;
        check(!apply_input_edit(inputs, bb(8), 8, InputField::AutoManual, "Manual", changed, message, kWestern),
              "input 9 on a model T3000 shows 8 of is refused");
        check(apply_input_edit(inputs, bb(8), 7, InputField::AutoManual, "Manual", changed, message, kWestern),
              "input 8 is not");
        check(!apply_input_edit(inputs, bb(64), 64, InputField::AutoManual, "Manual", changed, message, kWestern),
              "an input past the panel's is refused");
        check(!apply_input_edit(inputs, bb(64), -1, InputField::AutoManual, "Manual", changed, message, kWestern),
              "  and a negative one");
    }

    void test_a_range_is_chosen_by_its_number()
    {
        section("a range is chosen by its number in T3000's Range dialog");

        auto inputs = fresh_panel();

        // Bytes the range leaves alone: a value, a state, a calibration and a
        // signal type.
        inputs[0][input_at::value]            = 0x34;
        inputs[0][input_at::control]          = 1;
        inputs[0][input_at::calibration_l]    = 7;
        inputs[0][input_at::calibration_sign] = 1;
        inputs[0][input_at::decom]            = 0x21;
        const InputBytes before = inputs[0];

        bool changed = false;
        check(edit(inputs, 0, InputField::Range, "41", kWestern, &changed) && changed, "41 is taken");
        check_eq(inputs[0][input_at::digital_analog], 1, "  as analog");
        check_eq(inputs[0][input_at::range], 11, "  range 11");
        InputBytes rest = inputs[0];
        rest[input_at::digital_analog] = before[input_at::digital_analog];
        rest[input_at::range]          = before[input_at::range];
        check(rest == before, "  and every other byte as it was: value, state, calibration and signal type");

        check(edit(inputs, 0, InputField::Range, " 1 "), "1, with spaces around it, is taken");
        check(inputs[0][input_at::digital_analog] == 0 && inputs[0][input_at::range] == 1, "  as digital range 1");

        check(edit(inputs, 0, InputField::Range, "0"), "0 is taken");
        check(inputs[0][input_at::digital_analog] == 1 && inputs[0][input_at::range] == 0,
              "  as analog range 0, as T3000's OK makes Unused");

        check(edit(inputs, 0, InputField::Range, "66"), "66 is taken");
        check_eq(inputs[0][input_at::range], 36, "  as analog range 36");

        check(edit(inputs, 0, InputField::Range, "66", kWestern, &changed), "the same range again is taken");
        check(!changed, "  and changes nothing");

        std::string message;
        const InputBytes kept = inputs[0];
        check(!edit(inputs, 0, InputField::Range, "23", kWestern, nullptr, &message), "23, a custom range, is refused");
        check(message.find("Range dialog") != std::string::npos, "  saying ranges are the Range dialog's numbers");
        check(!edit(inputs, 0, InputField::Range, "65"), "65, a button with no name, is refused");
        check(!edit(inputs, 0, InputField::Range, "101"), "101, a multi-state range, is refused");
        check(!edit(inputs, 0, InputField::Range, "311"), "311, which T3000 would store as range 25, 281 cut to a byte, is refused");
        check(!edit(inputs, 0, InputField::Range, "41.0"), "41.0 is refused");
        check(!edit(inputs, 0, InputField::Range, "-1"), "-1 is refused");
        check(!edit(inputs, 0, InputField::Range, "Off/On"), "a name is refused");
        check(!edit(inputs, 0, InputField::Range, ""), "nothing is refused");

        check(!edit(inputs, 0, InputField::Range, "39", kWestern, nullptr, &message), "39, PT 1K, is refused");
        check(message.find("settings say") != std::string::npos, "  saying T3000 offers it only when the panel says it can");
        check(!edit(inputs, 0, InputField::Range, "55", kWestern, nullptr, &message),
              "55, the fast pulse count, is refused on a T3-BB's input 1");
        check(message.find("input 1 of a T3-BB") != std::string::npos, "  naming the input and the model");
        check(inputs[0] == kept, "and nothing refused changed the input");

        check(edit(inputs, 26, InputField::Range, "55"), "55 is taken on its input 27, which counts fast pulses");
    }

    void test_a_fixed_range_is_refused()
    {
        section("the range of a row the model fixes is refused");

        auto inputs = fresh_panel();
        const InputPanel oem = panel_of(ProductClassId::Tstat10, MiniType::Oem, 64);
        bool changed = false;
        std::string message;
        check(!apply_input_edit(inputs, oem, 13, InputField::Range, "41", changed, message, kWestern),
              "a T3-OEM's input 14 is refused");
        check(message.find("fixed") != std::string::npos && message.find("input 14 of a T3-OEM") != std::string::npos,
              "  saying its range is fixed");
        check(apply_input_edit(inputs, oem, 12, InputField::Range, "33", changed, message, kWestern),
              "its input 13 takes a 10K Type2 sensor");
        check(!apply_input_edit(inputs, oem, 12, InputField::Range, "41", changed, message, kWestern),
              "  and nothing else");
        check(apply_input_edit(inputs, oem, 13, InputField::Label, "t14", changed, message, kWestern),
              "input 14's label can still be changed");
    }

    void test_the_filter_of_a_digital_input_is_refused()
    {
        section("only an analog input's filter can be changed, as T3000's grid enables the cell");

        auto inputs = fresh_panel();
        std::string message;
        check(!edit(inputs, 0, InputField::Filter, "7", kWestern, nullptr, &message),
              "a new input's filter is refused: it starts digital");
        check(message.find("digital") != std::string::npos && message.find("analog range first") != std::string::npos,
              "  saying it is digital, and to give it an analog range");
        check_eq(inputs[0][input_at::filter], 5, "  and the filter stays 5");

        check(edit(inputs, 0, InputField::Range, "43"), "given an analog range");
        check(edit(inputs, 0, InputField::Filter, "7"), "  its filter is taken");
        check(edit(inputs, 0, InputField::Range, "2"), "given a digital one again");
        check(!edit(inputs, 0, InputField::Filter, "8"), "  it is refused again");
        check_eq(inputs[0][input_at::filter], 7, "  and the filter set while it was analog stays");
    }

    void test_the_columns_each_input_lets_be_changed()
    {
        section("the columns each input lets be changed, in the grid's order");

        auto names = [](const std::vector<InputField>& fields) {
            std::string out;
            for (const InputField f : fields)
                out += std::string(out.empty() ? "" : " ") + input_field_name(f);
            return out;
        };

        InputBytes digital = default_input(0);
        check(names(editable_input_fields(bb(64), 0, digital)) == "fullLabel autoManual range label",
              "a digital input: its names, Auto/Manual and range");

        InputBytes analog = digital;
        analog[input_at::digital_analog] = 1;
        check(names(editable_input_fields(bb(64), 0, analog)) == "fullLabel autoManual range filter label",
              "an analog one: and its filter");

        const InputPanel oem = panel_of(ProductClassId::Tstat10, MiniType::Oem, 64);
        check(names(editable_input_fields(oem, 13, analog)) == "fullLabel autoManual filter label",
              "a T3-OEM's input 14: not its range");

        InputBytes odd = digital;
        odd[input_at::digital_analog] = 2;
        std::string why;
        check(!input_field_enabled(bb(64), 0, odd, InputField::Filter, &why) && !why.empty(),
              "an input neither analog nor digital: not its filter, saying why");
    }

    void test_a_refused_edit_changes_nothing()
    {
        section("a refused edit leaves every input as it was");

        auto inputs = fresh_panel();
        edit(inputs, 0, InputField::Label, "A");
        edit(inputs, 1, InputField::Range, "41");
        const auto before = inputs;
        check(!edit(inputs, 1, InputField::Label, "a"), "a duplicate label is refused");
        check(!edit(inputs, 1, InputField::Filter, "300"), "a filter out of range is refused");
        check(!edit(inputs, 1, InputField::FullLabel, "IN1"), "a duplicate full label is refused");
        check(!edit(inputs, 1, InputField::Range, "39"), "a range not offered is refused");
        check(!edit(inputs, 2, InputField::Filter, "6"), "a digital input's filter is refused");
        check(inputs == before, "  and the panel is byte for byte as it was");
    }

    void test_changed_fields_name_the_columns()
    {
        section("the columns marked changed are the ones whose bytes differ");

        const InputBytes base = default_input(0);
        check(changed_fields(base, base).empty(), "an input as it started has none");

        auto one = [&](size_t at, uint8_t value) {
            InputBytes p = base;
            p[at] = value;
            const auto f = changed_fields(base, p);
            return f.size() == 1 ? f[0] : std::string("(" + std::to_string(f.size()) + " fields)");
        };

        check(one(input_at::description + 20, 'x') == "fullLabel", "the full label's last byte is the full label");
        check(one(input_at::label, 'x') == "label", "the label's first byte is the label");
        check(one(input_at::label + 8, 'x') == "label", "  and its last");
        check(one(input_at::value + 3, 1) == "value", "the value's top byte is the value");
        check(one(input_at::control, 1) == "value", "the control byte is the value: a digital input's state");
        check(one(input_at::auto_manual, 1) == "autoManual", "auto_manual is Auto/Manual");
        check(one(input_at::decom, 0x02) == "status", "decom's low nibble is the status");
        check(one(input_at::decom, 0x20) == "signalType", "  and its high nibble the signal type");
        check(one(input_at::range, 3) == "range", "range is the range");
        check(one(input_at::digital_analog, 1) == "range", "  and so is analog or digital");
        check(one(input_at::calibration_l, 1) == "calibration", "calibration's low byte is the calibration");
        check(one(input_at::calibration_h, 1) == "calibration", "  and its high byte");
        check(one(input_at::calibration_sign, 1) == "calibration", "  and its sign");
        check(one(input_at::filter, 6) == "filter", "the filter is the filter");
        check(one(input_at::sub_id, 1) == "external", "an external module's id");
        check(one(input_at::sub_product, 1) == "external", "  its product");
        check(one(input_at::sub_number, 1) == "external", "  and its input number are the external columns");

        InputBytes both = base;
        both[input_at::label] = 'x';
        both[input_at::description] = 'x';
        const auto f = changed_fields(base, both);
        check(f.size() == 2 && f[0] == "fullLabel" && f[1] == "label", "two columns are given in the grid's order");
    }

    void test_the_code_page_conversion()
    {
        section("text reaches the code page as T3000's conversion would put it, or not at all");

        std::string out, message;
        check(utf8_to_code_page("caf\xC3\xA9", kWestern, out, message) && out == "caf\xE9", "1252 takes \xC3\xA9 as one byte");
        check(utf8_to_code_page("\xE4\xB8\xAD", kChinese, out, message) && out == "\xD6\xD0", "936 takes \xE4\xB8\xAD as D6 D0");
        check(utf8_to_code_page("", kWestern, out, message) && out.empty(), "nothing is nothing");
        check(utf8_to_code_page("plain", 0, out, message) && out == "plain", "ASCII in this computer's code page is itself");
    }
}

int run_input_edit_tests()
{
    test_a_new_panels_inputs_are_t3000s();
    test_the_field_names_are_the_payloads();
    test_a_label_is_changed_as_t3000_changes_one();
    test_a_label_no_other_input_has();
    test_a_label_must_fit_in_bytes_too();
    test_text_the_code_page_cannot_hold_is_refused();
    test_text_that_is_not_one_line_is_refused();
    test_a_full_label_is_changed_as_t3000_changes_one();
    test_a_full_label_no_other_point_has();
    test_auto_manual();
    test_the_filter();
    test_only_the_rows_t3000_shows_can_be_changed();
    test_a_range_is_chosen_by_its_number();
    test_a_fixed_range_is_refused();
    test_the_filter_of_a_digital_input_is_refused();
    test_the_columns_each_input_lets_be_changed();
    test_a_refused_edit_changes_nothing();
    test_changed_fields_name_the_columns();
    test_the_code_page_conversion();
    return 0;
}
