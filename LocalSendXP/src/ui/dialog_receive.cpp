#include "lsxp/ui.h"
#include "lsxp/app.h"
#include "resource.h"

#include <commctrl.h>

namespace lsxp {
namespace ui {

namespace {

INT_PTR CALLBACK ReceiveProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetDialogFont(dialog);

            IncomingPrompt* prompt = (IncomingPrompt*)lParam;
            SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)prompt);
            if (prompt == NULL)
            {
                EndDialog(dialog, IDCANCEL);
                return TRUE;
            }

            SetWindowTextW(dialog, LoadStr(IDS_RECEIVE_TITLE).c_str());

            std::wstring info = FormatStr(IDS_FMT_RECEIVE_INFO,
                                          Utf8ToWide(prompt->peerAlias).c_str(),
                                          Utf8ToWide(prompt->peerIp).c_str(),
                                          (int)prompt->files.size(),
                                          FormatBytesW(prompt->totalSize).c_str());
            SetDlgItemTextW(dialog, IDC_RECEIVE_INFO, info.c_str());

            HWND list = GetDlgItem(dialog, IDC_RECEIVE_LIST);
            ListView_SetExtendedListViewStyle(list,
                LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_CHECKBOXES);

            LVCOLUMNW column;
            ZeroMemory(&column, sizeof(column));
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            column.pszText = (LPWSTR)LoadStr(IDS_COL_FILE).c_str();
            column.cx = 200;
            column.iSubItem = 0;
            ListView_InsertColumn(list, 0, &column);

            std::wstring sizeColumn = LoadStr(IDS_COL_SIZE);
            column.pszText = (LPWSTR)sizeColumn.c_str();
            column.cx = 80;
            column.iSubItem = 1;
            ListView_InsertColumn(list, 1, &column);

            for (size_t i = 0; i < prompt->files.size(); ++i)
            {
                std::wstring name = Utf8ToWide(prompt->files[i].name);
                std::wstring size = FormatBytesW(prompt->files[i].size);

                LVITEMW item;
                ZeroMemory(&item, sizeof(item));
                item.mask = LVIF_TEXT;
                item.iItem = (int)i;
                item.pszText = (LPWSTR)name.c_str();
                int row = ListView_InsertItem(list, &item);
                ListView_SetItemText(list, row, 1, (LPWSTR)size.c_str());

                ListView_SetCheckState(list, row, prompt->files[i].accepted ? TRUE : FALSE);
            }

            SetFocus(dialog);
            return FALSE;
        }

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            IncomingPrompt* prompt = (IncomingPrompt*)GetWindowLongPtrW(dialog, DWLP_USER);
            if (prompt != NULL)
            {
                HWND list = GetDlgItem(dialog, IDC_RECEIVE_LIST);
                bool any = false;
                for (size_t i = 0; i < prompt->files.size(); ++i)
                {
                    bool checked = ListView_GetCheckState(list, (int)i) != 0;
                    prompt->files[i].accepted = checked;
                    if (checked)
                    {
                        any = true;
                    }
                }
                prompt->accepted = any;
            }
            EndDialog(dialog, IDOK);
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL)
        {
            IncomingPrompt* prompt = (IncomingPrompt*)GetWindowLongPtrW(dialog, DWLP_USER);
            if (prompt != NULL)
            {
                prompt->accepted = false;
            }
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        {
            IncomingPrompt* prompt = (IncomingPrompt*)GetWindowLongPtrW(dialog, DWLP_USER);
            if (prompt != NULL)
            {
                prompt->accepted = false;
            }
            EndDialog(dialog, IDCANCEL);
        }
        return TRUE;

    case WM_DESTROY:
        FreeDialogFont(dialog);
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

INT_PTR CALLBACK PinProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        SetDialogFont(dialog);
        SetFocus(GetDlgItem(dialog, IDC_PIN_EDIT));
        return FALSE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK)
        {
            std::string* pin = (std::string*)GetWindowLongPtrW(dialog, DWLP_USER);
            HWND edit = GetDlgItem(dialog, IDC_PIN_EDIT);
            int length = GetWindowTextLengthW(edit);
            std::vector<wchar_t> buffer((size_t)length + 1);
            GetWindowTextW(edit, &buffer[0], length + 1);
            if (pin != NULL)
            {
                *pin = WideToUtf8(std::wstring(&buffer[0], (size_t)length));
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

int ShowReceiveDialog(HWND parent, IncomingPrompt* prompt)
{
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_RECEIVE),
                                     parent, ReceiveProc, (LPARAM)prompt);
    return (int)result;
}

bool ShowPinDialog(HWND parent, std::string& pin)
{
    std::string entered;
    INT_PTR result = DialogBoxParamW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDD_PIN),
                                     parent, PinProc, (LPARAM)&entered);
    if (result != IDOK || entered.empty())
    {
        return false;
    }
    pin = entered;
    return true;
}

}  // namespace ui
}  // namespace lsxp
