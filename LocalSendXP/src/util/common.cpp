#include "lsxp/common.h"
#include "resource.h"

#include <shlobj.h>
#include <shellapi.h>
#include <iphlpapi.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <string.h>

namespace lsxp {

const char* const LSXP_MULTICAST_GROUP  = "224.0.0.167";
const char* const LSXP_PROTOCOL_VERSION = "2.2";
const char* const LSXP_CLIENT_NAME      = "LocalSend XP";
const char* const LSXP_CLIENT_VERSION   = "1.0.0";

// ---------------------------------------------------------------- logging
namespace {

CRITICAL_SECTION g_logCs;
HANDLE  g_logFile = INVALID_HANDLE_VALUE;
bool    g_logReady = false;
std::wstring g_logPath;

void LogEnsureInit()
{
    if (!g_logReady)
    {
        InitializeCriticalSection(&g_logCs);
        g_logReady = true;
    }
}

// Advapi32 export available since Windows 2000/XP: the documented
// replacement for the old CryptGenRandom dance.
typedef BOOLEAN (WINAPI *RtlGenRandomFn)(PVOID buffer, ULONG length);

bool RandomBytes(unsigned char* buffer, size_t length)
{
    static RtlGenRandomFn genRandom = NULL;
    static bool probed = false;

    if (!probed)
    {
        HMODULE advapi = LoadLibraryW(L"advapi32.dll");
        if (advapi != NULL)
        {
            genRandom = (RtlGenRandomFn)GetProcAddress(advapi, "SystemFunction036");
        }
        probed = true;
    }

    if (genRandom != NULL)
    {
        size_t done = 0;
        while (done < length)
        {
            ULONG chunk = (ULONG)((length - done) > 512 ? 512 : (length - done));
            if (!genRandom(buffer + done, chunk))
            {
                break;
            }
            done += chunk;
        }
        if (done == length)
        {
            return true;
        }
    }

    // Fallback: mix tick count with the C runtime generator.
    static bool seeded = false;
    if (!seeded)
    {
        srand((unsigned)GetTickCount() ^ (unsigned)GetCurrentThreadId());
        seeded = true;
    }
    for (size_t i = 0; i < length; ++i)
    {
        buffer[i] = (unsigned char)(rand() & 0xFF);
    }
    return false;
}

std::wstring ModuleFileName()
{
    std::vector<wchar_t> buffer(260);
    for (;;)
    {
        DWORD written = GetModuleFileNameW(NULL, &buffer[0], (DWORD)buffer.size());
        if (written == 0)
        {
            return std::wstring();
        }
        if (written < buffer.size() - 1)
        {
            return std::wstring(&buffer[0], written);
        }
        if (buffer.size() > 32768)
        {
            return std::wstring(&buffer[0], written);
        }
        buffer.resize(buffer.size() * 2);
    }
}

}  // namespace

void LogInit(const std::wstring& filePath)
{
    LogEnsureInit();
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }

    g_logPath = filePath;
    g_logFile = CreateFileW(filePath.c_str(),
                            FILE_APPEND_DATA,
                            FILE_SHARE_READ | FILE_SHARE_WRITE,
                            NULL,
                            OPEN_ALWAYS,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);

    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        SetFilePointer(g_logFile, 0, NULL, FILE_END);
        LogLine("==== LocalSendXP %s started ====", LSXP_CLIENT_VERSION);
    }
}

void LogClose()
{
    if (!g_logReady)
    {
        return;
    }
    EnterCriticalSection(&g_logCs);
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_logFile);
        g_logFile = INVALID_HANDLE_VALUE;
    }
    LeaveCriticalSection(&g_logCs);
}

std::wstring LogPath()
{
    return g_logPath;
}

void LogLine(const char* format, ...)
{
    if (!g_logReady || g_logFile == INVALID_HANDLE_VALUE)
    {
        return;
    }

    char text[2048];
    va_list args;
    va_start(args, format);
    _vsnprintf(text, sizeof(text) - 1, format, args);
    va_end(args);
    text[sizeof(text) - 1] = '\0';

    SYSTEMTIME st;
    GetLocalTime(&st);

    char line[2300];
    _snprintf(line, sizeof(line) - 1, "%04d-%02d-%02d %02d:%02d:%02d.%03d  %s\r\n",
              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
              st.wMilliseconds, text);
    line[sizeof(line) - 1] = '\0';

    EnterCriticalSection(&g_logCs);
    if (g_logFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(g_logFile, line, (DWORD)strlen(line), &written, NULL);
        FlushFileBuffers(g_logFile);
    }
    LeaveCriticalSection(&g_logCs);
}

