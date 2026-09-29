#include "nav.h"

#include <ctype.h>

namespace t5000::web
{
    namespace
    {
        // In the colours each page defines on :root, so it follows the
        // page into dark mode. A strip of its own above the page's header,
        // which scrolls sideways on a narrow screen rather than wrapping.
        const char* const kNavStyle =
            "<style>\n"
            "  nav.pages{display:flex;gap:2px;padding:0 10px;flex:none;overflow-x:auto;white-space:nowrap;\n"
            "            background:var(--bg);border-bottom:1px solid var(--border);font-size:12.5px;\n"
            "            scrollbar-width:thin;scrollbar-color:var(--border) transparent}\n"
            "  nav.pages a,nav.pages span{padding:6px 10px 5px;border-bottom:2px solid transparent}\n"
            "  nav.pages a{color:var(--text);text-decoration:none}\n"
            "  nav.pages a:hover{color:var(--accent)}\n"
            "  nav.pages a[aria-current=page]{color:var(--accent);border-bottom-color:var(--accent);font-weight:600}\n"
            "  nav.pages span{color:var(--dim);opacity:.55;cursor:default}\n"
            "</style>\n";

        // T3000's Alt shortcuts, by a handler rather than accesskey, which
        // browsers bind to different modifiers (Alt, Alt+Shift, Ctrl+Alt).
        // Not while a dialog or the Inputs page's sheet is open, or the
        // cursor is in a field: that would leave the page with an edit half
        // made.
        const char* const kNavScript =
            "<script>\n"
            "document.addEventListener(\"keydown\", function (e) {\n"
            "  if (!e.altKey || e.ctrlKey || e.metaKey || e.shiftKey || e.repeat) return;\n"
            "  var k = (e.key || \"\").toLowerCase();\n"
            "  if (!/^[a-z]$/.test(k)) return;\n"
            "  var a = document.querySelector('nav.pages a[data-key=\"' + k + '\"]');\n"
            "  if (!a) return;\n"
            "  var f = document.activeElement;\n"
            "  if (document.querySelector(\"dialog[open], .scrim:not([hidden])\") ||\n"
            "      (f && /^(INPUT|TEXTAREA|SELECT)$/.test(f.tagName))) return;\n"
            "  e.preventDefault();\n"
            "  if (a.getAttribute(\"aria-current\") !== \"page\") location.href = a.href;\n"
            "});\n"
            "</script>\n";

        // Where `what` is in `s`, or npos when it is not there or is there
        // more than once.
        size_t find_once(const std::string& s, const char* what)
        {
            const size_t at = s.find(what);
            if (at == std::string::npos || s.find(what, at + 1) != std::string::npos)
                return std::string::npos;
            return at;
        }
    }

    const std::vector<NavPage>& nav_pages()
    {
        static const std::vector<NavPage> pages = {
            { "Devices", "/", 0 },   // T3000's Main
            { "Inputs", "/inputs", 'i' },
            { "Outputs", "/outputs", 'o' },
            { "Variables", "/variables", 'v' },
            { "Programs", nullptr, 'p' },
            { "Loops", nullptr, 'l' },
            { "Schedules", nullptr, 's' },
            { "Holidays", nullptr, 'h' },
            { "Trend Logs", nullptr, 't' },
            { "Alarms", nullptr, 'a' },
            // T3000's menu gives it Alt-T as well, which Trend Logs has.
            { "Remote Points", nullptr, 0 },
            { "Configuration", nullptr, 'e' },
            // Not a screen of T3000's toolbar: its Tools menu's "Load
            // firmware for a single device" (T3000.rc:11530). The menu
            // says [Ctrl+F2], but T3000 opens it on Ctrl+R
            // (MainFrm.cpp:6827), and its Ctrl+F2 offers to reset a device
            // to its factory defaults (:6785). The bar takes neither.
            { "Firmware", "/firmware", 0 },
        };
        return pages;
    }

    std::string nav_html(const std::string& current)
    {
        std::string out = "<nav class=\"pages\" aria-label=\"Pages\">";
        for (const NavPage& p : nav_pages())
        {
            if (!p.path)
            {
                out += "<span title=\"Not in T5000 yet\">";
                out += p.name;
                out += "</span>";
                continue;
            }
            out += "<a href=\"";
            out += p.path;
            out += '"';
            if (current == p.path)
                out += " aria-current=\"page\"";
            if (p.key)
            {
                out += " data-key=\"";
                out += p.key;
                out += "\" title=\"Alt+";
                out += (char)toupper((unsigned char)p.key);
                out += ", as in T3000\"";
            }
            out += '>';
            out += p.name;
            out += "</a>";
        }
        out += "</nav>\n";
        return out;
    }

    std::string with_nav(const char* page, const std::string& current)
    {
        std::string out = page ? page : "";
        const size_t head   = find_once(out, "</head>");
        const size_t header = find_once(out, "<header>");
        if (head == std::string::npos || header == std::string::npos || header < head)
            return out;

        // The later one first, so the earlier position still holds.
        out.insert(header, nav_html(current) + kNavScript);
        out.insert(head, kNavStyle);
        return out;
    }
}
