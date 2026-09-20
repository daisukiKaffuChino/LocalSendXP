# LocalSendXP

An unofficial [LocalSend](https://localsend.org/) client for **Windows XP SP3 and later**,
written in C++03 against the plain Win32 API.

The goal is simple: what would LocalSend have looked like if it had shipped in 2004?
Menu bar, toolbar, status bar, list views, tray icon, an `.ini` file next to the program,
and a setup wizard with a Start Menu folder. No Electron, no Qt, no .NET, no runtime to
install - one 1.4 MB executable and two OpenSSL DLLs.

![Windows XP](https://img.shields.io/badge/Windows-XP%20SP3%20%7C%20Vista%20%7C%207%20%7C%208%20%7C%2010%20%7C%2011-3a6ea5)
![Language](https://img.shields.io/badge/UI-%E7%AE%80%E4%BD%93%E4%B8%AD%E6%96%87%20%7C%20English-4c8f4c)
![License](https://img.shields.io/badge/license-Apache--2.0-blue)

## Features

**Transfers**

- Send single files, multiple files or a whole folder to any LocalSend device on the LAN.
- Receive files, with an optional 6 digit PIN and a per-file confirmation dialog.
- Drag and drop files straight onto the main window.
- Share files over a plain URL: the peer opens it in any browser and downloads.
- Live progress: file name, size, current speed, progress bar and estimated time left.

**Discovery and protocol**

- Speaks the official LocalSend protocol (v2.2, v1 compatible) - no private extensions.
- UDP multicast discovery on `224.0.0.167:53317`, with an automatic UDP broadcast
  fallback when multicast is blocked.
- Devices show up with name, model, type, IP address and when they were last seen.

**HTTPS**

- HTTPS (TLS 1.2) is used whenever the peer announces it, and the peer's certificate is
  pinned to the SHA-256 fingerprint from the discovery packet.
- OpenSSL 1.0.2u is bundled because Windows XP's own TLS stack stops at TLS 1.0 and could
  never talk to a modern LocalSend peer. No Schannel, no system OpenSSL required.
- A CA bundle (`certs\ca-bundle.crt`) ships with the program so HTTPS verification still
  works on machines with an ancient root certificate store.
- Certificate verification is on by default; the "allow insecure HTTPS" switch exists but
  is off unless you turn it on yourself.

**Desktop**

- Classic Win32 UI: menu bar, toolbar, status bar, context menus, tray icon with balloon
  notifications.
- **Receive history**: every received file is remembered; double click an entry to show it
  in Explorer, delete single records or clear the list (files on disk are never touched).
- **Simplified Chinese and English** UI, switchable at runtime without restarting.
- Start with Windows, minimize to tray, everything configurable from one settings dialog.

## Requirements

| | |
| --- | --- |
| Operating system | Windows XP SP3 or later (32-bit build, also runs on 64-bit Windows) |
| Disk space | about 3.5 MB |
| Runtime | none - no .NET, no Visual C++ redistributable, no Java |
| Network | IPv4 LAN; inbound TCP/UDP 53317 for receiving |

## Installing

### Installer (recommended)

Download `LocalSendXP-<version>-setup.exe` from the
[releases page](https://github.com/daisukiKaffuChino/LocalSendXP/releases) and run it.
The setup program:

- installs to `C:\Program Files\LocalSendXP` for all users, so it needs administrator
  rights;
- offers desktop icon, Quick Launch icon, "start with Windows" and - recommended -
  adding the Windows Firewall rules for TCP/UDP 53317 on the local subnet only;
- creates a Start Menu group and an uninstall entry in Add or Remove Programs.

An upgrade over an existing installation keeps your settings; close the running program
first (the tray icon exits it).

### Portable (no installation)

Unpack the ZIP anywhere you can write to - a USB stick, `D:\Tools\LocalSendXP`, your
desktop - and run `LocalSendXP.exe`. Everything the program writes stays in that folder.

### Where the program keeps its files

| | Portable | Installed |
| --- | --- | --- |
| `LocalSendXP.ini` (settings) | next to the exe | `%APPDATA%\LocalSendXP` |
| `LocalSendXP.log` (log) | next to the exe | `%APPDATA%\LocalSendXP` |
| `LocalSendXP.history` (receive history) | next to the exe | `%APPDATA%\LocalSendXP` |
| `LocalSendXP.pem` (generated HTTPS certificate) | next to the exe | `%APPDATA%\LocalSendXP` |
| Received files | *My Documents*\LocalSendXP | *My Documents*\LocalSendXP |

The rule is simple: if `LocalSendXP.ini` already sits next to the executable, the program
stays portable and keeps everything there; if the program folder is read only (which is
what `C:\Program Files` is once the program runs without elevation), it falls back to
`%APPDATA%\LocalSendXP`.

## Using it

**Sending.** Pick the receiver in *Nearby devices*, then use **File > Send file**,
**Send folder**, or drop the files onto the window. If nothing is listed, press <kbd>F5</kbd>.

**Receiving.** When someone sends you files a confirmation window appears; untick what you
do not want and press *Accept*. Files are saved to the receive folder configured in
*Tools > Settings* (by default *My Documents*\LocalSendXP).

**History.** *Tools > History* lists everything you received. Double click (or *Open
containing folder*) shows the file in Explorer, *Delete record* and *Clear history* only
remove the list entries - the files themselves are never deleted, and the dialog says so.

**Sharing through a browser.** *File > Share via browser* gives you a URL; the peer can
open it in any browser and download the files without installing anything.

**Shortcuts**

| Key | Action |
| --- | --- |
| <kbd>F1</kbd> | Built-in help |
| <kbd>F5</kbd> | Refresh the device list |
| <kbd>Ctrl</kbd>+<kbd>S</kbd> | Send files |
| <kbd>Ctrl</kbd>+<kbd>U</kbd> | Receive from a URL |
| <kbd>Ctrl</kbd>+<kbd>H</kbd> | Receive history |
| <kbd>Ctrl</kbd>+<kbd>O</kbd> | Settings |
| <kbd>Esc</kbd> | Minimize to the tray |

## Firewall

Receiving needs inbound **TCP and UDP 53317** on the local subnet; multicast discovery
also needs UDP answers to come back. The installer can add these rules for you (it asks
first). If you use the portable build, Windows shows the usual "Windows Security Alert"
the first time the program runs - allow it for private networks.

On Windows XP the equivalent manual commands are:

```bat
netsh firewall add portopening protocol=TCP port=53317 name="LocalSendXP" mode=ENABLE scope=SUBNET
netsh firewall add portopening protocol=UDP port=53317 name="LocalSendXP" mode=ENABLE scope=SUBNET
netsh firewall set multicastbroadcastresponse mode=ENABLE
```

## Interoperability with official LocalSend

- The protocol layer is the official one: register, `prepare-upload` / `upload`, v1
  `send-request` / `send`, download and cancel.
- **HTTPS:** an encrypted peer is only accepted when its certificate fingerprint matches
  the one it announced. If you keep getting "certificate verification failed" while
  talking to a self-signed peer, search for the device again so its fingerprint is
  up to date, or knowingly enable *Allow insecure HTTPS* in the settings.
- **TLS 1.2 is the ceiling on XP.** OpenSSL 1.0.2 has no TLS 1.3, so a peer that insists
  on TLS 1.3 cannot be reached from XP. On Windows 7 and later the same build still stops
  at TLS 1.2 by design.

## Troubleshooting

| Symptom | What to try |
| --- | --- |
| Other devices do not appear in the list | Same subnet? Firewall rules present (see above)? Some VPN clients and managed switches block multicast - the broadcast fallback is automatic, but both sides must be on the same LAN. |
| "Port 53317 is already in use" | Another LocalSendXP instance or another program holds the port. Change the port in *Tools > Settings > Network* and restart. |
| Receiving fails with *403/401* | The sender needs the PIN you configured in *Settings > Transfer*. |
| HTTPS errors against a specific device | Search for devices again (refreshes the pinned fingerprint), or check the peer's own HTTPS settings. |
| Files received, but I cannot find them | *Tools > History* (or the status bar message) shows the exact path; the default is *My Documents*\LocalSendXP. |
| Nothing happens when I double click the exe | The program is already running - it shows the existing window instead of starting a second copy. |

The log file (`LocalSendXP.log`, see the table above) records discovery, TLS and transfer
details and is the first thing to look at; *Tools > View log file* opens it.

## Building from source

Visual Studio 2008 (VC9) is the reference tool chain; the project also opens in VS2010.

```bat
cd LocalSendXP\build
build_release.bat                     :: Release|Win32, output in LocalSendXP\bin\Release
build_release.bat Win32 Release rebuild

cd ..\..\installer
build_installer.bat                   :: packages bin\Release into a setup program
```

The full developer notes - module layout, protocol details, XP compatibility rules,
the OpenSSL build, the installer design and every test that was run - are in
[architecture.md](architecture.md) (Chinese).

## Known limitations

- IPv4 only; no IPv6 discovery and no IPv6 transfers.
- One incoming transfer session at a time.
- TLS 1.2 is the maximum on every supported system (see above).
- The UI is deliberately 2000s style. It supports Chinese and English only, and the
  layout is fixed at the classic font sizes.

## License and third-party software

LocalSendXP is released under the [Apache License 2.0](LICENSE).

Copyright (c) 2026 daisukiKaffuChino. The [NOTICE](NOTICE) file carries the
project attribution and the third-party attributions that the Apache License
requires to be passed on with binary distributions.

The binary distribution bundles:

- **OpenSSL 1.0.2u** (`libeay32.dll`, `ssleay32.dll`) - OpenSSL License and SSLeay
  License, both BSD style.
- **Mozilla CA certificate bundle** (`certs\ca-bundle.crt`, from
  [curl's caextract](https://curl.se/docs/caextract.html), MPL-2.0).

The installer shows the license (with the copyright notice on the first screen)
and installs all of it under `licenses\`: `LICENSE.txt`, `NOTICE.txt`,
`LICENSE-OpenSSL.txt` and `THIRD-PARTY-NOTICES.txt`. The same notices are
collected in [installer/THIRD-PARTY-NOTICES.txt](installer/THIRD-PARTY-NOTICES.txt).

## Disclaimer

This is an unofficial, community made client. It is not affiliated with, endorsed by or
supported by the LocalSend project. The protocol is documented at
[localsend/protocol](https://github.com/localsend/protocol).
