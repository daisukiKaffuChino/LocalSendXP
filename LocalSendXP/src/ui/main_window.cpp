#include "lsxp/ui.h"
#include "lsxp/app.h"
#include "resource.h"

#include <commctrl.h>
#include <shellapi.h>
#include <stdio.h>

namespace lsxp {
namespace ui {

const TCHAR* const MAIN_WINDOW_CLASS = L"LocalSendXP_MainWindow";

namespace {

struct WindowState
{
    WindowState();

    HWND toolbar;
    HWND deviceList;
    HWND transferList;
    HWND statusBar;
    HWND progress;
    HWND sendButton;
    HWND refreshButton;
    HWND settingsButton;
    HWND deviceGroup;
    HWND transferGroup;
    HFONT font;
    HIMAGELIST toolbarImages;
    std::vector<Device> deviceCache;
    std::vector<long>   transferRowIds;
    std::vector<int>    transferPercents;
    std::vector<std::wstring> toolbarTexts;
    bool trayActive;
    bool modalActive;
};

WindowState::WindowState()
    : toolbar(NULL),
      deviceList(NULL),
      transferList(NULL),
      statusBar(NULL),
      progress(NULL),
      sendButton(NULL),
      refreshButton(NULL),
      settingsButton(NULL),
      deviceGroup(NULL),
      transferGroup(NULL),
      font(NULL),
      toolbarImages(NULL),
      trayActive(false),
      modalActive(false)
{
}

WindowState g_window;

const int kDeviceColumns = 5;
const int kTransferColumns = 6;
const int kMaxSendFiles = 2000;

std::wstring LastSeenText(const Device& device)
{
    DWORD age = TickCount() - device.lastSeen;
    if (age < 5000)
    {
        return LoadStr(IDS_TIME_JUSTNOW);
    }
    if (age < 60000)
    {
        return FormatStr(IDS_TIME_SECONDS_AGO, (int)(age / 1000));
    }
    return FormatStr(IDS_TIME_MINUTES_AGO, (int)(age / 60000));
}

void UpdateStatusParts(HWND hwnd)
{
    int widths[3];
    RECT client;
    GetClientRect(hwnd, &client);
    widths[0] = (client.right * 55) / 100;
    widths[1] = (client.right * 78) / 100;
    widths[2] = -1;
    SendMessageW(g_window.statusBar, SB_SETPARTS, 3, (LPARAM)widths);

    int deviceCount = (int)App::Instance().Devices().Count();
    SendMessageW(g_window.statusBar, SB_SETTEXTW, 1,
                 (LPARAM)FormatStr(IDS_STATUS_DEVFMT, deviceCount).c_str());

    Config& config = App::Instance().GetConfig();
    std::wstring self = FormatStr(IDS_STATUS_SELFFMT,
                                  config.alias.c_str(),
                                  Utf8ToWide(GetPrimaryLocalIPv4()).c_str(),
                                  config.port);
    SendMessageW(g_window.statusBar, SB_SETTEXTW, 2, (LPARAM)self.c_str());
}

void OnSendFiles(HWND hwnd)
{
    bool hasSelection = false;
    Device device = SelectedDevice(hwnd, &hasSelection);
    if (!hasSelection)
    {
        MessageBoxW(hwnd, LoadStr(IDS_MSG_SELECT_DEVICE).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }

    std::vector<std::wstring> files;
    if (!ShowOpenFilesDialog(hwnd, files))
    {
        return;
    }
    App::Instance().SendFiles(device, files);
}

void OnSendFolder(HWND hwnd)
{
    bool hasSelection = false;
    Device device = SelectedDevice(hwnd, &hasSelection);
    if (!hasSelection)
    {
        MessageBoxW(hwnd, LoadStr(IDS_MSG_SELECT_DEVICE).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }

    std::wstring folder;
    if (!ShowBrowseFolderDialog(hwnd, folder))
    {
        return;
    }

    std::vector<std::wstring> files;
    int count = CollectFilesW(folder, files, kMaxSendFiles);
    if (count <= 0)
    {
        MessageBoxW(hwnd, LoadStr(IDS_MSG_NO_FILES).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }
    if (count >= kMaxSendFiles)
    {
        MessageBoxW(hwnd, FormatStr(IDS_MSG_FILES_SKIPPED, kMaxSendFiles).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
    }
    App::Instance().SendFiles(device, files);
}

void OnShareFiles(HWND hwnd)
{
    std::vector<std::wstring> files;
    if (!ShowOpenFilesDialog(hwnd, files))
    {
        return;
    }
    App::Instance().ShareFiles(files);
}

void OnAbout(HWND hwnd)
{
    ShowAboutDialog(hwnd);
}

void OnSettings(HWND hwnd)
{
    ShowSettingsDialog(hwnd);
}

void OnOpenLog(HWND hwnd)
{
    std::wstring path = LogPath();
    if (path.empty() || !FileExistsW(path))
    {
        MessageBoxW(hwnd, LoadStr(IDS_LOG_OPENFAIL).c_str(), LoadStr(IDS_APP_TITLE).c_str(),
                    MB_ICONINFORMATION | MB_OK);
        return;
    }
    OpenPathWithShell(path);
}

void OnHelp(HWND hwnd)
{
    ShowHelpDialog(hwnd);
}

void OnProtocolPage()
{
    ShellExecuteW(NULL, L"open", L"https://github.com/localsend/protocol",
                  NULL, NULL, SW_SHOWNORMAL);
}

void HideToTray(HWND hwnd)
{
    ShowWindow(hwnd, SW_HIDE);
    TraySetTip(hwnd, LoadStr(IDS_MSG_TRAYHINT));
    TrayShowBalloon(hwnd, LoadStr(IDS_APP_TITLE), LoadStr(IDS_MSG_TRAYHINT));
}

void RestoreFromTray(HWND hwnd)
{
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    TraySetTip(hwnd, LoadStr(IDS_APP_TITLE));
}

void OnCancelTransfer(HWND hwnd)
{
    long id = SelectedTransferId(hwnd);
    if (id == 0)
    {
        return;
    }
    App::Instance().CancelTransfer(id);
}

void HandleDropFiles(HWND hwnd, HDROP drop)
{
    UINT count = DragQueryFileW(drop, 0xFFFFFFFF, NULL, 0);
    std::vector<std::wstring> files;
    for (UINT i = 0; i < count; ++i)
    {
        UINT length = DragQueryFileW(drop, i, NULL, 0);
        std::vector<wchar_t> buffer(length + 1);
        DragQueryFileW(drop, i, &buffer[0], length + 1);
        files.push_back(std::wstring(&buffer[0], length));
    }
    DragFinish(drop);

    if (files.empty())
    {
        return;
    }

    std::vector<std::wstring> expanded;
    ExpandSelectionW(files, expanded, kMaxSendFiles);
    if (expanded.empty())
    {
        return;
    }

    bool hasSelection = false;
    Device device = SelectedDevice(hwnd, &hasSelection);
    if (!hasSelection)
    {
        MessageBoxW(hwnd, LoadStr(IDS_MSG_SELECT_DEVICE).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }
    App::Instance().SendFiles(device, expanded);
}

// Owner drawn progress bar in the "progress" column, the way the download
// managers of that era did it.
void DrawRowProgress(HDC dc, const RECT& rect, int percent)
{
    if (percent < 0)
    {
        percent = 0;
    }
    if (percent > 100)
    {
        percent = 100;
    }

    RECT outer = rect;
    InflateRect(&outer, -1, -1);
    DrawEdge(dc, &outer, EDGE_SUNKEN, BF_RECT);

    RECT inner = outer;
    InflateRect(&inner, -1, -1);
    if (inner.right <= inner.left || inner.bottom <= inner.top)
    {
        return;
    }

    HBRUSH back = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
    FillRect(dc, &inner, back);
    DeleteObject(back);

    if (percent > 0)
    {
        RECT fill = inner;
        int width = inner.right - inner.left;
        fill.right = fill.left + (width * percent) / 100;
        HBRUSH bar = CreateSolidBrush(RGB(49, 106, 197));
        FillRect(dc, &fill, bar);
        DeleteObject(bar);
    }

    wchar_t text[16];
    _snwprintf(text, 15, L"%d%%", percent);
    text[15] = L'\0';

    int oldMode = SetBkMode(dc, TRANSPARENT);
    COLORREF oldColor = SetTextColor(dc, RGB(0, 0, 0));
    RECT textRect = inner;
    DrawTextW(dc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetTextColor(dc, oldColor);
    SetBkMode(dc, oldMode);
}

LRESULT HandleTransferCustomDraw(NMLVCUSTOMDRAW* draw)
{
    switch (draw->nmcd.dwDrawStage)
    {
    case CDDS_PREPAINT:
        return CDRF_NOTIFYITEMDRAW;

    case CDDS_ITEMPREPAINT:
        return CDRF_NOTIFYSUBITEMDRAW;

    case CDDS_ITEMPREPAINT | CDDS_SUBITEM:
        if (draw->iSubItem == 2)
        {
            int row = (int)draw->nmcd.dwItemSpec;
            int percent = 0;
            if (row >= 0 && row < (int)g_window.transferPercents.size())
            {
                percent = g_window.transferPercents[row];
            }
            DrawRowProgress(draw->nmcd.hdc, draw->nmcd.rc, percent);
            return CDRF_SKIPDEFAULT;
        }
        break;

    default:
        break;
    }
    return CDRF_DODEFAULT;
}

void HandleUiEvents(HWND hwnd)
{
    std::vector<App::UiEvent> events;
    App& app = App::Instance();
    app.DrainUiEvents(events);

    bool needDevices = false;
    bool needTransfers = false;
    bool needProgress = false;

    for (size_t i = 0; i < events.size(); ++i)
    {
        switch (events[i].type)
        {
        case App::UI_EV_DEVICES:
            needDevices = true;
            break;
        case App::UI_EV_TRANSFERS:
            needTransfers = true;
            needProgress = true;
            break;
        case App::UI_EV_PROGRESS:
            needProgress = true;
            break;
        case App::UI_EV_INCOMING:
            {
                IncomingPrompt* prompt = (IncomingPrompt*)events[i].data;
                if (prompt == NULL)
                {
                    break;
                }
                if (g_window.modalActive)
                {
                    prompt->accepted = false;
                }
                else
                {
                    MessageBeep(MB_ICONASTERISK);
                    g_window.modalActive = true;
                    ShowReceiveDialog(hwnd, prompt);
                    g_window.modalActive = false;
                }
                if (prompt->doneEvent != NULL)
                {
                    SetEvent(prompt->doneEvent);
                }
            }
            break;
        case App::UI_EV_PIN:
            {
                App::PinPrompt* prompt = (App::PinPrompt*)events[i].data;
                if (prompt == NULL)
                {
                    break;
                }
                std::string pin;
                if (!g_window.modalActive && ShowPinDialog(hwnd, pin))
                {
                    prompt->pin = pin;
                    prompt->accepted = true;
                }
                else
                {
                    prompt->accepted = false;
                }
                if (prompt->doneEvent != NULL)
                {
                    SetEvent(prompt->doneEvent);
                }
            }
            break;
        case App::UI_EV_TRAY_HINT:
            {
                App::BalloonInfo* info = (App::BalloonInfo*)events[i].data;
                if (info != NULL)
                {
                    TrayShowBalloon(hwnd, info->title, info->text);
                    delete info;
                }
            }
            break;
        default:
            break;
        }
    }

    if (needDevices)
    {
        RefreshDeviceList(hwnd);
    }
    if (needTransfers)
    {
        RefreshTransferList(hwnd);
    }
    if (needProgress)
    {
        UpdateProgressBar(hwnd);
    }
}

void CreateChildren(HWND hwnd, HINSTANCE instance)
{
    g_window.font = CreateGuiFont(false);

    g_window.toolbar = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL,
                                       WS_CHILD | WS_VISIBLE | TBSTYLE_FLAT | TBSTYLE_LIST |
                                       TBSTYLE_TOOLTIPS | CCS_TOP | CCS_NOPARENTALIGN,
                                       0, 0, 10, 24, hwnd,
                                       (HMENU)IDC_TOOLBAR, instance, NULL);
    if (g_window.toolbar != NULL)
    {
        SendMessageW(g_window.toolbar, TB_BUTTONSTRUCTSIZE, (WPARAM)sizeof(TBBUTTON), 0);
        g_window.toolbarImages = CreateToolbarImages();
        if (g_window.toolbarImages != NULL)
        {
            SendMessageW(g_window.toolbar, TB_SETIMAGELIST, 0, (LPARAM)g_window.toolbarImages);
        }
        SendMessageW(g_window.toolbar, TB_SETBITMAPSIZE, 0, MAKELPARAM(32, 32));
        SendMessageW(g_window.toolbar, TB_SETPADDING, 0, MAKELPARAM(8, 5));
        SendMessageW(g_window.toolbar, TB_SETMAXTEXTROWS, 1, 0);

        const int buttonCount = 5;
        const int commandIds[5] =
        {
            IDM_FILE_SENDFOLDER, IDM_FILE_FROMURL, IDM_DEVICE_OPENFOLDER,
            IDM_TOOLS_SETTINGS, IDM_HELP_ABOUT
        };
        const int textIds[5] =
        {
            IDS_TB_SENDFOLDER, IDS_TB_FROMURL, IDS_TB_OPENFOLDER,
            IDS_TB_SETTINGS, IDS_TB_ABOUT
        };
        const int imageIds[5] = { 0, 1, 2, 3, 4 };

        g_window.toolbarTexts.clear();
        for (int i = 0; i < buttonCount; ++i)
        {
            g_window.toolbarTexts.push_back(LoadStr(textIds[i]));
        }

        TBBUTTON buttons[5];
        ZeroMemory(buttons, sizeof(buttons));
        for (int i = 0; i < buttonCount; ++i)
        {
            buttons[i].iBitmap = imageIds[i];
            buttons[i].idCommand = commandIds[i];
            buttons[i].fsState = TBSTATE_ENABLED;
            buttons[i].fsStyle = TBSTYLE_BUTTON | TBSTYLE_AUTOSIZE;
            buttons[i].iString = (INT_PTR)g_window.toolbarTexts[i].c_str();
        }
        SendMessageW(g_window.toolbar, TB_ADDBUTTONS, (WPARAM)buttonCount, (LPARAM)buttons);
        SendMessageW(g_window.toolbar, TB_AUTOSIZE, 0, 0);
    }

    g_window.deviceGroup = CreateWindowExW(0, L"BUTTON", LoadStr(IDS_GROUP_DEVICES).c_str(),
                                           WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                           0, 0, 10, 10, hwnd,
                                           (HMENU)IDC_DEVICE_GROUP, instance, NULL);

    g_window.deviceList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                          LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                          0, 0, 10, 10, hwnd,
                                          (HMENU)IDC_DEVICE_LIST, instance, NULL);

    g_window.transferGroup = CreateWindowExW(0, L"BUTTON", LoadStr(IDS_GROUP_TRANSFERS).c_str(),
                                             WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                             0, 0, 10, 10, hwnd,
                                             (HMENU)IDC_TRANSFER_GROUP, instance, NULL);

    g_window.transferList = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                            WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                            LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                            0, 0, 10, 10, hwnd,
                                            (HMENU)IDC_TRANSFER_LIST, instance, NULL);

    g_window.progress = CreateWindowExW(0, PROGRESS_CLASSW, L"",
                                        WS_CHILD | WS_VISIBLE,
                                        0, 0, 10, 10, hwnd,
                                        (HMENU)IDC_PROGRESS, instance, NULL);

    g_window.sendButton = CreateWindowExW(0, L"BUTTON", LoadStr(IDS_BTN_SEND).c_str(),
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                          0, 0, 10, 10, hwnd,
                                          (HMENU)IDC_BTN_SEND, instance, NULL);

    g_window.refreshButton = CreateWindowExW(0, L"BUTTON", LoadStr(IDS_BTN_REFRESH).c_str(),
                                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                             0, 0, 10, 10, hwnd,
                                             (HMENU)IDC_BTN_REFRESH, instance, NULL);

    g_window.settingsButton = CreateWindowExW(0, L"BUTTON", LoadStr(IDS_BTN_SETTINGS).c_str(),
                                              WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                              0, 0, 10, 10, hwnd,
                                              (HMENU)IDC_BTN_SETTINGS, instance, NULL);

    g_window.statusBar = CreateWindowExW(0, STATUSCLASSNAMEW, L"",
                                         WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                         0, 0, 10, 10, hwnd,
                                         (HMENU)IDC_STATUSBAR, instance, NULL);

    const int deviceColumnIds[kDeviceColumns] =
    {
        IDS_COL_NAME, IDS_COL_MODEL, IDS_COL_TYPE, IDS_COL_ADDR, IDS_COL_SEEN
    };
    const int deviceColumnWidths[kDeviceColumns] = { 150, 130, 80, 110, 70 };
    InitListViewColumns(g_window.deviceList, deviceColumnIds, deviceColumnWidths, kDeviceColumns);

    const int transferColumnIds[kTransferColumns] =
    {
        IDS_COL_FILE, IDS_COL_SIZE, IDS_COL_PROGRESS, IDS_COL_SPEED, IDS_COL_ETA, IDS_COL_STATE
    };
    const int transferColumnWidths[kTransferColumns] = { 160, 70, 90, 90, 80, 110 };
    InitListViewColumns(g_window.transferList, transferColumnIds, transferColumnWidths, kTransferColumns);

    HWND children[8];
    children[0] = g_window.deviceList;
    children[1] = g_window.transferList;
    children[2] = g_window.progress;
    children[3] = g_window.sendButton;
    children[4] = g_window.refreshButton;
    children[5] = g_window.settingsButton;
    children[6] = g_window.deviceGroup;
    children[7] = g_window.transferGroup;

    for (int i = 0; i < 8; ++i)
    {
        SendMessageW(children[i], WM_SETFONT, (WPARAM)g_window.font, TRUE);
    }
    SendMessageW(g_window.statusBar, WM_SETFONT, (WPARAM)g_window.font, TRUE);

    SendMessageW(g_window.progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(g_window.statusBar, SB_SETTEXTW, 0, (LPARAM)LoadStr(IDS_STATUS_ONLINE).c_str());

    DragAcceptFiles(hwnd, TRUE);
}

LRESULT CALLBACK MainWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            CREATESTRUCTW* create = (CREATESTRUCTW*)lParam;
            CreateChildren(hwnd, create->hInstance);
            App::Instance().SetMainWindow(hwnd);
            g_window.trayActive = TrayCreate(hwnd);
            if (g_window.trayActive)
            {
                TraySetTip(hwnd, LoadStr(IDS_APP_TITLE));
                LogLine("tray icon created");
            }
            else
            {
                LogLine("tray icon could not be created");
            }
            return 0;
        }

    case WM_SIZE:
        LayoutMainWindow(hwnd);
        return 0;

    case WM_GETMINMAXINFO:
        {
            MINMAXINFO* info = (MINMAXINFO*)lParam;
            info->ptMinTrackSize.x = 620;   // 5 toolbar buttons have to stay visible
            info->ptMinTrackSize.y = 430;
            return 0;
        }

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_FILE_SEND:
        case IDM_DEVICE_SEND:
        case IDC_BTN_SEND:
            OnSendFiles(hwnd);
            return 0;

        case IDM_FILE_SHARE:
            OnShareFiles(hwnd);
            return 0;

        case IDM_FILE_SENDFOLDER:
            OnSendFolder(hwnd);
            return 0;

        case IDM_FILE_FROMURL:
            {
                std::wstring url;
                if (ShowUrlDialog(hwnd, url))
                {
                    App::Instance().ReceiveFromUrl(url);
                }
            }
            return 0;

        case IDM_FILE_EXIT:
            DestroyWindow(hwnd);
            return 0;

        case IDM_DEVICE_REFRESH:
        case IDC_BTN_REFRESH:
            App::Instance().RefreshDevices();
            return 0;

        case IDM_DEVICE_OPENFOLDER:
            App::Instance().OpenDownloadFolder();
            return 0;

        case IDM_TOOLS_SETTINGS:
        case IDC_BTN_SETTINGS:
            OnSettings(hwnd);
            return 0;

        case IDM_TOOLS_TRAY:
            HideToTray(hwnd);
            return 0;

        case IDM_TOOLS_AUTOSTART:
            {
                Config& config = App::Instance().GetConfig();
                config.autoStart = !config.autoStart;
                App::Instance().ApplySettingChange();
            }
            return 0;

        case IDM_TOOLS_LOG:
            OnOpenLog(hwnd);
            return 0;

        case IDM_HELP_GUIDE:
            OnHelp(hwnd);
            return 0;

        case IDM_HELP_PROTOCOL:
            OnProtocolPage();
            return 0;

        case IDM_HELP_ABOUT:
            OnAbout(hwnd);
            return 0;

        case IDM_TRANSFER_CANCEL:
            OnCancelTransfer(hwnd);
            return 0;

        case IDM_TRANSFER_CLEAR:
            {
                int removed = App::Instance().Transfers().RemoveFinished();
                RefreshTransferList(hwnd);
                UpdateProgressBar(hwnd);
                if (removed > 0)
                {
                    UpdateStatusText(hwnd, LoadStr(IDS_MSG_HISTORY_CLEARED));
                }
            }
            return 0;

        default:
            break;
        }
        break;

    case WM_INITMENUPOPUP:
        if (LOWORD(lParam) == 2)  // Tools menu: keep the auto start check box in sync
        {
            HMENU menu = (HMENU)wParam;
            UINT state = App::Instance().GetConfig().autoStart ? MF_CHECKED : MF_UNCHECKED;
            CheckMenuItem(menu, IDM_TOOLS_AUTOSTART, MF_BYCOMMAND | state);
        }
        return 0;

    case WM_NOTIFY:
        {
            NMHDR* header = (NMHDR*)lParam;
            if (header->idFrom == IDC_DEVICE_LIST)
            {
                if (header->code == NM_DBLCLK)
                {
                    OnSendFiles(hwnd);
                    return 0;
                }
            }
            else if (header->idFrom == IDC_TRANSFER_LIST)
            {
                if (header->code == NM_CUSTOMDRAW)
                {
                    return HandleTransferCustomDraw((NMLVCUSTOMDRAW*)lParam);
                }
                if (header->code == NM_RCLICK)
                {
                    POINT cursor;
                    GetCursorPos(&cursor);
                    HMENU menu = CreatePopupMenu();
                    if (menu != NULL)
                    {
                        AppendMenuW(menu, MF_STRING, IDM_TRANSFER_CANCEL,
                                    LoadStr(IDS_MSG_CANCEL_TRANSFER).c_str());
                        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
                        AppendMenuW(menu, MF_STRING, IDM_TRANSFER_CLEAR,
                                    LoadStr(IDS_MSG_CLEAR_FINISHED).c_str());
                        TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_RIGHTBUTTON,
                                       cursor.x, cursor.y, 0, hwnd, NULL);
                        DestroyMenu(menu);
                    }
                    return 0;
                }
            }
        }
        break;

    case WM_DROPFILES:
        HandleDropFiles(hwnd, (HDROP)wParam);
        return 0;

    case WM_APP_UI_EVENT:
        HandleUiEvents(hwnd);
        return 0;

    case WM_APP_TRAY:
        switch (lParam)
        {
        case WM_LBUTTONDBLCLK:
            RestoreFromTray(hwnd);
            return 0;
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            TrayShowMenu(hwnd);
            return 0;
        default:
            break;
        }
        break;

    case WM_SYSCOMMAND:
        if ((wParam & 0xFFF0) == SC_MINIMIZE && App::Instance().GetConfig().minimizeToTray)
        {
            HideToTray(hwnd);
            return 0;
        }
        break;

    case WM_CLOSE:
        if (App::Instance().GetConfig().minimizeToTray)
        {
            HideToTray(hwnd);
            return 0;
        }
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        {
            WINDOWPLACEMENT placement;
            placement.length = sizeof(placement);
            if (GetWindowPlacement(hwnd, &placement))
            {
                Config& config = App::Instance().GetConfig();
                config.windowMaximized = (placement.showCmd == SW_SHOWMAXIMIZED);
                config.windowX = placement.rcNormalPosition.left;
                config.windowY = placement.rcNormalPosition.top;
                config.windowWidth = placement.rcNormalPosition.right - placement.rcNormalPosition.left;
                config.windowHeight = placement.rcNormalPosition.bottom - placement.rcNormalPosition.top;
            }
        }
        if (g_window.trayActive)
        {
            TrayRemove(hwnd);
            g_window.trayActive = false;
        }
        if (g_window.font != NULL)
        {
            DeleteObject(g_window.font);
            g_window.font = NULL;
        }
        if (g_window.toolbarImages != NULL)
        {
            ImageList_Destroy(g_window.toolbarImages);
            g_window.toolbarImages = NULL;
        }
        PostQuitMessage(0);
        return 0;

    default:
        break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

}  // namespace

HWND CreateMainWindow(HINSTANCE instance)
{
    WNDCLASSEXW windowClass;
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = MainWndProc;
    windowClass.hInstance = instance;
    windowClass.hIcon = LoadAppIcon(32);
    windowClass.hCursor = LoadCursorW(NULL, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    windowClass.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    windowClass.lpszClassName = MAIN_WINDOW_CLASS;
    windowClass.hIconSm = LoadAppIcon(16);

    if (!RegisterClassExW(&windowClass))
    {
        if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return NULL;
        }
    }

    std::wstring title = LoadStr(IDS_APP_TITLE);

    Config& config = App::Instance().GetConfig();

    int x = CW_USEDEFAULT;
    int y = CW_USEDEFAULT;
    int width = 660;
    int height = 540;

    if (config.windowWidth >= 400 && config.windowHeight >= 300)
    {
        // Reuse the remembered placement, but never restore the window fully
        // outside the current virtual screen.
        int virtualX = GetSystemMetrics(SM_XVIRTUALSCREEN);
        int virtualY = GetSystemMetrics(SM_YVIRTUALSCREEN);
        int virtualWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
        int virtualHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);

        width = config.windowWidth;
        height = config.windowHeight;
        x = config.windowX;
        y = config.windowY;

        if (x < virtualX - 40 || x > virtualX + virtualWidth - 80)
        {
            x = CW_USEDEFAULT;
        }
        if (y < virtualY - 20 || y > virtualY + virtualHeight - 80)
        {
            y = CW_USEDEFAULT;
        }
    }

