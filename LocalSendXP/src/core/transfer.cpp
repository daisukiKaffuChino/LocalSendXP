#include "lsxp/transfer.h"
#include "resource.h"

namespace lsxp {

TransferFile::TransferFile()
    : size(0),
      accepted(true),
      completed(false),
      transferred(0)
{
}

Transfer::Transfer()
    : id(0),
      direction(0),
      peerPort(0),
      state(TS_WAITING),
      totalBytes(0),
      doneBytes(0),
      startTick(0),
      lastTick(0),
      lastBytes(0),
      speedBps(0.0),
      thread(NULL),
      cancelRequested(0)
{
}

double Transfer::Progress() const
{
    if (totalBytes == 0)
    {
        return (state == TS_DONE) ? 1.0 : 0.0;
    }
    double value = (double)doneBytes / (double)totalBytes;
    if (value > 1.0)
    {
        value = 1.0;
    }
    return value;
}

void Transfer::RecomputeTotals()
{
    totalBytes = 0;
    doneBytes = 0;
    for (size_t i = 0; i < files.size(); ++i)
    {
        if (!files[i].accepted)
        {
            continue;
        }
        totalBytes += files[i].size;
        doneBytes += files[i].transferred;
    }
}

TransferManager::TransferManager()
    : m_nextId(1)
{
    InitializeCriticalSection(&m_cs);
}

TransferManager::~TransferManager()
{
    Clear();
    DeleteCriticalSection(&m_cs);
}

long TransferManager::NextId()
{
    EnterCriticalSection(&m_cs);
    long id = m_nextId++;
    LeaveCriticalSection(&m_cs);
    return id;
}

void TransferManager::Add(Transfer* transfer)
{
    if (transfer == NULL)
    {
        return;
    }
    transfer->startTick = TickCount();
    transfer->lastTick = transfer->startTick;
    transfer->lastBytes = 0;
    transfer->RecomputeTotals();

    EnterCriticalSection(&m_cs);
    m_transfers.push_back(transfer);
    LeaveCriticalSection(&m_cs);
}

Transfer* TransferManager::Find(long id) const
{
    Transfer* result = NULL;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        if (m_transfers[i]->id == id)
        {
            result = m_transfers[i];
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return result;
}

bool TransferManager::SetThread(long id, HANDLE thread)
{
    bool updated = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        if (m_transfers[i]->id == id)
        {
            m_transfers[i]->thread = thread;
            updated = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return updated;
}

bool TransferManager::SetFiles(long id, const std::vector<TransferFile>& files)
{
    bool updated = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        if (m_transfers[i]->id == id)
        {
            m_transfers[i]->files = files;
            m_transfers[i]->RecomputeTotals();
            updated = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return updated;
}

int TransferManager::RemoveFinished()
{
    std::vector<Transfer*> removed;
    int count = 0;

    EnterCriticalSection(&m_cs);
    for (std::vector<Transfer*>::iterator it = m_transfers.begin(); it != m_transfers.end(); )
    {
        Transfer* transfer = *it;
        bool finished = (transfer->state == TS_DONE || transfer->state == TS_FAILED ||
                         transfer->state == TS_CANCELED || transfer->state == TS_DENIED ||
                         transfer->state == TS_BLOCKED);
        bool threadDone = true;
        if (transfer->thread != NULL &&
            WaitForSingleObject(transfer->thread, 0) != WAIT_OBJECT_0)
        {
            threadDone = false;
        }

        if (finished && threadDone)
        {
            if (transfer->thread != NULL)
            {
                CloseHandle(transfer->thread);
                transfer->thread = NULL;
            }
            it = m_transfers.erase(it);
            removed.push_back(transfer);
            ++count;
        }
        else
        {
            ++it;
        }
    }
    LeaveCriticalSection(&m_cs);

    for (size_t i = 0; i < removed.size(); ++i)
    {
        delete removed[i];
    }
    return count;
}

void TransferManager::Remove(long id)
{
    EnterCriticalSection(&m_cs);
    for (std::vector<Transfer*>::iterator it = m_transfers.begin(); it != m_transfers.end(); ++it)
    {
        if ((*it)->id == id)
        {
            Transfer* transfer = *it;
            m_transfers.erase(it);
            if (transfer->thread != NULL)
            {
                CloseHandle(transfer->thread);
            }
            delete transfer;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
}

void TransferManager::Clear()
{
    std::vector<Transfer*> transfers;

    EnterCriticalSection(&m_cs);
    transfers = m_transfers;
    m_transfers.clear();
    LeaveCriticalSection(&m_cs);

    for (size_t i = 0; i < transfers.size(); ++i)
    {
        Transfer* transfer = transfers[i];
        InterlockedExchange(&transfer->cancelRequested, 1);
        if (transfer->thread != NULL)
        {
            WaitForSingleObject(transfer->thread, 5000);
            CloseHandle(transfer->thread);
            transfer->thread = NULL;
        }
        delete transfer;
    }
}

void TransferManager::Snapshot(std::vector<Transfer*>& out) const
{
    EnterCriticalSection(&m_cs);
    out = m_transfers;
    LeaveCriticalSection(&m_cs);
}

void TransferManager::SnapshotCopy(std::vector<Transfer>& out) const
{
    out.clear();
    EnterCriticalSection(&m_cs);
    out.reserve(m_transfers.size());
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        Transfer copy = *m_transfers[i];
        copy.thread = NULL;
        out.push_back(copy);
    }
    LeaveCriticalSection(&m_cs);
}

size_t TransferManager::Count() const
{
    EnterCriticalSection(&m_cs);
    size_t count = m_transfers.size();
    LeaveCriticalSection(&m_cs);
    return count;
}

bool TransferManager::HasActive() const
{
    bool active = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        TransferState state = m_transfers[i]->state;
        if (state == TS_WAITING || state == TS_TRANSFERRING || state == TS_PIN_REQUIRED)
        {
            active = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return active;
}

uint64 TransferManager::TotalBytes() const
{
    uint64 total = 0;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        total += m_transfers[i]->totalBytes;
    }
    LeaveCriticalSection(&m_cs);
    return total;
}

uint64 TransferManager::TotalDone() const
{
    uint64 total = 0;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        total += m_transfers[i]->doneBytes;
    }
    LeaveCriticalSection(&m_cs);
    return total;
}

int TransferManager::OverallPercent() const
{
    uint64 total = 0;
    uint64 done = 0;
    const Transfer* newest = NULL;
    bool haveActive = false;

    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        const Transfer* transfer = m_transfers[i];
        newest = transfer;

        // Only transfers that are still running may contribute: a failed or
        // cancelled transfer keeps its partial byte count forever, and adding
        // that to the total used to pin the progress bar below 100% for every
        // later transfer.
        if (transfer->state == TS_WAITING || transfer->state == TS_TRANSFERRING ||
            transfer->state == TS_PIN_REQUIRED)
        {
            haveActive = true;
            total += transfer->totalBytes;
            done += transfer->doneBytes;
        }
    }
    if (!haveActive && newest != NULL)
    {
        // Nothing is running: show how far the most recent transfer got.
        total = newest->totalBytes;
        done = newest->doneBytes;
    }
    LeaveCriticalSection(&m_cs);

    if (total == 0)
    {
        return 0;
    }
    int percent = (int)((done * 100) / total);
    if (percent > 100)
    {
        percent = 100;
    }
    return percent;
}

bool TransferManager::UpdateProgress(long id, uint64 doneBytes)
{
    bool updated = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        Transfer* transfer = m_transfers[i];
        if (transfer->id != id)
        {
            continue;
        }

        transfer->doneBytes = doneBytes;

        DWORD now = TickCount();
        DWORD elapsed = now - transfer->lastTick;
        if (elapsed >= 500)
        {
            uint64 delta = (doneBytes > transfer->lastBytes) ? (doneBytes - transfer->lastBytes) : 0;
            double instant = (double)delta * 1000.0 / (double)elapsed;
            if (transfer->speedBps <= 0.0)
            {
                transfer->speedBps = instant;
            }
            else
            {
                transfer->speedBps = transfer->speedBps * 0.6 + instant * 0.4;
            }
            transfer->lastTick = now;
            transfer->lastBytes = doneBytes;
        }
        updated = true;
        break;
    }
    LeaveCriticalSection(&m_cs);
    return updated;
}

bool TransferManager::RecomputeDone(long id)
{
    uint64 done = 0;
    bool found = false;

    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        Transfer* transfer = m_transfers[i];
        if (transfer->id != id)
        {
            continue;
        }
        for (size_t k = 0; k < transfer->files.size(); ++k)
        {
            if (transfer->files[k].accepted)
            {
                done += transfer->files[k].transferred;
            }
        }
        found = true;
        break;
    }
    LeaveCriticalSection(&m_cs);

    if (!found)
    {
        return false;
    }
    return UpdateProgress(id, done);
}

bool TransferManager::SetState(long id, TransferState state, const std::string& messageUtf8)
{
    bool updated = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        if (m_transfers[i]->id == id)
        {
            m_transfers[i]->state = state;
            m_transfers[i]->message = messageUtf8;
            updated = true;
            break;
        }
    }
    LeaveCriticalSection(&m_cs);
    return updated;
}

