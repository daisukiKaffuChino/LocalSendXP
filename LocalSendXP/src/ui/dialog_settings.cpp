#include "lsxp/ui.h"
#include "lsxp/app.h"
#include "lsxp/tls.h"
#include "resource.h"

#include <commctrl.h>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

namespace lsxp {
namespace ui {

namespace {

const int kPageCount = 4;

const int kPageIds[kPageCount] =
{
    IDD_SETTINGS_GENERAL, IDD_SETTINGS_TRANSFER, IDD_SETTINGS_NETWORK, IDD_SETTINGS_SECURITY
};

const int kPageTabTextIds[kPageCount] =
{
    IDS_SET_TAB_GENERAL, IDS_SET_TAB_TRANSFER, IDS_SET_TAB_NETWORK, IDS_SET_TAB_SECURITY
};

struct DeviceTypeEntry
{
    int         textId;
    const char* value;
};

const int kTypeCount = 5;

const DeviceTypeEntry kDeviceTypes[kTypeCount] =
{
    { IDS_TYPE_DESKTOP,  "desktop"  },
    { IDS_TYPE_MOBILE,   "mobile"   },
    { IDS_TYPE_WEB,      "web"      },
    { IDS_TYPE_HEADLESS, "headless" },
    { IDS_TYPE_SERVER,   "server"   }
};

struct SettingsContext
{
    SettingsContext();

    Config* config;
    HWND    tabs;
    HWND    pages[kPageCount];
    int     currentPage;
};

SettingsContext::SettingsContext()
    : config(NULL), tabs(NULL), currentPage(0)
{
    for (int i = 0; i < kPageCount; ++i)
    {
        pages[i] = NULL;
    }
}

// Fits a long path into the label the way Explorer does ("D:\...\name.pem").
std::wstring Ellipsize(HWND control, const std::wstring& text);

std::wstring CompactPath(HWND control, const std::wstring& path)
{
    if (control == NULL || path.empty())
    {
        return path;
    }
    if (Ellipsize(control, path) == path)
    {
        return path;   // already short enough
    }

    // A fixed character budget keeps this deterministic: 32 characters stay
    // well inside the 172 dialog units of the label for both 宋体 and Tahoma.
    const int characters = 32;
    if ((int)path.size() <= characters)
    {
        return Ellipsize(control, path);
    }
    std::vector<wchar_t> buffer(path.size() + 32);
    if (PathCompactPathExW(&buffer[0], path.c_str(), (UINT)characters + 1, 0))
    {
        return std::wstring(&buffer[0]);
    }
    return Ellipsize(control, path);
}

// Shortens a long value (fingerprint, path) so that it fits the control
// instead of being cut off in the middle of the text.
std::wstring Ellipsize(HWND control, const std::wstring& text)
{
    if (control == NULL || text.empty())
    {
        return text;
    }

    RECT bounds;
    GetClientRect(control, &bounds);
    // Reserve room for the trailing ellipsis so that the shortened text
    // really does fit inside the control.
    int available = (bounds.right - bounds.left) - 24;
    if (available <= 16)
    {
        return text;
    }

    HDC dc = GetDC(control);
    if (dc == NULL)
    {
        return text;
    }
    HFONT font = (HFONT)SendMessageW(control, WM_GETFONT, 0, 0);
    if (font == NULL)
    {
        font = (HFONT)SendMessageW(GetParent(control), WM_GETFONT, 0, 0);
    }
    HGDIOBJ previousFont = (font != NULL) ? SelectObject(dc, font) : NULL;

    std::wstring result = text;
    SIZE size;
    ZeroMemory(&size, sizeof(size));
    while (result.size() > 4)
    {
        GetTextExtentPoint32W(dc, result.c_str(), (int)result.size(), &size);
        if (size.cx <= available)
        {
            break;
        }
        result.erase(result.size() - 1);
    }
    if (result.size() < text.size())
    {
        result += L"...";
    }

    if (previousFont != NULL)
    {
        SelectObject(dc, previousFont);
    }
    ReleaseDC(control, dc);
    return result;
}

SettingsContext* ContextOf(HWND dialog)
{
    return (SettingsContext*)GetWindowLongPtrW(dialog, DWLP_USER);
}

std::wstring GetText(HWND dialog, int controlId)
{
    HWND control = GetDlgItem(dialog, controlId);
    if (control == NULL)
    {
        return std::wstring();
    }
    int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer((size_t)length + 1);
    GetWindowTextW(control, &buffer[0], length + 1);
    return std::wstring(&buffer[0], (size_t)length);
}

// ---------------------------------------------------------------- combo boxes
void FillTypeCombo(HWND page, const std::string& current)
{
    HWND combo = GetDlgItem(page, IDC_SET_TYPE);
    if (combo == NULL)
    {
        return;
    }

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    int selected = 0;
    for (int i = 0; i < kTypeCount; ++i)
    {
        int index = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)LoadStr(kDeviceTypes[i].textId).c_str());
        SendMessageW(combo, CB_SETITEMDATA, index, (LPARAM)kDeviceTypes[i].value);
        if (current == kDeviceTypes[i].value)
        {
            selected = i;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)selected, 0);
}

std::string SelectedType(HWND page)
{
    HWND combo = GetDlgItem(page, IDC_SET_TYPE);
    int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0)
    {
        return "desktop";
    }
    const char* value = (const char*)SendMessageW(combo, CB_GETITEMDATA, (WPARAM)index, 0);
    return (value != NULL) ? std::string(value) : std::string("desktop");
}

