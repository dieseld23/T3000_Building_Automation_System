#include "stdafx.h"
#include "WebViewHost.h"

#include <ShlObj.h>

using namespace Microsoft::WRL;

namespace WebUI
{
    BEGIN_MESSAGE_MAP(CWebViewHost, CWnd)
        ON_WM_SIZE()
        ON_WM_DESTROY()
    END_MESSAGE_MAP()

    CWebViewHost::CWebViewHost()
        : m_alive(std::make_shared<bool>(true))
    {
    }

    CWebViewHost::~CWebViewHost()
    {
        // Clearing this before the COM pointers go away is what tells any
        // in-flight creation callback that it must not touch this object.
        *m_alive = false;
    }

    BOOL CWebViewHost::Create(CWnd* pParent, const CRect& rect, UINT nID)
    {
        // No CS_HREDRAW/CS_VREDRAW and no background brush: the WebView2 child
        // covers the whole client area and paints itself, so erasing underneath
        // it only produces flicker while the window is being resized.
        const CString className = AfxRegisterWndClass(0,
            ::LoadCursor(NULL, IDC_ARROW), NULL, NULL);

        if (!CWnd::CreateEx(0, className, _T(""), WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
            rect, pParent, nID))
        {
            return FALSE;
        }

        // Same profile folder as the existing popup webviews, so cookies and
        // cache are shared rather than duplicated per host.
        PWSTR localAppData = NULL;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData)))
            return FALSE;

        std::wstring userDataFolder(localAppData);
        CoTaskMemFree(localAppData);
        userDataFolder += L"\\T3000";

        std::weak_ptr<bool> alive = m_alive;

        HRESULT hr = CreateCoreWebView2EnvironmentWithOptions(nullptr, userDataFolder.c_str(), nullptr,
            Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>(
                [this, alive](HRESULT result, ICoreWebView2Environment* environment) -> HRESULT
                {
                    std::shared_ptr<bool> stillAlive = alive.lock();
                    if (!stillAlive || !*stillAlive)
                        return S_OK;

                    // This is where a missing Evergreen runtime actually shows up.
                    // CreateCoreWebView2EnvironmentWithOptions succeeds when the
                    // request is queued, not when the browser exists, so failing
                    // silently here is what leaves a blank window with no clue why.
                    if (FAILED(result) || environment == nullptr)
                    {
                        TRACE(_T("WebViewHost: WebView2 environment creation failed (hr=0x%08X). ")
                              _T("The Evergreen runtime is most likely not installed.\n"), result);
                        return S_OK;
                    }

                    return environment->CreateCoreWebView2Controller(m_hWnd,
                        Callback<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler>(
                            [this, alive](HRESULT r, ICoreWebView2Controller* controller) -> HRESULT
                            {
                                std::shared_ptr<bool> present = alive.lock();
                                if (!present || !*present || FAILED(r) || controller == nullptr)
                                    return S_OK;

                                OnControllerCreated(controller);
                                return S_OK;
                            }).Get());
                }).Get());

        // A failure here is almost always a missing WebView2 runtime. The window
        // still exists and stays blank; the caller decides how to report that.
        return SUCCEEDED(hr);
    }

    void CWebViewHost::OnControllerCreated(ICoreWebView2Controller* controller)
    {
        m_controller = controller;
        m_controller->get_CoreWebView2(&m_webView);
        if (m_webView == nullptr)
            return;

        std::weak_ptr<bool> alive = m_alive;

        EventRegistrationToken token;
        m_webView->add_WebMessageReceived(
            Callback<ICoreWebView2WebMessageReceivedEventHandler>(
                [this, alive](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT
                {
                    std::shared_ptr<bool> present = alive.lock();
                    if (!present || !*present || !m_onMessage)
                        return S_OK;

                    wil::unique_cotaskmem_string message;
                    if (SUCCEEDED(args->TryGetWebMessageAsString(&message)) && message)
                        m_onMessage(CString(message.get()));

                    return S_OK;
                }).Get(), &token);

        if (!m_virtualHost.IsEmpty())
            ApplyVirtualHostMapping();

        ResizeToClient();

        if (!m_pendingUrl.IsEmpty())
        {
            m_webView->Navigate(m_pendingUrl);
            m_pendingUrl.Empty();
        }
    }

    void CWebViewHost::ApplyVirtualHostMapping()
    {
        // Virtual host mapping arrived in ICoreWebView2_3, so it has to be queried
        // for rather than called on the base interface. A runtime older than that
        // simply does not get the mapping; the caller sees a failed navigation
        // rather than a crash.
        wil::com_ptr<ICoreWebView2_3> webView3 = m_webView.try_query<ICoreWebView2_3>();
        if (webView3 == nullptr)
        {
            TRACE(_T("WebViewHost: runtime predates ICoreWebView2_3; ")
                  _T("local content mapping unavailable.\n"));
            return;
        }

        webView3->SetVirtualHostNameToFolderMapping(m_virtualHost, m_virtualFolder,
            COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_ALLOW);
    }

    void CWebViewHost::SetLocalContent(const CString& hostName, const CString& folderPath)
    {
        m_virtualHost = hostName;
        m_virtualFolder = folderPath;

        if (m_webView != nullptr)
            ApplyVirtualHostMapping();
    }

    void CWebViewHost::Navigate(const CString& url)
    {
        if (m_webView != nullptr)
            m_webView->Navigate(url);
        else
            m_pendingUrl = url;   // replayed once the browser exists
    }

    void CWebViewHost::PostJson(const CString& json)
    {
        // Deliberately dropped rather than queued when the page is not up yet.
        // These messages carry live point values; replaying a stale one after
        // initialization would show data that has already been superseded.
        if (m_webView != nullptr)
            m_webView->PostWebMessageAsJson(json);
    }

    void CWebViewHost::ResizeToClient()
    {
        if (m_controller == nullptr)
            return;

        CRect client;
        GetClientRect(&client);
        m_controller->put_Bounds(client);
    }

    void CWebViewHost::OnSize(UINT nType, int cx, int cy)
    {
        CWnd::OnSize(nType, cx, cy);
        ResizeToClient();   // no-op while the controller is still being created
    }

    void CWebViewHost::OnDestroy()
    {
        if (m_controller != nullptr)
        {
            m_controller->Close();
            m_controller = nullptr;
        }
        m_webView = nullptr;

        CWnd::OnDestroy();
    }
}
