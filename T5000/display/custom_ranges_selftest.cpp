// Tests for the custom range names, against global_function.cpp:4316-4368
// and :5544-5603, and for the SplitCStringA port they are split with.
//
// Every name here is ASCII, so the results do not depend on this machine's
// code page. The one rule that can only be seen with a double-byte code page
// - an off-name with no NUL running on into the on-name and still being
// under 12 characters - is described at state_name_wide() rather than
// tested.

#include "custom_ranges.h"
#include "device_text.h"
#include "../testing/check.h"

#include <string.h>
#include <vector>

namespace
{
    using namespace t5000::display;
    using namespace t5000::testing;
    namespace w = t5000::wire;

    using Bytes = std::vector<uint8_t>;

    // One Str_Units_element. Names are copied without their NUL, so a
    // 12-character name fills its field with none.
    Bytes unit(uint8_t direct, const char* off, const char* on)
    {
        Bytes u(w::kCustomUnitWireSize, 0);
        u[w::custom_unit_at::direct] = direct;
        memcpy(&u[w::custom_unit_at::off], off, strlen(off) < 12 ? strlen(off) : 12);
        memcpy(&u[w::custom_unit_at::on], on, strlen(on) < 12 ? strlen(on) : 12);
        return u;
    }

    // One Str_table_point: a name, then data that is never NUL, so a name
    // with no NUL of its own runs on into it as T3000's strlen does.
    Bytes table(const uint8_t* name, size_t name_bytes)
    {
        Bytes t(w::kAnalogTableWireSize, 0x11);
        memset(&t[0], 0, w::analog_table_at::name_length);
        memcpy(&t[0], name, name_bytes);
        return t;
    }

    Bytes table(const char* name)
    {
        return table((const uint8_t*)name, strlen(name));
    }

    Bytes concat(const std::vector<Bytes>& parts)
    {
        Bytes all;
        for (const Bytes& p : parts)
            all.insert(all.end(), p.begin(), p.end());
        return all;
    }

    size_t parts_of(const wchar_t* s)
    {
        return split_like_t3000(s, L'/').size();
    }

    void test_split_like_t3000()
    {
        section("splitting as SplitCStringA splits, which is not a plain split");

        const std::vector<std::wstring> two = split_like_t3000(L"Off/On", L'/');
        check(two.size() == 2 && two[0] == L"Off" && two[1] == L"On", "Off/On is Off and On");

        const std::vector<std::wstring> spaced = split_like_t3000(L" Off / On ", L'/');
        check(spaced.size() == 2 && spaced[0] == L"Off" && spaced[1] == L"On", "each part is trimmed");

        check(parts_of(L"a//b") == 2, "a doubled token is one token");
        check(parts_of(L"/a/b") == 2, "a leading token is skipped");
        check(parts_of(L"Off/") == 1, "a trailing token adds nothing");
        check(parts_of(L"/On") == 1, "nor does a leading one before a single part");
        check(parts_of(L"/") == 0, "a token alone is no parts at all");
        check(parts_of(L"a/b/c") == 3, "three parts are three");
        check(parts_of(L"Off/ /On") == 3, "a part that trims to nothing still counts");

        const std::vector<std::wstring> none = split_like_t3000(L" OffOn ", L'/');
        check(none.size() == 1 && none[0] == L" OffOn ", "no token: the whole string, untrimmed");
    }

    void test_state_names()
    {
        section("an off or on name is cut out as T3000 cuts it");

        const Bytes u = unit(0, "Closed", "Tripped");
        check(digital_state_name(u.data(), w::custom_unit_at::off) == "Closed", "off name");
        check(digital_state_name(u.data(), w::custom_unit_at::on) == "Tripped", "on name");

        const Bytes eleven = unit(0, "ABCDEFGHIJK", "On");
        check(digital_state_name(eleven.data(), w::custom_unit_at::off) == "ABCDEFGHIJK",
              "11 characters is kept");

        // Twelve fills the field with no NUL: T3000's strlen runs on into the
        // on-name and the result is too long either way.
        const Bytes twelve = unit(0, "ABCDEFGHIJKL", "");
        check(digital_state_name(twelve.data(), w::custom_unit_at::off).empty(), "12 characters is dropped");

        const Bytes on_twelve = unit(0, "Off", "ABCDEFGHIJKL");
        check(digital_state_name(on_twelve.data(), w::custom_unit_at::on).empty(),
              "12 characters in the on-name is dropped too");
        check(digital_state_name(on_twelve.data(), w::custom_unit_at::off) == "Off",
              "without touching the off-name");
    }

