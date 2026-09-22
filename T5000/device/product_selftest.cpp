// Tests for the product identity model.
//
// The point of this module is that T3000 keeps two different product
// numbering schemes in one int, and the numbers overlap. Most of what follows
// asserts that the hazard is still real - because the day the numbers stop
// colliding is the day the elaborate comments in product.h become misleading.

#include "product.h"
#include "read_path.h"
#include "../testing/check.h"

#include <type_traits>

namespace
{
    using namespace t5000::device;
    using namespace t5000::testing;

    void test_the_two_schemes_cannot_be_mixed_up()
    {
        section("the two product id schemes are different types");

        // The whole design rests on this. If either of these ever becomes a
        // plain enum or an int, `bacnet_device_type`-style bugs become
        // writable again - silently.
        check(!std::is_same_v<ProductClassId, MiniType>,
              "ProductClassId and MiniType are distinct types");
        check(!std::is_convertible_v<ProductClassId, MiniType>,
              "a hardware id does not implicitly become a panel type");
        check(!std::is_convertible_v<MiniType, ProductClassId>,
              "a panel type does not implicitly become a hardware id");
        check(!std::is_convertible_v<ProductClassId, int>,
              "a product id does not decay to int, so it cannot be compared "
              "against a raw constant from the other scheme");
    }

    void test_the_collisions_are_still_real()
    {
        section("the two schemes still collide numerically");

        // These pairs share a number and mean completely different things.
        // They are the reason the types above must stay distinct. If one of
        // these checks ever fails, someone renumbered a scheme and the
        // documentation in product.h needs revisiting - it is not a licence
        // to merge the types.
        struct Collision { MiniType mini; ProductClassId product; const char* what; };
        const Collision collisions[] = {
            { MiniType::Tstat10,  ProductClassId::Tstat8,         "T3_TSTAT10 / PM_TSTAT8 (9)" },
            { MiniType::Bms,      ProductClassId::Tstat10,        "T3_BMS / PM_TSTAT10 (10)" },
            { MiniType::EspLw,    ProductClassId::T3IOA,          "T3_ESP_LW / PM_T3IOA (21)" },
            { MiniType::Ng3,      ProductClassId::T332AI,         "T3_NG3 / PM_T332AI (22)" },
            { MiniType::ThreeIic, ProductClassId::T3PT10,         "T3_3IIC / PM_T3PT10 (26)" },
            { MiniType::Tstat11,  ProductClassId::T3Performance,  "T3_TSTAT11 / PM_T3PERFORMANCE (27)" },
            { MiniType::Rmc1232,  ProductClassId::T36CT,          "T3_RMC1232 / PM_T36CT (29)" },
        };

        for (const auto& c : collisions)
            check(static_cast<uint8_t>(c.mini) == static_cast<uint8_t>(c.product), c.what);
    }

    void test_the_mirrored_values_still_agree()
    {
        section("the deliberately-mirrored ids still match");

        // The high MiniType values were chosen to equal their ProductClassId
        // counterparts, which is why the enum has gaps. Code in T3000 relies
        // on it - BacnetOutput.cpp:421-550 compares bacnet_device_type (a
        // mini_type) against PM_ constants and works precisely because of
        // this. If one side drifts, that code silently stops matching.
        struct Mirror { MiniType mini; ProductClassId product; const char* what; };
        const Mirror mirrors[] = {
            { MiniType::T322AI,      ProductClassId::T322AI,      "T322AI (43)" },
            { MiniType::T38AI8AO6DO, ProductClassId::T38AI8AO6DO, "T38AI8AO6DO (44)" },
            { MiniType::T3PT12,      ProductClassId::T3PT12,      "T3PT12 (46)" },
            { MiniType::T332AI,      ProductClassId::T332AIArm,   "T332AI / T332AI_ARM (53)" },
            { MiniType::T36CTA,      ProductClassId::T36CTA,      "T36CTA (95)" },
        };

        for (const auto& m : mirrors)
            check(static_cast<uint8_t>(m.mini) == static_cast<uint8_t>(m.product), m.what);
    }

