#include "lsxp/app.h"
#include "lsxp/ui.h"
#include "lsxp/sha256.h"
#include "resource.h"

namespace lsxp {

namespace {

const uint64 kHashLimitBytes = 64 * 1024 * 1024;

}  // namespace

App::PinPrompt::PinPrompt()
    : accepted(false), doneEvent(NULL)
{
}

App& App::Instance()
{
    static App instance;
    return instance;
}

App::App()
    : m_instance(NULL),
      m_mainWindow(NULL),
      m_discovery(NULL),
      m_server(NULL),
      m_serverHandler(NULL),
      m_serverRunning(false),
      m_quitting(0),
      m_incomingBusy(0)
{
    InitializeCriticalSection(&m_eventCs);
}

App::~App()
{
    DeleteCriticalSection(&m_eventCs);
}

App::JobProgress::JobProgress(App* app, long transferId)
    : m_app(app), m_transferId(transferId), m_currentFile(0), m_lastNotify(0)
{
}

void App::JobProgress::OnProgress(uint64 transferred, uint64 total)
{
    (void)total;
    m_app->Transfers().SetFileProgress(m_transferId, m_currentFile, transferred, false);
    m_app->Transfers().RecomputeDone(m_transferId);

    DWORD now = TickCount();
    if ((now - m_lastNotify) >= 120 || transferred == 0)
    {
        m_lastNotify = now;
        m_app->NotifyProgress();
    }
}

void App::JobProgress::OnState(TransferState state, const std::string& messageUtf8)
{
    m_app->Transfers().SetState(m_transferId, state, messageUtf8);
    m_app->NotifyTransfersChanged();
}

bool App::JobProgress::IsCancelRequested()
{
    if (m_app->m_quitting != 0)
    {
        return true;
    }
    Transfer* transfer = m_app->Transfers().Find(m_transferId);
    if (transfer == NULL)
    {
        return true;
    }
    return transfer->cancelRequested != 0;
}

bool App::Init(HINSTANCE instance, const std::wstring& commandLine)
{
    m_instance = instance;

    std::string errorText;
    if (!WinsockStartup(errorText))
    {
        MessageBoxW(NULL, AnsiToWide(errorText).c_str(), L"LocalSend XP",
                    MB_ICONERROR | MB_OK);
        return false;
    }

    m_config.Load();
    LogInit(JoinPathW(GetModuleDirectoryW(), L"LocalSendXP.log"));
    LogLine("---- startup: %s (port %d, fingerprint %s) ----",
            WideToUtf8(m_config.alias).c_str(), m_config.port, m_config.fingerprint.c_str());

    if (!StartServices(errorText))
    {
        MessageBoxW(NULL, FormatStr(IDS_MSG_PORTBUSY, m_config.port).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONWARNING | MB_OK);
    }

    m_mainWindow = ui::CreateMainWindow(instance);
    if (m_mainWindow == NULL)
    {
        return false;
    }

    bool silent = commandLine.find(L"-silent") != std::wstring::npos ||
                  commandLine.find(L"/silent") != std::wstring::npos;
    if (silent || m_config.minimizeToTray)
    {
        if (silent)
        {
            ui::TrayShowBalloon(m_mainWindow, LoadStr(IDS_APP_TITLE), LoadStr(IDS_MSG_TRAYHINT));
        }
    }
    else
    {
        ShowWindow(m_mainWindow, SW_SHOW);
        UpdateWindow(m_mainWindow);
    }

    ui::RefreshDeviceList(m_mainWindow);
    ui::UpdateStatusText(m_mainWindow, LoadStr(IDS_STATUS_ONLINE));

    // Drain anything that was queued before the window existed.
    PostUiEvent(UI_EV_PROGRESS, NULL);

    // Make ourselves known once the window exists.
    if (m_discovery != NULL)
    {
        m_discovery->AnnounceNow();
    }
    return true;
}

int App::Run()
{
    MSG message;
    HACCEL accelerator = LoadAcceleratorsW(m_instance, MAKEINTRESOURCE(IDR_ACCELERATOR));

    while (GetMessageW(&message, NULL, 0, 0) > 0)
    {
        if (accelerator == NULL || !TranslateAcceleratorW(m_mainWindow, accelerator, &message))
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    return (int)message.wParam;
}

void App::Shutdown()
{
    if (m_quitting != 0)
    {
        return;
    }
    InterlockedExchange(&m_quitting, 1);

    StopServices();
    m_config.Save();

    DrainUiEventsQuiet();

    LogLine("---- shutdown ----");
    LogClose();
    WinsockCleanup();
}

void App::DrainUiEventsQuiet()
{
    std::vector<UiEvent> events;
    DrainUiEvents(events);
    for (size_t i = 0; i < events.size(); ++i)
    {
        if (events[i].data == NULL)
        {
            continue;
        }
        switch (events[i].type)
        {
        case UI_EV_INCOMING:
            {
                IncomingPrompt* prompt = (IncomingPrompt*)events[i].data;
                prompt->accepted = false;
                if (prompt->doneEvent != NULL)
                {
                    SetEvent(prompt->doneEvent);
                }
            }
            break;
        case UI_EV_PIN:
            {
                PinPrompt* prompt = (PinPrompt*)events[i].data;
                prompt->accepted = false;
                if (prompt->doneEvent != NULL)
                {
                    SetEvent(prompt->doneEvent);
                }
            }
            break;
        case UI_EV_TRAY_HINT:
            delete (BalloonInfo*)events[i].data;
            break;
        default:
            break;
        }
    }
}

// ----------------------------------------------------------- UI events
void App::PostUiEvent(int type, void* data)
{
    EnterCriticalSection(&m_eventCs);
    UiEvent event;
    event.type = type;
    event.data = data;
    m_events.push_back(event);
    LeaveCriticalSection(&m_eventCs);

    if (m_mainWindow != NULL)
    {
        PostMessageW(m_mainWindow, WM_APP_UI_EVENT, 0, 0);
    }
}

void App::DrainUiEvents(std::vector<UiEvent>& out)
{
    EnterCriticalSection(&m_eventCs);
    out = m_events;
    m_events.clear();
    LeaveCriticalSection(&m_eventCs);
}

void App::NotifyDevicesChanged()
{
    PostUiEvent(UI_EV_DEVICES, NULL);
}

void App::NotifyTransfersChanged()
{
    PostUiEvent(UI_EV_TRANSFERS, NULL);
}

void App::NotifyProgress()
{
    PostUiEvent(UI_EV_PROGRESS, NULL);
}

void App::Balloon(const std::wstring& title, const std::wstring& text)
{
    BalloonInfo* info = new BalloonInfo();
    info->title = title;
    info->text = text;
    PostUiEvent(UI_EV_TRAY_HINT, info);
}

bool App::PromptIncomingTransfer(IncomingPrompt* prompt)
{
    if (prompt == NULL)
    {
        return false;
    }
    if (m_quitting != 0)
    {
        return false;
    }

    if (prompt->doneEvent == NULL)
    {
        prompt->doneEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (prompt->doneEvent == NULL)
        {
            return false;
        }
    }

    PostUiEvent(UI_EV_INCOMING, prompt);

    // The prompt stays alive until the user answers: the confirmation dialog is
    // modal, exactly like the official client, and the sender applies its own
    // timeout. The worker thread owns the prompt and frees it here.
    DWORD wait = WaitForSingleObject(prompt->doneEvent, INFINITE);
    bool accepted = (wait == WAIT_OBJECT_0) && prompt->accepted;

    if (prompt->doneEvent != NULL)
    {
        CloseHandle(prompt->doneEvent);
        prompt->doneEvent = NULL;
    }
    return accepted;
}

bool App::PromptForPin(std::string& pin)
{
    if (m_quitting != 0)
    {
        return false;
    }

    PinPrompt* prompt = new PinPrompt();
    prompt->doneEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (prompt->doneEvent == NULL)
    {
        delete prompt;
        return false;
    }

    PostUiEvent(UI_EV_PIN, prompt);

    bool accepted = false;
    if (WaitForSingleObject(prompt->doneEvent, 120000) == WAIT_OBJECT_0)
    {
        accepted = prompt->accepted;
        pin = prompt->pin;
    }

    CloseHandle(prompt->doneEvent);
    delete prompt;
    return accepted;
}

// -------------------------------------------------------------- actions
void App::RefreshDevices()
{
    if (m_mainWindow != NULL)
    {
        ui::UpdateStatusText(m_mainWindow, LoadStr(IDS_STATUS_SCANNING));
    }
    if (m_discovery != NULL)
    {
        m_discovery->AnnounceNow();
        m_discovery->RequestScan();
    }
}

void App::SendFiles(const Device& device, const std::vector<std::wstring>& paths)
{
    if (paths.empty())
    {
        return;
    }

    if (device.protocol == "https")
    {
        MessageBoxW(m_mainWindow, LoadStr(IDS_MSG_ENCRYPTED_PEER).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }

    Transfer* transfer = new Transfer();
    transfer->id = m_transfers.NextId();
    transfer->direction = 0;
    transfer->peerAlias = device.alias;
    transfer->peerIp = device.ip;
    transfer->peerPort = device.port;
    transfer->state = TS_WAITING;

    for (size_t i = 0; i < paths.size(); ++i)
    {
        if (!FileExistsW(paths[i]))
        {
            continue;
        }

        TransferFile file;
        file.id = RandomHex(8);
        file.fileName = WideToUtf8(FileNameFromPathW(paths[i]));
        file.size = FileSizeW(paths[i]);
        file.mimeType = MimeTypeFromFileName(file.fileName);
        file.path = paths[i];
        file.accepted = true;
        transfer->files.push_back(file);
    }

    if (transfer->files.empty())
    {
        delete transfer;
        return;
    }

    m_transfers.Add(transfer);
    NotifyTransfersChanged();

    SendJob* job = new SendJob();
    job->app = this;
    job->transferId = transfer->id;
    job->device = device;

    HANDLE thread = CreateThread(NULL, 0, &App::SendThreadEntry, job, 0, NULL);
    if (thread == NULL)
    {
        delete job;
        m_transfers.SetState(transfer->id, TS_FAILED, "cannot create the transfer thread");
        NotifyTransfersChanged();
        return;
    }
    m_transfers.SetThread(transfer->id, thread);
    NotifyTransfersChanged();
}

void App::CancelTransfer(long id)
{
    Transfer* transfer = m_transfers.Find(id);
    if (transfer == NULL)
    {
        return;
    }
    InterlockedExchange(&transfer->cancelRequested, 1);

    if (transfer->direction == 0)
    {
        m_transfers.SetState(id, TS_CANCELED, "");
    }
    NotifyTransfersChanged();
}

void App::OpenDownloadFolder()
{
    std::wstring directory = m_config.ResolvedDownloadDirectory();
    if (directory.empty() || !OpenPathWithShell(directory))
    {
        MessageBoxW(m_mainWindow,
                    FormatStr(IDS_MSG_OPENFOLDER_FAIL, directory.c_str()).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(),
                    MB_ICONWARNING | MB_OK);
    }
}

void App::ShareFiles(const std::vector<std::wstring>& paths)
{
    std::string sessionId;
    std::string errorText;
    if (!proto::ShareManager::Instance().Create(paths, sessionId, errorText))
    {
        MessageBoxW(m_mainWindow, LoadStr(IDS_MSG_SHARE_EMPTY).c_str(),
                    LoadStr(IDS_APP_TITLE).c_str(), MB_ICONINFORMATION | MB_OK);
        return;
    }
    std::wstring url = proto::ShareManager::Instance().BuildUrl(sessionId);
    ui::ShowShareDialog(m_mainWindow, url);
}

void App::ApplySettingChange()
{
    m_config.Save();

    std::string errorText;
    if (!SetAutoStart(m_config.autoStart, &errorText))
    {
        LogLine("autostart update failed: %s", errorText.c_str());
    }

    if (m_mainWindow != NULL)
    {
        ui::TraySetTip(m_mainWindow, LoadStr(IDS_APP_TITLE));
        ui::UpdateStatusText(m_mainWindow, LoadStr(IDS_STATUS_ONLINE));
        ui::RefreshDeviceList(m_mainWindow);
    }
}

bool App::StartServices(std::string& errorText)
{
    if (m_server != NULL)
    {
        return true;
    }

    m_serverHandler = new proto::ServerHandler();
    m_server = new HttpServer();

    if (!m_server->Start((unsigned short)m_config.port, m_serverHandler, errorText))
    {
        delete m_server;
        m_server = NULL;
        delete m_serverHandler;
        m_serverHandler = NULL;
        return false;
    }
    m_serverRunning = true;

    m_discovery = new DiscoveryService();
    if (!m_discovery->Start(this, &m_config, &m_devices))
    {
        LogLine("discovery service failed to start; device discovery is disabled");
    }
    return true;
}

void App::StopServices()
{
    if (m_discovery != NULL)
    {
        m_discovery->Stop();
        delete m_discovery;
        m_discovery = NULL;
    }
    if (m_server != NULL)
    {
        m_server->Stop();
        delete m_server;
        m_server = NULL;
    }
    if (m_serverHandler != NULL)
    {
        delete m_serverHandler;
        m_serverHandler = NULL;
    }
    m_serverRunning = false;
    m_transfers.Clear();
}

// --------------------------------------------------------- send worker
DWORD WINAPI App::SendThreadEntry(LPVOID parameter)
{
    SendJob* job = (SendJob*)parameter;
    if (job != NULL)
    {
        job->app->RunSendJob(job);
    }
    return 0;
}

void App::RunSendJob(SendJob* job)
{
    const long transferId = job->transferId;
    const Device device = job->device;
    delete job;

    Transfer* transfer = m_transfers.Find(transferId);
    if (transfer == NULL)
    {
        return;
    }

    std::vector<TransferFile> files = transfer->files;
    JobProgress progress(this, transferId);

    // Hash the smaller files so the receiver can verify them.
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (files[i].size == 0 || files[i].size > kHashLimitBytes)
        {
            continue;
        }
        std::string hex;
        std::string hashError;
        if (Sha256File(files[i].path, hex, hashError))
        {
            files[i].sha256 = hex;
        }
    }

    std::string sessionId;
    std::string pin;
    std::string errorText;
    int httpStatus = 0;
    bool prepared = false;

    while (!prepared)
    {
        if (proto::PrepareUpload(device, m_config, files, pin, sessionId, httpStatus, errorText))
        {
            prepared = true;
            break;
        }

        if (httpStatus == 401)
        {
            m_transfers.SetState(transferId, TS_PIN_REQUIRED, "");
            NotifyTransfersChanged();

            std::string entered;
            if (PromptForPin(entered))
            {
                pin = entered;
                continue;
            }
            m_transfers.SetState(transferId, TS_CANCELED, "");
            NotifyTransfersChanged();
            return;
        }

        TransferState state = TS_FAILED;
        if (httpStatus == 403)
        {
            state = TS_DENIED;
        }
        else if (httpStatus == 409)
        {
            state = TS_BLOCKED;
        }
        LogLine("prepare-upload to %s failed: %s", device.ip.c_str(), errorText.c_str());
        m_transfers.SetState(transferId, state, errorText);
        NotifyTransfersChanged();
        return;
    }

    int uploadCount = 0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (files[i].accepted && !files[i].token.empty())
        {
            ++uploadCount;
        }
    }

    if (uploadCount == 0)
    {
        m_transfers.SetState(transferId, TS_DONE, "");
        NotifyTransfersChanged();
        return;
    }

    m_transfers.SetState(transferId, TS_TRANSFERRING, "");
    NotifyTransfersChanged();

    bool canceled = false;
    bool failed = false;
    int sentCount = 0;

    for (size_t i = 0; i < files.size(); ++i)
    {
        if (!files[i].accepted || files[i].token.empty())
        {
            continue;
        }
        if (progress.IsCancelRequested())
        {
            canceled = true;
            break;
        }

        progress.SetCurrentFile((int)i);
        httpStatus = 0;
        errorText.clear();

        if (!proto::UploadFile(device, m_config, files[i], sessionId, &progress,
                              httpStatus, errorText))
        {
            if (errorText == "canceled" || progress.IsCancelRequested())
            {
                canceled = true;
            }
            else
            {
                failed = true;
                m_transfers.SetFileError(transferId, (int)i, errorText);
                LogLine("upload of %s failed: %s", files[i].fileName.c_str(), errorText.c_str());
            }
            break;
        }

        m_transfers.SetFileProgress(transferId, (int)i, files[i].size, true);
        m_transfers.RecomputeDone(transferId);
        ++sentCount;
        NotifyProgress();
    }

    if (canceled)
    {
        std::string cancelError;
        proto::CancelUpload(device, m_config, sessionId, cancelError);
        m_transfers.SetState(transferId, TS_CANCELED, "");
        NotifyTransfersChanged();
        return;
    }

    if (failed)
    {
        std::string cancelError;
        proto::CancelUpload(device, m_config, sessionId, cancelError);
        m_transfers.SetState(transferId, TS_FAILED, errorText);
        NotifyTransfersChanged();
        return;
    }

    m_transfers.SetState(transferId, TS_DONE, "");
    NotifyTransfersChanged();

    Balloon(LoadStr(IDS_MSG_NOTIFY_TITLE),
            FormatStr(IDS_MSG_TRANSFER_SENT, Utf8ToWide(device.alias).c_str(), sentCount));
}

// ----------------------------------------------------------- receive worker
void App::ReceiveFromUrl(const std::wstring& url)
{
    std::string ip;
    std::string sessionId;
    std::string pin;
    std::string errorText;
    unsigned short port = 0;

    if (!proto::ParseShareUrl(WideToUtf8(url), ip, port, sessionId, pin, errorText))
    {
        std::wstring message;
        if (errorText == "https")
        {
            message = LoadStr(IDS_MSG_URL_HTTPS);
        }
        else
        {
            message = FormatStr(IDS_MSG_URL_INVALID, Utf8ToWide(errorText).c_str());
        }
        MessageBoxW(m_mainWindow, message.c_str(), LoadStr(IDS_APP_TITLE).c_str(),
                    MB_ICONWARNING | MB_OK);
        return;
    }

    Transfer* transfer = new Transfer();
    transfer->id = m_transfers.NextId();
    transfer->direction = 1;
    transfer->peerAlias = ip;
    transfer->peerIp = ip;
    transfer->peerPort = port;
    transfer->sessionId = sessionId;
    transfer->saveDirectory = WideToUtf8(m_config.ResolvedDownloadDirectory());
    transfer->state = TS_WAITING;
    m_transfers.Add(transfer);
    NotifyTransfersChanged();

    ReceiveJob* job = new ReceiveJob();
    job->app = this;
    job->transferId = transfer->id;
    job->ip = ip;
    job->port = port;
    job->sessionId = sessionId;
    job->pin = pin;
    job->saveDirectory = transfer->saveDirectory;

    HANDLE thread = CreateThread(NULL, 0, &App::ReceiveThreadEntry, job, 0, NULL);
    if (thread == NULL)
    {
        delete job;
        m_transfers.SetState(transfer->id, TS_FAILED, "cannot create the receive thread");
        NotifyTransfersChanged();
        return;
    }
    m_transfers.SetThread(transfer->id, thread);
}

DWORD WINAPI App::ReceiveThreadEntry(LPVOID parameter)
{
    ReceiveJob* job = (ReceiveJob*)parameter;
    if (job != NULL)
    {
        job->app->RunReceiveJob(job);
    }
    return 0;
}

void App::RunReceiveJob(ReceiveJob* job)
{
    const long transferId = job->transferId;
    const std::string ip = job->ip;
    const unsigned short port = job->port;
    std::string sessionId = job->sessionId;
    const std::string pin = job->pin;
    const std::string saveDirectoryUtf8 = job->saveDirectory;
    delete job;

    std::vector<SharedFileInfo> remoteFiles;
    std::string peerAlias;
    std::string errorText;
    int httpStatus = 0;

    if (!proto::FetchShareList(ip, port, sessionId, pin, peerAlias,
                               remoteFiles, httpStatus, errorText))
    {
        TransferState state = (httpStatus == 401) ? TS_PIN_REQUIRED : TS_FAILED;
        LogLine("receive from %s failed: %s", ip.c_str(), errorText.c_str());
        m_transfers.SetState(transferId, state, errorText);
        NotifyTransfersChanged();
        return;
    }

    if (remoteFiles.empty())
    {
        m_transfers.SetState(transferId, TS_DONE, "nothing to receive");
        NotifyTransfersChanged();
        return;
    }

    std::wstring saveDirectory = Utf8ToWide(saveDirectoryUtf8);
    if (saveDirectory.empty())
    {
        saveDirectory = m_config.ResolvedDownloadDirectory();
    }
    EnsureDirectoryW(saveDirectory);

    std::vector<TransferFile> files;
    for (size_t i = 0; i < remoteFiles.size(); ++i)
    {
        TransferFile file;
        file.id = remoteFiles[i].id;
        file.fileName = remoteFiles[i].name;
        file.size = remoteFiles[i].size;
        file.mimeType = remoteFiles[i].mimeType;
        file.accepted = true;
        file.path = MakeUniquePathW(
            JoinPathW(saveDirectory, SanitizeFileNameW(Utf8ToWide(remoteFiles[i].name))));
        files.push_back(file);
    }

    m_transfers.SetFiles(transferId, files);
    m_transfers.SetState(transferId, TS_TRANSFERRING, "");
    NotifyTransfersChanged();

    Transfer* transfer = m_transfers.Find(transferId);
    if (transfer == NULL)
    {
        return;
    }

    JobProgress progress(this, transferId);
    int completed = 0;
    bool canceled = false;
    bool failed = false;

    for (size_t i = 0; i < files.size(); ++i)
    {
        if (progress.IsCancelRequested())
        {
            canceled = true;
            break;
        }

        progress.SetCurrentFile((int)i);
        httpStatus = 0;
        errorText.clear();

        if (!proto::DownloadSharedFile(ip, port, sessionId, files[i].id, files[i].path,
                                       files[i].size, &progress, &transfer->cancelRequested,
                                       httpStatus, errorText))
        {
            if (errorText == "canceled" || progress.IsCancelRequested())
            {
                canceled = true;
            }
            else
            {
                failed = true;
                m_transfers.SetFileError(transferId, (int)i, errorText);
                LogLine("download of %s failed: %s", files[i].fileName.c_str(), errorText.c_str());
            }
            break;
        }

        m_transfers.SetFileProgress(transferId, (int)i, files[i].size, true);
        m_transfers.RecomputeDone(transferId);
        ++completed;
        NotifyProgress();
    }

    if (canceled)
    {
        m_transfers.SetState(transferId, TS_CANCELED, "");
        NotifyTransfersChanged();
        return;
    }
    if (failed)
    {
        m_transfers.SetState(transferId, TS_FAILED, errorText);
        NotifyTransfersChanged();
        return;
    }

    m_transfers.SetState(transferId, TS_DONE, "");
    NotifyTransfersChanged();

    Balloon(LoadStr(IDS_MSG_NOTIFY_TITLE),
            FormatStr(IDS_MSG_RECEIVED, completed, saveDirectory.c_str()));
    if (m_config.openFolderAfterReceive && !saveDirectory.empty())
    {
        OpenPathWithShell(saveDirectory);
    }
}

}  // namespace lsxp
