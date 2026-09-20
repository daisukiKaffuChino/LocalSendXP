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
    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_ABOUT),
                    parent, AboutProc, 0);
    return true;
}

}  // namespace ui
}  // namespace lsxp
