#ifndef LSXP_UI_H
#define LSXP_UI_H

#include "common.h"
#include "app.h"

#include <commctrl.h>

namespace lsxp {
namespace ui {

extern const TCHAR* const MAIN_WINDOW_CLASS;

HWND CreateMainWindow(HINSTANCE instance);
void LayoutMainWindow(HWND hwnd);
void RefreshDeviceList(HWND hwnd);
void RefreshTransferList(HWND hwnd);
void UpdateStatusText(HWND hwnd, const std::wstring& text);
void UpdateProgressBar(HWND hwnd);
Device SelectedDevice(HWND hwnd, bool* hasSelection);
long   SelectedTransferId(HWND hwnd);

bool ShowSettingsDialog(HWND parent);
bool ShowAboutDialog(HWND parent);
bool ShowHelpDialog(HWND parent);
int  ShowReceiveDialog(HWND parent, IncomingPrompt* prompt);
bool ShowPinDialog(HWND parent, std::string& pin);
void ShowShareDialog(HWND parent, const std::wstring& url);
bool ShowUrlDialog(HWND parent, std::wstring& url);
bool ShowBrowseFolderDialog(HWND parent, std::wstring& folder);
bool ShowOpenFilesDialog(HWND parent, std::vector<std::wstring>& files);

HICON LoadAppIcon(int size);
HIMAGELIST CreateToolbarImages();
HFONT CreateGuiFont(bool bold);
void  SetDialogFont(HWND dialog);
void  FreeDialogFont(HWND dialog);
void  ApplyGuiFont(HWND hwnd, bool bold);
void  InitListViewColumns(HWND list, const int* stringIds, const int* widths, int count);
int   TypeTextId(const std::string& deviceType);

// Tray icon (Shell_NotifyIcon)
bool TrayCreate(HWND owner);
void TrayRemove(HWND owner);
void TraySetTip(HWND owner, const std::wstring& tip);
void TrayShowBalloon(HWND owner, const std::wstring& title, const std::wstring& text);
void TrayShowMenu(HWND owner);

}  // namespace ui
}  // namespace lsxp

#endif  // LSXP_UI_H
