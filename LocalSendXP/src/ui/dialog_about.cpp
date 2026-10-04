#include "lsxp/ui.h"
#include "resource.h"

#include <shellapi.h>

namespace lsxp {
namespace ui {

namespace {

// Shows the banner at the top of the dialog.  The template reserves 64 dialog
// units for it, which is exactly 96 pixels with the Chinese dialog font but can
// be less with a smaller one, so the rows below are pushed down whenever the
// bitmap does not fit that space.
void PlaceBanner(HWND dialog, HBITMAP banner)
{
    if (banner == NULL)
    {
        return;
    }

    BITMAP info;
    ZeroMemory(&info, sizeof(info));
    if (GetObject(banner, sizeof(info), &info) == 0 || info.bmWidth <= 0 || info.bmHeight <= 0)
    {
        return;
    }

    SendDlgItemMessageW(dialog, IDC_ABOUT_BANNER, STM_SETIMAGE, IMAGE_BITMAP, (LPARAM)banner);

    // The strip the template keeps free for the banner, in pixels.
    RECT reserved;
    reserved.left = 10;
    reserved.top = 10;
    reserved.right = 330;
    reserved.bottom = 74;
    MapDialogRect(dialog, &reserved);
    int reservedWidth = reserved.right - reserved.left;
    int reservedHeight = reserved.bottom - reserved.top;

    int extraWidth = (info.bmWidth > reservedWidth) ? (info.bmWidth - reservedWidth) : 0;
    int extraHeight = (info.bmHeight > reservedHeight) ? (info.bmHeight - reservedHeight) : 0;

    RECT window;
    GetWindowRect(dialog, &window);

    if (extraHeight > 0)
    {
        HWND child = GetWindow(dialog, GW_CHILD);
        while (child != NULL)
        {
            if (GetDlgCtrlID(child) != IDC_ABOUT_BANNER)
            {
                RECT rect;
                GetWindowRect(child, &rect);
                MapWindowPoints(NULL, dialog, (POINT*)&rect, 2);
                SetWindowPos(child, NULL, rect.left, rect.top + extraHeight,
                             rect.right - rect.left, rect.bottom - rect.top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            child = GetWindow(child, GW_HWNDNEXT);
        }
    }

    if (extraWidth > 0 || extraHeight > 0)
    {
        SetWindowPos(dialog, NULL, 0, 0,
                     (window.right - window.left) + extraWidth,
                     (window.bottom - window.top) + extraHeight,
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    int x = reserved.left + (reservedWidth - info.bmWidth) / 2;
    int y = reserved.top + (reservedHeight - info.bmHeight) / 2;
    if (x < 0)
    {
        x = 0;
    }
    if (y < 0)
    {
        y = 0;
    }
    SetWindowPos(GetDlgItem(dialog, IDC_ABOUT_BANNER), NULL, x, y,
                 info.bmWidth, info.bmHeight, SWP_NOZORDER | SWP_NOACTIVATE);
}

// Every language shares one dialog template, and its geometry is tuned for the
// short Chinese caption.  The English caption of the repository button is a lot
// wider, so measure the real text and widen the button to the left instead of
// letting the classic push button clip it.
void FitSourceButton(HWND dialog)
{
    HWND button = GetDlgItem(dialog, IDC_ABOUT_SOURCE);
    HWND okButton = GetDlgItem(dialog, IDOK);
    if (button == NULL || okButton == NULL)
    {
        return;
    }

    std::wstring caption = LoadStr(IDS_BTN_SOURCE);
    std::wstring plain;
    for (size_t i = 0; i < caption.size(); ++i)
    {
        if (caption[i] != L'&')          // the mnemonic marker is never drawn
        {
            plain += caption[i];
        }
    }

    HDC dc = GetDC(button);
    if (dc == NULL)
    {
        return;
    }
    HFONT font = (HFONT)SendMessageW(button, WM_GETFONT, 0, 0);
    if (font == NULL)
    {
        font = (HFONT)GetPropW(dialog, L"lsxpUiFont");
    }
    HGDIOBJ previous = (font != NULL) ? SelectObject(dc, font) : NULL;
    SIZE textSize;
    ZeroMemory(&textSize, sizeof(textSize));
    BOOL measured = GetTextExtentPoint32W(dc, plain.c_str(), (int)plain.size(), &textSize);
    if (previous != NULL)
    {
        SelectObject(dc, previous);
    }
    ReleaseDC(button, dc);

    if (!measured || textSize.cx <= 0)
    {
        return;
    }

    RECT buttonRect;
    RECT okRect;
    GetWindowRect(button, &buttonRect);
    GetWindowRect(okButton, &okRect);
    MapWindowPoints(NULL, dialog, (POINT*)&buttonRect, 2);
    MapWindowPoints(NULL, dialog, (POINT*)&okRect, 2);

    int width = buttonRect.right - buttonRect.left;
    // Room for the button frame plus the padding XP keeps around a caption.
    int needed = textSize.cx + GetSystemMetrics(SM_CXEDGE) * 4 + 24;
    if (needed <= width)
    {
        return;
    }

    int gap = okRect.left - buttonRect.right;
    if (gap < 0)
    {
        gap = 8;
    }
    int right = okRect.left - gap;
    int left = right - needed;
    if (left < 10)
    {
        left = 10;
    }
    SetWindowPos(button, NULL, left, buttonRect.top, right - left,
                 buttonRect.bottom - buttonRect.top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

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
            HFONT boldFont = CreateGuiFont(true);
            if (boldFont != NULL)
            {
                SendDlgItemMessageW(dialog, IDC_ABOUT_NAME, WM_SETFONT,
                                    (WPARAM)boldFont, TRUE);
                SetPropW(dialog, L"lsxpAboutBoldFont", (HANDLE)boldFont);
            }
            ApplyText(dialog, IDC_ABOUT_SUBTITLE, IDS_APP_SUBTITLE);
            ApplyText(dialog, IDC_ABOUT_INFO, IDS_ABOUT_INFO);
            ApplyText(dialog, IDC_ABOUT_PROTOCOL, IDS_ABOUT_PROTOCOL);
            ApplyText(dialog, IDC_ABOUT_CREDIT, IDS_ABOUT_CREDIT);
            ApplyText(dialog, IDC_ABOUT_AUTHOR, IDS_ABOUT_AUTHOR);
            ApplyText(dialog, IDC_ABOUT_SOURCE, IDS_BTN_SOURCE);
            ApplyText(dialog, IDOK, IDS_BTN_OK);

            HBITMAP banner = (HBITMAP)LoadImageW(GetModuleHandleW(NULL),
                                                 MAKEINTRESOURCEW(IDB_BANNER),
                                                 IMAGE_BITMAP, 0, 0, LR_DEFAULTCOLOR);
            if (banner != NULL)
            {
                SetWindowLongPtrW(dialog, DWLP_USER, (LONG_PTR)banner);
                PlaceBanner(dialog, banner);
            }

            FitSourceButton(dialog);
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
        {
            HFONT boldFont = (HFONT)RemovePropW(dialog, L"lsxpAboutBoldFont");
            if (boldFont != NULL)
            {
                DeleteObject(boldFont);
            }
            HBITMAP banner = (HBITMAP)GetWindowLongPtrW(dialog, DWLP_USER);
            if (banner != NULL)
            {
                DeleteObject(banner);
                SetWindowLongPtrW(dialog, DWLP_USER, 0);
            }
        }
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
