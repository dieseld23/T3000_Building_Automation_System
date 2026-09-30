#pragma once

// The bar of pages across the top of every page.
//
// T3000's screens, in the order of its toolbar (IDR_TOOLBAR_BACNET,
// T3000.rc:7001-7016) and named as its Control menu names them
// (T3000.rc:11566-11581), less Graphics, which T5000 leaves out; then
// Firmware, T3000's "Load firmware for a single device" (Tools,
// T3000.rc:11530). The pages built are links. The rest are named and
// dimmed, so the bar says what T5000 has and what it still has to do,
// rather than hiding it.
//
// Added to a page as it is served, rather than written into each one, so
// every page has the same bar and a new page gets it by being served
// through with_nav.

#include <string>
#include <vector>

namespace t5000::web
{
    struct NavPage
    {
        const char* name;   // T3000's name for the screen
        const char* path;   // T5000's route, or null when it is not built yet
        char        key;    // T3000's Alt shortcut, or 0 for none
    };

    // Every screen in the bar, in T3000's order.
    const std::vector<NavPage>& nav_pages();

    // The bar, with the page at `current`, one of nav_pages' paths, marked
    // as the one shown. A built page carries its T3000 shortcut as data-key.
    std::string nav_html(const std::string& current);

    // `page` with the bar's style before its </head>, and the bar before its
    // <header> with the script that opens a page on its shortcut: Alt+I for
    // Inputs, as in T3000. A page without exactly one of each is returned as
    // it is, with no bar: the self-test holds every page to having both.
    std::string with_nav(const char* page, const std::string& current);
}
