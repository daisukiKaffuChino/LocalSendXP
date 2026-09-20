#include "lsxp/ui.h"
#include "lsxp/history.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <windowsx.h>

namespace lsxp {
namespace ui {

namespace {

const int kColumnCount = 5;
const int kColumnTextIds[kColumnCount] =
{
    IDS_HIST_COL_TIME, IDS_HIST_COL_PEER, IDS_COL_FILE,
    IDS_COL_SIZE, IDS_HIST_COL_LOCATION
};
const int kColumnWidths[kColumnCount] = { 130, 120, 180, 80, 220 };
const int kFixedColumnsWidth = 130 + 120 + 180 + 80;

// Dialog units -> pixels, done once per layout so that the dialog keeps
// looking right with a different dialog font.
struct DialogMetrics
{
    int x100;
    int y100;
};

DialogMetrics MeasureDialog(HWND dialog)
{
    RECT unit;
    unit.left = 0;
    unit.top = 0;
    unit.right = 100;
    unit.bottom = 100;
    MapDialogRect(dialog, &unit);

    DialogMetrics metrics;
    metrics.x100 = (unit.right > 0) ? unit.right : 100;
    metrics.y100 = (unit.bottom > 0) ? unit.bottom : 100;
    return metrics;
}

int ScaleX(const DialogMetrics& metrics, int value)
{
    return (value * metrics.x100 + 50) / 100;
}

int ScaleY(const DialogMetrics& metrics, int value)
{
    return (value * metrics.y100 + 50) / 100;
}

std::wstring ControlText(HWND control)
{
    if (control == NULL)
    {
        return std::wstring();
    }
    int length = GetWindowTextLengthW(control);
    std::vector<wchar_t> buffer((size_t)length + 1);
    GetWindowTextW(control, &buffer[0], length + 1);
    return std::wstring(&buffer[0], (size_t)length);
}

// Push buttons are sized from their own caption so that the English labels
// still fit inside them.
int ButtonWidth(HWND button, int minimum)
{
    int width = minimum;
    if (button == NULL)
    {
        return width;
    }

    std::wstring text = ControlText(button);
    HDC dc = CreateCompatibleDC(NULL);
    if (dc != NULL)
    {
        HFONT font = (HFONT)SendMessageW(button, WM_GETFONT, 0, 0);
        HGDIOBJ previous = (font != NULL) ? SelectObject(dc, font) : NULL;
        SIZE size;
        ZeroMemory(&size, sizeof(size));
        if (!text.empty())
        {
            GetTextExtentPoint32W(dc, text.c_str(), (int)text.size(), &size);
        }
        if (size.cx + 20 > width)
        {
            width = size.cx + 20;
        }
        if (previous != NULL)
        {
            SelectObject(dc, previous);
        }
        DeleteDC(dc);
    }
    return width;
}

void LayoutHistoryDialog(HWND dialog)
{
    DialogMetrics metrics = MeasureDialog(dialog);

    RECT client;
    GetClientRect(dialog, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;

    int marginX = ScaleX(metrics, 7);
    int marginY = ScaleY(metrics, 6);
    int buttonHeight = ScaleY(metrics, 14);
    int gap = ScaleX(metrics, 5);

    int summaryY = marginY;
    HWND summary = GetDlgItem(dialog, IDC_HIST_SUMMARY);
    if (summary != NULL)
    {
        MoveWindow(summary, marginX, summaryY, width - 2 * marginX, ScaleY(metrics, 10), TRUE);
    }

    int listTop = ScaleY(metrics, 20);
    int buttonsY = height - marginY - buttonHeight;
    int hintY = buttonsY - ScaleY(metrics, 15);
    int listBottom = hintY - ScaleY(metrics, 3);

    int listHeight = listBottom - listTop;
    if (listHeight < ScaleY(metrics, 60))
    {
        listHeight = ScaleY(metrics, 60);
    }

    HWND list = GetDlgItem(dialog, IDC_HIST_LIST);
    if (list != NULL)
    {
        MoveWindow(list, marginX, listTop, width - 2 * marginX, listHeight, TRUE);

        // The last column takes the remaining room so that long paths are
        // visible without scrolling sideways.
        int available = (width - 2 * marginX) - kFixedColumnsWidth;
        int lastWidth = (available > 180) ? available : 180;
        ListView_SetColumnWidth(list, kColumnCount - 1, lastWidth);
    }

    HWND hint = GetDlgItem(dialog, IDC_HIST_HINT);
    if (hint != NULL)
    {
        MoveWindow(hint, marginX, hintY, width - 2 * marginX, ScaleY(metrics, 10), TRUE);
    }

    // Minimum widths in dialog units, sized for the longest label of both
    // languages ("Open containing folder" is the widest one).
    const int buttonIds[4] = { IDCANCEL, IDC_HIST_CLEAR, IDC_HIST_DELETE, IDC_HIST_OPEN };
    const int buttonMinDu[4] = { 50, 72, 76, 114 };
    int x = width - marginX;
    for (int i = 0; i < 4; ++i)
    {
        HWND button = GetDlgItem(dialog, buttonIds[i]);
        if (button == NULL)
        {
            continue;
        }
        int buttonWidth = ButtonWidth(button, ScaleX(metrics, buttonMinDu[i]));
        x -= buttonWidth;
        MoveWindow(button, x, buttonsY, buttonWidth, buttonHeight, TRUE);
        x -= gap;
    }
}

void SetListColumnText(HWND list, int column, const std::wstring& text)
{
    LVCOLUMNW info;
    ZeroMemory(&info, sizeof(info));
    info.mask = LVCF_TEXT;
    info.pszText = (LPWSTR)text.c_str();
    SendMessageW(list, LVM_SETCOLUMN, (WPARAM)column, (LPARAM)&info);
}

void ApplyHistoryTexts(HWND dialog)
{
    SetWindowTextW(dialog, LoadStr(IDS_HISTORY_TITLE).c_str());
    ApplyText(dialog, IDC_HIST_OPEN, IDS_HIST_OPEN);
    ApplyText(dialog, IDC_HIST_DELETE, IDS_HIST_DELETE);
    ApplyText(dialog, IDC_HIST_CLEAR, IDS_HIST_CLEAR);
    ApplyText(dialog, IDCANCEL, IDS_BTN_CLOSE);
    ApplyText(dialog, IDC_HIST_HINT, IDS_HIST_HINT);

    HWND list = GetDlgItem(dialog, IDC_HIST_LIST);
    for (int i = 0; i < kColumnCount; ++i)
    {
        SetListColumnText(list, i, LoadStr(kColumnTextIds[i]));
    }
}

void FillHistoryList(HWND dialog)
{
    HWND list = GetDlgItem(dialog, IDC_HIST_LIST);
    if (list == NULL)
    {
        return;
    }

    SendMessageW(list, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(list);

    HistoryStore& store = HistoryStore::Instance();
    int count = store.Count();
    for (int i = 0; i < count; ++i)
    {
        HistoryEntry entry;
        if (!store.Get(i, entry))
        {
            continue;
        }

        std::wstring peer = Utf8ToWide(entry.peerAlias);
        if (peer.empty())
        {
            peer = Utf8ToWide(entry.peerIp);
        }
        if (peer.empty())
        {
            peer = L"-";
        }

        std::wstring fileName = Utf8ToWide(entry.fileName);
        if (fileName.empty())
        {
            fileName = L"-";
        }

        std::wstring location = entry.path;
        if (!FileExistsW(entry.path))
        {
            location += LoadStr(IDS_HIST_MISSING);
        }

        LVITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = i;
        item.iSubItem = 0;
        item.pszText = (LPWSTR)entry.time.c_str();
        item.lParam = (LPARAM)i;

        int row = ListView_InsertItem(list, &item);
        if (row < 0)
        {
            continue;
        }

        const wchar_t* texts[4];
        std::wstring sizeText = FormatBytesW(entry.size);
        texts[0] = peer.c_str();
        texts[1] = fileName.c_str();
        texts[2] = sizeText.c_str();
        texts[3] = location.c_str();
        for (int column = 0; column < 4; ++column)
        {
            ListView_SetItemText(list, row, column + 1, (LPWSTR)texts[column]);
        }
    }

    SendMessageW(list, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(list, NULL, TRUE);

    std::wstring summary = (count > 0)
                           ? FormatStr(IDS_HIST_SUMMARY, count)
                           : LoadStr(IDS_HIST_EMPTY);
    SetDlgItemTextW(dialog, IDC_HIST_SUMMARY, summary.c_str());

    EnableWindow(GetDlgItem(dialog, IDC_HIST_OPEN), count > 0);
    EnableWindow(GetDlgItem(dialog, IDC_HIST_DELETE), count > 0);
    EnableWindow(GetDlgItem(dialog, IDC_HIST_CLEAR), count > 0);
}

// Reads the selection as indexes into the history store.
void SelectedIndexes(HWND list, std::vector<int>& indexes)
{
    indexes.clear();
    if (list == NULL)
    {
        return;
    }

    int item = -1;
    while ((item = ListView_GetNextItem(list, item, LVNI_SELECTED)) != -1)
    {
        LVITEMW info;
        ZeroMemory(&info, sizeof(info));
        info.mask = LVIF_PARAM;
        info.iItem = item;
        if (SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&info))
        {
            indexes.push_back((int)info.lParam);
        }
    }
}

void RevealEntry(HWND dialog, int index)
{
    HistoryEntry entry;
    if (!HistoryStore::Instance().Get(index, entry))
    {
        return;
    }
    if (!RevealPathInExplorer(entry.path))
    {
        MessageBoxW(dialog, FormatStr(IDS_HIST_OPENFAIL, entry.path.c_str()).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONWARNING | MB_OK);
    }
}

void OnOpenSelected(HWND dialog, int clickedItem)
{
    HWND list = GetDlgItem(dialog, IDC_HIST_LIST);
    if (list == NULL)
    {
        return;
    }

    int item = clickedItem;
    if (item < 0)
    {
        item = ListView_GetNextItem(list, -1, LVNI_SELECTED);
    }
    if (item < 0)
    {
        MessageBoxW(dialog, LoadStr(IDS_HIST_NO_SELECTION).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }

    LVITEMW info;
    ZeroMemory(&info, sizeof(info));
    info.mask = LVIF_PARAM;
    info.iItem = item;
    if (SendMessageW(list, LVM_GETITEMW, 0, (LPARAM)&info))
    {
        RevealEntry(dialog, (int)info.lParam);
    }
}

void OnDeleteSelected(HWND dialog)
{
    HWND list = GetDlgItem(dialog, IDC_HIST_LIST);
    std::vector<int> indexes;
    SelectedIndexes(list, indexes);
    if (indexes.empty())
    {
        MessageBoxW(dialog, LoadStr(IDS_HIST_NO_SELECTION).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }

    if (MessageBoxW(dialog, FormatStr(IDS_HIST_DELETE_CONFIRM, (int)indexes.size()).c_str(),
                    LoadStr(IDS_HISTORY_TITLE).c_str(),
                    MB_ICONQUESTION | MB_YESNO) != IDYES)
    {
        return;
    }

    int removed = HistoryStore::Instance().Remove(indexes);
    FillHistoryList(dialog);
    if (removed > 0)
    {
        UpdateStatusText(App::Instance().MainWindow(),
                         FormatStr(IDS_HIST_DELETED, removed));
    }
}

void OnClearHistory(HWND dialog)
{
    int count = HistoryStore::Instance().Count();
    if (count <= 0)
    {
        return;
    }

    if (MessageBoxW(dialog, FormatStr(IDS_HIST_CLEAR_CONFIRM, count).c_str(),
                    LoadStr(IDS_HISTORY_TITLE).c_str(),
                    MB_ICONQUESTION | MB_YESNO) != IDYES)
    {
        return;
    }

    int removed = HistoryStore::Instance().Clear();
    FillHistoryList(dialog);
    if (removed > 0)
    {
        UpdateStatusText(App::Instance().MainWindow(),
                         FormatStr(IDS_HIST_DELETED, removed));
    }
}

int ItemAtPoint(HWND list, POINT screenPoint)
{
    POINT cursor = screenPoint;
    ScreenToClient(list, &cursor);

    LVHITTESTINFO hit;
    ZeroMemory(&hit, sizeof(hit));
    hit.pt = cursor;
    return ListView_HitTest(list, &hit);
}

void ShowHistoryMenu(HWND dialog, HWND list, int item, POINT screenPoint)
{
    HMENU menu = CreatePopupMenu();
    if (menu == NULL)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, IDC_HIST_OPEN, LoadStr(IDS_HIST_OPEN).c_str());
    AppendMenuW(menu, MF_STRING, IDC_HIST_DELETE, LoadStr(IDS_HIST_DELETE).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDC_HIST_CLEAR, LoadStr(IDS_HIST_CLEAR).c_str());

    if (screenPoint.x < 0 && screenPoint.y < 0)
    {
        GetCursorPos(&screenPoint);
    }

    UINT flags = TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD;
    if (item >= 0)
    {
        ListView_SetItemState(list, -1, 0, LVIS_SELECTED);
        ListView_SetItemState(list, item, LVIS_SELECTED, LVIS_SELECTED);
    }

    UINT command = TrackPopupMenu(menu, flags, screenPoint.x, screenPoint.y, 0, dialog, NULL);
    DestroyMenu(menu);

    switch (command)
    {
    case IDC_HIST_OPEN:
        OnOpenSelected(dialog, item);
        break;
    case IDC_HIST_DELETE:
        OnDeleteSelected(dialog);
        break;
    case IDC_HIST_CLEAR:
        OnClearHistory(dialog);
        break;
    default:
        break;
    }
}

INT_PTR CALLBACK HistoryProc(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        {
            SetDialogFont(dialog);

            HINSTANCE instance = GetModuleHandleW(NULL);
            HICON icon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_TOOL_HISTORY),
                                           IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
            if (icon != NULL)
            {
                SendMessageW(dialog, WM_SETICON, ICON_BIG, (LPARAM)icon);
            }
            HICON small = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(IDI_TOOL_HISTORY),
                                            IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
            if (small != NULL)
            {
                SendMessageW(dialog, WM_SETICON, ICON_SMALL, (LPARAM)small);
            }

            HWND list = GetDlgItem(dialog, IDC_HIST_LIST);
            for (int i = 0; i < kColumnCount; ++i)
            {
                LVCOLUMNW column;
                ZeroMemory(&column, sizeof(column));
                column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                column.cx = kColumnWidths[i];
                column.iSubItem = i;
                column.pszText = (LPWSTR)L"";
                ListView_InsertColumn(list, i, &column);
            }
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
            SendMessageW(list, LVM_SETBKCOLOR, 0, (LPARAM)GetSysColor(COLOR_WINDOW));

            ApplyHistoryTexts(dialog);
            FillHistoryList(dialog);
            LayoutHistoryDialog(dialog);

            if (ListView_GetItemCount(list) > 0)
            {
                ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
            }
            SetFocus(list);
            return FALSE;
        }

    case WM_SIZE:
        LayoutHistoryDialog(dialog);
        return TRUE;

    case WM_GETMINMAXINFO:
        {
            DialogMetrics metrics = MeasureDialog(dialog);
            MINMAXINFO* info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = ScaleX(metrics, 300);
            info->ptMinTrackSize.y = ScaleY(metrics, 170);
        }
        return TRUE;

    case WM_NOTIFY:
        if (((NMHDR*)lParam)->idFrom == IDC_HIST_LIST)
        {
            if (((NMHDR*)lParam)->code == NM_DBLCLK)
            {
                OnOpenSelected(dialog, ((NMITEMACTIVATE*)lParam)->iItem);
                return TRUE;
            }
        }
        return FALSE;

    case WM_CONTEXTMENU:
        if ((HWND)wParam == GetDlgItem(dialog, IDC_HIST_LIST))
        {
            HWND list = GetDlgItem(dialog, IDC_HIST_LIST);
            POINT point;
            point.x = GET_X_LPARAM(lParam);
            point.y = GET_Y_LPARAM(lParam);
            ShowHistoryMenu(dialog, list, ItemAtPoint(list, point), point);
            return TRUE;
        }
        return FALSE;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDC_HIST_OPEN:
        case IDOK:
            OnOpenSelected(dialog, -1);
            return TRUE;

        case IDC_HIST_DELETE:
            OnDeleteSelected(dialog);
            return TRUE;

        case IDC_HIST_CLEAR:
            OnClearHistory(dialog);
            return TRUE;

        case IDCANCEL:
            EndDialog(dialog, IDCANCEL);
            return TRUE;

        default:
            break;
        }
        break;

    case WM_CLOSE:
        EndDialog(dialog, IDCANCEL);
        return TRUE;

    case WM_DESTROY:
        {
            HICON icon = (HICON)SendMessageW(dialog, WM_GETICON, ICON_BIG, 0);
            if (icon != NULL)
            {
                DestroyIcon(icon);
            }
            HICON small = (HICON)SendMessageW(dialog, WM_GETICON, ICON_SMALL, 0);
            if (small != NULL)
            {
                DestroyIcon(small);
            }
            FreeDialogFont(dialog);
        }
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

}  // namespace

bool ShowHistoryDialog(HWND parent)
{
    LxpDialogBoxParam(GetModuleHandleW(NULL), IDD_HISTORY, parent, HistoryProc, 0);
    return true;
}

}  // namespace ui
}  // namespace lsxp