    void test_digital_ranges()
    {
        section("the eight custom digital ranges, with the direction swap");

        const Bytes reply = concat({
            unit(0, "Closed", "Tripped"),         // direct: as sent
            unit(1, "Closed", "Tripped"),         // reversed
            unit(2, "Closed", "Tripped"),         // any non-zero reverses
            unit(0, "A/B", "C"),                  // splits into three
            unit(0, "", ""),                      // nothing
            unit(0, "ABCDEFGHIJKL", "On"),        // off dropped, on kept
            unit(0, " Lo ", " Hi "),              // trimmed for the value only
            unit(0, "Stop", "Run"),
        });

        CustomRanges r;
        if (!require(take_digital_ranges(reply.data(), reply.size(), 0, 8, r), "a reply for all eight is taken"))
            return;

        check(r.digital_known, "and marks the names known");

        check(r.digital[0].text == "Closed/Tripped", "direct 0: off/on as sent");
        check(r.digital[0].has_states && r.digital[0].off == "Closed" && r.digital[0].on == "Tripped",
              "  and split back into its two states");
        check(r.digital[1].text == "Tripped/Closed", "direct 1: swapped");
        check(r.digital[1].off == "Tripped", "  so control 0 shows the stored on-name");
        check(r.digital[2].text == "Tripped/Closed", "direct 2: swapped as well");

        check(r.digital[3].text == "A/B/C", "a name containing / is shown in full");
        check(!r.digital[3].has_states, "  but gives no states, so no value");
        check(r.digital[4].text == "/" && !r.digital[4].has_states, "two empty names: \"/\", no states");
        check(r.digital[5].text == "/On" && !r.digital[5].has_states, "a dropped off-name leaves \"/On\"");

        check(r.digital[6].text == " Lo / Hi ", "the Range column keeps the spaces");
        check(r.digital[6].off == "Lo" && r.digital[6].on == "Hi", "the states are trimmed");
        check(r.digital[7].text == "Stop/Run", "the last unit");
    }

    void test_digital_ranges_known_only_when_complete()
    {
        section("the names count as known only once the reply reaches the last unit");

        const Bytes three = concat({ unit(0, "a", "b"), unit(0, "c", "d"), unit(0, "e", "f") });

        CustomRanges first_three;
        check(take_digital_ranges(three.data(), three.size(), 0, 3, first_three), "units 0-2 are taken");
        check(!first_three.digital_known, "but do not make the names known");
        check(first_three.digital[2].text == "e/f", "  though they are stored");

        CustomRanges last_three;
        take_digital_ranges(three.data(), three.size(), 5, 3, last_three);
        check(last_three.digital_known, "units 5-7 end at the last one, which is T3000's test");
        check(last_three.digital[5].text == "a/b", "  and land at 5");
    }

    void test_digital_ranges_refuse_bad_input()
    {
        section("a custom-unit reply of the wrong shape changes nothing");

        const Bytes eight = concat({ unit(0, "a", "b"), unit(0, "a", "b"), unit(0, "a", "b"), unit(0, "a", "b"),
                                     unit(0, "a", "b"), unit(0, "a", "b"), unit(0, "a", "b"), unit(0, "a", "b") });
        CustomRanges r;
        check(!take_digital_ranges(eight.data(), eight.size() - 1, 0, 8, r), "a byte short");
        check(!take_digital_ranges(eight.data(), eight.size(), 1, 8, r), "past the eighth unit");
        check(!take_digital_ranges(eight.data(), 0, 0, 0, r), "no units");
        check(!r.digital_known && r.digital[0].text.empty(), "and nothing was stored");
    }

