// Tests for the product identity model.
//
// The point of this module is that T3000 keeps two different product
// numbering schemes in one int, and the numbers overlap. Most of what follows
// asserts that the hazard is still real - because the day the numbers stop
// colliding is the day the elaborate comments in product.h become misleading.

#include "product.h"
#include "read_path.h"
#include "../testing/check.h"

#include <string.h>

#include <string>
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

    void test_tstat10_variants_are_variants_not_products()
    {
        section("the TSTAT10 variants are one product with some ranges fixed, not separate products");

        // This is the finding that four "unimplemented" verdicts were wrong
        // about. T3_OEM, T3_OEM_12I, T3_TSTAT10 and T3_TSTAT11 have no point
        // counts and appear in neither init chain - not because they are
        // unfinished, but because they are one PM_TSTAT10 on which some
        // inputs' ranges cannot be edited (BacnetInput.cpp:1662-1683, in the
        // grid's click handler). Those rows are still shown.
        struct Variant { MiniType type; int first; int last; const char* what; };
        const Variant variants[] = {
            { MiniType::Oem,     13, 17, "T3_OEM fixes the range of rows 13-17" },
            { MiniType::Oem12I,  17, 21, "T3_OEM_12I fixes rows 17-21" },
            { MiniType::Tstat10,  9, 12, "T3_TSTAT10 fixes rows 9-12" },
            { MiniType::Tstat11,  9, 12, "T3_TSTAT11 fixes rows 9-12" },
        };

        for (const auto& v : variants)
        {
            const auto info = mini_type_info(v.type);
            check(info.support == MiniTypeSupport::VariantOfTstat10, v.what);
            check_eq(info.fixed_range_first, v.first, "first row with a fixed range");
            check_eq(info.fixed_range_last, v.last, "last row with a fixed range");

            // They must NOT have their own counts - if one ever gains them,
            // the variant model is wrong for it and this needs revisiting.
            check(!point_counts(v.type).known,
                  "a variant has no counts of its own");
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

    void test_unconfigured_panel_does_not_borrow_cm5_counts()
    {
        section("mini_type 0 on non-CM5 hardware yields unknown, not CM5's counts");

        // This is a regression test for a bug that was written, built, and
        // served from /api/device before being caught by actually running it:
        // a TSTAT10 reporting mini_type 0 came back claiming CM5's 10/8/0/10,
        // so the page showed eighteen confident inputs for a panel that had
        // never said it had any. Compiling proved nothing here.
        const auto unconfigured = resolve_panel(ProductClassId::Tstat10, 0);
        check(!unconfigured.resolved, "a TSTAT10 with mini_type 0 is unresolved");
        check(!unconfigured.counts.known, "and reports NO point counts");
        check_eq(unconfigured.counts.inputs(), 0, "not CM5's 18");
        check(unconfigured.reason != nullptr && unconfigured.reason[0] != '\0',
              "and says why");

        // But a real CM5 reporting 0 is correct - 0 IS its value - so it must
        // still resolve. Refusing every zero would break the one product for
        // which zero is the right answer.
        const auto cm5 = resolve_panel(ProductClassId::Cm5, 0);
        check(cm5.resolved, "a CM5 with mini_type 0 does resolve");
        check(cm5.counts.known, "and has counts");
        check_eq(cm5.counts.analog_inputs, 10, "CM5_MINIPANEL_IN_A");
        check_eq(cm5.counts.digital_inputs, 8, "CM5_MINIPANEL_IN_D");

        // A non-zero mini_type is taken at its word regardless of hardware:
        // that is what T3000 does once the != 0 guard passes.
        const auto configured = resolve_panel(ProductClassId::Tstat10,
                                              static_cast<int>(MiniType::BigMiniPanel));
        check(configured.resolved, "a set mini_type resolves");
        check_eq(configured.counts.analog_inputs, 32, "and gives that panel's counts");
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

    void test_a_t3_oem_is_a_tstat10()
    {
        section("a T3-OEM is a TSTAT10 set up as panel type T3_OEM");

        // The report that added models: T3-OEM was missing from the Add
        // device list, which listed products. T3000 lists it as pid
        // PM_TSTAT10, sub_pid T3_OEM (global_function.cpp:12127-12135).
        const Model* oem = find_model(ProductClassId::Tstat10, 11);
        if (require(oem != nullptr, "a TSTAT10 with panel type 11 is a model"))
        {
            check_streq(oem->name, "T3-OEM", "  named T3-OEM, as T3000's Settings page names it");
            check(oem->type == MiniType::Oem, "  panel type T3_OEM");
        }
        check_streq(panel_name(ProductClassId::Tstat10, MiniType::Oem), "T3-OEM",
                    "the panel of a TSTAT10 set up so goes by that name");
        check_streq(panel_name(ProductClassId::MiniPanelArm, MiniType::Oem), "OEM",
                    "  but not the same panel type on a product T3000 does not pair it with");
        check_streq(panel_name(ProductClassId::Cm5, MiniType::Cm5), "CM5",
                    "panel type 0 on a CM5 is named CM5, as T3000 names it");
        check_streq(panel_name(ProductClassId::Tstat10, MiniType::NotSet), to_string(MiniType::NotSet),
                    "  but on anything else keeps saying it may be unset");

        const Model* bb = find_model(ProductClassId::MiniPanelArm, static_cast<int>(MiniType::MiniPanelArm));
        check(bb && strcmp(bb->name, "T3-BB") == 0, "a MiniPanel ARM with panel type 5 is a T3-BB");

        // T3000's list gives T3_3IIC sub_pid T3_NG3 (global_function.cpp:12113);
        // the table corrects it, and T5000Conformance checks that it still
        // needs to.
        const Model* iic = find_model(ProductClassId::Esp32T3Series, static_cast<int>(MiniType::ThreeIic));
        check(iic && strcmp(iic->name, "T3-3IIC") == 0, "T3-3IIC has panel type T3_3IIC");
        const Model* ng2 = find_model(ProductClassId::Esp32T3Series, static_cast<int>(MiniType::Ng3));
        check(ng2 && strcmp(ng2->name, "T3-NG2") == 0, "  and T3_NG3 is T3-NG2's alone");
    }

    void test_find_model_names_only_pairs_t3000_names()
    {
        section("find_model finds only the pairs T3000 names");

        check(find_model(ProductClassId::Tstat10, 0) == nullptr, "panel type 0 is no model: it is \"not set\"");
        check(find_model(ProductClassId::Cm5, 0) == nullptr, "  not even on a CM5");
        check(find_model(ProductClassId::Tstat10, static_cast<int>(MiniType::Tb11I)) == nullptr,
              "a panel type T3000 does not pair with the product is no model of it");
        check(find_model(ProductClassId::MiniPanelArm, 11) == nullptr, "a T3-OEM is not a MiniPanel ARM");
        check(find_model(ProductClassId::Tstat10, 256 + 11) == nullptr, "a value past a byte is not cut down to one");
        check(find_model(ProductClassId::Tstat10, -245) == nullptr, "  nor a negative one");
    }

    void test_every_model_is_presentable()
    {
        section("every model is a product T5000 knows, once, under its own name");

        const ModelTable models = known_models();
        check_eq(models.count, 15, "T3000's list, less its Custom Device");

        for (int i = 0; i < models.count; i++)
        {
            const Model& m = models.entries[i];
            check(m.name && m.name[0] != '\0', "a model has a name");
            check(static_cast<int>(m.type) != 0, (std::string(m.name) + ": its panel type is not 0").c_str());
            check(capabilities(m.product).id == m.product,
                  (std::string(m.name) + ": its product is in the capability table").c_str());
            check(m.product != ProductClassId::ThirdPartyDevice,
                  (std::string(m.name) + ": and is not a third-party device").c_str());
            check(find_model(m.product, static_cast<int>(m.type)) == &m,
                  (std::string(m.name) + ": find_model finds this entry").c_str());

            for (int j = i + 1; j < models.count; j++)
            {
                const Model& n = models.entries[j];
                check(strcmp(m.name, n.name) != 0, (std::string(m.name) + ": no other model has its name").c_str());
                check(m.product != n.product || m.type != n.type,
                      (std::string(m.name) + ": no other model is the same pair").c_str());
            }
        }
    }

    void test_a_chosen_panel_is_not_said_to_be_read()
    {
        section("a panel type chosen by hand is not described as read");

        const int cases[][2] = {
            { static_cast<int>(ProductClassId::Tstat10), 11 },
            { static_cast<int>(ProductClassId::Tstat10), 0 },
            { static_cast<int>(ProductClassId::Cm5), 0 },
            { static_cast<int>(ProductClassId::MiniPanelArm), 5 },
        };
        for (const auto& c : cases)
        {
            const auto product = static_cast<ProductClassId>(c[0]);
            const PanelResolution chosen = resolve_chosen_panel(product, c[1]);
            const PanelResolution read   = resolve_panel(product, c[1]);
            const std::string what = std::string(to_string(product)) + " with panel type " + std::to_string(c[1]);

            check(chosen.resolved == read.resolved && chosen.type == read.type,
                  (what + ": resolves as it would if read").c_str());
            check(chosen.counts.known == read.counts.known &&
                      chosen.counts.analog_inputs == read.counts.analog_inputs,
                  (what + ": with the same counts").c_str());
            const std::string reason = chosen.reason;
            check(reason.find("added by hand") != std::string::npos, (what + ": says it was chosen").c_str());
            check(reason.find("read from") == std::string::npos && reason.find("reports") == std::string::npos,
                  (what + ": and claims no reading").c_str());
        }
        check(std::string(resolve_chosen_panel(ProductClassId::Tstat10, 0).reason).find("no model") !=
                  std::string::npos,
              "a product added with its model not known says none was chosen");
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
    test_tstat10_variants_are_variants_not_products();
    test_unimplemented_types_are_named_honestly();
    test_partial_types_say_which_half_works();
    test_unconfigured_panel_does_not_borrow_cm5_counts();
    test_no_duplicate_entries();
    test_a_t3_oem_is_a_tstat10();
    test_find_model_names_only_pairs_t3000_names();
    test_every_model_is_presentable();
    test_a_chosen_panel_is_not_said_to_be_read();
    return 0;
}
