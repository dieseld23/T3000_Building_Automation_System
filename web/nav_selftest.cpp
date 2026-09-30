// Tests for the bar of pages.
//
// Every page T5000 serves is run through with_nav here, so a page that loses
// its <header>, or gains a second, fails the build rather than going out
// without a bar.

#include "nav.h"
#include "devices_page.h"
#include "firmware_page.h"
#include "inputs_page.h"
#include "outputs_page.h"
#include "variables_page.h"
#include "../testing/check.h"

#include <string.h>

namespace
{
    using namespace t5000::web;
    using namespace t5000::testing;

    int count(const std::string& s, const std::string& what)
    {
        int n = 0;
        for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1))
            n++;
        return n;
    }

    void test_the_pages_are_t3000s_in_its_order()
    {
        section("the bar holds T3000's screens in its toolbar's order, less Graphics, then Firmware");

        const auto& pages = nav_pages();
        const char* const names[] = { "Devices", "Inputs", "Outputs", "Variables", "Programs", "Loops",
                                      "Schedules", "Holidays", "Trend Logs", "Alarms", "Remote Points",
                                      "Configuration", "Firmware" };
        if (!require(pages.size() == sizeof(names) / sizeof(names[0]), "twelve screens and Firmware"))
            return;
        for (size_t i = 0; i < pages.size(); i++)
            check_streq(pages[i].name, names[i], "in order");

        for (const NavPage& p : pages)
            check(strcmp(p.name, "Graphics") != 0, "Graphics is left out, as T5000 leaves it out");

        int built = 0;
        for (const NavPage& p : pages)
            built += p.path != nullptr;
        check_eq(built, 5, "five are built: Devices, Inputs, Outputs, Variables and Firmware");
        check_streq(pages.back().path, "/firmware", "Firmware, at /firmware");
        check_eq((int)pages.back().key, 0, "  with no Alt shortcut: T3000's menu item has none");
    }

    void test_built_pages_are_links_and_the_rest_are_not()
    {
        section("a built page is a link, and one not built yet is named but not linked");

        const std::string bar = nav_html("/inputs");
        check(bar.find("<a href=\"/\"") != std::string::npos, "Devices is a link");
        check(bar.find("<a href=\"/variables\"") != std::string::npos, "Variables is a link");
        check(bar.find("<span title=\"Not in T5000 yet\">Programs</span>") != std::string::npos,
              "Programs is named, not linked");
        check_eq(count(bar, "<a "), 5, "five links");
        check(bar.find("<a href=\"/firmware\">Firmware</a>") != std::string::npos, "Firmware is a link");
        check_eq(count(bar, "<span "), 8, "eight not built");
    }

    void test_the_page_shown_is_marked()
    {
        section("the page shown is marked, and only that one");

        const std::string bar = nav_html("/outputs");
        check_eq(count(bar, "aria-current"), 1, "one page is marked");
        check(bar.find("<a href=\"/outputs\" aria-current=\"page\"") != std::string::npos, "Outputs");

        check(nav_html("/").find("<a href=\"/\" aria-current=\"page\"") != std::string::npos,
              "Devices, at /");
        check_eq(count(nav_html("/programs"), "aria-current"), 0, "a page not in the bar marks none");
    }

    void test_shortcuts_are_t3000s()
    {
        section("a built page carries its T3000 shortcut, and the page listens for it");

        const std::string bar = nav_html("/");
        check(bar.find("href=\"/inputs\" data-key=\"i\" title=\"Alt+I, as in T3000\"") != std::string::npos,
              "Alt+I is Inputs");
        check(bar.find("data-key=\"o\"") != std::string::npos, "Alt+O is Outputs");
        check(bar.find("data-key=\"v\"") != std::string::npos, "Alt+V is Variables");
        check_eq(count(bar, "data-key"), 3, "and a page not built has none");

        const std::string page = with_nav(kOutputsPage, "/outputs");
        check_eq(count(page, "addEventListener(\"keydown\", function (e) {\n  if (!e.altKey"), 1,
                 "the page has the shortcut handler once");
        check(page.find("dialog[open], .scrim:not([hidden])") != std::string::npos,
              "which holds off while a dialog or sheet is open");
    }

    void test_every_page_gets_the_bar()
    {
        section("every page is served with the bar, above its header, and its style");

        const struct
        {
            const char* page;
            const char* path;
            const char* name;
        } served[] = {
            { kDevicesPage, "/", "Devices" },
            { kInputsPage, "/inputs", "Inputs" },
            { kOutputsPage, "/outputs", "Outputs" },
            { kVariablesPage, "/variables", "Variables" },
            { kFirmwarePage, "/firmware", "Firmware" },
        };

        for (const auto& s : served)
        {
            const std::string raw = s.page;
            const std::string what = std::string(s.name) + ": ";
            check_eq(count(raw, "<header>"), 1, (what + "one <header>").c_str());
            check_eq(count(raw, "</head>"), 1, (what + "one </head>").c_str());
            check_eq(count(raw, "<nav"), 0, (what + "no bar of its own").c_str());

            const std::string page = with_nav(s.page, s.path);
            check_eq(count(page, "<nav class=\"pages\""), 1, (what + "the bar, once").c_str());
            check(page.find("<nav class=\"pages\"") < page.find("<header>"), (what + "above the header").c_str());
            check(page.find("nav.pages{") < page.find("</head>"), (what + "its style in the head").c_str());
            check_eq(count(page, "aria-current=\"page\""), 1, (what + "with the page marked").c_str());
            check(page.find(std::string("href=\"") + s.path + "\" aria-current") != std::string::npos,
                  (what + "as itself").c_str());
        }
    }

    void test_a_page_without_one_header_is_left_alone()
    {
        section("a page without exactly one header is served as it is");

        const char* none = "<html><head></head><body>x</body></html>";
        check_streq(with_nav(none, "/").c_str(), none, "no header: no bar");
        const char* two = "<html><head></head><body><header></header><header></header></body></html>";
        check_streq(with_nav(two, "/").c_str(), two, "two headers: no bar");
        const char* headless = "<header></header>";
        check_streq(with_nav(headless, "/").c_str(), headless, "no head: no bar");
        check_streq(with_nav(nullptr, "/").c_str(), "", "and nothing is nothing");
    }
}

int run_nav_tests()
{
    test_the_pages_are_t3000s_in_its_order();
    test_built_pages_are_links_and_the_rest_are_not();
    test_the_page_shown_is_marked();
    test_shortcuts_are_t3000s();
    test_every_page_gets_the_bar();
    test_a_page_without_one_header_is_left_alone();
    return 0;
}