void LogLineW(const wchar_t* format, ...)
{
    wchar_t text[2048];
    va_list args;
    va_start(args, format);
    _vsnwprintf(text, 2047, format, args);
    va_end(args);
    text[2047] = L'\0';
    LogLine("%s", WideToUtf8(text).c_str());
}

// --------------------------------------------------------------- strings
std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                                     NULL, 0, NULL, NULL);
    if (needed <= 0)
    {
        return std::string();
    }
    std::string result;
    result.resize((size_t)needed);
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                        &result[0], needed, NULL, NULL);
    return result;
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), NULL, 0);
    if (needed <= 0)
    {
        needed = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), NULL, 0);
        if (needed <= 0)
        {
            return std::wstring();
        }
        std::wstring fallback;
        fallback.resize((size_t)needed);
        MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), &fallback[0], needed);
        return fallback;
    }
    std::wstring result;
    result.resize((size_t)needed);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), (int)text.size(), &result[0], needed);
    return result;
}

std::string WideToAnsi(const std::wstring& text)
{
    if (text.empty())
    {
        return std::string();
    }
    int needed = WideCharToMultiByte(CP_ACP, 0, text.c_str(), (int)text.size(), NULL, 0, NULL, NULL);
    if (needed <= 0)
    {
        return std::string();
    }
    std::string result;
    result.resize((size_t)needed);
    WideCharToMultiByte(CP_ACP, 0, text.c_str(), (int)text.size(), &result[0], needed, NULL, NULL);
    return result;
}

std::wstring AnsiToWide(const std::string& text)
{
    if (text.empty())
    {
        return std::wstring();
    }
    int needed = MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), NULL, 0);
    if (needed <= 0)
    {
        return std::wstring();
    }
    std::wstring result;
    result.resize((size_t)needed);
    MultiByteToWideChar(CP_ACP, 0, text.c_str(), (int)text.size(), &result[0], needed);
    return result;
}

std::string Trim(const std::string& text)
{
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && (unsigned char)text[begin] <= 0x20)
    {
        ++begin;
    }
    while (end > begin && (unsigned char)text[end - 1] <= 0x20)
    {
        --end;
    }
    return text.substr(begin, end - begin);
}

std::string ToLower(const std::string& text)
{
    std::string result(text);
    for (size_t i = 0; i < result.size(); ++i)
    {
        if (result[i] >= 'A' && result[i] <= 'Z')
        {
            result[i] = (char)(result[i] - 'A' + 'a');
        }
    }
    return result;
}

std::wstring ToLowerW(const std::wstring& text)
{
    std::wstring result(text);
    CharLowerBuffW(&result[0], (DWORD)result.size());
    return result;
}

bool StartsWith(const std::string& text, const std::string& prefix)
{
    if (text.size() < prefix.size())
    {
        return false;
    }
    return text.compare(0, prefix.size(), prefix) == 0;
}

bool EqualsNoCase(const std::string& a, const std::string& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb)
        {
            return false;
        }
    }
    return true;
}

std::vector<std::string> Split(const std::string& text, char separator)
{
    std::vector<std::string> result;
    std::string current;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == separator)
        {
            result.push_back(current);
            current.clear();
        }
        else
        {
            current += text[i];
        }
    }
    result.push_back(current);
    return result;
}

std::string Join(const std::vector<std::string>& parts, const std::string& separator)
{
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
        {
            result += separator;
        }
        result += parts[i];
    }
    return result;
}

