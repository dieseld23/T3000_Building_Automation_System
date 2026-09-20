#pragma once

// Transport-agnostic view of the current device's input points.
//
// Nothing in here knows about WebView2, HTTP or any other delivery mechanism.
// That is deliberate: the WebView2 bridge calls BuildInputPointsJson today, and
// an HTTP handler can call the same function later without this file changing.

#include <json/json.h>

namespace WebUI
{
    // UTF-8 encoded copy of a CString. JSON is UTF-8 by definition, while the
    // device strings arrive as CP_ACP bytes, so every string crossing into JSON
    // has to go through here.
    std::string ToUtf8(const CString& text);

    // The current device's input points, as
    //   { "serialNumber": n, "count": n, "inputs": [ ... ] }
    //
    // m_Input_data holds whichever device is currently selected and is sized by
    // Initial_All_Point/ReInital_Someof_Point, so it is empty before a device has
    // been read. That case returns count 0 rather than an error: "no device
    // selected" is a normal state, not a failure.
    Json::Value BuildInputPointsJson();
}
