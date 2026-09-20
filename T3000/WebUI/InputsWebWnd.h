#pragma once

// Read-only web view of the current device's inputs, beside the existing MFC
// dialog rather than replacing it. Open both against the same device to compare
// the grids column by column.
//
// Created entirely in code, with no dialog template. T3000.rc holds GBK-encoded
// text that scripted edits have corrupted before, so a window that needs no
// resource entry is the safer shape for a first slice.

#include "WebViewHost.h"

namespace WebUI
{
    class CInputsWebWnd : public CFrameWnd
    {
        DECLARE_DYNAMIC(CInputsWebWnd)

    public:
        CInputsWebWnd();
        virtual ~CInputsWebWnd();

        // Creates and shows the window. Modeless and self-owned: it deletes
        // itself on close, so the caller does not keep the pointer.
        static CInputsWebWnd* CreateAndShow(CWnd* pParent);

    protected:
        afx_msg int OnCreate(LPCREATESTRUCT lpCreateStruct);
        afx_msg void OnSize(UINT nType, int cx, int cy);
        virtual void PostNcDestroy();
        DECLARE_MESSAGE_MAP()

    private:
        void OnPageMessage(const CString& message);
        void SendInputs();

        CWebViewHost m_host;
    };
}
