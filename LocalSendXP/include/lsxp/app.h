#ifndef LSXP_APP_H
#define LSXP_APP_H

#include "common.h"
#include "config.h"
#include "device.h"
#include "transfer.h"
#include "protocol.h"
#include "discovery.h"
#include "httpserver.h"

namespace lsxp {

// Private window messages used to hand work to the UI thread.
#define WM_APP_UI_EVENT (WM_APP + 1)
#define WM_APP_TRAY     (WM_APP + 2)

class App
{
public:
    static App& Instance();

    App();
    ~App();

    bool Init(HINSTANCE instance, const std::wstring& commandLine);
    int  Run();
    void Shutdown();

    HINSTANCE GetInstanceHandle() const { return m_instance; }
    HWND      MainWindow() const { return m_mainWindow; }
    void      SetMainWindow(HWND hwnd) { m_mainWindow = hwnd; }

    Config&          GetConfig() { return m_config; }
    DeviceManager&   Devices() { return m_devices; }
    TransferManager& Transfers() { return m_transfers; }

    enum UiEventType
    {
        UI_EV_DEVICES = 1,
        UI_EV_TRANSFERS,
        UI_EV_PROGRESS,
        UI_EV_INCOMING,
        UI_EV_TRAY_HINT,
        UI_EV_PIN
    };

    struct UiEvent
    {
        int   type;
        void* data;
    };

    struct PinPrompt
    {
        PinPrompt();
        bool        accepted;
        std::string pin;
        HANDLE      doneEvent;
    };

    struct BalloonInfo
    {
        std::wstring title;
        std::wstring text;
    };

    void PostUiEvent(int type, void* data);
    void DrainUiEvents(std::vector<UiEvent>& out);

    // Worker thread helpers
    void NotifyDevicesChanged();
    void NotifyTransfersChanged();
    void NotifyProgress();
    bool PromptIncomingTransfer(IncomingPrompt* prompt);
    bool PromptForPin(std::string& pin);
    void Balloon(const std::wstring& title, const std::wstring& text);

    // UI actions
    void RefreshDevices();
    void SendFiles(const Device& device, const std::vector<std::wstring>& paths);
    void CancelTransfer(long id);
    void OpenDownloadFolder();
    void ShareFiles(const std::vector<std::wstring>& paths);
    void ReceiveFromUrl(const std::wstring& url);
    void ApplySettingChange();

    bool StartServices(std::string& errorText);
    void StopServices();
    bool ServicesRunning() const { return m_serverRunning; }

    volatile LONG m_quitting;
    volatile LONG m_incomingBusy;

private:
    // Owned by a send worker thread.
    struct SendJob
    {
        App*   app;
        long   transferId;
        Device device;
    };

    struct ReceiveJob
    {
        App*           app;
        long           transferId;
        std::string    ip;
        unsigned short port;
        std::string    sessionId;
        std::string    pin;
        std::string    saveDirectory;   // UTF-8
    };

    class JobProgress : public ITransferProgress
    {
    public:
        JobProgress(App* app, long transferId);
        void SetCurrentFile(int fileIndex) { m_currentFile = fileIndex; }
        virtual void OnProgress(uint64 transferred, uint64 total);
        virtual void OnState(TransferState state, const std::string& messageUtf8);
        virtual bool IsCancelRequested();
    private:
        App*  m_app;
        long  m_transferId;
        int   m_currentFile;
        DWORD m_lastNotify;
    };

    static DWORD WINAPI SendThreadEntry(LPVOID parameter);
    void RunSendJob(SendJob* job);
    static DWORD WINAPI ReceiveThreadEntry(LPVOID parameter);
    void RunReceiveJob(ReceiveJob* job);
    void DrainUiEventsQuiet();

    HINSTANCE m_instance;
    HWND      m_mainWindow;
    Config    m_config;
    DeviceManager   m_devices;
    TransferManager m_transfers;
    DiscoveryService*     m_discovery;
    HttpServer*           m_server;
    proto::ServerHandler* m_serverHandler;
    bool                  m_serverRunning;

    CRITICAL_SECTION     m_eventCs;
    std::vector<UiEvent> m_events;
};

}  // namespace lsxp

#endif  // LSXP_APP_H