    void test_private_data_set_agrees_with_read_path()
    {
        section("the capability table and read_path.cpp agree on which devices "
                "use private data");

        // Two independently written encodings of Bacnet_Private_Device
        // (global_function.cpp:13544). They must not drift: read_path.cpp
        // decides HOW to read, this table decides WHAT a product is, and a
        // disagreement means one of them is reading the wrong way.
        const auto table = known_products();
        int private_count = 0;

        for (int i = 0; i < table.count; i++)
        {
            const auto& p = table.entries[i];
            const bool by_read_path = is_private_data_device(static_cast<int>(p.id));
            const bool by_table     = p.path == DataPath::BacnetPrivateData;

            check(by_read_path == by_table, p.name);
            if (by_table) private_count++;
        }

        // Bacnet_Private_Device names exactly five products.
        check_eq(private_count, 5, "exactly five private-data devices");
    }

    void test_unknown_products_are_refused_not_guessed()
    {
        section("an unrecognised product gets nothing, not a default");

        // The dangerous failure is treating an unknown device as some default
        // product and reading it with the wrong register map. It must come
        // back with no screens and no data path.
        const auto& c = capabilities(static_cast<ProductClassId>(177));

        check(c.path == DataPath::Unsupported, "no data path");
        check(c.screens == Screen::None, "no screens");
        check(c.support == SupportState::Unverified, "not claimed as supported");
        check(!is_supported(static_cast<ProductClassId>(177)), "not supported");
        check(c.name != nullptr && c.name[0] != '\0', "still has a name to show");
    }

    void test_zero_points_and_unknown_points_are_different()
    {
        section("no points by design is not the same as unknown");

        // T3_BMS really has 0/0/0/0 (global_define.h:1402-1405) because a BMS
        // is comms-only. An unlisted type also yields zeros. Collapsing the
        // two would show an operator an empty point list for a device we
        // simply failed to recognise.
        const auto bms = point_counts(MiniType::Bms);
        check(bms.known, "BMS counts are known");
        check_eq(bms.inputs(), 0, "and are zero");
        check_eq(bms.outputs(), 0, "on both sides");

        const auto missing = point_counts(MiniType::EspSauter);
        check(!missing.known, "a type T3000 never gave counts for reads as unknown");
    }

    void test_counts_match_the_header()
    {
        section("point counts match global_define.h");

        // Spot-checks against the constants at global_define.h:1340-1426.
        // Transcription is exactly the kind of work that goes wrong quietly.
        const auto big = point_counts(MiniType::BigMiniPanel);
        check_eq(big.analog_inputs, 32, "BIG_MINIPANEL_IN_A");
        check_eq(big.analog_outputs, 12, "BIG_MINIPANEL_OUT_A");
        check_eq(big.digital_outputs, 12, "BIG_MINIPANEL_OUT_D");

        const auto cm5 = point_counts(MiniType::Cm5);
        check_eq(cm5.analog_inputs, 10, "CM5_MINIPANEL_IN_A");
        check_eq(cm5.digital_inputs, 8, "CM5_MINIPANEL_IN_D");
        check_eq(cm5.analog_outputs, 0, "CM5_MINIPANEL_OUT_A is zero");
        check_eq(cm5.digital_outputs, 10, "CM5_MINIPANEL_OUT_D");

        // The ARM panels share their originals' counts.
        const auto arm = point_counts(MiniType::MiniPanelArm);
        check_eq(arm.analog_inputs, big.analog_inputs, "MINIPANELARM matches BIG_MINIPANEL");
        check_eq(arm.digital_outputs, big.digital_outputs, "on outputs too");

        // FAN_MOUDLE_OUT_A - the misspelling is in the original header, and
        // transcribing it correctly means reading it carefully.
        const auto fan = point_counts(MiniType::FanModule);
        check_eq(fan.analog_inputs, 12, "FAN_MODULE_IN_A");
        check_eq(fan.analog_outputs, 1, "FAN_MOUDLE_OUT_A [sic]");
    }

    void test_screens_exclude_graphics_everywhere()
    {
        section("no product claims the graphics screen");

        // WINDOW_SCREEN is out of scope by decision. There is deliberately no
        // Screen::Graphics enumerator, so this asserts the bitmask never grew
        // one by accident - a 16th bit would mean someone added it.
        const auto table = known_products();
        for (int i = 0; i < table.count; i++)
        {
            const auto bits = static_cast<uint32_t>(table.entries[i].screens);
            check((bits & ~0x7FFFu) == 0, table.entries[i].name);
        }
    }