std::string Format(const char* format, ...)
{
    char buffer[4096];
    va_list args;
    va_start(args, format);
    _vsnprintf(buffer, sizeof(buffer) - 1, format, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    return std::string(buffer);
}

std::wstring FormatW(const wchar_t* format, ...)
{
    wchar_t buffer[4096];
    va_list args;
    va_start(args, format);
    _vsnwprintf(buffer, 4095, format, args);
    va_end(args);
    buffer[4095] = L'\0';
    return std::wstring(buffer);
}

std::string FormatInt(int64 value)
{
    char buffer[64];
    _snprintf(buffer, sizeof(buffer) - 1, "%I64d", value);
    buffer[sizeof(buffer) - 1] = '\0';
    return std::string(buffer);
}

std::string FormatUInt(uint64 value)
{
    char buffer[64];
    _snprintf(buffer, sizeof(buffer) - 1, "%I64u", value);
    buffer[sizeof(buffer) - 1] = '\0';
    return std::string(buffer);
}

std::string RandomHex(int byteCount)
{
    if (byteCount <= 0)
    {
        return std::string();
    }
    std::vector<unsigned char> data((size_t)byteCount);
    RandomBytes(&data[0], data.size());

    static const char* digits = "0123456789abcdef";
    std::string result;
    result.reserve((size_t)byteCount * 2);
    for (int i = 0; i < byteCount; ++i)
    {
        result += digits[(data[i] >> 4) & 0x0F];
        result += digits[data[i] & 0x0F];
    }
    return result;
}

std::string UrlEncode(const std::string& text)
{
    static const char* digits = "0123456789ABCDEF";
    std::string result;
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char ch = (unsigned char)text[i];
        bool plain = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
                     (ch >= '0' && ch <= '9') ||
                     ch == '-' || ch == '_' || ch == '.' || ch == '~';
        if (plain)
        {
            result += (char)ch;
        }
        else
        {
            result += '%';
            result += digits[(ch >> 4) & 0x0F];
            result += digits[ch & 0x0F];
        }
    }
    return result;
}

std::string UrlDecode(const std::string& text)
{
    std::string result;
    for (size_t i = 0; i < text.size(); ++i)
    {
        char ch = text[i];
        if (ch == '%' && i + 2 < text.size())
        {
            char hi = text[i + 1];
            char lo = text[i + 2];
            int value = 0;
            bool ok = true;
            for (int k = 0; k < 2; ++k)
            {
                char digit = (k == 0) ? hi : lo;
                int part = 0;
                if (digit >= '0' && digit <= '9')      part = digit - '0';
                else if (digit >= 'a' && digit <= 'f') part = digit - 'a' + 10;
                else if (digit >= 'A' && digit <= 'F') part = digit - 'A' + 10;
                else { ok = false; break; }
                value = value * 16 + part;
            }
            if (ok)
            {
                result += (char)value;
                i += 2;
                continue;
            }
        }
        else if (ch == '+')
        {
            result += ' ';
            continue;
        }
        result += ch;
    }
    return result;
}

std::string StripControlChars(const std::string& text)
{
    std::string result;
    for (size_t i = 0; i < text.size(); ++i)
    {
        unsigned char ch = (unsigned char)text[i];
        if (ch >= 0x20)
        {
            result += (char)ch;
        }
    }
    return result;
}

// ------------------------------------------------------------------ time
std::string Iso8601UtcFromSystemTime(const SYSTEMTIME& st)
{
    return Format("%04d-%02d-%02dT%02d:%02d:%02dZ",
                  (int)st.wYear, (int)st.wMonth, (int)st.wDay,
                  (int)st.wHour, (int)st.wMinute, (int)st.wSecond);
}

std::string Iso8601UtcNow()
{
    SYSTEMTIME st;
    GetSystemTime(&st);
    return Iso8601UtcFromSystemTime(st);
}

DWORD TickCount()
{
    return GetTickCount();
}

// ----------------------------------------------------------------- files
bool FileExistsW(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

uint64 FileSizeW(const std::wstring& path)
{
    WIN32_FILE_ATTRIBUTE_DATA info;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &info))
    {
        return 0;
    }
    return ((uint64)info.nFileSizeHigh << 32) | (uint64)info.nFileSizeLow;
}

std::wstring DirectoryFromPathW(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return std::wstring();
    }
    return path.substr(0, pos);
}

std::wstring FileNameFromPathW(const std::wstring& path)
{
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos)
    {
        return path;
    }
    return path.substr(pos + 1);
}

std::wstring FileExtensionW(const std::wstring& path)
{
    std::wstring name = FileNameFromPathW(path);
    size_t pos = name.find_last_of(L'.');
    if (pos == std::wstring::npos)
    {
        return std::wstring();
    }
    return ToLowerW(name.substr(pos + 1));
}

