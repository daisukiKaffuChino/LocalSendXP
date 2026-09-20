#include "lsxp/ui.h"
#include "lsxp/app.h"
#include "resource.h"

#include <commctrl.h>

namespace lsxp {
namespace ui {

namespace {

const int kTypeCount = 5;

struct DeviceTypeEntry
{
    int         textId;
    const char* value;
};

const DeviceTypeEntry kDeviceTypes[kTypeCount] =
{
    { IDS_TYPE_DESKTOP,  "desktop"  },
    { IDS_TYPE_MOBILE,   "mobile"   },
    { IDS_TYPE_WEB,      "web"      },
    { IDS_TYPE_HEADLESS, "headless" },
    { IDS_TYPE_SERVER,   "server"   }
};

void FillTypeCombo(HWND dialog, const std::string& current)
{
    HWND combo = GetDlgItem(dialog, IDC_SET_TYPE);
    if (combo == NULL)
    {
        return;
    }

    int selected = 0;
    for (int i = 0; i < kTypeCount; ++i)
    {
        std::wstring text = LoadStr(kDeviceTypes[i].textId);
        int index = (int)SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)text.c_str());
        SendMessageW(combo, CB_SETITEMDATA, index, (LPARAM)kDeviceTypes[i].value);
        if (current == kDeviceTypes[i].value)
        {
            selected = i;
        }
    }
    SendMessageW(combo, CB_SETCURSEL, (WPARAM)selected, 0);
}

std::string SelectedType(HWND dialog)
{
    HWND combo = GetDlgItem(dialog, IDC_SET_TYPE);
    int index = (int)SendMessageW(combo, CB_GETCURSEL, 0, 0);
    if (index < 0)
    {
        return "desktop";
    }
    const char* value = (const char*)SendMessageW(combo, CB_GETITEMDATA, (WPARAM)index, 0);
    return (value != NULL) ? std::string(value) : std::string("desktop");
}

std::wstring GetText(HWND dialog, int controlId)
{
    HWND control = GetDlgItem(dialog, controlId);
    int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer((size_t)length + 1);
    GetWindowTextW(control, &buffer[0], length + 1);
    return std::wstring(&buffer[0], (size_t)length);
}

INT_PTR CALLBACK SettingsProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetDialogFont(dialog);
            Config& config = App::Instance().GetConfig();

            wchar_t portText[16];
            _snwprintf(portText, 15, L"%d", config.port);
            portText[15] = L'\0';

            SetDlgItemTextW(dialog, IDC_SET_ALIAS, config.alias.c_str());
            SetDlgItemTextW(dialog, IDC_SET_MODEL, config.deviceModel.c_str());
            SetDlgItemTextW(dialog, IDC_SET_PORT, portText);
            SetDlgItemTextW(dialog, IDC_SET_PIN, Utf8ToWide(config.pin).c_str());
            SetDlgItemTextW(dialog, IDC_SET_DIR, config.downloadDirectory.c_str());

            FillTypeCombo(dialog, config.deviceType);

            CheckDlgButton(dialog, IDC_SET_ASK,
                           config.askBeforeReceive ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_SET_OPENAFTER,
                           config.openFolderAfterReceive ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_SET_TRAY,
                           config.minimizeToTray ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(dialog, IDC_SET_AUTOSTART,
                           config.autoStart ? BST_CHECKED : BST_UNCHECKED);
            return TRUE;
        }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_SET_BROWSE:
            {
                std::wstring folder;
                if (ShowBrowseFolderDialog(dialog, folder))
                {
                    SetDlgItemTextW(dialog, IDC_SET_DIR, folder.c_str());
                }
            }
            return TRUE;

        case IDOK:
            {
                Config& config = App::Instance().GetConfig();

                std::wstring alias = Trim(WideToUtf8(GetText(dialog, IDC_SET_ALIAS))).empty()
                                     ? std::wstring() : GetText(dialog, IDC_SET_ALIAS);
                alias = GetText(dialog, IDC_SET_ALIAS);
                if (Trim(WideToUtf8(alias)).empty())
                {
                    MessageBoxW(dialog, LoadStr(IDS_MSG_NONAME).c_str(),
                                LoadStr(IDS_APP_TITLE).c_str(), MB_ICONWARNING | MB_OK);
                    SetFocus(GetDlgItem(dialog, IDC_SET_ALIAS));
                    return TRUE;
                }

                std::wstring portText = GetText(dialog, IDC_SET_PORT);
                int port = _wtoi(portText.c_str());
                if (port < 1024 || port > 65535)
                {
                    MessageBoxW(dialog, LoadStr(IDS_SET_ERR_PORT).c_str(),
                                LoadStr(IDS_APP_TITLE).c_str(), MB_ICONWARNING | MB_OK);
                    SetFocus(GetDlgItem(dialog, IDC_SET_PORT));
                    return TRUE;
                }

                std::wstring directory = GetText(dialog, IDC_SET_DIR);
                if (!directory.empty() && !DirectoryExistsW(directory))
                {
                    if (MessageBoxW(dialog, LoadStr(IDS_SET_ERR_DIR).c_str(),
                                    LoadStr(IDS_APP_TITLE).c_str(),
                                    MB_ICONQUESTION | MB_YESNO) == IDYES)
                    {
                        EnsureDirectoryW(directory);
                    }
                }

                std::wstring pin = GetText(dialog, IDC_SET_PIN);
                if (pin.size() > 32)
                {
                    pin = pin.substr(0, 32);
                }

                config.alias = alias;
                config.deviceModel = GetText(dialog, IDC_SET_MODEL);
                config.deviceType = SelectedType(dialog);
                config.port = port;
                config.pin = WideToUtf8(pin);
                config.downloadDirectory = directory;
                config.askBeforeReceive = IsDlgButtonChecked(dialog, IDC_SET_ASK) == BST_CHECKED;
                config.openFolderAfterReceive = IsDlgButtonChecked(dialog, IDC_SET_OPENAFTER) == BST_CHECKED;
                config.minimizeToTray = IsDlgButtonChecked(dialog, IDC_SET_TRAY) == BST_CHECKED;
                config.autoStart = IsDlgButtonChecked(dialog, IDC_SET_AUTOSTART) == BST_CHECKED;

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

INT_PTR CALLBACK ShareProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetDialogFont(dialog);
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
            std::wstring text = GetText(dialog, IDC_SHARE_URL);
            CopyTextToClipboard(dialog, text);
            MessageBoxW(dialog, LoadStr(IDS_SHARE_COPIED).c_str(),
                        LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
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
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_SETTINGS),
                                     parent, SettingsProc, 0);
    return result == 1;
}

void ShowShareDialog(HWND parent, const std::wstring& url)
{
    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_SHARE),
                    parent, ShareProc, (LPARAM)&url);
}

}  // namespace ui
}  // namespace lsxp
