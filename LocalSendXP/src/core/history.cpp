#include "lsxp/history.h"

namespace lsxp {

const int kMaxHistoryEntries = 500;

namespace {

// A field must never contain a tab or a line break, otherwise the line based
// format would break.
void SanitizeField(std::string& text)
{
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '\t' || text[i] == '\r' || text[i] == '\n')
        {
            text[i] = ' ';
        }
    }
}

std::wstring NowText()
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    return FormatW(L"%04d-%02d-%02d %02d:%02d:%02d",
                   (int)st.wYear, (int)st.wMonth, (int)st.wDay,
                   (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
}

std::string FileNameOfPath(const std::wstring& path)
{
    size_t slash = path.find_last_of(L"\\/");
    std::wstring name = (slash == std::wstring::npos) ? path : path.substr(slash + 1);
    return WideToUtf8(name);
}

bool ReadWholeFile(const std::wstring& path, std::string& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }

    char buffer[8192];
    DWORD read = 0;
    while (ReadFile(file, buffer, (DWORD)sizeof(buffer), &read, NULL) && read > 0)
    {
        data.append(buffer, (size_t)read);
        if (data.size() > 4 * 1024 * 1024)   // keep a broken file from filling memory
        {
            break;
        }
    }
    CloseHandle(file);
    return true;
}

void WriteWholeFile(const std::wstring& path, const std::string& data)
{
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        LogLine("history: cannot write %s (%lu)", WideToUtf8(path).c_str(), GetLastError());
        return;
    }

    size_t offset = 0;
    while (offset < data.size())
    {
        DWORD chunk = (DWORD)((data.size() - offset > 65536) ? 65536 : (data.size() - offset));
        DWORD written = 0;
        if (!WriteFile(file, data.data() + offset, chunk, &written, NULL) || written == 0)
        {
            LogLine("history: write failed (%lu)", GetLastError());
            break;
        }
        offset += written;
    }

    CloseHandle(file);
}

void SplitFields(const std::string& line, std::vector<std::string>& fields)
{
    fields.clear();
    std::string current;
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '\t')
        {
            fields.push_back(current);
            current.clear();
        }
        else
        {
            current += line[i];
        }
    }
    fields.push_back(current);
}

}  // namespace

HistoryEntry::HistoryEntry()
    : size(0)
{
}

// A file scope object: function local statics are not initialised atomically
// before C++11, and this store is also touched from the HTTP thread.
HistoryStore g_historyStore;

HistoryStore& HistoryStore::Instance()
{
    return g_historyStore;
}

HistoryStore::HistoryStore()
    : m_loaded(false)
{
    InitializeCriticalSection(&m_cs);
}

HistoryStore::~HistoryStore()
{
    DeleteCriticalSection(&m_cs);
}

std::wstring HistoryStore::FilePath() const
{
    return JoinPathW(GetModuleDirectoryW(), L"LocalSendXP.history");
}

void HistoryStore::Load()
{
    EnterCriticalSection(&m_cs);
    bool alreadyLoaded = m_loaded;
    LeaveCriticalSection(&m_cs);
    if (alreadyLoaded)
    {
        return;
    }

    std::string data;
    if (!ReadWholeFile(FilePath(), data))
    {
        return;   // no history yet
    }

    // Skip a UTF-8 BOM if an editor added one.
    size_t start = 0;
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF &&
        (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF)
    {
        start = 3;
    }

    std::vector<HistoryEntry> parsed;
    std::string line;
    for (size_t i = start; i <= data.size(); ++i)
    {
        if (i < data.size() && data[i] != '\n')
        {
            line += data[i];
            continue;
        }
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }
        if (!line.empty())
        {
            std::vector<std::string> fields;
            SplitFields(line, fields);
            if (fields.size() >= 6)
            {
                HistoryEntry entry;
                entry.time      = Utf8ToWide(fields[0]);
                entry.peerAlias = fields[1];
                entry.peerIp    = fields[2];
                entry.size      = (uint64)_strtoui64(fields[3].c_str(), NULL, 10);
                entry.fileName  = fields[4];
                entry.path      = Utf8ToWide(fields[5]);
                parsed.push_back(entry);
            }
        }
        line.clear();
    }

    EnterCriticalSection(&m_cs);
    m_entries = parsed;
    m_loaded = true;
    TrimLocked();
    LeaveCriticalSection(&m_cs);

    LogLine("history: %d record(s) loaded", (int)m_entries.size());
}

