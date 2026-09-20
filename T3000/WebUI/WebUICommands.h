#pragma once

// Command IDs for the WebView2-hosted screens.
//
// Deliberately not in resource.h. That file is not pure ASCII and the menu entry
// is appended at runtime rather than declared in T3000.rc, so nothing here needs
// to be visible to the resource editor. Picked above _APS_NEXT_COMMAND_VALUE
// (34093) to stay clear of the next ID the editor would hand out, and well below
// 0xE000 where MFC's own command range begins.

#define ID_WEBUI_INPUTS   34200