    void test_every_entry_is_presentable()
    {
        section("every table entry can be shown to an operator");

        const auto table = known_products();
        check(table.count > 0, "the table is not empty");

        for (int i = 0; i < table.count; i++)
        {
            const auto& p = table.entries[i];
            check(p.name != nullptr && p.name[0] != '\0', "has a name");
            check(to_string(p.path)[0] != '\0', "data path has a label");
            check(to_string(p.support)[0] != '\0', "support state has a label");

            // A product with no data path must not advertise screens: the UI
            // would offer a tab that cannot possibly load.
            if (p.path == DataPath::Unsupported)
                check(p.screens == Screen::None, "unsupported products offer no screens");
        }
    }

    void test_tstat10_variants_are_masks_not_products()
    {
        section("the TSTAT10 variants are row masks, not separate products");

        // This is the finding that four "unimplemented" verdicts were wrong
        // about. T3_OEM, T3_OEM_12I, T3_TSTAT10 and T3_TSTAT11 have no point
        // counts and appear in neither init chain - not because they are
        // unfinished, but because they are one PM_TSTAT10 with rows hidden
        // (BacnetInput.cpp:1662-1683).
        struct Variant { MiniType type; int first; int last; const char* what; };
        const Variant variants[] = {
            { MiniType::Oem,     13, 17, "T3_OEM hides rows 13-17" },
            { MiniType::Oem12I,  17, 21, "T3_OEM_12I hides rows 17-21" },
            { MiniType::Tstat10,  9, 12, "T3_TSTAT10 hides rows 9-12" },
            { MiniType::Tstat11,  9, 12, "T3_TSTAT11 hides rows 9-12" },
        };

        for (const auto& v : variants)
        {
            const auto info = mini_type_info(v.type);
            check(info.support == MiniTypeSupport::RowMaskOnTstat10, v.what);
            check_eq(info.hidden_row_first, v.first, "first hidden row");
            check_eq(info.hidden_row_last, v.last, "last hidden row");

            // They must NOT have their own counts - if one ever gains them,
            // the mask model is wrong for it and this needs revisiting.
            check(!point_counts(v.type).known,
                  "a masked variant has no counts of its own");
        }
    }

    void test_unimplemented_types_are_named_honestly()
    {
        section("types T3000 never implemented say so");

        // Supporting these is new product development, not porting. The tool
        // must not imply otherwise by quietly showing an empty screen.
        const MiniType never[] = {
            MiniType::EspTransducer, MiniType::EspTstat9,
            MiniType::EspSauter, MiniType::Airlab,
        };

        for (auto t : never)
        {
            const auto info = mini_type_info(t);
            check(info.support == MiniTypeSupport::NotImplemented, to_string(t));
            check(info.note != nullptr, "and says why");
        }
    }

    void test_partial_types_say_which_half_works()
    {
        section("partially implemented types name the missing direction");

        check(mini_type_info(MiniType::FanModule).support == MiniTypeSupport::InputsOnly,
              "Fan Module: inputs only");
        check(mini_type_info(MiniType::ThreeIic).support == MiniTypeSupport::InputsOnly,
              "3IIC: inputs only");
        check(mini_type_info(MiniType::EspLw).support == MiniTypeSupport::OutputsOnly,
              "ESP Lighting: outputs only");

        // A normal panel must NOT be flagged as partial.
        check(mini_type_info(MiniType::BigMiniPanel).support == MiniTypeSupport::CountsFromInitChain,
              "Big MiniPanel is fully in the init chain");
        check(mini_type_info(MiniType::Cm5).support == MiniTypeSupport::CountsFromInitChain,
              "CM5 is too - its output case is at BacnetOutput.cpp:262, which an "
              "earlier partial-range search missed");
    }

    void test_no_duplicate_entries()
    {
        section("no product appears twice");

        // A duplicate would make capabilities() return whichever came first,
        // which is a silent and very confusing way to get the wrong answer.
        const auto table = known_products();
        for (int i = 0; i < table.count; i++)
            for (int j = i + 1; j < table.count; j++)
                check(table.entries[i].id != table.entries[j].id, table.entries[i].name);
    }
}

int run_product_tests()
{
    test_the_two_schemes_cannot_be_mixed_up();
    test_the_collisions_are_still_real();
    test_the_mirrored_values_still_agree();
    test_private_data_set_agrees_with_read_path();
    test_unknown_products_are_refused_not_guessed();
    test_zero_points_and_unknown_points_are_different();
    test_counts_match_the_header();
    test_screens_exclude_graphics_everywhere();
    test_every_entry_is_presentable();
    test_tstat10_variants_are_masks_not_products();
    test_unimplemented_types_are_named_honestly();
    test_partial_types_say_which_half_works();
    test_no_duplicate_entries();
    return 0;
}