    HWND hwnd = CreateWindowExW(WS_EX_ACCEPTFILES, MAIN_WINDOW_CLASS, title.c_str(),
                                WS_OVERLAPPEDWINDOW, x, y, width, height,
                                NULL, NULL, instance, NULL);

    if (hwnd != NULL && config.windowMaximized)
    {
        WINDOWPLACEMENT placement;
        placement.length = sizeof(placement);
        if (GetWindowPlacement(hwnd, &placement))
        {
            placement.showCmd = SW_SHOWMAXIMIZED;
            SetWindowPlacement(hwnd, &placement);
        }
    }
    return hwnd;
}

void LayoutMainWindow(HWND hwnd)
{
    if (g_window.statusBar == NULL || g_window.deviceList == NULL)
    {
        return;
    }

    RECT client;
    GetClientRect(hwnd, &client);
    int width = client.right - client.left;
    int height = client.bottom - client.top;
    if (width <= 0 || height <= 0)
    {
        return;
    }

    SendMessageW(g_window.statusBar, WM_SIZE, 0, 0);
    RECT statusRect;
    GetWindowRect(g_window.statusBar, &statusRect);
    int statusHeight = statusRect.bottom - statusRect.top;

    int toolbarHeight = 0;
    if (g_window.toolbar != NULL)
    {
        SendMessageW(g_window.toolbar, TB_AUTOSIZE, 0, 0);
        RECT toolbarRect;
        GetWindowRect(g_window.toolbar, &toolbarRect);
        toolbarHeight = toolbarRect.bottom - toolbarRect.top;
        MoveWindow(g_window.toolbar, 0, 0, width, toolbarHeight, TRUE);
    }

    const int margin = 7;
    const int gap = 6;
    const int buttonWidth = 82;
    const int buttonHeight = 23;
    const int progressHeight = 14;

    int usable = height - statusHeight - margin * 2 - toolbarHeight;
    int groupSpace = usable - buttonHeight - progressHeight - gap * 3;
    if (groupSpace < 120)
    {
        groupSpace = 120;
    }

    int deviceGroupHeight = (groupSpace * 45) / 100;
    int transferGroupHeight = groupSpace - deviceGroupHeight;
    int contentWidth = width - margin * 2;
    if (contentWidth < 100)
    {
        return;
    }

    int top = toolbarHeight + margin;

    MoveWindow(g_window.deviceGroup, margin, top, contentWidth, deviceGroupHeight, TRUE);
    MoveWindow(g_window.deviceList, margin + 8, top + 17, contentWidth - 16,
               deviceGroupHeight - 25, TRUE);

    int buttonY = top + deviceGroupHeight + gap;
    int buttonX = width - margin - buttonWidth;
    MoveWindow(g_window.settingsButton, buttonX, buttonY, buttonWidth, buttonHeight, TRUE);
    buttonX -= (buttonWidth + gap);
    MoveWindow(g_window.refreshButton, buttonX, buttonY, buttonWidth, buttonHeight, TRUE);
    buttonX -= (buttonWidth + gap);
    MoveWindow(g_window.sendButton, buttonX, buttonY, buttonWidth, buttonHeight, TRUE);

    int transferY = buttonY + buttonHeight + gap;
    MoveWindow(g_window.transferGroup, margin, transferY, contentWidth, transferGroupHeight, TRUE);
    MoveWindow(g_window.transferList, margin + 8, transferY + 17, contentWidth - 16,
               transferGroupHeight - 25, TRUE);

    int progressY = transferY + transferGroupHeight + gap;
    MoveWindow(g_window.progress, margin, progressY, contentWidth, progressHeight, TRUE);

    UpdateStatusParts(hwnd);
}

void RefreshDeviceList(HWND hwnd)
{
    if (g_window.deviceList == NULL)
    {
        return;
    }

    std::vector<Device> devices;
    App::Instance().Devices().Snapshot(devices);

    std::string previousIp;
    unsigned short previousPort = 0;
    bool hadSelection = false;
    int selected = ListView_GetNextItem(g_window.deviceList, -1, LVNI_SELECTED);
    if (selected >= 0 && selected < (int)g_window.deviceCache.size())
    {
        previousIp = g_window.deviceCache[selected].ip;
        previousPort = g_window.deviceCache[selected].port;
        hadSelection = true;
    }

    SendMessageW(g_window.deviceList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_window.deviceList);
    g_window.deviceCache.clear();

    int restoreIndex = -1;
    for (size_t i = 0; i < devices.size(); ++i)
    {
        const Device& device = devices[i];

        std::wstring alias = Utf8ToWide(device.alias);
        std::wstring model = Utf8ToWide(device.deviceModel);
        std::wstring type = LoadStr(TypeTextId(device.deviceType));
        if (device.protocol == "https")
        {
            type += LoadStr(IDS_SUFFIX_ENCRYPTED);
        }
        std::wstring address = Utf8ToWide(device.ip);
        std::wstring seen = LastSeenText(device);

        LVITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = (int)i;
        item.pszText = (LPWSTR)alias.c_str();
        item.lParam = (LPARAM)i;
        int row = ListView_InsertItem(g_window.deviceList, &item);

        ListView_SetItemText(g_window.deviceList, row, 1, (LPWSTR)model.c_str());
        ListView_SetItemText(g_window.deviceList, row, 2, (LPWSTR)type.c_str());
        ListView_SetItemText(g_window.deviceList, row, 3, (LPWSTR)address.c_str());
        ListView_SetItemText(g_window.deviceList, row, 4, (LPWSTR)seen.c_str());

        g_window.deviceCache.push_back(device);

        if (hadSelection && device.ip == previousIp && device.port == previousPort)
        {
            restoreIndex = row;
        }
    }
    SendMessageW(g_window.deviceList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_window.deviceList, NULL, TRUE);

    if (restoreIndex >= 0)
    {
        ListView_SetItemState(g_window.deviceList, restoreIndex,
                              LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
    }
    UpdateStatusParts(hwnd);
}

void RefreshTransferList(HWND hwnd)
{
    if (g_window.transferList == NULL)
    {
        return;
    }

    std::vector<Transfer> transfers;
    App::Instance().Transfers().SnapshotCopy(transfers);

    SendMessageW(g_window.transferList, WM_SETREDRAW, FALSE, 0);
    ListView_DeleteAllItems(g_window.transferList);
    g_window.transferRowIds.clear();
    g_window.transferPercents.clear();

    for (size_t i = 0; i < transfers.size(); ++i)
    {
        const Transfer& transfer = transfers[i];
        for (size_t k = 0; k < transfer.files.size(); ++k)
        {
            const TransferFile& file = transfer.files[k];

            std::wstring name = Utf8ToWide(file.fileName);
            std::wstring size = FormatBytesW(file.size);

            std::wstring progressText;
            int percent = 0;
            if (file.size > 0)
            {
                percent = (int)((file.transferred * 100) / file.size);
                progressText = FormatW(L"%d%%", percent);
            }
            else
            {
                progressText = file.completed ? L"100%" : L"0%";
                percent = file.completed ? 100 : 0;
            }

            std::wstring speed = FormatSpeedW(transfer.speedBps);

            std::wstring eta = L"--";
            if (file.size > file.transferred)
            {
                eta = FormatEtaW(file.size - file.transferred, transfer.speedBps);
            }

            std::wstring state = TransferStateText(transfer.state);
            if (!file.errorText.empty())
            {
                state += L" (" + Utf8ToWide(file.errorText) + L")";
            }

            LVITEMW item;
            ZeroMemory(&item, sizeof(item));
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = (int)g_window.transferRowIds.size();
            item.pszText = (LPWSTR)name.c_str();
            item.lParam = (LPARAM)transfer.id;
            int row = ListView_InsertItem(g_window.transferList, &item);

            ListView_SetItemText(g_window.transferList, row, 1, (LPWSTR)size.c_str());
            ListView_SetItemText(g_window.transferList, row, 2, (LPWSTR)progressText.c_str());
            ListView_SetItemText(g_window.transferList, row, 3, (LPWSTR)speed.c_str());
            ListView_SetItemText(g_window.transferList, row, 4, (LPWSTR)eta.c_str());
            ListView_SetItemText(g_window.transferList, row, 5, (LPWSTR)state.c_str());

            g_window.transferRowIds.push_back(transfer.id);
            g_window.transferPercents.push_back(percent);
        }
    }

    SendMessageW(g_window.transferList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_window.transferList, NULL, TRUE);
}

void UpdateStatusText(HWND hwnd, const std::wstring& text)
{
    (void)hwnd;
    SendMessageW(g_window.statusBar, SB_SETTEXTW, 0, (LPARAM)text.c_str());
}

void UpdateProgressBar(HWND hwnd)
{
    (void)hwnd;
    int percent = App::Instance().Transfers().OverallPercent();
    SendMessageW(g_window.progress, PBM_SETPOS, (WPARAM)percent, 0);
}

Device SelectedDevice(HWND hwnd, bool* hasSelection)
{
    (void)hwnd;
    if (hasSelection != NULL)
    {
        *hasSelection = false;
    }

    int index = ListView_GetNextItem(g_window.deviceList, -1, LVNI_SELECTED);
    if (index < 0 || index >= (int)g_window.deviceCache.size())
    {
        return Device();
    }
    if (hasSelection != NULL)
    {
        *hasSelection = true;
    }
    return g_window.deviceCache[index];
}

long SelectedTransferId(HWND hwnd)
{
    (void)hwnd;
    int index = ListView_GetNextItem(g_window.transferList, -1, LVNI_SELECTED);
    if (index < 0 || index >= (int)g_window.transferRowIds.size())
    {
        return 0;
    }
    return g_window.transferRowIds[index];
}

}  // namespace ui
}  // namespace lsxp
