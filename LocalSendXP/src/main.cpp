#include "lsxp/app.h"
#include "lsxp/ui.h"
#include "resource.h"

#include <commctrl.h>

// Entry point, single instance guard and common controls setup.
namespace {

const wchar_t* const kSingleInstanceMutex = L"LocalSendXP_SingleInstance_Mutex";

}  // namespace

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previousInstance,
                      LPWSTR commandLine, int showCommand)
{
    (void)previousInstance;
    (void)showCommand;

    INITCOMMONCONTROLSEX controls;
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_LISTVIEW_CLASSES | ICC_PROGRESS_CLASS | ICC_BAR_CLASSES |
                     ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_USEREX_CLASSES;
    InitCommonControlsEx(&controls);

    HANDLE mutex = CreateMutexW(NULL, FALSE, kSingleInstanceMutex);
    if (mutex != NULL && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND existing = FindWindowW(lsxp::ui::MAIN_WINDOW_CLASS, NULL);
        if (existing != NULL)
        {
            ShowWindow(existing, SW_SHOWNORMAL);
            SetForegroundWindow(existing);
        }
        CloseHandle(mutex);
        return 0;
    }

    lsxp::App& app = lsxp::App::Instance();

    std::wstring arguments = (commandLine != NULL) ? commandLine : L"";
    if (!app.Init(instance, arguments))
    {
        if (mutex != NULL)
        {
            CloseHandle(mutex);
        }
        return 1;
    }

    int result = app.Run();
    app.Shutdown();

    if (mutex != NULL)
    {
        CloseHandle(mutex);
    }
    return result;
}
