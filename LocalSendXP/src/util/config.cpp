#include "lsxp/config.h"

#include <stdio.h>

namespace lsxp {

namespace {

std::wstring ReadIniString(const std::wstring& file, const wchar_t* section,
                           const wchar_t* key, const wchar_t* defaultValue)
{
    wchar_t buffer[2048];
    buffer[0] = L'\0';
    GetPrivateProfileStringW(section, key, defaultValue, buffer, 2047, file.c_str());
    buffer[2047] = L'\0';
    return std::wstring(buffer);
}

void WriteIniString(const std::wstring& file, const wchar_t* section,
                    const wchar_t* key, const std::wstring& value)
{
    WritePrivateProfileStringW(section, key, value.c_str(), file.c_str());
}

void WriteIniInt(const std::wstring& file, const wchar_t* section,
                 const wchar_t* key, int value)
{
    wchar_t text[32];
    _snwprintf(text, 31, L"%d", value);
    text[31] = L'\0';
    WriteIniString(file, section, key, text);
}

void WriteIniBool(const std::wstring& file, const wchar_t* section,
                  const wchar_t* key, bool value)
{
    WriteIniString(file, section, key, value ? L"1" : L"0");
}

std::string ReadIniAnsi(const std::wstring& file, const wchar_t* section,
                        const wchar_t* key, const char* defaultValue)
{
    return WideToAnsi(ReadIniString(file, section, key, AnsiToWide(defaultValue).c_str()));
}

std::wstring DetectWindowsName()
{
    OSVERSIONINFOW info;
    ZeroMemory(&info, sizeof(info));
    info.dwOSVersionInfoSize = sizeof(info);

    if (!GetVersionExW(&info))
    {
        return L"Windows";
    }

    if (info.dwMajorVersion == 5)
    {
        if (info.dwMinorVersion == 0) return L"Windows 2000";
        if (info.dwMinorVersion == 1) return L"Windows XP";
        return L"Windows Server 2003";
    }
    if (info.dwMajorVersion == 6)
    {
        if (info.dwMinorVersion == 0) return L"Windows Vista";
        if (info.dwMinorVersion == 1) return L"Windows 7";
        if (info.dwMinorVersion == 2) return L"Windows 8";
        return L"Windows 8.1";
    }
    if (info.dwMajorVersion == 10)
    {
        if (info.dwBuildNumber >= 22000)
        {
            return L"Windows 11";
        }
        return L"Windows 10";
    }
    return FormatW(L"Windows %u.%u", info.dwMajorVersion, info.dwMinorVersion);
}

}  // namespace

Config::Config()
{
    ApplyDefaults();
}

void Config::ApplyDefaults()
{
    alias = GetLocalComputerNameW();
    deviceModel = DetectWindowsName();
    deviceType = "desktop";
    port = LSXP_DEFAULT_PORT;
    downloadDirectory = GetDefaultDownloadDirectoryW();
    pin.clear();
    askBeforeReceive = true;
    openFolderAfterReceive = true;
    minimizeToTray = false;
    autoStart = GetAutoStart();
    announceIntervalSec = 30;
    windowX = 0;
    windowY = 0;
    windowWidth = 0;
    windowHeight = 0;
    windowMaximized = false;
    httpsEnabled = true;
    allowInsecureHttps = false;
    allowLegacyTls = false;
    requireClientCertificate = false;
    certificatePath.clear();
    caBundlePath.clear();
    language = "zh";
    fingerprint = RandomHex(16);
    m_iniPath = GetConfigFilePathW();
    m_wasCreated = false;
}

void Config::Load()
{
    ApplyDefaults();

    m_iniPath = GetConfigFilePathW();
    if (!FileExistsW(m_iniPath))
    {
        m_wasCreated = true;
        Save();
        return;
    }

    alias = ReadIniString(m_iniPath, L"general", L"alias", alias.c_str());
    if (Trim(WideToUtf8(alias)).empty())
    {
        alias = GetLocalComputerNameW();
    }

    deviceModel = ReadIniString(m_iniPath, L"general", L"deviceModel", deviceModel.c_str());
    if (Trim(WideToUtf8(deviceModel)).empty())
    {
        deviceModel = DetectWindowsName();
    }

    deviceType = ReadIniAnsi(m_iniPath, L"general", L"deviceType", "desktop");
    if (deviceType != "desktop" && deviceType != "mobile" && deviceType != "web" &&
        deviceType != "headless" && deviceType != "server")
    {
        deviceType = "desktop";
    }

    port = GetPrivateProfileIntW(L"general", L"port", port, m_iniPath.c_str());
    if (port < 1024 || port > 65535)
    {
        port = LSXP_DEFAULT_PORT;
    }

    downloadDirectory = ReadIniString(m_iniPath, L"general", L"downloadDir", downloadDirectory.c_str());
    pin = ReadIniAnsi(m_iniPath, L"general", L"pin", "");
    askBeforeReceive = ReadIniString(m_iniPath, L"general", L"askBeforeReceive", L"1") != L"0";
    openFolderAfterReceive = ReadIniString(m_iniPath, L"general", L"openFolderAfterReceive", L"1") != L"0";
    minimizeToTray = ReadIniString(m_iniPath, L"general", L"minimizeToTray", L"0") == L"1";
    autoStart = ReadIniString(m_iniPath, L"general", L"autoStart", L"0") == L"1";
    announceIntervalSec = GetPrivateProfileIntW(L"general", L"announceInterval", 30, m_iniPath.c_str());
    if (announceIntervalSec < 5 || announceIntervalSec > 3600)
    {
        announceIntervalSec = 30;
    }

    fingerprint = ReadIniAnsi(m_iniPath, L"general", L"fingerprint", "");
    if (fingerprint.empty())
    {
        fingerprint = RandomHex(16);
        Save();
    }

    httpsEnabled = ReadIniString(m_iniPath, L"security", L"https", L"1") != L"0";
    allowInsecureHttps = ReadIniString(m_iniPath, L"security", L"allowInsecureHttps", L"0") == L"1";
    allowLegacyTls = ReadIniString(m_iniPath, L"security", L"allowLegacyTls", L"0") == L"1";
    requireClientCertificate = ReadIniString(m_iniPath, L"security", L"requireClientCertificate", L"0") == L"1";
    certificatePath = ReadIniString(m_iniPath, L"security", L"certificate", L"");
    caBundlePath = ReadIniString(m_iniPath, L"security", L"caBundle", L"");
    language = ReadIniAnsi(m_iniPath, L"general", L"language", "zh");
    if (language != "en")
    {
        language = "zh";
    }

    windowX = GetPrivateProfileIntW(L"window", L"x", 0, m_iniPath.c_str());
    windowY = GetPrivateProfileIntW(L"window", L"y", 0, m_iniPath.c_str());
    windowWidth = GetPrivateProfileIntW(L"window", L"width", 0, m_iniPath.c_str());
    windowHeight = GetPrivateProfileIntW(L"window", L"height", 0, m_iniPath.c_str());
    windowMaximized = ReadIniString(m_iniPath, L"window", L"maximized", L"0") == L"1";
    if (windowWidth < 400 || windowHeight < 300)
    {
        windowWidth = 0;
        windowHeight = 0;
    }

    LogLine("config loaded from %s (alias=%s port=%d)",
            WideToUtf8(m_iniPath).c_str(), WideToUtf8(alias).c_str(), port);
}

void Config::Save() const
{
    if (m_iniPath.empty())
    {
        return;
    }

    WriteIniString(m_iniPath, L"general", L"alias", alias);
    WriteIniString(m_iniPath, L"general", L"deviceModel", deviceModel);
    WriteIniString(m_iniPath, L"general", L"deviceType", AnsiToWide(deviceType.c_str()));
    WriteIniInt(m_iniPath, L"general", L"port", port);
    WriteIniString(m_iniPath, L"general", L"downloadDir", downloadDirectory);
    WriteIniString(m_iniPath, L"general", L"pin", AnsiToWide(pin.c_str()));
    WriteIniBool(m_iniPath, L"general", L"askBeforeReceive", askBeforeReceive);
    WriteIniBool(m_iniPath, L"general", L"openFolderAfterReceive", openFolderAfterReceive);
    WriteIniBool(m_iniPath, L"general", L"minimizeToTray", minimizeToTray);
    WriteIniBool(m_iniPath, L"general", L"autoStart", autoStart);
    WriteIniInt(m_iniPath, L"general", L"announceInterval", announceIntervalSec);
    WriteIniString(m_iniPath, L"general", L"fingerprint", AnsiToWide(fingerprint.c_str()));
    WriteIniString(m_iniPath, L"general", L"language", AnsiToWide(language.c_str()));

    WriteIniString(m_iniPath, L"network", L"multicastGroup", AnsiToWide(LSXP_MULTICAST_GROUP));
    WriteIniInt(m_iniPath, L"network", L"httpPort", port);
    WriteIniString(m_iniPath, L"protocol", L"version", AnsiToWide(LSXP_PROTOCOL_VERSION));

    if (windowWidth >= 400 && windowHeight >= 300)
    {
        WriteIniInt(m_iniPath, L"window", L"x", windowX);
        WriteIniInt(m_iniPath, L"window", L"y", windowY);
        WriteIniInt(m_iniPath, L"window", L"width", windowWidth);
        WriteIniInt(m_iniPath, L"window", L"height", windowHeight);
        WriteIniBool(m_iniPath, L"window", L"maximized", windowMaximized);
    }

    WriteIniBool(m_iniPath, L"security", L"https", httpsEnabled);
    WriteIniBool(m_iniPath, L"security", L"allowInsecureHttps", allowInsecureHttps);
    WriteIniBool(m_iniPath, L"security", L"allowLegacyTls", allowLegacyTls);
    WriteIniBool(m_iniPath, L"security", L"requireClientCertificate", requireClientCertificate);
    WriteIniString(m_iniPath, L"security", L"certificate", certificatePath);
    WriteIniString(m_iniPath, L"security", L"caBundle", caBundlePath);
}

std::wstring Config::ResolvedDownloadDirectory() const
{
    std::wstring directory = downloadDirectory;
    if (directory.empty())
    {
        directory = GetDefaultDownloadDirectoryW();
    }
    if (!DirectoryExistsW(directory))
    {
        EnsureDirectoryW(directory);
    }
    return directory;
}

std::wstring Config::ResolvedCertificatePath() const
{
    if (!certificatePath.empty())
    {
        return certificatePath;
    }
    return JoinPathW(GetDataDirectoryW(), L"LocalSendXP.pem");
}

std::wstring Config::ResolvedCaBundlePath() const
{
    if (!caBundlePath.empty())
    {
        return caBundlePath;
    }
    // The CA bundle is a shipped, read only asset, so it stays next to the exe.
    return JoinPathW(JoinPathW(GetModuleDirectoryW(), L"certs"), L"ca-bundle.crt");
}

WORD Config::ResourceLanguageId() const
{
    if (language == "en")
    {
        return MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    }
    return MAKELANGID(LANG_CHINESE, SUBLANG_CHINESE_SIMPLIFIED);
}

}  // namespace lsxp
