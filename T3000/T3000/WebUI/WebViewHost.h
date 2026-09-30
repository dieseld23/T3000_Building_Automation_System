#pragma once

// A WebView2 embedded as a child window, driven by MFC's message pump.
//
// This is deliberately not BacnetWebViewAppWindow. That class owns a top-level
// window and runs its own message loop (RunMessagePump), which suits the popup
// browser it was written for but cannot be docked, embedded in a dialog, or
// sized by a parent. Every screen that replaces an MFC dialog needs the opposite:
// a plain child HWND that MFC lays out and pumps.

#include <wrl.h>
#include <wil/com.h>
#include "webview2.h"

#include <functional>
#include <memory>

namespace WebUI
{
    class CWebViewHost : public CWnd
    {
    public:
        CWebViewHost();
        virtual ~CWebViewHost();

        // Called with the JSON body of each message the page posts via
        // window.chrome.webview.postMessage.
        typedef std::function<void(const CString&)> MessageHandler;

        // Creates the child window and starts asynchronous WebView2 creation.
        // Returns as soon as the HWND exists - the browser is not ready yet.
        BOOL Create(CWnd* pParent, const CRect& rect, UINT nID);

        // Maps a local folder to https://<hostName>/ so the page can be loaded
        // over a real origin instead of file://, which keeps fetch and modules
        // working. Safe to call before the browser is ready; it is applied once
        // creation completes.
        void SetLocalContent(const CString& hostName, const CString& folderPath);

        // Both are safe to call before the browser exists: the navigation is
        // remembered and replayed, and a post made too early is dropped rather
        // than queued, since stale data is worse than none.
        void Navigate(const CString& url);
        void PostJson(const CString& json);

        void SetMessageHandler(MessageHandler handler) { m_onMessage = handler; }
        bool IsReady() const { return m_controller != nullptr; }

    protected:
        afx_msg void OnSize(UINT nType, int cx, int cy);
        afx_msg void OnDestroy();
        DECLARE_MESSAGE_MAP()

    private:
        void OnControllerCreated(ICoreWebView2Controller* controller);
        void ApplyVirtualHostMapping();
        void ResizeToClient();

        wil::com_ptr<ICoreWebView2Controller> m_controller;
        wil::com_ptr<ICoreWebView2> m_webView;

        // WebView2 creation is two asynchronous callbacks deep, so the host can
        // be destroyed while they are still in flight. The callbacks hold a weak
        // reference to this flag and do nothing once it is cleared, which is what
        // keeps a dialog closed mid-initialization from writing through a dangling
        // this pointer.
        std::shared_ptr<bool> m_alive;

        MessageHandler m_onMessage;
        CString m_pendingUrl;
        CString m_virtualHost;
        CString m_virtualFolder;
    };
}