std::wstring EnsureTrailingSlashW(const std::wstring& path)
{
    if (path.empty())
    {
        return path;
    }
    wchar_t last = path[path.size() - 1];
    if (last == L'\\' || last == L'/')
    {
        return path;
    }
    return path + L"\\";
}

std::wstring JoinPathW(const std::wstring& directory, const std::wstring& name)
{
    if (directory.empty())
    {
        return name;
    }
    return EnsureTrailingSlashW(directory) + name;
}

std::wstring SanitizeFileNameW(const std::wstring& name)
{
    std::wstring result;
    for (size_t i = 0; i < name.size(); ++i)
    {
        wchar_t ch = name[i];
        if (ch < 32 || ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' ||
            ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|')
        {
            result += L'_';
        }
        else
        {
            result += ch;
        }
    }

    // Windows does not allow file names that end with a dot or a space.
    while (!result.empty() && (result[result.size() - 1] == L'.' || result[result.size() - 1] == L' '))
    {
        result.erase(result.size() - 1);
    }

    if (result.empty() || result == L"." || result == L"..")
    {
        result = L"unnamed";
    }
    if (result.size() > 200)
    {
        std::wstring extension = FileExtensionW(result);
        result = result.substr(0, 190);
        if (!extension.empty())
        {
            result += L"." + extension;
        }
    }
    return result;
}

std::wstring MakeUniquePathW(const std::wstring& path)
{
    if (!FileExistsW(path) && !DirectoryExistsW(path))
    {
        return path;
    }

    std::wstring directory = DirectoryFromPathW(path);
    std::wstring name = FileNameFromPathW(path);
    std::wstring base = name;
    std::wstring extension;

    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0)
    {
        base = name.substr(0, dot);
        extension = name.substr(dot);
    }

    for (int index = 1; index < 10000; ++index)
    {
        wchar_t suffix[32];
        _snwprintf(suffix, 31, L" (%d)", index);
        suffix[31] = L'\0';
        std::wstring candidate = JoinPathW(directory, base + suffix + extension);
        if (!FileExistsW(candidate) && !DirectoryExistsW(candidate))
        {
            return candidate;
        }
    }
    return path;
}

bool DirectoryExistsW(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

namespace {

// Depth limited on purpose: junctions and symbolic links may loop.
void CollectFilesRecursive(const std::wstring& directory,
                           std::vector<std::wstring>& out,
                           int maxFiles,
                           int depth)
{
    if (depth > 16 || (int)out.size() >= maxFiles)
    {
        return;
    }

    std::wstring pattern = JoinPathW(directory, L"*");
    WIN32_FIND_DATAW data;
    ZeroMemory(&data, sizeof(data));
    HANDLE find = FindFirstFileW(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE)
    {
        return;
    }

    do
    {
        if (data.cFileName[0] == L'.' &&
            (data.cFileName[1] == L'\0' ||
             (data.cFileName[1] == L'.' && data.cFileName[2] == L'\0')))
        {
            continue;
        }

        std::wstring full = JoinPathW(directory, data.cFileName);
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            CollectFilesRecursive(full, out, maxFiles, depth + 1);
        }
        else
        {
            out.push_back(full);
        }
        if ((int)out.size() >= maxFiles)
        {
            break;
        }
    }
    while (FindNextFileW(find, &data));

    FindClose(find);
}

}  // namespace

int CollectFilesW(const std::wstring& directory, std::vector<std::wstring>& out, int maxFiles)
{
    if (!DirectoryExistsW(directory) || maxFiles <= 0)
    {
        return 0;
    }
    int before = (int)out.size();
    CollectFilesRecursive(directory, out, maxFiles, 0);
    return (int)out.size() - before;
}

int ExpandSelectionW(const std::vector<std::wstring>& paths,
                     std::vector<std::wstring>& out,
                     int maxFiles)
{
    out.clear();
    for (size_t i = 0; i < paths.size(); ++i)
    {
        if ((int)out.size() >= maxFiles)
        {
            break;
        }
        if (DirectoryExistsW(paths[i]))
        {
            CollectFilesRecursive(paths[i], out, maxFiles, 0);
        }
        else if (FileExistsW(paths[i]))
        {
            out.push_back(paths[i]);
        }
    }
    return (int)out.size();
}