void HistoryStore::Save()
{
    std::string data;
    EnterCriticalSection(&m_cs);
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const HistoryEntry& entry = m_entries[i];
        std::string alias = entry.peerAlias;
        std::string ip = entry.peerIp;
        std::string name = entry.fileName;
        std::string path = WideToUtf8(entry.path);
        SanitizeField(alias);
        SanitizeField(ip);
        SanitizeField(name);
        SanitizeField(path);

        data += WideToUtf8(entry.time);
        data += '\t';
        data += alias;
        data += '\t';
        data += ip;
        data += '\t';
        data += Format("%I64u", entry.size);
        data += '\t';
        data += name;
        data += '\t';
        data += path;
        data += "\r\n";
    }
    LeaveCriticalSection(&m_cs);

    WriteWholeFile(FilePath(), data);
}

void HistoryStore::TrimLocked()
{
    if ((int)m_entries.size() > kMaxHistoryEntries)
    {
        m_entries.erase(m_entries.begin() + kMaxHistoryEntries, m_entries.end());
    }
}

void HistoryStore::AddReceived(const std::string& peerAlias, const std::string& peerIp,
                               const std::wstring& path, uint64 size)
{
    if (path.empty())
    {
        return;
    }

    HistoryEntry entry;
    entry.time = NowText();
    entry.peerAlias = peerAlias;
    entry.peerIp = peerIp;
    entry.fileName = FileNameOfPath(path);
    entry.size = size;
    entry.path = path;

    EnterCriticalSection(&m_cs);
    m_entries.insert(m_entries.begin(), entry);
    TrimLocked();
    m_loaded = true;
    LeaveCriticalSection(&m_cs);

    Save();
    LogLine("history: received %s (%s)", entry.fileName.c_str(), WideToUtf8(path).c_str());
}

int HistoryStore::Count() const
{
    EnterCriticalSection(&m_cs);
    int count = (int)m_entries.size();
    LeaveCriticalSection(&m_cs);
    return count;
}

bool HistoryStore::Get(int index, HistoryEntry& entry) const
{
    bool found = false;
    EnterCriticalSection(&m_cs);
    if (index >= 0 && index < (int)m_entries.size())
    {
        entry = m_entries[index];
        found = true;
    }
    LeaveCriticalSection(&m_cs);
    return found;
}

int HistoryStore::Remove(const std::vector<int>& indexes)
{
    std::vector<bool> drop;
    EnterCriticalSection(&m_cs);
    drop.assign(m_entries.size(), false);
    for (size_t i = 0; i < indexes.size(); ++i)
    {
        int index = indexes[i];
        if (index >= 0 && index < (int)m_entries.size())
        {
            drop[index] = true;
        }
    }

    std::vector<HistoryEntry> kept;
    kept.reserve(m_entries.size());
    int removed = 0;
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        if (drop[i])
        {
            ++removed;
        }
        else
        {
            kept.push_back(m_entries[i]);
        }
    }
    m_entries.swap(kept);
    LeaveCriticalSection(&m_cs);

    if (removed > 0)
    {
        Save();
    }
    return removed;
}

int HistoryStore::Clear()
{
    EnterCriticalSection(&m_cs);
    int removed = (int)m_entries.size();
    m_entries.clear();
    LeaveCriticalSection(&m_cs);

    Save();
    return removed;
}

}  // namespace lsxp
