#include "lsxp/ui.h"
#include "resource.h"

#include <shellapi.h>

namespace lsxp {
namespace ui {

namespace {

NOTIFYICONDATAW g_tray;
bool g_trayCreated = false;

const UINT kTrayId = 1;

}  // namespace

bool TrayCreate(HWND owner)
{
    if (g_trayCreated)
    {
        return true;
    }

    ZeroMemory(&g_tray, sizeof(g_tray));
    g_tray.cbSize = sizeof(NOTIFYICONDATAW);
    g_tray.hWnd = owner;
    g_tray.uID = kTrayId;
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_tray.uCallbackMessage = WM_APP_TRAY;
    g_tray.hIcon = LoadAppIcon(16);
    lstrcpynW(g_tray.szTip, LoadStr(IDS_APP_TITLE).c_str(), 128);

    if (!Shell_NotifyIconW(NIM_ADD, &g_tray))
    {
        return false;
    }
    g_trayCreated = true;
    return true;
}

void TrayRemove(HWND owner)
{
    (void)owner;
    if (!g_trayCreated)
    {
        return;
    }
    Shell_NotifyIconW(NIM_DELETE, &g_tray);
    g_trayCreated = false;
}

void TraySetTip(HWND owner, const std::wstring& tip)
{
    (void)owner;
    if (!g_trayCreated)
    {
        return;
    }
    lstrcpynW(g_tray.szTip, tip.c_str(), 128);
    g_tray.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_tray);
    g_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
}

void TrayShowBalloon(HWND owner, const std::wstring& title, const std::wstring& text)
{
    (void)owner;
    if (!g_trayCreated)
    {
        return;
    }

    NOTIFYICONDATAW data = g_tray;
    data.uFlags = NIF_INFO;
    data.dwInfoFlags = NIIF_INFO;
    lstrcpynW(data.szInfoTitle, title.c_str(), 64);
    lstrcpynW(data.szInfo, text.c_str(), 256);
    Shell_NotifyIconW(NIM_MODIFY, &data);
}

void TrayShowMenu(HWND owner)
{
    if (!g_trayCreated)
    {
        return;
    }

    HMENU menu = CreatePopupMenu();
    if (menu == NULL)
    {
        return;
    }

    AppendMenuW(menu, MF_STRING, IDM_DEVICE_REFRESH, LoadStr(IDS_MENU_SHOW).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_FILE_SEND, LoadStr(IDS_MENU_SEND).c_str());
    AppendMenuW(menu, MF_STRING, IDM_DEVICE_OPENFOLDER, LoadStr(IDS_MENU_OPENFOLDER).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_TOOLS_SETTINGS, LoadStr(IDS_MENU_SETTINGS).c_str());
    AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(menu, MF_STRING, IDM_FILE_EXIT, LoadStr(IDS_MENU_EXIT).c_str());

    POINT cursor;
    GetCursorPos(&cursor);
    SetForegroundWindow(owner);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, owner, NULL);
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

}  // namespace ui
}  // namespace lsxp