bool EnsureDirectoryW(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    if (DirectoryExistsW(path))
    {
        return true;
    }
    if (CreateDirectoryW(path.c_str(), NULL))
    {
        return true;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return true;
    }

    // Create parent directories first (Windows 2000/XP compatible).
    std::wstring parent = DirectoryFromPathW(path);
    if (parent.empty() || parent == path)
    {
        return false;
    }
    if (!EnsureDirectoryW(parent))
    {
        return false;
    }
    if (CreateDirectoryW(path.c_str(), NULL))
    {
        return true;
    }
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::wstring FormatBytesW(uint64 bytes)
{
    const wchar_t* units[] = { L"B", L"KB", L"MB", L"GB", L"TB" };
    double value = (double)bytes;
    int unit = 0;
    while (value >= 1024.0 && unit < 4)
    {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
    {
        return FormatW(L"%I64u B", bytes);
    }
    return FormatW(L"%.2f %s", value, units[unit]);
}

std::wstring FormatSpeedW(double bytesPerSecond)
{
    if (bytesPerSecond <= 0.0)
    {
        return L"--";
    }
    std::wstring text = FormatBytesW((uint64)bytesPerSecond);
    return text + L"/s";
}

std::wstring FormatEtaW(uint64 remainingBytes, double bytesPerSecond)
{
    if (bytesPerSecond <= 1.0)
    {
        return L"--";
    }
    double seconds = (double)remainingBytes / bytesPerSecond;
    if (seconds < 1.0)
    {
        return LoadStr(IDS_ETA_LESS_SEC);
    }
    if (seconds < 60.0)
    {
        return FormatStr(IDS_ETA_SECONDS, seconds);
    }
    if (seconds < 3600.0)
    {
        return FormatStr(IDS_ETA_MINUTE_SECOND, floor(seconds / 60.0), fmod(seconds, 60.0));
    }
    return FormatStr(IDS_ETA_HOUR_MINUTE, floor(seconds / 3600.0),
                     floor(fmod(seconds, 3600.0) / 60.0));
}

std::wstring GetModuleFilePathW()
{
    return ModuleFileName();
}

std::wstring GetModuleDirectoryW()
{
    std::wstring path = ModuleFileName();
    std::wstring directory = DirectoryFromPathW(path);
    if (directory.empty())
    {
        wchar_t current[MAX_PATH + 1];
        if (GetCurrentDirectoryW(MAX_PATH, current) > 0)
        {
            directory = current;
        }
    }
    return directory;
}

std::wstring GetConfigFilePathW()
{
    return JoinPathW(GetModuleDirectoryW(), L"LocalSendXP.ini");
}

std::wstring GetAppDataDirectoryW()
{
    wchar_t buffer[MAX_PATH + 1];
    buffer[0] = L'\0';
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, buffer)))
    {
        return JoinPathW(buffer, L"LocalSendXP");
    }
    return JoinPathW(GetModuleDirectoryW(), L"data");
}

std::wstring GetDefaultDownloadDirectoryW()
{
    wchar_t buffer[MAX_PATH + 1];
    buffer[0] = L'\0';
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, buffer)))
    {
        return JoinPathW(buffer, L"LocalSendXP");
    }
    return JoinPathW(GetModuleDirectoryW(), L"Received");
}

std::string MimeTypeFromFileName(const std::string& fileNameUtf8)
{
    std::wstring name = ToLowerW(Utf8ToWide(fileNameUtf8));
    std::wstring extension = FileExtensionW(name);

    if (extension == L"png")  return "image/png";
    if (extension == L"jpg" || extension == L"jpeg" || extension == L"jpe") return "image/jpeg";
    if (extension == L"gif")  return "image/gif";
    if (extension == L"bmp")  return "image/bmp";
    if (extension == L"webp") return "image/webp";
    if (extension == L"ico")  return "image/x-icon";
    if (extension == L"mp4")  return "video/mp4";
    if (extension == L"mkv")  return "video/x-matroska";
    if (extension == L"avi")  return "video/x-msvideo";
    if (extension == L"mov")  return "video/quicktime";
    if (extension == L"mp3")  return "audio/mpeg";
    if (extension == L"wav")  return "audio/wav";
    if (extension == L"flac") return "audio/flac";
    if (extension == L"m4a")  return "audio/mp4";
    if (extension == L"ogg")  return "audio/ogg";
    if (extension == L"pdf")  return "application/pdf";
    if (extension == L"zip")  return "application/zip";
    if (extension == L"rar")  return "application/vnd.rar";
    if (extension == L"7z")   return "application/x-7z-compressed";
    if (extension == L"gz")   return "application/gzip";
    if (extension == L"tar")  return "application/x-tar";
    if (extension == L"exe")  return "application/vnd.microsoft.portable-executable";
    if (extension == L"msi")  return "application/x-msi";
    if (extension == L"doc")  return "application/msword";
    if (extension == L"docx") return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    if (extension == L"xls")  return "application/vnd.ms-excel";
    if (extension == L"xlsx") return "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet";
    if (extension == L"ppt")  return "application/vnd.ms-powerpoint";
    if (extension == L"pptx") return "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    if (extension == L"txt" || extension == L"log" || extension == L"ini" ||
        extension == L"cpp" || extension == L"h" || extension == L"cs" ||
        extension == L"json" || extension == L"xml" || extension == L"md")
    {
        return "text/plain";
    }
    if (extension == L"html" || extension == L"htm") return "text/html";
    if (extension == L"apk")  return "application/vnd.android.package-archive";
    return "application/octet-stream";
}

