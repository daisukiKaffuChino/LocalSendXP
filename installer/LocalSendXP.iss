; ---------------------------------------------------------------------------
; LocalSendXP setup script
;
; Build with installer\build_installer.bat (recommended), which takes the
; version number from src\util\common.cpp and checks that the payload exists.
;
; Target: Windows XP SP3 and later, x86.  Written for Inno Setup 5.6.1 - the
; last release line whose compiler runs on (and targets) Windows XP.  Inno
; Setup 6.x is rejected on purpose: it raises the baseline of both the compiler
; and the produced setup program.
;
; The wizard is bilingual (Simplified Chinese + English).  The Chinese
; translation file is not part of Inno Setup itself, so:
;   * drop ChineseSimplified.isl into this folder, or
;   * copy it into <Inno Setup>\Languages\,
; otherwise the setup is built in English only (see the preprocessor block
; below).
; ---------------------------------------------------------------------------

#define MyAppName "LocalSendXP"
#define RepoRoot ".."
#define BinDir RepoRoot + "\LocalSendXP\bin\Release"

#ifndef MyAppVersion
  #define MyAppVersion "1.0.0"
#endif
#ifndef MyAppPublisher
  #define MyAppPublisher "daisukiKaffuChino"
#endif

; ---- Simplified Chinese wizard -------------------------------------------------
; build_installer.bat passes /DZH_ISL_FILE=<path to ChineseSimplified.isl> when it
; finds that file (either in this folder or in Inno Setup's Languages folder).
; When the script is compiled from the Inno Setup IDE instead, the file is picked
; up automatically if it sits next to this script.  Without it the setup is built
; in English only.
#ifdef ZH_ISL_FILE
  #define HAVE_ZH
#endif
#ifndef HAVE_ZH
  #if FileExists(AddBackslash(SourcePath) + "ChineseSimplified.isl")
    #define HAVE_ZH
    #define ZH_ISL_FILE AddBackslash(SourcePath) + "ChineseSimplified.isl"
  #endif
#endif

[Setup]
; Never change AppId: it is what lets a newer version upgrade an older one.
AppId={{9E1B4A61-5C3D-4F0E-8E77-2B6A0C9D5E12}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppCopyright=Copyright (C) 2026 daisukiKaffuChino
AppPublisherURL=https://github.com/daisukiKaffuChino/LocalSendXP
AppSupportURL=https://github.com/daisukiKaffuChino/LocalSendXP/issues
AppUpdatesURL=https://github.com/daisukiKaffuChino/LocalSendXP/releases
DefaultDirName={pf}\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableStartupPrompt=yes
AllowNoIcons=yes
; Windows XP SP3 or later, x86 (the binary also runs on x64 systems).
MinVersion=5.1
PrivilegesRequired=admin
; The program holds this mutex while it is running, so setup can ask the user
; to close it instead of failing halfway through an upgrade.
AppMutex=LocalSendXP_SingleInstance_Mutex
LicenseFile={#RepoRoot}\LICENSE
OutputDir=output
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-setup
SetupIconFile={#RepoRoot}\LocalSendXP\icons\icon.ico
UninstallDisplayIcon={app}\LocalSendXP.exe
UninstallDisplayName={#MyAppName} {#MyAppVersion}
Compression=lzma2/max
SolidCompression=yes
VersionInfoVersion={#MyAppVersion}
VersionInfoCompany={#MyAppPublisher}
VersionInfoCopyright=Copyright (C) 2026 daisukiKaffuChino
VersionInfoDescription={#MyAppName} setup program
VersionInfoProductName={#MyAppName}
VersionInfoProductVersion={#MyAppVersion}

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
#ifdef HAVE_ZH
Name: "chinese"; MessagesFile: "{#ZH_ISL_FILE}"
#endif

; Custom messages have to live in [CustomMessages]; [Messages] only overrides
; the ones that Inno Setup itself defines (and would silently ignore new names).
[CustomMessages]
english.FirewallTask=Add Windows Firewall rules for LocalSendXP (TCP/UDP 53317, local subnet only)
english.RemoveData=Also delete the LocalSendXP configuration, certificate and history?%n%nReceived files are never deleted.
#ifdef HAVE_ZH
chinese.FirewallTask=在 Windows 防火墙中放行 LocalSendXP（仅局域网，TCP/UDP 53317）
chinese.RemoveData=是否同时删除 LocalSendXP 的配置、证书与历史记录？%n%n收到的文件不会被删除。
#endif

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"
; Quick Launch lives in the current user's profile, so it is opt in.
Name: "quicklaunch"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "autostart"; Description: "{cm:AutoStartProgram,{#MyAppName}}"; GroupDescription: "{cm:AdditionalIcons}"
Name: "firewall"; Description: "{cm:FirewallTask}"; GroupDescription: "{cm:AdditionalIcons}"

[Files]
Source: "{#BinDir}\LocalSendXP.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\libeay32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\ssleay32.dll"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#BinDir}\certs\ca-bundle.crt"; DestDir: "{app}\certs"; Flags: ignoreversion
Source: "{#RepoRoot}\LICENSE"; DestDir: "{app}\licenses"; DestName: "LICENSE.txt"; Flags: ignoreversion
Source: "{#RepoRoot}\NOTICE"; DestDir: "{app}\licenses"; DestName: "NOTICE.txt"; Flags: ignoreversion
; The OpenSSL license text is vendored here on purpose: third_party\ is not
; tracked by git, and the OpenSSL license must be redistributed verbatim.
Source: "licenses\LICENSE-OpenSSL.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "THIRD-PARTY-NOTICES.txt"; DestDir: "{app}\licenses"; Flags: ignoreversion
Source: "{#RepoRoot}\README.md"; DestDir: "{app}\docs"; Flags: ignoreversion
Source: "{#RepoRoot}\architecture.md"; DestDir: "{app}\docs"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\LocalSendXP.exe"
Name: "{group}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"
; A machine wide install puts the desktop icon on the all users desktop.
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\LocalSendXP.exe"; Tasks: desktopicon
Name: "{userappdata}\Microsoft\Internet Explorer\Quick Launch\{#MyAppName}"; Filename: "{app}\LocalSendXP.exe"; Tasks: quicklaunch

[Registry]
; Exactly the value the program writes from "Start with Windows", so both
; switches stay in sync and the uninstaller removes it again.
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; \
    ValueName: "LocalSendXP"; ValueData: """{app}\LocalSendXP.exe"" -silent"; \
    Flags: uninsdeletevalue; Tasks: autostart

[Run]
; Firewall rules: Vista and later, then XP/2003.  These run during the normal
; install step, i.e. before the postinstall entry that starts the program.
; Only the local subnet is allowed.
; --- Vista / 7 / 8 / 10 / 11 -------------------------------------------------
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""LocalSendXP (TCP)"" dir=in action=allow protocol=TCP localport=53317 profile=private"; Flags: runhidden; Tasks: firewall; Check: IsModernWindows
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall add rule name=""LocalSendXP (UDP)"" dir=in action=allow protocol=UDP localport=53317 profile=private"; Flags: runhidden; Tasks: firewall; Check: IsModernWindows
; --- XP / Server 2003 --------------------------------------------------------
Filename: "{sys}\netsh.exe"; Parameters: "firewall add portopening protocol=TCP port=53317 name=""LocalSendXP"" mode=ENABLE scope=SUBNET"; Flags: runhidden; Tasks: firewall; Check: IsLegacyWindows
Filename: "{sys}\netsh.exe"; Parameters: "firewall add portopening protocol=UDP port=53317 name=""LocalSendXP"" mode=ENABLE scope=SUBNET"; Flags: runhidden; Tasks: firewall; Check: IsLegacyWindows
; Lets the multicast discovery answers come back through the XP firewall.
Filename: "{sys}\netsh.exe"; Parameters: "firewall set multicastbroadcastresponse mode=ENABLE"; Flags: runhidden; Tasks: firewall; Check: IsLegacyWindows
; -----------------------------------------------------------------------------
Filename: "{app}\LocalSendXP.exe"; Description: "{cm:LaunchProgram,{#MyAppName}}"; Flags: nowait postinstall skipifsilent

[UninstallRun]
Filename: "{cmd}"; Parameters: "/c taskkill /IM LocalSendXP.exe /F"; Flags: runhidden; RunOnceId: "KillApp"
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""LocalSendXP (TCP)"""; Flags: runhidden; RunOnceId: "FwTcpNew"; Check: IsModernWindows
Filename: "{sys}\netsh.exe"; Parameters: "advfirewall firewall delete rule name=""LocalSendXP (UDP)"""; Flags: runhidden; RunOnceId: "FwUdpNew"; Check: IsModernWindows
Filename: "{sys}\netsh.exe"; Parameters: "firewall delete portopening protocol=TCP port=53317"; Flags: runhidden; RunOnceId: "FwTcpOld"; Check: IsLegacyWindows
Filename: "{sys}\netsh.exe"; Parameters: "firewall delete portopening protocol=UDP port=53317"; Flags: runhidden; RunOnceId: "FwUdpOld"; Check: IsLegacyWindows

[UninstallDelete]
; Everything the program may have written next to itself (portable use inside
; the install folder); the %APPDATA% copy is handled in [Code].
Type: files; Name: "{app}\LocalSendXP.ini"
Type: files; Name: "{app}\LocalSendXP.log"
Type: files; Name: "{app}\LocalSendXP.history"
Type: files; Name: "{app}\LocalSendXP.pem"
Type: filesandordirs; Name: "{app}\certs"
Type: filesandordirs; Name: "{app}\licenses"
Type: filesandordirs; Name: "{app}\docs"

[Code]
function IsModernWindows: Boolean;
begin
  // 6.0 is Vista; XP reports 5.1, Server 2003 reports 5.2.
  Result := GetWindowsVersion >= $06000000;
end;

function IsLegacyWindows: Boolean;
begin
  Result := not IsModernWindows;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usUninstall then
  begin
    // The "Start with Windows" switch of the program writes this value, so it
    // has to go even when the setup task was not selected.  Only the account
    // that runs the uninstaller is cleaned - the setting lives in HKCU.
    RegDeleteValue(HKEY_CURRENT_USER,
                   'Software\Microsoft\Windows\CurrentVersion\Run', 'LocalSendXP');

    DataDir := ExpandConstant('{userappdata}\LocalSendXP');
    if DirExists(DataDir) then
    begin
      if MsgBox(CustomMessage('RemoveData'), mbConfirmation, MB_YESNO) = IDYES then
        DelTree(DataDir, True, True, True);
    end;
  end;
end;