bool TransferManager::SetFileProgress(long id, int fileIndex, uint64 transferred, bool completed)
{
    bool updated = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        Transfer* transfer = m_transfers[i];
        if (transfer->id != id)
        {
            continue;
        }
        if (fileIndex >= 0 && fileIndex < (int)transfer->files.size())
        {
            transfer->files[fileIndex].transferred = transferred;
            transfer->files[fileIndex].completed = completed;
        }
        updated = true;
        break;
    }
    LeaveCriticalSection(&m_cs);
    return updated;
}

bool TransferManager::SetFileError(long id, int fileIndex, const std::string& errorUtf8)
{
    bool updated = false;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_transfers.size(); ++i)
    {
        Transfer* transfer = m_transfers[i];
        if (transfer->id != id)
        {
            continue;
        }
        if (fileIndex >= 0 && fileIndex < (int)transfer->files.size())
        {
            transfer->files[fileIndex].errorText = errorUtf8;
            transfer->files[fileIndex].transferred = transfer->files[fileIndex].size;
        }
        updated = true;
        break;
    }
    LeaveCriticalSection(&m_cs);
    return updated;
}

std::wstring TransferStateText(TransferState state)
{
    switch (state)
    {
    case TS_WAITING:      return LoadStr(IDS_ST_WAITING);
    case TS_TRANSFERRING: return LoadStr(IDS_ST_TRANSFER);
    case TS_DONE:         return LoadStr(IDS_ST_DONE);
    case TS_FAILED:       return LoadStr(IDS_ST_FAILED);
    case TS_CANCELED:     return LoadStr(IDS_ST_CANCELED);
    case TS_DENIED:       return LoadStr(IDS_ST_DENIED);
    case TS_PIN_REQUIRED: return LoadStr(IDS_ST_PINWAIT);
    case TS_BLOCKED:      return LoadStr(IDS_ST_BLOCKED);
    default:              return LoadStr(IDS_ST_WAITING);
    }
}

}  // namespace lsxp
