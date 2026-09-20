#ifndef LSXP_COMMON_H
#define LSXP_COMMON_H

// LocalSendXP - common definitions.
// Targets Windows XP SP3 and later, Visual C++ 9 (VS2008), C++03 only.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <tchar.h>

#include <string>
#include <vector>
#include <map>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "ole32.lib")

namespace lsxp {

typedef unsigned __int64 uint64;
typedef __int64 int64;

const int LSXP_DEFAULT_PORT = 53317;
extern const char* const LSXP_MULTICAST_GROUP;
extern const char* const LSXP_PROTOCOL_VERSION;
extern const char* const LSXP_CLIENT_NAME;
extern const char* const LSXP_CLIENT_VERSION;

// ---- logging ------------------------------------------------------------
void LogInit(const std::wstring& filePath);
void LogClose();
void LogLine(const char* format, ...);
void LogLineW(const wchar_t* format, ...);
std::wstring LogPath();

// ---- string helpers (UTF-8 unless the name says otherwise) --------------
std::string  WideToUtf8(const std::wstring& text);
std::wstring Utf8ToWide(const std::string& text);
std::string  WideToAnsi(const std::wstring& text);
std::wstring AnsiToWide(const std::string& text);

std::string  Trim(const std::string& text);
std::string  ToLower(const std::string& text);
std::wstring ToLowerW(const std::wstring& text);
bool         StartsWith(const std::string& text, const std::string& prefix);
bool         EqualsNoCase(const std::string& a, const std::string& b);
std::vector<std::string> Split(const std::string& text, char separator);
std::string  Join(const std::vector<std::string>& parts, const std::string& separator);
std::string  Format(const char* format, ...);
std::wstring FormatW(const wchar_t* format, ...);
std::string  FormatInt(int64 value);
std::string  FormatUInt(uint64 value);

std::string RandomHex(int byteCount);
std::string UrlEncode(const std::string& text);
std::string UrlDecode(const std::string& text);
std::string StripControlChars(const std::string& text);

// ---- time ---------------------------------------------------------------
std::string Iso8601UtcNow();
std::string Iso8601UtcFromSystemTime(const SYSTEMTIME& st);
DWORD       TickCount();

// ---- files --------------------------------------------------------------
bool         FileExistsW(const std::wstring& path);
uint64       FileSizeW(const std::wstring& path);
std::wstring FileNameFromPathW(const std::wstring& path);
std::wstring FileExtensionW(const std::wstring& path);
std::wstring DirectoryFromPathW(const std::wstring& path);
std::wstring EnsureTrailingSlashW(const std::wstring& path);
std::wstring JoinPathW(const std::wstring& directory, const std::wstring& name);
std::wstring SanitizeFileNameW(const std::wstring& name);
std::wstring MakeUniquePathW(const std::wstring& path);
bool         EnsureDirectoryW(const std::wstring& path);
bool         DirectoryExistsW(const std::wstring& path);
int          CollectFilesW(const std::wstring& directory, std::vector<std::wstring>& out, int maxFiles);
int          ExpandSelectionW(const std::vector<std::wstring>& paths, std::vector<std::wstring>& out, int maxFiles);
std::wstring FormatBytesW(uint64 bytes);
std::wstring FormatSpeedW(double bytesPerSecond);
std::wstring FormatEtaW(uint64 remainingBytes, double bytesPerSecond);

std::wstring GetModuleFilePathW();
std::wstring GetModuleDirectoryW();
// True when a new file can be created inside that directory.
bool         DirectoryIsWritableW(const std::wstring& directory);
// Where LocalSendXP keeps its own files: the program folder when that folder is
// writable (portable mode, the historical behaviour), otherwise
// %APPDATA%\LocalSendXP (installed mode, e.g. under C:\Program Files).
std::wstring GetDataDirectoryW();
std::wstring GetConfigFilePathW();
std::wstring GetDefaultDownloadDirectoryW();
std::wstring GetAppDataDirectoryW();

std::string  MimeTypeFromFileName(const std::string& fileNameUtf8);

// ---- local ip -----------------------------------------------------------
void        GetLocalIPv4List(std::vector<std::string>& addresses);
std::string GetPrimaryLocalIPv4();
bool        AddrIsPrivate(const std::string& ip);

// ---- system -------------------------------------------------------------
std::wstring GetLocalComputerNameW();
bool SetAutoStart(bool enable, std::string* errorText);
bool GetAutoStart();
bool OpenPathWithShell(const std::wstring& path);
// Opens the containing folder with the file selected (Explorer /select).
bool RevealPathInExplorer(const std::wstring& path);
bool CopyTextToClipboard(HWND owner, const std::wstring& text);

// ---- UI strings ---------------------------------------------------------
// Resource language used by LoadStr()/FormatStr() and the dialog loader.
void SetResourceLanguage(WORD languageId);
WORD ResourceLanguage();

std::wstring LoadStr(int id);
std::wstring FormatStr(int id, ...);
std::wstring FormatStr1(int id, const std::wstring& a);
std::wstring FormatStr2(int id, const std::wstring& a, const std::wstring& b);

}  // namespace lsxp

#endif  // LSXP_COMMON_H
