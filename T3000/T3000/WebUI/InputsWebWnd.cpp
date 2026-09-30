#include "stdafx.h"
#include "InputsWebWnd.h"
#include "InputsData.h"

#include "../global_variable_extern.h"

namespace WebUI
{
    // The page is loaded over a real https origin rather than file://, which keeps
    // fetch, ES modules and normal same-origin rules working. The folder is mapped
    // read-only into that host name; nothing is served over the network.
    static const TCHAR* const kVirtualHost = _T("t3000.webui");

    IMPLEMENT_DYNAMIC(CInputsWebWnd, CFrameWnd)

    BEGIN_MESSAGE_MAP(CInputsWebWnd, CFrameWnd)
        ON_WM_CREATE()
        ON_WM_SIZE()
        ON_MESSAGE(MY_RESUME_DATA, &CInputsWebWnd::OnWriteComplete)
    END_MESSAGE_MAP()

    CInputsWebWnd::CInputsWebWnd()
    {
    }

    CInputsWebWnd::~CInputsWebWnd()
    {
    }

    CInputsWebWnd* CInputsWebWnd::CreateAndShow(CWnd* pParent)
    {
        CInputsWebWnd* pWnd = new CInputsWebWnd();

        if (!pWnd->CreateEx(0, NULL, _T("Inputs (Web) - preview"),
            WS_OVERLAPPEDWINDOW, CRect(0, 0, 1100, 640), pParent, NULL))
        {
            delete pWnd;
            return NULL;
        }

        pWnd->CenterWindow(pParent);
        pWnd->ShowWindow(SW_SHOW);
        pWnd->UpdateWindow();
        return pWnd;
    }

    int CInputsWebWnd::OnCreate(LPCREATESTRUCT lpCreateStruct)
    {
        if (CFrameWnd::OnCreate(lpCreateStruct) == -1)
            return -1;

        CRect client;
        GetClientRect(&client);

        if (!m_host.Create(this, client, 1))
            return -1;

        m_host.SetMessageHandler(
            [this](const CString& message) { OnPageMessage(message); });

        const CString folder = g_strExePth + _T("ResourceFile\\webui");
        m_host.SetLocalContent(kVirtualHost, folder);
        m_host.Navigate(CString(_T("https://")) + kVirtualHost + _T("/inputs.html"));

        return 0;
    }

    void CInputsWebWnd::OnSize(UINT nType, int cx, int cy)
    {
        CFrameWnd::OnSize(nType, cx, cy);

        if (m_host.GetSafeHwnd() != NULL)
            m_host.MoveWindow(0, 0, cx, cy);
    }

    void CInputsWebWnd::OnPageMessage(const CString& message)
    {
        // The page drives the handshake. It cannot be told to load data before it
        // exists, and the host has no way to know when script has finished parsing,
        // so the page says "ready" and the data follows.
        if (message == _T("ready") || message == _T("refresh"))
        {
            SendInputs();
            return;
        }

        // Anything else is a JSON command.
        CT2A utf8Message(message, CP_UTF8);
        const std::string body((const char*)utf8Message);

        Json::CharReaderBuilder builder;
        Json::Value command;
        std::string errors;

        std::unique_ptr<Json::CharReader> reader(builder.newCharReader());
        if (!reader->parse(body.c_str(), body.c_str() + body.size(), &command, &errors))
            return;

        if (command["action"].asString() != "updateFullLabel")
            return;

        const int index = command["index"].asInt();

        // The page sends back the label it believes it is editing. If that no
        // longer matches, the write is refused rather than applied to whatever is
        // in that row now - see UpdateInputFullLabel.
        const CString value(CA2T(command["value"].asCString(), CP_UTF8));
        const CString expected(CA2T(command["expected"].asCString(), CP_UTF8));

        UpdateResult result = UpdateInputFullLabel(index, value, expected, m_hWnd);

        if (!result.queued)
        {
            SendResult(index, false, result.message);
            return;
        }

        SendResult(index, true, _T("Writing to device..."));
    }

    LRESULT CInputsWebWnd::OnWriteComplete(WPARAM wParam, LPARAM lParam)
    {
        _MessageInvokeIDInfo* pInvoke = (_MessageInvokeIDInfo*)lParam;
        if (pInvoke == NULL)
            return 0;

        const bool succeeded = MKBOOL(wParam) ? true : false;
        const int row = pInvoke->mRow;

        // Rolls the point back in memory when the device refused it.
        ReportUpdateOutcome(row, succeeded);

        // Order matters: the fresh list re-renders the grid and clears any
        // per-row state, so it has to go first or it wipes the result styling
        // that tells the user what just happened.
        SendInputs();

        SendResult(row, succeeded,
                   succeeded ? _T("Saved to device.")
                             : _T("The device rejected the change. Reverted."));

        delete pInvoke;   // ownership transfers with the message
        return 0;
    }

    void CInputsWebWnd::SendResult(int index, bool ok, const CString& message)
    {
        Json::Value payload(Json::objectValue);
        payload["type"] = "result";
        payload["index"] = index;
        payload["ok"] = ok;
        payload["message"] = ToUtf8(message);
        PostToPage(payload);
    }

    void CInputsWebWnd::SendInputs()
    {
        Json::Value payload = BuildInputPointsJson();
        payload["type"] = "inputs";
        PostToPage(payload);
    }

    void CInputsWebWnd::PostToPage(const Json::Value& payload)
    {
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        const std::string utf8 = Json::writeString(builder, payload);

        // PostWebMessageAsJson takes UTF-16, and the JSON is UTF-8, so this
        // widening step is not optional - skipping it is what turns non-ASCII
        // point labels into mojibake on the page.
        const int chars = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), NULL, 0);
        if (chars <= 0)
            return;

        CString json;
        MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(),
            json.GetBuffer(chars), chars);
        json.ReleaseBuffer(chars);

        m_host.PostJson(json);
    }

    void CInputsWebWnd::PostNcDestroy()
    {
        CFrameWnd::PostNcDestroy();
        delete this;   // modeless and self-owned
    }
}