// ------------------------------------------------------------- local ip
void GetLocalIPv4List(std::vector<std::string>& addresses)
{
    addresses.clear();

    ULONG size = 16 * 1024;
    std::vector<unsigned char> buffer(size);
    ULONG result = GetAdaptersInfo((IP_ADAPTER_INFO*)&buffer[0], &size);
    if (result == ERROR_BUFFER_OVERFLOW)
    {
        buffer.resize(size);
        result = GetAdaptersInfo((IP_ADAPTER_INFO*)&buffer[0], &size);
    }
    if (result != ERROR_SUCCESS)
    {
        return;
    }

    IP_ADAPTER_INFO* adapter = (IP_ADAPTER_INFO*)&buffer[0];
    while (adapter != NULL)
    {
        IP_ADDR_STRING* addr = &adapter->IpAddressList;
        while (addr != NULL)
        {
            std::string ip = addr->IpAddress.String;
            if (!ip.empty() && ip != "0.0.0.0" && ip != "127.0.0.1")
            {
                bool duplicate = false;
                for (size_t i = 0; i < addresses.size(); ++i)
                {
                    if (addresses[i] == ip)
                    {
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                {
                    addresses.push_back(ip);
                }
            }
            addr = addr->Next;
        }
        adapter = adapter->Next;
    }
}

std::string GetPrimaryLocalIPv4()
{
    std::vector<std::string> addresses;
    GetLocalIPv4List(addresses);

    for (size_t i = 0; i < addresses.size(); ++i)
    {
        if (AddrIsPrivate(addresses[i]))
        {
            return addresses[i];
        }
    }
    if (!addresses.empty())
    {
        return addresses[0];
    }
    return "127.0.0.1";
}

bool AddrIsPrivate(const std::string& ip)
{
    std::vector<std::string> parts = Split(ip, '.');
    if (parts.size() != 4)
    {
        return false;
    }
    int a = atoi(parts[0].c_str());
    int b = atoi(parts[1].c_str());

    if (a == 10)
    {
        return true;
    }
    if (a == 192 && b == 168)
    {
        return true;
    }
    if (a == 172 && b >= 16 && b <= 31)
    {
        return true;
    }
    return false;
}

// ------------------------------------------------------------- system
std::wstring GetLocalComputerNameW()
{
    wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = MAX_COMPUTERNAME_LENGTH + 1;
    if (GetComputerNameW(buffer, &size))
    {
        return std::wstring(buffer, size);
    }
    return L"Windows PC";
}

namespace {
const wchar_t* const kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* const kRunValue = L"LocalSendXP";
}

bool SetAutoStart(bool enable, std::string* errorText)
{
    HKEY key = NULL;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key);
    if (result != ERROR_SUCCESS)
    {
        if (errorText != NULL)
        {
            *errorText = Format("RegOpenKeyEx failed (%ld)", result);
        }
        return false;
    }

    if (enable)
    {
        std::wstring command = L"\"" + GetModuleFilePathW() + L"\" -silent";
        result = RegSetValueExW(key, kRunValue, 0, REG_SZ,
                                (const BYTE*)command.c_str(),
                                (DWORD)((command.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        result = RegDeleteValueW(key, kRunValue);
        if (result == ERROR_FILE_NOT_FOUND)
        {
            result = ERROR_SUCCESS;
        }
    }

    RegCloseKey(key);

    if (result != ERROR_SUCCESS)
    {
        if (errorText != NULL)
        {
            *errorText = Format("registry update failed (%ld)", result);
        }
        return false;
    }
    return true;
}

bool GetAutoStart()
{
    HKEY key = NULL;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    wchar_t buffer[1024];
    DWORD size = sizeof(buffer);
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, kRunValue, NULL, &type, (BYTE*)buffer, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

bool OpenPathWithShell(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }
    HINSTANCE instance = ShellExecuteW(NULL, L"open", path.c_str(), NULL, NULL, SW_SHOWNORMAL);
    return ((INT_PTR)instance > 32);
}

bool RevealPathInExplorer(const std::wstring& path)
{
    if (path.empty())
    {
        return false;
    }

    // Explorer highlights the file when it is still on disk.  "explorer.exe
    // /select,..." is the classic (XP friendly) way of doing this.
    if (FileExistsW(path))
    {
        std::wstring arguments = L"/select,\"" + path + L"\"";
        HINSTANCE result = ShellExecuteW(NULL, L"open", L"explorer.exe",
                                        arguments.c_str(), NULL, SW_SHOWNORMAL);
        if ((INT_PTR)result > 32)
        {
            return true;
        }
    }

    // The file is gone (or Explorer refused): at least show the folder.
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos || slash == 0)
    {
        return false;
    }
    return OpenPathWithShell(path.substr(0, slash));
}

bool CopyTextToClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner))
    {
        return false;
    }
    EmptyClipboard();

    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory == NULL)
    {
        CloseClipboard();
        return false;
    }

    void* target = GlobalLock(memory);
    if (target != NULL)
    {
        memcpy(target, text.c_str(), bytes);
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
    return true;
}