    void test_analog_table_names()
    {
        section("a table's name, with its precision marker");

        check(analog_table_name(table("kPa").data()) == "kPa", "a short name");
        check(analog_table_name(table("ABCDEFGHI").data()) == "ABCDEFGHI",
              "nine characters and no NUL: all nine, though the data follows");

        // 0xEF in the ninth byte marks precision; it is not text.
        const uint8_t kpa_marked[] = { 'k', 'P', 'a', 0, 0, 0, 0, 0, 0xEF };
        check(analog_table_name(table(kpa_marked, 9).data()) == "kPa", "a marked short name");

        const uint8_t eight_marked[] = { 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 0xEF };
        const Bytes eight = table(eight_marked, 9);

        // The name runs on into the data unless there is a NUL within 22
        // bytes. Put one at byte 21: strlen is 21, not too long, so the
        // marker is honoured.
        Bytes just_short = eight;
        memset(&just_short[9], 'x', 12);
        just_short[21] = 0;
        check(analog_table_name(just_short.data()) == "ABCDEFGH", "strlen 21: the marker is honoured");

        // With the NUL one byte later, strlen is 22: too long. T3000 zeroes
        // its stored copy, the marker test then fails, and all nine bytes -
        // marker included - become the name.
        Bytes too_long = eight;
        memset(&too_long[9], 'x', 13);
        too_long[22] = 0;
        check(analog_table_name(too_long.data()) == acp_to_utf8(eight_marked, 9),
              "strlen 22: too long, so the marker is read as text");
        check(analog_table_name(too_long.data()) != "ABCDEFGH", "  which is not the eight-character name");
    }

    void test_analog_tables()
    {
        section("the five analog tables, read as T3000 reads them: 0-3, then 4");

        const Bytes first_four = concat({ table("degC"), table("kPa"), table("RH"), table("ppm") });
        CustomRanges r;
        check(take_analog_tables(first_four.data(), first_four.size(), 0, 4, r), "tables 0-3");
        check(r.analog_known[0] && r.analog_known[3] && !r.analog_known[4], "four known, the fifth not yet");
        check(r.analog[1] == "kPa", "table 2 is kPa");

        const Bytes fifth = table("Lux");
        check(take_analog_tables(fifth.data(), fifth.size(), 4, 1, r), "table 4");
        check(r.analog_known[4] && r.analog[4] == "Lux", "the fifth");

        CustomRanges bad;
        check(!take_analog_tables(fifth.data(), fifth.size(), 5, 1, bad), "past the fifth table");
        check(!take_analog_tables(fifth.data(), fifth.size() + 1, 4, 1, bad), "a byte long");
        check(!bad.analog_known[4], "and nothing was stored");
    }

    void test_device_text()
    {
        section("device text is cut at its first NUL or its field, then converted");

        const uint8_t padded[] = { 'I', 'N', '1', 0, 'x', 'x' };
        check(acp_to_utf8(padded, sizeof(padded)) == "IN1", "stops at the NUL");
        const uint8_t full[] = { 'A', 'B', 'C' };
        check(acp_to_utf8(full, 2) == "AB", "or at the field's end");
        check(acp_to_utf8(nullptr, 5).empty(), "nothing from nothing");
        check(length_to_nul(padded, sizeof(padded)) == 3, "length to the NUL");
        // Built from code units: this project's compiler settings turn
        // L"°" into two units, 00C2 00B0, as if it were UTF-8 bytes.
        const std::wstring degrees_c = std::wstring(1, (wchar_t)0x00B0) + L"C";
        check(wide_to_utf8(degrees_c) == "\xC2\xB0" "C", "UTF-16 to UTF-8");
        check(utf8_to_wide("\xC2\xB0" "C") == degrees_c, "and back");
    }
}

int run_custom_ranges_tests()
{
    test_device_text();
    test_split_like_t3000();
    test_state_names();
    test_digital_ranges();
    test_digital_ranges_known_only_when_complete();
    test_digital_ranges_refuse_bad_input();
    test_analog_table_names();
    test_analog_tables();
    return 0;
}
