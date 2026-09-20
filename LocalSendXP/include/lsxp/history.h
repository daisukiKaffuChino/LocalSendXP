#ifndef LSXP_HISTORY_H
#define LSXP_HISTORY_H

#include "common.h"

namespace lsxp {

// One received file.  The whole list is kept in a plain text file
// (LocalSendXP.history) next to the executable, one record per line, so that it
// stays readable with Notepad - the way 2000s software did it.
struct HistoryEntry
{
    HistoryEntry();

    std::wstring time;       // local time, L"2026-09-20 14:05:12"
    std::string  peerAlias;  // UTF-8, may be empty
    std::string  peerIp;
    std::string  fileName;   // UTF-8, name only
    uint64       size;
    std::wstring path;       // full local path of the saved file
};

// Records are kept newest first.  The store is written from the HTTP thread
// and read from the UI thread, so every public method is locked.
class HistoryStore
{
public:
    static HistoryStore& Instance();

    void Load();
    void Save();

    void AddReceived(const std::string& peerAlias, const std::string& peerIp,
                     const std::wstring& path, uint64 size);

    int  Count() const;
    bool Get(int index, HistoryEntry& entry) const;
    // Removes the given indexes (as returned by the dialog row lParam values).
    int  Remove(const std::vector<int>& indexes);
    int  Clear();

    std::wstring FilePath() const;

    // Public only so that the file scope instance in history.cpp can be
    // created; use Instance() instead of building your own store.
    HistoryStore();
    ~HistoryStore();

private:
    HistoryStore(const HistoryStore&);
    HistoryStore& operator=(const HistoryStore&);

    void TrimLocked();

    mutable CRITICAL_SECTION   m_cs;
    std::vector<HistoryEntry>  m_entries;   // newest first
    bool                       m_loaded;
};

// Maximum number of records that is kept on disk; older ones are dropped.
extern const int kMaxHistoryEntries;

}  // namespace lsxp

#endif  // LSXP_HISTORY_H