// ------------------------------------------------------------ UI strings
namespace {

WORD g_resourceLanguage = MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);

}  // namespace

void SetResourceLanguage(WORD languageId)
{
    g_resourceLanguage = languageId;
}

WORD ResourceLanguage()
{
    return g_resourceLanguage;
}

// Reads a string table entry for the currently selected language. The string
// table format is 16 entries per block, each preceded by its length in wchars.
std::wstring LoadStr(int id)
{
    HINSTANCE instance = GetModuleHandleW(NULL);
    UINT blockId = ((UINT)id >> 4) + 1;

    HRSRC resource = FindResourceExW(instance, RT_STRING,
                                     MAKEINTRESOURCEW(blockId), ResourceLanguage());
    if (resource == NULL)
    {
        resource = FindResourceW(instance, MAKEINTRESOURCEW(blockId), RT_STRING);
    }
    if (resource == NULL)
    {
        return FormatW(L"#%d", id);
    }

    HGLOBAL loaded = LoadResource(instance, resource);
    const BYTE* data = (const BYTE*)LockResource(loaded);
    DWORD size = SizeofResource(instance, resource);
    if (data == NULL || size == 0)
    {
        return FormatW(L"#%d", id);
    }

    const BYTE* cursor = data;
    const BYTE* end = data + size;
    const int wanted = id & 0x0F;

    for (int index = 0; index < 16; ++index)
    {
        if (cursor + sizeof(WORD) > end)
        {
            break;
        }
        WORD length = *(const WORD*)cursor;
        cursor += sizeof(WORD);
        if (cursor + (size_t)length * sizeof(wchar_t) > end)
        {
            break;
        }
        if (index == wanted)
        {
            return std::wstring((const wchar_t*)cursor, (size_t)length);
        }
        cursor += (size_t)length * sizeof(wchar_t);
    }
    return FormatW(L"#%d", id);
}

std::wstring FormatStr(int id, ...)
{
    std::wstring format = LoadStr(id);
    wchar_t buffer[2048];
    va_list args;
    va_start(args, id);
    _vsnwprintf(buffer, 2047, format.c_str(), args);
    va_end(args);
    buffer[2047] = L'\0';
    return std::wstring(buffer);
}

std::wstring FormatStr1(int id, const std::wstring& a)
{
    return FormatStr(id, a.c_str());
}

std::wstring FormatStr2(int id, const std::wstring& a, const std::wstring& b)
{
    return FormatStr(id, a.c_str(), b.c_str());
}

}  // namespace lsxp