void FillLanguageCombo(HWND page, const std::string& current)
{
    HWND combo = GetDlgItem(page, IDC_SET_LANGUAGE);
    if (combo == NULL)
    {
        return;
    }

    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_LANG_ZH).c_str());
    SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)LoadStr(IDS_LANG_EN).c_str());
    SendMessageW(combo, CB_SETCURSEL, (current == "en") ? 1 : 0, 0);
}

// --------------------------------------------------------------- text refresh
void ApplyPageTexts(HWND page, int pageIndex)
{
    if (page == NULL)
    {
        return;
    }

    switch (pageIndex)
    {
    case 0:
        ApplyText(page, IDC_GRP_DEVICE, IDS_SET_GROUP_DEVICE);
        ApplyText(page, IDC_LBL_ALIAS, IDS_LBL_ALIAS);
        ApplyText(page, IDC_LBL_TYPE, IDS_LBL_TYPE);
        ApplyText(page, IDC_LBL_MODEL, IDS_LBL_MODEL);
        ApplyText(page, IDC_LBL_LANGUAGE, IDS_SET_LANGUAGE);
        ApplyText(page, IDC_GRP_BEHAVIOR, IDS_SET_GROUP_BEHAVIOR);
        ApplyText(page, IDC_SET_AUTOSTART, IDS_CHK_AUTOSTART);
        ApplyText(page, IDC_SET_TRAY, IDS_CHK_TRAY);
        break;

    case 1:
        ApplyText(page, IDC_GRP_FILES, IDS_SET_GROUP_FILES);
        ApplyText(page, IDC_LBL_DIR, IDS_LBL_DIR);
        ApplyText(page, IDC_SET_BROWSE, IDS_BTN_BROWSE);
        ApplyText(page, IDC_SET_ASK, IDS_CHK_ASK);
        ApplyText(page, IDC_SET_OPENAFTER, IDS_CHK_OPENAFTER);
        ApplyText(page, IDC_GRP_PROTECT, IDS_SET_GROUP_PROTECT);
        ApplyText(page, IDC_LBL_PIN, IDS_LBL_PIN);
        ApplyText(page, IDC_LBL_PIN_HINT, IDS_LBL_PIN_HINT);
        break;

    case 2:
        ApplyText(page, IDC_GRP_NET, IDS_SET_GROUP_NET);
        ApplyText(page, IDC_LBL_PORT, IDS_LBL_PORT);
        ApplyText(page, IDC_LBL_ANNOUNCE, IDS_SET_ANNOUNCE);
        ApplyText(page, IDC_LBL_ADDR, IDS_SET_DEVICE_INFO);
        break;

    default:
        ApplyText(page, IDC_GRP_TLS, IDS_SET_GROUP_TLS);
        ApplyText(page, IDC_SET_HTTPS, IDS_SET_HTTPS);
        ApplyText(page, IDC_SET_LEGACY, IDS_SET_LEGACY);
        ApplyText(page, IDC_SET_INSECURE, IDS_SET_INSECURE);
        ApplyText(page, IDC_SET_CLIENTCERT, IDS_SET_CLIENTCERT);
        ApplyText(page, IDC_LBL_FP, IDS_SET_FINGERPRINT);
        ApplyText(page, IDC_GRP_CERT, IDS_SET_CERT_PATH);
        ApplyText(page, IDC_LBL_CERT, IDS_SET_CERT_PATH);
        ApplyText(page, IDC_LBL_CA, IDS_SET_CABUNDLE_PATH);
        ApplyText(page, IDC_SET_CA_BROWSE, IDS_BTN_BROWSEFILE);
        break;
    }
}

