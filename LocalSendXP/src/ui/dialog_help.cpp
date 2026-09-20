#include "lsxp/ui.h"
#include "resource.h"

namespace lsxp {
namespace ui {

namespace {

INT_PTR CALLBACK HelpProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    (void)lParam;

    switch (message)
    {
    case WM_INITDIALOG:
        SetDialogFont(dialog);
        SetWindowTextW(dialog, LoadStr(IDS_HELP_TITLE).c_str());
        SetDlgItemTextW(dialog, IDC_HELP_TEXT, LoadStr(IDS_HELP_TEXT).c_str());
        SendDlgItemMessageW(dialog, IDC_HELP_TEXT, EM_SETSEL, 0, 0);
        SetFocus(GetDlgItem(dialog, IDCANCEL));
        return FALSE;

    case WM_COMMAND:
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

bool ShowHelpDialog(HWND parent)
{
    DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_HELP),
                    parent, HelpProc, 0);
    return true;
}

}  // namespace ui
}  // namespace lsxp
