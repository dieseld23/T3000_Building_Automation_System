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

    struct UpdateResult
    {
        bool    queued;     // accepted and sent to the device
        CString message;    // why not, when queued is false
    };

    // Queues a Full Label change for one input on the current device.
    //
    // This writes to a live controller, so it is guarded rather than trusting the
    // caller. expectedCurrentLabel is the value the caller believes is in that
    // row; if it does not match, the write is refused. Without that check, a row
    // index that has drifted between the page and m_Input_data would silently
    // rename the wrong physical point.
    //
    // Full Label specifically because it cannot affect control behaviour. Filter
    // or Range change signal processing on real equipment and are not worth
    // touching until the path itself is proven.
    //
    // Returns once the write is *queued*, not once the device has taken it.
    // Completion arrives as MY_RESUME_DATA at completionTarget; the receiver owns
    // the _MessageInvokeIDInfo in lParam and must delete it, then call
    // ReportUpdateOutcome so a rejected write is rolled back.
    UpdateResult UpdateInputFullLabel(int index,
                                      const CString& newLabel,
                                      const CString& expectedCurrentLabel,
                                      HWND completionTarget);

    // Restores the pre-write snapshot when the device rejected the change, the
    // same way CBacnetInput::InputMessageCallBack restores from m_temp_Input_data.
    void ReportUpdateOutcome(int index, bool succeeded);
}
