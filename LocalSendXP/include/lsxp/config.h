#ifndef LSXP_CONFIG_H
#define LSXP_CONFIG_H

#include "common.h"

namespace lsxp {

// INI based configuration: LocalSendXP.ini next to the executable.
class Config
{
public:
    Config();

    void Load();
    void Save() const;
    void ApplyDefaults();

    std::wstring IniPath() const { return m_iniPath; }
    bool WasCreated() const { return m_wasCreated; }
    void MarkCreated() { m_wasCreated = true; }

    std::wstring alias;
    std::wstring deviceModel;
    std::string  deviceType;      // desktop | mobile | web | headless | server
    int          port;
    std::wstring downloadDirectory;
    std::string  pin;             // empty = no PIN
    bool         askBeforeReceive;
    bool         openFolderAfterReceive;
    bool         minimizeToTray;
    bool         autoStart;
    int          announceIntervalSec;

    // Remembered window placement (0 width = nothing saved yet).
    int  windowX;
    int  windowY;
    int  windowWidth;
    int  windowHeight;
    bool windowMaximized;

    // Runtime only
    std::string  fingerprint;

    std::string ProtocolName() const { return "http"; }
    bool PinRequired() const { return !pin.empty(); }
    std::wstring ResolvedDownloadDirectory() const;

private:
    std::wstring m_iniPath;
    bool         m_wasCreated;
};

}  // namespace lsxp

#endif  // LSXP_CONFIG_H
