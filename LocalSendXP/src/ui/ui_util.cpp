#include "lsxp/ui.h"
#include "resource.h"

#include <commctrl.h>
#include <shlobj.h>
#include <commdlg.h>

namespace lsxp {
namespace ui {

namespace {

void ApplyFontToChildren(HWND parent, HFONT font)
{
    HWND child = GetWindow(parent, GW_CHILD);
    while (child != NULL)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

}  // namespace

HFONT CreateGuiFont(bool bold)
{
    LOGFONTW logFont;
    ZeroMemory(&logFont, sizeof(logFont));

    LANGID language = GetSystemDefaultLangID();
    if (PRIMARYLANGID(language) == LANG_CHINESE)
    {
        std::wstring face = LoadStr(IDS_FONT_FACE);
        lstrcpynW(logFont.lfFaceName, face.c_str(), 32);
        logFont.lfHeight = -12;              // 9 pt, the classic Chinese UI size
        logFont.lfCharSet = GB2312_CHARSET;
    }
    else
    {
        lstrcpynW(logFont.lfFaceName, L"Tahoma", 32);
        logFont.lfHeight = -11;              // 8 pt
        logFont.lfCharSet = DEFAULT_CHARSET;
    }

    logFont.lfWeight = bold ? FW_BOLD : FW_NORMAL;
    logFont.lfQuality = DEFAULT_QUALITY;
    return CreateFontIndirectW(&logFont);
}

void SetDialogFont(HWND dialog)
{
    HFONT font = CreateGuiFont(false);
    if (font == NULL)
    {
        return;
    }
    SendMessageW(dialog, WM_SETFONT, (WPARAM)font, TRUE);

    HWND child = GetWindow(dialog, GW_CHILD);
    while (child != NULL)
    {
        SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
    SetPropW(dialog, L"lsxpUiFont", (HANDLE)font);
}

void FreeDialogFont(HWND dialog)
{
    HFONT font = (HFONT)RemovePropW(dialog, L"lsxpUiFont");
    if (font != NULL)
    {
        DeleteObject(font);
    }
}

void ApplyGuiFont(HWND hwnd, bool bold)
{
    HFONT font = CreateGuiFont(bold);
    if (font == NULL)
    {
        return;
    }
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, TRUE);
    ApplyFontToChildren(hwnd, font);
}

void InitListViewColumns(HWND list, const int* stringIds, const int* widths, int count)
{
    for (int i = 0; i < count; ++i)
    {
        LVCOLUMNW column;
        ZeroMemory(&column, sizeof(column));
        std::wstring text = LoadStr(stringIds[i]);

        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = (LPWSTR)text.c_str();
        column.cx = widths[i];
        column.iSubItem = i;
        ListView_InsertColumn(list, i, &column);
    }
    ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
    SendMessageW(list, LVM_SETBKCOLOR, 0, (LPARAM)GetSysColor(COLOR_WINDOW));
}

HICON LoadAppIcon(int size)
{
    if (size <= 0)
    {
        size = 32;
    }
    HICON icon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCE(IDI_APP),
                                   IMAGE_ICON, size, size, LR_DEFAULTCOLOR | LR_SHARED);
    if (icon == NULL)
    {
        icon = LoadIconW(NULL, IDI_APPLICATION);
    }
    return icon;
}

int TypeTextId(const std::string& deviceType)
{
    if (deviceType == "mobile")   return IDS_TYPE_MOBILE;
    if (deviceType == "desktop")  return IDS_TYPE_DESKTOP;
    if (deviceType == "web")      return IDS_TYPE_WEB;
    if (deviceType == "headless") return IDS_TYPE_HEADLESS;
    if (deviceType == "server")   return IDS_TYPE_SERVER;
    return IDS_TYPE_UNKNOWN;
}

INT_PTR LxpDialogBoxParam(HINSTANCE instance, int dialogId, HWND parent, DLGPROC proc, LPARAM param)
{
    HRSRC resource = FindResourceExW(instance, RT_DIALOG, MAKEINTRESOURCEW(dialogId),
                                     ResourceLanguage());
    if (resource != NULL)
    {
        HGLOBAL loaded = LoadResource(instance, resource);
        const void* data = LockResource(loaded);
        if (data != NULL)
        {
            return DialogBoxIndirectParamW(instance, (LPCDLGTEMPLATEW)data, parent, proc, param);
        }
    }
    return DialogBoxParamW(instance, MAKEINTRESOURCEW(dialogId), parent, proc, param);
}

HWND LxpCreateDialogParam(HINSTANCE instance, int dialogId, HWND parent, DLGPROC proc, LPARAM param)
{
    HRSRC resource = FindResourceExW(instance, RT_DIALOG, MAKEINTRESOURCEW(dialogId),
                                     ResourceLanguage());
    if (resource != NULL)
    {
        HGLOBAL loaded = LoadResource(instance, resource);
        const void* data = LockResource(loaded);
        if (data != NULL)
        {
            return CreateDialogIndirectParamW(instance, (LPCDLGTEMPLATEW)data, parent, proc, param);
        }
    }
    return CreateDialogParamW(instance, MAKEINTRESOURCEW(dialogId), parent, proc, param);
}

void ApplyText(HWND dialog, int controlId, int stringId)
{
    SetDlgItemTextW(dialog, controlId, LoadStr(stringId).c_str());
}

namespace {

const int kToolbarIconSize = 32;
const int kToolbarIconCount = 5;

const int kToolbarIconIds[kToolbarIconCount] =
{
    IDI_TOOL_FROM_URL,
    IDI_TOOL_OPEN_FOLDER,
    IDI_TOOL_HISTORY,
    IDI_TOOL_SETTINGS,
    IDI_TOOL_ABOUT
};

}  // namespace

HIMAGELIST CreateToolbarImages()
{
    HIMAGELIST images = ImageList_Create(kToolbarIconSize, kToolbarIconSize,
                                         ILC_COLOR32 | ILC_MASK,
                                         kToolbarIconCount, 0);
    if (images != NULL)
    {
        for (int i = 0; i < kToolbarIconCount; ++i)
        {
            HICON icon = (HICON)LoadImageW(GetModuleHandleW(NULL),
                                           MAKEINTRESOURCEW(kToolbarIconIds[i]),
                                           IMAGE_ICON, kToolbarIconSize,
                                           kToolbarIconSize, LR_DEFAULTCOLOR);
            if (icon == NULL)
            {
                LogLine("toolbar: icon resource %d could not be loaded", kToolbarIconIds[i]);
                icon = LoadIconW(NULL, IDI_APPLICATION);
            }
            else
            {
                ImageList_AddIcon(images, icon);
                DestroyIcon(icon);
                continue;
            }
            ImageList_AddIcon(images, icon);
        }
    }
    return images;
}

bool ShowBrowseFolderDialog(HWND parent, std::wstring& folder)
{
    wchar_t displayName[MAX_PATH + 1];
    displayName[0] = L'\0';

    BROWSEINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.hwndOwner = parent;
    info.pszDisplayName = displayName;
    std::wstring title = LoadStr(IDS_BROWSE_TITLE);
    info.lpszTitle = title.c_str();
    info.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE | BIF_EDITBOX;

    LPITEMIDLIST idList = SHBrowseForFolderW(&info);
    if (idList == NULL)
    {
        return false;
    }

    wchar_t path[MAX_PATH + 1];
    path[0] = L'\0';
    bool ok = SHGetPathFromIDListW(idList, path) != FALSE;
    CoTaskMemFree(idList);

    if (!ok)
    {
        return false;
    }
    folder = path;
    return true;
}

bool ShowOpenFilesDialog(HWND parent, std::vector<std::wstring>& files)
{
    std::vector<wchar_t> buffer(64 * 1024);
    buffer[0] = L'\0';

    std::wstring filterName = LoadStr(IDS_FILTER_FILES);
    std::wstring filter = filterName;
    filter += L'\0';
    filter += L"*.*";
    filter += L'\0';
    filter += L'\0';

    std::wstring title = LoadStr(IDS_TITLE_SFILES);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = parent;
    ofn.lpstrFilter = filter.c_str();
    ofn.lpstrFile = &buffer[0];
    ofn.nMaxFile = (DWORD)buffer.size();
    ofn.lpstrTitle = title.c_str();
    ofn.Flags = OFN_ALLOWMULTISELECT | OFN_EXPLORER | OFN_FILEMUSTEXIST |
                OFN_HIDEREADONLY | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

    files.clear();
    if (!GetOpenFileNameW(&ofn))
    {
        return false;
    }

    std::wstring all(&buffer[0]);
    size_t position = all.find(L'\0');
    if (position == std::wstring::npos)
    {
        files.push_back(all);
        return true;
    }

    std::wstring directory = all.substr(0, position);
    bool multiple = false;

    while (position < all.size())
    {
        size_t start = position + 1;
        if (start >= all.size() || all[start] == L'\0')
        {
            break;
        }
        size_t end = all.find(L'\0', start);
        if (end == std::wstring::npos)
        {
            end = all.size();
        }
        std::wstring name = all.substr(start, end - start);
        if (!name.empty())
        {
            multiple = true;
            files.push_back(JoinPathW(directory, name));
        }
        position = end;
    }

    if (!multiple && !directory.empty())
    {
        files.push_back(directory);
    }
    return !files.empty();
}

}  // namespace ui
}  // namespace lsxp
