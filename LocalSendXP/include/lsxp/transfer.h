#ifndef LSXP_TRANSFER_H
#define LSXP_TRANSFER_H

#include "common.h"

namespace lsxp {

enum TransferState
{
    TS_WAITING = 0,
    TS_TRANSFERRING,
    TS_DONE,
    TS_FAILED,
    TS_CANCELED,
    TS_DENIED,
    TS_PIN_REQUIRED,
    TS_BLOCKED
};

struct TransferFile
{
    TransferFile();

    std::string  id;            // LocalSend file id
    std::string  token;         // token returned by the receiver
    std::wstring path;          // local path (send) or target path (receive)
    std::string  fileName;      // UTF-8 file name only
    uint64       size;
    std::string  mimeType;
    std::string  sha256;        // hex, may be empty
    bool         accepted;
    bool         completed;
    uint64       transferred;
    std::string  errorText;
};

struct Transfer
{
    Transfer();

    long           id;
    int            direction;     // 0 = outgoing, 1 = incoming
    std::string    peerAlias;     // UTF-8
    std::string    peerIp;
    unsigned short peerPort;
    std::string    sessionId;
    std::string    saveDirectory; // UTF-8 (receive)
    std::vector<TransferFile> files;
    TransferState  state;
    std::string    message;       // UTF-8 status detail
    uint64         totalBytes;
    uint64         doneBytes;
    DWORD          startTick;
    DWORD          lastTick;
    uint64         lastBytes;
    double         speedBps;
    HANDLE         thread;
    volatile LONG  cancelRequested;

    int    FileCount() const { return (int)files.size(); }
    double Progress() const;
    void   RecomputeTotals();
};

// Implemented by the worker thread that reports progress to the UI.
class ITransferProgress
{
public:
    virtual ~ITransferProgress() {}
    virtual void OnProgress(uint64 transferred, uint64 total) = 0;
    virtual void OnState(TransferState state, const std::string& messageUtf8) = 0;
    virtual bool IsCancelRequested() = 0;
};

class TransferManager
{
public:
    TransferManager();
    ~TransferManager();

    long      NextId();
    void      Add(Transfer* transfer);
    Transfer* Find(long id) const;
    bool      SetThread(long id, HANDLE thread);
    bool      SetFiles(long id, const std::vector<TransferFile>& files);
    int       RemoveFinished();
    void      Remove(long id);
    void      Clear();
    void      Snapshot(std::vector<Transfer*>& out) const;
    void      SnapshotCopy(std::vector<Transfer>& out) const;
    size_t    Count() const;
    bool      HasActive() const;

    int    OverallPercent() const;
    uint64 TotalBytes() const;
    uint64 TotalDone() const;

    bool UpdateProgress(long id, uint64 doneBytes);
    bool RecomputeDone(long id);
    bool SetState(long id, TransferState state, const std::string& messageUtf8);
    bool SetFileProgress(long id, int fileIndex, uint64 transferred, bool completed);
    bool SetFileError(long id, int fileIndex, const std::string& errorUtf8);

private:
    mutable CRITICAL_SECTION m_cs;
    std::vector<Transfer*>   m_transfers;
    long                     m_nextId;
};

std::wstring TransferStateText(TransferState state);

}  // namespace lsxp

#endif  // LSXP_TRANSFER_H
