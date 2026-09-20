#include "lsxp/ui.h"
#include "lsxp/app.h"
#include "resource.h"

#include <commctrl.h>

namespace lsxp {
namespace ui {

namespace {

std::wstring TrimWide(const std::wstring& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && text[begin] <= L' ')
    {
        ++begin;
    }
    while (end > begin && text[end - 1] <= L' ')
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::wstring ClipboardUrl(HWND owner)
{
    std::wstring text;
    if (!OpenClipboard(owner))
    {
        return text;
    }

    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != NULL)
    {
        const wchar_t* raw = (const wchar_t*)GlobalLock(handle);
        if (raw != NULL)
        {
            text = raw;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();

    text = TrimWide(text);
    if (text.size() > 512)
    {
        text.clear();
    }
    if (text.size() < 4 || ToLowerW(text.substr(0, 4)) != L"http")
    {
        text.clear();
    }
    return text;
}

INT_PTR CALLBACK UrlProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetDialogFont(dialog);
            SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)lParam);

            SetWindowTextW(dialog, LoadStr(IDS_TB_FROMURL).c_str());
            ApplyText(dialog, IDC_URL_INFO, IDS_URL_INFO);
            ApplyText(dialog, IDC_URL_HINT, IDS_URL_HINT);
            ApplyText(dialog, IDOK, IDS_BTN_OK);
            ApplyText(dialog, IDCANCEL, IDS_BTN_CANCEL);

            std::wstring fromClipboard = ClipboardUrl(dialog);
            if (!fromClipboard.empty())
            {
                SetDlgItemTextW(dialog, IDC_URL_EDIT, fromClipboard.c_str());
            }

            SendDlgItemMessageW(dialog, IDC_URL_EDIT, EM_SETSEL, 0, -1);
            SetFocus(GetDlgItem(dialog, IDC_URL_EDIT));
            return FALSE;
        }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            HWND edit = GetDlgItem(dialog, IDC_URL_EDIT);
            int length = GetWindowTextLengthW(edit);
            std::vector<wchar_t> buffer((size_t)length + 1);
            GetWindowTextW(edit, &buffer[0], length + 1);

            std::wstring text = TrimWide(std::wstring(&buffer[0], (size_t)length));
            if (text.empty())
            {
                MessageBeep(MB_ICONWARNING);
                SetFocus(edit);
                return TRUE;
            }

            std::wstring* result = (std::wstring*)GetWindowLongPtrW(dialog, DWLP_USER);
            if (result != NULL)
            {
                *result = text;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(dialog, IDCANCEL);
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

bool ShowUrlDialog(HWND parent, std::wstring& url)
{
    std::wstring entered;
    INT_PTR result = LxpDialogBoxParam(GetModuleHandleW(NULL), IDD_URL,
                                       parent, UrlProc, (LPARAM)&entered);
    if (result != IDOK || entered.empty())
    {
        return false;
    }
    url = entered;
    return true;
}

}  // namespace ui
}  // namespace lsxp