void ApplyDynamicTexts(SettingsContext* context)
{
    Config& config = *context->config;

    std::wstring address = FormatW(L"%s:%d", Utf8ToWide(GetPrimaryLocalIPv4()).c_str(), config.port);
    SetDlgItemTextW(context->pages[2], IDC_SET_DEVICE_INFO,
                    Ellipsize(GetDlgItem(context->pages[2], IDC_SET_DEVICE_INFO), address).c_str());

    std::wstring fingerprint = TlsContext::Instance().IsReady()
                               ? Utf8ToWide(TlsContext::Instance().Fingerprint())
                               : LoadStr(IDS_ABOUT_TLS_OFF);
    // Break the 64 hex characters into groups so that the label can wrap onto
    // a second line instead of being cut off.
    std::wstring grouped;
    for (size_t i = 0; i < fingerprint.size(); ++i)
    {
        if (i > 0 && (i % 4) == 0)
        {
            grouped += L' ';
        }
        grouped += fingerprint[i];
    }
    SetDlgItemTextW(context->pages[3], IDC_SET_FINGERPRINT,
                    grouped.c_str());
    std::wstring certificate = config.ResolvedCertificatePath();
    HWND certificateLabel = GetDlgItem(context->pages[3], IDC_SET_CERT);
    SetDlgItemTextW(context->pages[3], IDC_SET_CERT,
                    CompactPath(certificateLabel, certificate).c_str());
}

