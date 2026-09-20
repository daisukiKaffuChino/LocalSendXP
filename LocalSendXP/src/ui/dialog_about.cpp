#include "lsxp/ui.h"
#include "resource.h"

#include <shellapi.h>

namespace lsxp {
namespace ui {

namespace {

INT_PTR CALLBACK AboutProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (message)
    {
    case WM_INITDIALOG:
        SetDialogFont(dialog);
        {
            SetWindowTextW(dialog, LoadStr(IDS_ABOUT_TITLE).c_str());

            std::wstring product = LoadStr(IDS_APP_TITLE) + L" " +
                                   AnsiToWide(LSXP_CLIENT_VERSION);
            SetDlgItemTextW(dialog, IDC_ABOUT_NAME, product.c_str());
            ApplyText(dialog, IDC_ABOUT_SUBTITLE, IDS_APP_SUBTITLE);
            ApplyText(dialog, IDC_ABOUT_INFO, IDS_ABOUT_INFO);
            ApplyText(dialog, IDC_ABOUT_PROTOCOL, IDS_ABOUT_PROTOCOL);
            ApplyText(dialog, IDC_ABOUT_CREDIT, IDS_ABOUT_CREDIT);
            ApplyText(dialog, IDC_ABOUT_AUTHOR, IDS_ABOUT_AUTHOR);
            ApplyText(dialog, IDC_ABOUT_SOURCE, IDS_BTN_SOURCE);
            ApplyText(dialog, IDOK, IDS_BTN_OK);
        }
        return TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_ABOUT_SOURCE)
        {
            ShellExecuteW(dialog, L"open",
                          L"https://github.com/daisukiKaffuChino/LocalSendXP",
                          NULL, NULL, SW_SHOWNORMAL);
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

bool ShowAboutDialog(HWND parent)
{
    LxpDialogBoxParam(GetModuleHandleW(NULL), IDD_ABOUT, parent, AboutProc, 0);
    return true;
}

}  // namespace ui
}  // namespace lsxp