void ApplySettingsTexts(HWND dialog, SettingsContext* context)
{
    SetWindowTextW(dialog, LoadStr(IDS_SETTINGS_TITLE).c_str());
    ApplyText(dialog, IDC_SET_NOTE, IDS_SET_NOTE);
    ApplyText(dialog, IDOK, IDS_BTN_OK);
    ApplyText(dialog, IDCANCEL, IDS_BTN_CANCEL);

    if (context->tabs != NULL)
    {
        for (int i = 0; i < kPageCount; ++i)
        {
            // Keep the text alive for the duration of the message: TCM_SETITEM
            // copies it, but the pointer must not dangle.
            std::wstring title = LoadStr(kPageTabTextIds[i]);
            TCITEMW item;
            ZeroMemory(&item, sizeof(item));
            item.mask = TCIF_TEXT;
            item.pszText = (LPWSTR)title.c_str();
            SendMessageW(context->tabs, TCM_SETITEMW, (WPARAM)i, (LPARAM)&item);
        }
    }

    for (int i = 0; i < kPageCount; ++i)
    {
        ApplyPageTexts(context->pages[i], i);
    }
    {
        std::wstring titles;
        for (int i = 0; i < kPageCount; ++i)
        {
            if (!titles.empty())
            {
                titles += L" | ";
            }
            titles += LoadStr(kPageTabTextIds[i]);
        }
        LogLine("settings tabs: %s", WideToUtf8(titles).c_str());
    }
    FillTypeCombo(context->pages[0], context->config->deviceType);
    FillLanguageCombo(context->pages[0], context->config->language);
    ApplyDynamicTexts(context);

    // Applying text through SetDlgItemText() does not repaint the pages that
    // live inside the tab control, so the new language only showed up after
    // switching tabs.  Redraw every page (and the tab strip) right away.
    for (int i = 0; i < kPageCount; ++i)
    {
        if (context->pages[i] != NULL)
        {
            RedrawWindow(context->pages[i], NULL, NULL,
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                         RDW_UPDATENOW);
        }
    }
    if (context->tabs != NULL)
    {
        RedrawWindow(context->tabs, NULL, NULL,
                     RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    }
    RedrawWindow(dialog, NULL, NULL,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

// -------------------------------------------------------------- page handling
void ShowPage(SettingsContext* context, int index)
{
    for (int i = 0; i < kPageCount; ++i)
    {
        if (context->pages[i] != NULL)
        {
            ShowWindow(context->pages[i], (i == index) ? SW_SHOW : SW_HIDE);
        }
    }
    context->currentPage = index;
}

// Child pages forward their notifications to the settings dialog.
INT_PTR CALLBACK PageProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (message)
    {
    case WM_INITDIALOG:
        SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)lParam);
        SetDialogFont(dialog);
        return TRUE;

    case WM_COMMAND:
        {
            HWND parent = GetParent(dialog);
            if (parent != NULL)
            {
                SendMessageW(parent, WM_COMMAND, wParam, (LPARAM)dialog);
            }
        }
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

// --------------------------------------------------------------- load / save
void LoadConfigIntoSettings(HWND dialog, SettingsContext* context)
{
    Config& config = *context->config;
    HWND general = context->pages[0];
    HWND transfer = context->pages[1];
    HWND network = context->pages[2];
    HWND security = context->pages[3];
    (void)dialog;

    wchar_t number[32];
    _snwprintf(number, 31, L"%d", config.port);
    number[31] = L'\0';
    SetDlgItemTextW(network, IDC_SET_PORT, number);

    _snwprintf(number, 31, L"%d", config.announceIntervalSec);
    number[31] = L'\0';
    SetDlgItemTextW(network, IDC_SET_ANNOUNCE, number);

    SetDlgItemTextW(general, IDC_SET_ALIAS, config.alias.c_str());
    SetDlgItemTextW(general, IDC_SET_MODEL, config.deviceModel.c_str());
    FillTypeCombo(general, config.deviceType);
    FillLanguageCombo(general, config.language);
    CheckDlgButton(general, IDC_SET_AUTOSTART, config.autoStart ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(general, IDC_SET_TRAY, config.minimizeToTray ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemTextW(transfer, IDC_SET_DIR, config.downloadDirectory.c_str());
    SetDlgItemTextW(transfer, IDC_SET_PIN, Utf8ToWide(config.pin).c_str());
    CheckDlgButton(transfer, IDC_SET_ASK, config.askBeforeReceive ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(transfer, IDC_SET_OPENAFTER,
                   config.openFolderAfterReceive ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemTextW(security, IDC_SET_CABUNDLE, config.caBundlePath.c_str());
    CheckDlgButton(security, IDC_SET_HTTPS, config.httpsEnabled ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(security, IDC_SET_LEGACY, config.allowLegacyTls ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(security, IDC_SET_INSECURE,
                   config.allowInsecureHttps ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(security, IDC_SET_CLIENTCERT,
                   config.requireClientCertificate ? BST_CHECKED : BST_UNCHECKED);
}

bool SaveSettingsFromPages(HWND dialog, SettingsContext* context)
{
    Config& config = *context->config;
    HWND general = context->pages[0];
    HWND transfer = context->pages[1];
    HWND network = context->pages[2];
    HWND security = context->pages[3];

    std::wstring alias = GetText(general, IDC_SET_ALIAS);
    if (Trim(WideToUtf8(alias)).empty())
    {
        MessageBoxW(dialog, LoadStr(IDS_MSG_NONAME).c_str(), LoadStr(IDS_APP_TITLE).c_str(),
                    MB_ICONWARNING | MB_OK);
        ShowPage(context, 0);
        SendMessageW(context->tabs, TCM_SETCURSEL, 0, 0);
        SetFocus(GetDlgItem(general, IDC_SET_ALIAS));
        return false;
    }

    int port = _wtoi(GetText(network, IDC_SET_PORT).c_str());
    if (port < 1024 || port > 65535)
    {
        MessageBoxW(dialog, LoadStr(IDS_SET_ERR_PORT).c_str(), LoadStr(IDS_APP_TITLE).c_str(),
                    MB_ICONWARNING | MB_OK);
        ShowPage(context, 2);
        SendMessageW(context->tabs, TCM_SETCURSEL, 2, 0);
        SetFocus(GetDlgItem(network, IDC_SET_PORT));
        return false;
    }

    int interval = _wtoi(GetText(network, IDC_SET_ANNOUNCE).c_str());
    if (interval < 5 || interval > 3600)
    {
        interval = 30;
    }

    std::wstring directory = GetText(transfer, IDC_SET_DIR);
    if (!directory.empty() && !DirectoryExistsW(directory))
    {
        if (MessageBoxW(dialog, LoadStr(IDS_SET_ERR_DIR).c_str(), LoadStr(IDS_APP_TITLE).c_str(),
                        MB_ICONQUESTION | MB_YESNO) == IDYES)
        {
            EnsureDirectoryW(directory);
        }
    }

    std::wstring pin = GetText(transfer, IDC_SET_PIN);
    if (pin.size() > 32)
    {
        pin = pin.substr(0, 32);
    }

    config.alias = alias;
    config.deviceModel = GetText(general, IDC_SET_MODEL);
    config.deviceType = SelectedType(general);
    config.port = port;
    config.announceIntervalSec = interval;
    config.downloadDirectory = directory;
    config.pin = WideToUtf8(pin);
    config.askBeforeReceive = IsDlgButtonChecked(transfer, IDC_SET_ASK) == BST_CHECKED;
    config.openFolderAfterReceive = IsDlgButtonChecked(transfer, IDC_SET_OPENAFTER) == BST_CHECKED;
    config.minimizeToTray = IsDlgButtonChecked(general, IDC_SET_TRAY) == BST_CHECKED;
    config.autoStart = IsDlgButtonChecked(general, IDC_SET_AUTOSTART) == BST_CHECKED;

    config.httpsEnabled = IsDlgButtonChecked(security, IDC_SET_HTTPS) == BST_CHECKED;
    config.allowLegacyTls = IsDlgButtonChecked(security, IDC_SET_LEGACY) == BST_CHECKED;
    config.allowInsecureHttps = IsDlgButtonChecked(security, IDC_SET_INSECURE) == BST_CHECKED;
    config.requireClientCertificate = IsDlgButtonChecked(security, IDC_SET_CLIENTCERT) == BST_CHECKED;
    config.caBundlePath = GetText(security, IDC_SET_CABUNDLE);
    return true;
}

// ------------------------------------------------------------- settings proc
INT_PTR CALLBACK SettingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    SettingsContext* context = ContextOf(dialog);

    switch (message)
    {
    case WM_INITDIALOG:
        {
            context = (SettingsContext*)lParam;
            SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)context);
            SetDialogFont(dialog);

            context->tabs = GetDlgItem(dialog, IDC_SET_TABS);
            HINSTANCE instance = GetModuleHandleW(NULL);

            for (int i = 0; i < kPageCount; ++i)
            {
                context->pages[i] = LxpCreateDialogParam(instance, kPageIds[i], dialog,
                                                         PageProc, (LPARAM)context);
            }

            for (int i = 0; i < kPageCount; ++i)
            {
                std::wstring title = LoadStr(kPageTabTextIds[i]);
                TCITEMW item;
                ZeroMemory(&item, sizeof(item));
                item.mask = TCIF_TEXT;
                item.pszText = (LPWSTR)title.c_str();
                SendMessageW(context->tabs, TCM_INSERTITEMW, (WPARAM)i, (LPARAM)&item);
            }

            RECT display;
            GetClientRect(context->tabs, &display);
            TabCtrl_AdjustRect(context->tabs, FALSE, &display);
            MapWindowPoints(context->tabs, dialog, (POINT*)&display, 2);

            for (int i = 0; i < kPageCount; ++i)
            {
                if (context->pages[i] == NULL)
                {
                    continue;
                }
                SetWindowPos(context->pages[i], NULL,
                             display.left, display.top,
                             display.right - display.left, display.bottom - display.top,
                             SWP_NOZORDER);
            }

            LoadConfigIntoSettings(dialog, context);
            ApplySettingsTexts(dialog, context);
            ShowPage(context, 0);
            SendMessageW(context->tabs, TCM_SETCURSEL, 0, 0);
            SetFocus(GetDlgItem(context->pages[0], IDC_SET_ALIAS));
            return FALSE;
        }

    case WM_NOTIFY:
        if (context != NULL && ((NMHDR*)lParam)->hwndFrom == context->tabs &&
            ((NMHDR*)lParam)->code == TCN_SELCHANGE)
        {
            int selected = TabCtrl_GetCurSel(context->tabs);
            if (selected >= 0 && selected < kPageCount)
            {
                ShowPage(context, selected);
            }
        }
        return TRUE;

    case WM_COMMAND:
        if (context == NULL)
        {
            break;
        }

        switch (LOWORD(wParam))
        {
        case IDC_SET_BROWSE:
            {
                std::wstring folder;
                if (ShowBrowseFolderDialog(dialog, folder))
                {
                    SetDlgItemTextW(context->pages[1], IDC_SET_DIR, folder.c_str());
                }
            }
            return TRUE;

        case IDC_SET_CA_BROWSE:
            {
                std::vector<std::wstring> files;
                if (ShowOpenFilesDialog(dialog, files) && !files.empty())
                {
                    SetDlgItemTextW(context->pages[3], IDC_SET_CABUNDLE, files[0].c_str());
                }
            }
            return TRUE;

        case IDC_SET_LANGUAGE:
            if (HIWORD(wParam) == CBN_SELCHANGE)
            {
                HWND combo = GetDlgItem(context->pages[0], IDC_SET_LANGUAGE);
                int selected = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
                std::string language = (selected == 1) ? "en" : "zh";
                if (language != context->config->language)
                {
                    context->config->language = language;
                    App::Instance().ApplyLanguageChange();
                    ApplySettingsTexts(dialog, context);
                }
            }
            return TRUE;

        case IDOK:
            if (SaveSettingsFromPages(dialog, context))
            {
                context->config->Save();
                App::Instance().ApplySettingChange();
                EndDialog(dialog, 1);
            }
            return TRUE;

        case IDCANCEL:
            EndDialog(dialog, 0);
            return TRUE;

        default:
            break;
        }
        break;

    case WM_CLOSE:
        EndDialog(dialog, 0);
        return TRUE;

    case WM_DESTROY:
        FreeDialogFont(dialog);
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

// ---------------------------------------------------------------- share dialog
INT_PTR CALLBACK ShareProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetDialogFont(dialog);
            SetWindowTextW(dialog, LoadStr(IDS_SHARE_TITLE).c_str());
            ApplyText(dialog, IDC_SHARE_INFO, IDS_SHARE_INFO);
            ApplyText(dialog, IDC_SHARE_COPY, IDS_SHARE_COPY);
            ApplyText(dialog, IDCANCEL, IDS_BTN_CLOSE);

            const std::wstring* url = (const std::wstring*)lParam;
            if (url != NULL)
            {
                SetDlgItemTextW(dialog, IDC_SHARE_URL, url->c_str());
                SendDlgItemMessageW(dialog, IDC_SHARE_URL, EM_SETSEL, 0, -1);
            }
            return TRUE;
        }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SHARE_COPY)
        {
            CopyTextToClipboard(dialog, GetText(dialog, IDC_SHARE_URL));
            MessageBoxW(dialog, LoadStr(IDS_SHARE_COPIED).c_str(), LoadStr(IDS_APP_TITLE).c_str(),
                        MB_ICONINFORMATION | MB_OK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(dialog, LOWORD(wParam));
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    case WM_DESTROY:
        FreeDialogFont(dialog);
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

}  // namespace

bool ShowSettingsDialog(HWND parent)
{
    SettingsContext context;
    context.config = &App::Instance().GetConfig();

    INT_PTR result = LxpDialogBoxParam(GetModuleHandleW(NULL), IDD_SETTINGS, parent,
                                       SettingsProc, (LPARAM)&context);
    return result == 1;
}

void ShowShareDialog(HWND parent, const std::wstring& url)
{
    LxpDialogBoxParam(GetModuleHandleW(NULL), IDD_SHARE, parent, ShareProc, (LPARAM)&url);
}

}  // namespace ui
}  // namespace lsxp
