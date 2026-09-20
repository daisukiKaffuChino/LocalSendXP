# LocalSendXP 架构与开发笔记

> 这份文档面向开发者：模块划分、LocalSend 协议实现细节、Windows XP 兼容性、
> HTTPS / OpenSSL 的取舍、编译与打包过程，以及各阶段的实测记录。
> 全部内容保留中文。
>
> 面向最终用户的说明在 [README.md](README.md)（英文）。

---
# LocalSend XP

LocalSend XP 是 [LocalSend](https://github.com/localsend/protocol) 协议的**非官方 Windows 客户端**，
用 C++03 + 纯 Win32 API 从零实现，目标是 **Windows XP SP3 优先**，同时能在 Vista / 7 / 8 / 10 / 11 上运行。

设计出发点只有一句话：**如果 LocalSend 在 2004 年发布，它会长什么样** —— 所以界面走的是
Windows 2000 / XP Luna 时代的原生控件风格（菜单、分组框、ListView、状态栏、系统托盘），
而不是现代的 Fluent / Electron / Qt 风格。

## 技术约束

| 项目 | 取值 |
| --- | --- |
| 语言 | C++（C++03，不使用 C++11 及以后特性，无 lambda / 无智能指针 / 不用 STL 高级设施） |
| 工具链 | Visual Studio 2008（VC9），可选 VS2010 |
| 字符集 | Unicode（`UNICODE` / `_UNICODE`），协议字符串统一 UTF-8 |
| 运行时库 | 静态 CRT（`/MT`、`/MTd`），无需部署 MSVCR90.dll |
| 目标平台 | `Win32`（x86，子系统版本 5.01 = XP），可选 `x64` |
| 依赖 | user32 / gdi32 / kernel32 / shell32 / advapi32 / comctl32 / ws2_32 / iphlpapi / comdlg32 / ole32（全部系统库，无第三方库） |

禁止项已全部遵守：没有 MFC / .NET / Qt / Electron / Chromium / WinUI，也没有引入任何第三方 JSON 或网络库。

### 代码风格说明

- 按你的要求使用 `NULL` / `DWORD` / `HANDLE` / `LPCTSTR` 这一套 Win32 类型与常量，
  同步用 `CRITICAL_SECTION` / `Event` / `InterlockedXxx`，全程 **没有** `auto`、`nullptr`、
  `constexpr`、智能指针、lambda、`std::thread`、范围 for（可以用脚本复核：
  在 `src` / `include` 里搜这些关键字，只会命中我自己写的 `Sha256::Final()` 这种同名词）。
- 两处与原样式清单不完全一致的地方，说明一下取舍：函数返回值用 `bool` 而不是 `BOOL`
  （两者语义一致，`bool` 更不易把 `TRUE/FALSE` 和 `true/false` 混用出错），
  字符串用 `std::wstring` / `std::string` 而不是 `TCHAR` 定长缓冲
  （本项目是 Unicode-only 构建，且协议线上的字符串必须是 UTF-8，用容器能避免越界与手工长度管理）。
  Win32 调用处的参数类型仍然全部是原生类型。

## 目录结构

```
LocalSendXP/                       解决方案根目录
├─ LocalSendXP.sln                 VS2008 解决方案（Win32 / x64，Debug / Release）
├─ README.md                       面向用户的项目介绍（英文，发布用）
├─ architecture.md                 本文档（开发/架构笔记，中文）
├─ LICENSE                         Apache License 2.0
├─ installer/                      安装程序（Inno Setup 脚本 + 打包脚本 + 许可证文本）
└─ LocalSendXP/                    工程目录
   ├─ LocalSendXP.vcproj           VS2008 工程文件（含 RC / 链接 / 清单设置）
   ├─ build/build_release.bat      命令行构建脚本（自动定位 VS2008）
   ├─ src/
   │  ├─ main.cpp                  入口：InitCommonControlsEx、单实例互斥体、wWinMain
   │  ├─ util/                     common.cpp / json.cpp / sha256.cpp / base64.cpp / config.cpp
   │  ├─ network/                  udp_socket.cpp / tcp_socket.cpp / http_client.cpp / http_server.cpp
   │  ├─ protocol/                 discovery.cpp / register.cpp / upload.cpp / download.cpp
   │  ├─ core/                     device.cpp / transfer.cpp / app.cpp
   │  └─ ui/                       main_window.cpp / tray.cpp / ui_util.cpp / dialog_*.cpp
   ├─ include/lsxp/                对应头文件（common.h / json.h / network.h / http.h / …）
   ├─ resource/                    LocalSendXP.rc、resource.h、LocalSendXP.manifest
   ├─ icons/                       应用与工具栏 ICO 资源（ICO 内嵌透明通道）
   └─ _wizard_backup/              VS 向导生成的空壳文件（已废弃，可删除）
```

构建产物：`LocalSendXP/bin/Release/LocalSendXP.exe`（或 `bin/Debug/`、`bin/x64/...`）。
首次运行会生成 `LocalSendXP.ini`（配置）、`LocalSendXP.log`（日志）、
`LocalSendXP.history`（接收历史）与 `LocalSendXP.pem`（自签证书）。
**放哪里由下面的「数据目录」决定**：绿色版放 exe 同目录，安装版放 `%APPDATA%\LocalSendXP`。

## 编译方法

**方式一：VS2008 IDE**

1. 双击 `LocalSendXP.sln`（VS2008 会提示升级则选择不升级）。
2. 选择 `Release|Win32`，菜单「生成 → 生成解决方案」。

**方式二：命令行（脚本会自动在 E:/C:/D: 盘查找 VS2008）**

```bat
cd LocalSendXP\build
build_release.bat            :: 默认 Release|Win32（命令行下跑完就退出）
build_release.bat Win32 Debug
build_release.bat x64 Release
build_release.bat Win32 Release rebuild   :: 强制全量重新生成
```

**双击 `build_release.bat` 也可以**：脚本发现没有参数（说明是从资源管理器双击的）时，
会在结束前 `pause`，窗口不会一闪而过；带参数运行时不会停，方便脚本调用。

`vcbuild` 是增量构建，没有任何源文件改动时会输出 `LocalSendXP - 最新`（英文工具链是
`up to date`）并在 1 秒内返回——这不是失败。想强制重新编译就加上第三个参数 `rebuild`
（内部是 `vcbuild /rebuild`）。

工程里刻意做了三处与"现代默认值"不同的设置，都是为了 XP 与脚本化构建：

- `DebugInformationFormat=1`（`/Z7`，调试信息进 .obj）而不是 `/Zi`：避免多文件并行编译时争抢同一个
  `vc90.pdb`（VS2008 会报 `C2471 无法更新程序数据库`）。
- 链接器 `MinimumRequiredVersion=5.01`：PE 头声明最低支持 XP。
- 依赖库只写在源码的 `#pragma comment(lib, ...)` 里，不写进 `AdditionalDependencies`：
  VS2008 的 `vcbuild` 会把该属性当成一个整体文件名传给 link.exe（`LNK1181`）。

## 数据目录（便携模式与安装模式）

程序自己要写四类文件：配置 `LocalSendXP.ini`、日志 `LocalSendXP.log`、
接收历史 `LocalSendXP.history`、自签证书 `LocalSendXP.pem`。它们放在哪里由
`GetDataDirectoryW()`（`src/util/common.cpp`）在启动时决定，规则只有三条，按顺序判断：

1. exe 同目录**已经存在** `LocalSendXP.ini` → 判定为绿色版，所有文件继续留在 exe 同目录
   （即历史行为，解压即用、U 盘可用）；
2. 否则探测 exe 同目录**是否可写**（`DirectoryIsWritableW()`：用一个
   `FILE_FLAG_DELETE_ON_CLOSE` 的临时文件试写一次，不留痕迹）→ 可写就用 exe 同目录；
3. 否则（典型的 `C:\Program Files\LocalSendXP` 且未提权运行）→ 落到
   `%APPDATA%\LocalSendXP`，目录不存在时自动创建。

为什么必须做这件事：manifest 里声明了 `requestedExecutionLevel level="asInvoker"`，
带清单的程序在 Vista 之后**不会**被 UAC 文件虚拟化重定向，写 Program Files 会直接
`ACCESS_DENIED`；XP 上 `Program Files` 对 `Users` 组同样只有读+执行权限。要么把数据
搬进用户区，要么要求以管理员身份运行——这里选了前者，因为后者每次启动都弹 UAC 更糟。

配套改动（`GetModuleDirectoryW()` → `GetDataDirectoryW()`）：

| 位置 | 文件 |
| --- | --- |
| 配置路径 | `GetConfigFilePathW()` |
| 日志路径 | `App::Init()` 里的 `LogInit()` |
| 接收历史 | `HistoryStore::FilePath()` |
| 自签证书 | `Config::ResolvedCertificatePath()` |
| 兜底接收目录 | `GetDefaultDownloadDirectoryW()`、`upload.cpp` 的 saveDirectory 兜底 |

CA 证书包 `certs\ca-bundle.crt` 与两个 OpenSSL DLL 是**随程序安装的只读资产**，
仍然从 exe 同目录加载（`Config::ResolvedCaBundlePath()`、`openssl_api.cpp`），
不跟着数据目录走。

实测（三个场景，脚本驱动）：

1. exe 同目录有 ini（绿色版）→ 运行后 `%APPDATA%\LocalSendXP` **没有**被创建；
2. 把 exe+DLL+certs 复制到临时目录，用 `icacls /deny <用户>:(W,D)` 把目录设为只读后运行
   → 程序正常启动，`%APPDATA%\LocalSendXP` 下自动生成 `LocalSendXP.ini` / `.log` / `.pem`，
   程序目录里**一个文件都没写**，日志里能看到证书生成在 `%APPDATA%` 路径下、HTTPS 正常；
3. 新解压的目录（可写、无 ini）→ `ini` / `log` / `pem` 全部生成在 exe 同目录，
   `%APPDATA%` 里的旧 ini 未被触碰。

## 模块说明（含文件结构变化 / 编译方法 / XP 兼容注意事项）

### Phase 1　Win32 GUI 骨架

- **文件结构**：新增 `src/ui/main_window.cpp`、`src/ui/ui_util.cpp`、`src/ui/dialog_about.cpp`、
  `src/ui/dialog_settings.cpp`、`src/ui/dialog_receive.cpp`、`src/ui/tray.cpp`、`src/main.cpp`，
  资源集中在 `resource/LocalSendXP.rc`（菜单、快捷键、对话框、字符串表、图标、版本信息、清单）。
- **编译方法**：`build_release.bat Win32 Release`，或 IDE 生成。本模块不依赖网络部分，可单独编译通过。
- **XP 兼容注意**：
  - 界面字符串全部放在 `.rc` 的字符串表里，C++ 源码保持纯 ASCII。
    `.rc` 用 UTF-8 + `#pragma code_page(65001)`，VS2008 自带的 `rc.exe 6.0.5724` 可正确编译出中文；
    这样避免了 MSVC2008 对 UTF-8 源码（无 BOM）按代码页 936 误读导致的 `C2001 常量中有换行符`。
  - 对话框字体写死为 `宋体 9pt（0x86 字符集）`，主窗口字体在中文系统上用宋体 9pt、其他语言用 Tahoma 8pt，
    这是 2000/XP 时代中文软件的观感。
  - 清单声明 `Microsoft.Windows.Common-Controls 6.0`（Luna 主题）与 `supportedOS`（XP→Win11），
    并用 `asInvoker` 权限运行（不弹 UAC）。
  - 界面布局全部用 `WM_SIZE` + `MoveWindow` 手算，不依赖任何布局框架；最小窗口 620×430
    （宽度下限是为了让 5 个工具栏按钮始终完整可见）。

### Phase 2　UDP 设备发现

- **文件结构**：新增 `src/network/udp_socket.cpp`（Winsock2 UDP 封装）、
  `src/protocol/discovery.cpp`（组播广播 + 广播回退 + 遗留 HTTP 扫描）、`src/core/device.cpp`（设备表）。
- **编译方法**：`build_release.bat Win32 Release`。
- **实现要点**：
  - 组播地址 `224.0.0.167`，UDP 端口默认 `53317`，TTL=1，关闭组播回环（避免收到自己的公告）。
  - 启动即发送一次 `announce:true` 公告，之后每 30 秒（可在 ini 调整）重发；同时向
    `255.255.255.255` 与各网卡的 `x.y.z.255` 广播，作为组播被交换机丢弃时的回退手段。
  - 收到 `announce:true` 时，按协议先用 HTTP `POST /api/localsend/v2/register` 回注册；
    若失败则退回 UDP 单播响应（`announce:false`）。
  - 兼容协议 v1：v1 公告里没有 `version` 字段、用的是 `announcement`，此时走
    `/api/localsend/v1/register` 并回复 v1 格式；后续传输也对该设备自动改用 v1 端点。
  - 手动「刷新」会额外触发旧式 HTTP 扫描：把本机所在 /24 网段的所有地址按 8 线程并行探测
    （400ms 超时），用于组播被禁的网络环境。
  - 设备 90 秒无公告即从列表移除。
- **XP 兼容注意**：`IP_ADD_MEMBERSHIP` / `SO_BROADCAST` / `select()` 全是 Winsock1 时代就有的调用；
  本机 IP 枚举用 `GetAdaptersInfo`（iphlpapi，XP 自带），没有使用 XP 之后才有的 `GetAdaptersAddresses`。

### Phase 3　register（设备注册）

- **文件结构**：新增 `src/protocol/register.cpp`（设备信息构造/解析、注册收发、`/register`、`/info` 路由）、
  `src/network/http_client.cpp`（自研 HTTP/1.1 客户端，含分块与流式上传）、
  `src/network/http_server.cpp`（自研 HTTP/1.1 服务器，含 keep-alive 与文件流式下发）。
- **编译方法**：`build_release.bat Win32 Release`。
- **实现要点**：
  - 请求头固定带 `User-Agent`、`Content-Type: application/json`；客户端用 `Connection: close`，
    服务器同时支持 `keep-alive`（官方 Rust 客户端会复用连接）。
  - 指纹（fingerprint）保存在 ini 中，用于跳过自己发出的公告。
- **XP 兼容注意**：HTTP 全部自己实现，没有用 WinHTTP（XP 上默认只有 1.0）；
  服务器"每连接一线程"，线程数上限 24，超出直接返回 503，避免 XP 上资源被耗尽。

### Phase 4　文件发送与接收

- **文件结构**：新增 `src/protocol/upload.cpp`（prepare-upload / upload / cancel，v2 + v1 回退）、
  `src/protocol/download.cpp`（download API、浏览器分享页、`ShareManager`）、
  `src/core/transfer.cpp`（传输任务表、速度与剩余时间计算）、`src/core/app.cpp`（发送线程、UI 事件队列）。
- **编译方法**：`build_release.bat Win32 Release`。
- **实现要点**：
  - 发送方流程：`prepare-upload` →（收到 `sessionId` + 每个文件的 `token`）→ 逐文件 `upload`
    （`HttpClient::ExecuteStreaming` 边读 `ReadFile` 边 `send`，64KB 一块，回调上报进度）→ 结束或 `cancel`。
  - 接收方流程：确认对话框（可逐文件勾选）→ 生成 `sessionId` 与每文件 token → 校验
    `sessionId`/`token`/来源 IP → 边收边写盘并**增量计算 SHA-256**，与发送方声明的哈希比对，
    不一致则删除文件并回 `422`。
  - 重名文件自动改名（`bigfile (1).dat`），文件名单个非法字符替换为 `_`，防止路径穿越。
  - 文件 id 用随机十六进制串；会话 5 分钟无活动自动作废，避免"对方异常退出后一直忙碌"。
  - 支持多文件与**文件夹**（协议 v2.2 本身没有目录结构字段，因此按文件逐个发送，接收端只保留文件名，
    与官方实现一致）。文件夹递归枚举有深度限制（16 层）与数量上限（2000 个文件）。
  - 小于 64MB 的文件在发送前计算 SHA-256 一并声明，接收端会校验；大文件跳过哈希以节省时间。
  - 反向传输（浏览器下载）：`工具 → 通过浏览器分享` 选中文件后生成 `http://本机IP:端口/?sessionId=…`
    链接，接收方用浏览器打开即可下载，服务端实现了 `prepare-download` / `download` 与一个 2000 年代风格的
    下载索引页。
  - 反向传输的**客户端侧**：`文件 → 从网址接收…`（Ctrl+U）粘贴对方分享的网址（也支持直接粘贴
    `192.168.1.5:53317` 这种省略协议头的写法，打开对话框时会自动读剪贴板），客户端会先
    `POST /api/localsend/v2/prepare-download` 拿到文件清单，再逐个 `GET /api/localsend/v2/download`
    并把响应体**边收边写盘**（不整包进内存），进度同样显示在传输列表里。
- **XP 兼容注意**：
  - 文件读写只用 `CreateFileW` / `ReadFile` / `WriteFile` / `SetFilePointer`，没有异步 IO 框架；
  - SHA-256 自己实现（`src/util/sha256.cpp`），不依赖 XP 上可能缺失的 CSP 算法；
  - 随机数优先用 `advapi32!SystemFunction036`（XP 可用），失败时退化为 `rand()`；
  - 时间与速度计算用 `GetTickCount()`（49.7 天回绕在差值计算下是安全的）。

### Phase 5　界面完善

- **文件结构**：`src/ui/*` 内补全托盘菜单、设置对话框、接收确认对话框、PIN 输入、分享链接对话框、拖放发送。
- **编译方法**：`build_release.bat Win32 Release`。
- **实现要点**：
  - 顶部工具栏（`ToolbarWindow32`，`TBSTYLE_FLAT | TBSTYLE_LIST`）：发送文件夹、从网址接收、
    接收文件夹、历史记录、设置、关于共 6 个按钮。图标从 EXE 内嵌的 ICO 资源加载为 32×32 的
    `ILC_COLOR32` 图像列表，保留透明通道（**图像列表顺序必须与按钮顺序一致**，
    按钮文字用 `TB_SETBUTTONINFO` 更新，注意它的 `wParam` 是命令 ID 而不是按钮下标）。
  - “帮助 > 使用说明”使用原生对话框和内置字符串，不再查找或打开 `README.md`，复制 EXE 即可使用。
  - 主窗口：菜单栏、`附近设备` 列表（名称/型号/类型/IP/最后发现）、`文件传输` 列表
    （文件名/大小/进度/速度/剩余时间/状态）、整体进度条、三栏状态栏（状态、设备数、本机信息）。
  - 传输列表的"进度"列是 `NM_CUSTOMDRAW` 自绘的进度条（`DrawEdge` 画凹陷边框 + 蓝色填充 + 居中百分比），
    就是当年迅雷/FlashGet 的样子。
  - 传输列表右键菜单：取消传输、清除已完成的传输记录。
  - 窗口位置与大小记在 `LocalSendXP.ini` 的 `[window]` 段（`x/y/width/height/maximized`），
    下次启动原位恢复；如果屏幕布局变了、记下的坐标已经跑到屏幕外，会自动退回默认位置。
  - 收到对方传输请求时会响一声系统提示音（`MessageBeep(MB_ICONASTERISK)`），
    这是当年局域网工具的标准做法。
  - `-silent` 参数（开机自启用它）启动时窗口不显示，直接进托盘并弹气泡提示。
  - 系统托盘（`Shell_NotifyIcon`）：双击还原窗口，右键菜单（显示/发送/打开接收文件夹/设置/退出），
    气泡通知提示收到的文件。
  - 支持把文件或文件夹直接拖到窗口上发送（`WM_DROPFILES` + `DragAcceptFiles`）。
  - 接收对话框可逐文件勾选；对方要求 PIN 时弹出 PIN 输入框并自动重试 `prepare-upload`。
  - 设置写入 `LocalSendXP.ini`（`GetPrivateProfileStringW`/`WritePrivateProfileStringW`），
    开机启动写 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`。
- **XP 兼容注意**：
  - 托盘与对话框用的都是 XP 世代的结构体字段；气泡提示用 `NIF_INFO`（XP 支持）。
  - 手动编辑 ini 时请用记事本默认的 ANSI 或另存为 Unicode；**不要存成 UTF-8**，
    `GetPrivateProfileStringW` 只识别 ANSI/UTF-16 文件（程序自己保存时写的就是 UTF-16）。

### Phase 6　XP 兼容测试

- 编译期：整个工程用 **真实 VS2008 工具链**（`cl.exe` 15.00 + SDK 6.0A）在 `Release|Win32` 与
  `Debug|Win32` 下编译，0 错误（仅 1 条 `mt.exe` 对 `compatibility` 段的清单编写警告，可忽略）。
- 运行期：在 Windows 10 上实测（见下方"验证记录"）。
- 真机 XP 验证需要你在 XP SP3 机器上运行 exe；因为静态链接 CRT、子系统 5.01、只调用 XP 自带 API，
  exe 可以直接拷过去运行（详见"XP 兼容注意事项汇总"）。

## 与官方协议的对应关系

| 端点 | 我们实现的方向 | 说明 |
| --- | --- | --- |
| `POST /api/localsend/v2/register` | 收发双向 | 设备注册（含 v1 版本 `/v1/register`） |
| `GET /api/localsend/v2/info` | 服务端 | 调试用信息（含 v1 `/v1/info`） |
| `POST /api/localsend/v2/prepare-upload?pin=` | 收发双向 | 元数据协商，返回 `sessionId` 与每文件 token |
| `POST /api/localsend/v2/upload?sessionId=&fileId=&token=` | 收发双向 | 二进制上传，支持并行 |
| `POST /api/localsend/v2/cancel?sessionId=` | 收发双向 | 取消会话 |
| `POST /api/localsend/v2/prepare-download` | 收发双向 | 反向传输元数据（服务端供浏览器下载，客户端用于"从网址接收"） |
| `GET /api/localsend/v2/download?sessionId=&fileId=` | 收发双向 | 反向传输文件内容（客户端流式落盘） |
| `POST /api/localsend/v1/send-request`、`/v1/send`、`/v1/cancel` | 收发双向 | 兼容只支持 v1 的旧客户端 |

错误码严格按协议返回：`400 / 401 / 403 / 404 / 409 / 422 / 429 / 500 / 503 / 204`。

## HTTPS / TLS（默认开启）

## 界面语言与设置对话框

### 语言切换（简体中文 / English）

界面语言在 **设置 → 常规 → 界面语言** 里切换，默认 `简体中文`，**不需要重启**：切换后标题、
菜单、工具栏、列表表头、状态栏、设置对话框本身会立刻换成另一种语言。选择保存在
`LocalSendXP.ini` 的 `[general] language`（`zh` / `en`）。

实现方式（延续 2000 年代的做法，不引入任何 i18n 库）：

| 部分 | 做法 |
| --- | --- |
| 字符串 | `.rc` 里同一批 ID 写两份 `STRINGTABLE`，分别放在 `LANGUAGE LANG_CHINESE` 与 `LANGUAGE LANG_ENGLISH` 块；`LoadStr()` 用 `FindResourceExW` 按当前语言取块，再按 Win32 字符串表格式（每块 16 条、每条一个 WORD 长度前缀）手工解析，不依赖线程区域设置 |
| 菜单 | 菜单不再使用 MENU 资源，而是运行时用 `CreateMenu`/`AppendMenu` 按字符串 ID 构建，因此可以随语言重建 |
| 对话框 | 新增 `LxpDialogBoxParam()` / `LxpCreateDialogParam()`，用 `FindResourceExW(RT_DIALOG)` + `DialogBoxIndirectParam` 显式按语言加载模板；对话框内的标题、分组框、标签、按钮、复选框统一在 `WM_INITDIALOG` 里用字符串 ID 赋值 |
| 立即生效 | 语言改变时先 `SetResourceLanguage()`，再重建菜单并调用 `RefreshMainWindowTexts()`（按控件 ID 遍历子窗口，因此**即使同一个分组框 ID 重复出现也会全部刷新**），最后重绘主窗口与设置对话框 |
| 工具栏 | 工具栏按钮文字用 `TB_SETBUTTONINFO` 更新；该消息的 `wParam` 是**命令 ID 而不是按钮下标**，工程里为每个按钮同时保存了命令 ID（`toolbarCommandIds`） |

另外三个对话框（从网址接收 `IDD_URL`、使用说明 `IDD_HELP`、关于 `IDD_ABOUT`）以及接收确认
（`IDD_RECEIVE`）和 PIN 输入（`IDD_PIN`）都统一走 `LxpDialogBoxParam()`，并在 `WM_INITDIALOG`
里按字符串 ID 赋值——模板里的中文只是设计期占位，运行时会被当前语言覆盖。关于对话框不再显示
TLS 版本与证书指纹（指纹仍在 **设置 → 安全** 里显示），作者与开源仓库按钮随语言切换。

`IDD_PIN` 的标题、提示文字与按钮原先直接使用模板中文，现已改为字符串 ID；同时补上了
`PinProc` 里漏掉的 `SetWindowLongPtrW(dialog, DWLP_USER, lParam)`——此前输入的 PIN 根本传不回调用方。

### 设置对话框（选项卡）

设置对话框改为 4 个选项卡，项目重新归类：

| 选项卡 | 内容 |
| --- | --- |
| **常规** | 设备名称、本机类型、设备型号、**界面语言**；启动与托盘：开机自动启动、关闭时最小化到托盘 |
| **传输** | 接收文件保存到（含浏览）、收到文件时先询问、接收完成后打开文件夹；传输保护：接收 PIN 码 |
| **网络** | 监听端口、设备公告间隔（秒）、本机地址（实时显示，例：`192.168.1.8:53317`） |
| **安全** | 启用 HTTPS、TLS 1.0/1.1 回退、允许不安全 HTTPS、要求客户端证书、证书指纹（实时显示）；证书文件路径与 CA 证书包路径 |

实现：`IDD_SETTINGS` 只放 `SysTabControl32` 与说明/按钮，4 个页面各自是 `WS_CHILD` 的对话框模板
（`IDD_SETTINGS_GENERAL/TRANSFER/NETWORK/SECURITY`），用 `CreateDialogIndirectParam` 建成子窗口，
按 `TabCtrl_AdjustRect` 算出的显示区定位，`TCN_SELCHANGE` 时切换显示；页面把 `WM_COMMAND`
转发给设置对话框统一处理。

### 已实测

- 打开设置：4 个页签齐全，各页控件与动态内容（本机地址、证书指纹、证书路径）正常显示。
- 中文 → English → 中文 双向切换：对话框标题、说明、分组框、标签、复选框、主窗口菜单/工具栏/分组框、
  列表表头全部随之切换，且**切换过程中程序不崩溃**（脚本逐控件读取窗口文本核对）。
- 工具栏语言跟随：抓取工具栏位图做像素比对，中文 → English 有 25.6% 像素变化，English → 中文同样变化，
  切回中文后与初始位图 **0% 差异**，说明按钮文字确实随语言重建且不会留下残留。
- 切换后立即重绘：切语言瞬间抓图与随后强制 `RedrawWindow` 再抓图 **0% 差异**，即不需要再点一下页签
  文字就是新的（此前需要切换选项卡才显示）。
- 文本裁剪：用控件自身的字体（`WM_GETFONT` + `GetTextExtentPoint32W`）逐控件量宽度，
  关于 / 使用说明 / 从网址接收 / 设置四个对话框在中文与英文两种语言下均无裁剪；
  设置页按英文尺寸排布（标签列 110 DU、输入控件 x=132、复选框宽 288 DU）。
- 选择持久化：退出后 `LocalSendXP.ini` 为 `language=zh`（中文默认）。

> 中文字符串表曾经被误写成英文（186 条里有 130 条是英文原文），表现为中文界面里夹杂英文
> （例如设置里的「加密传输」）。现已从 git 历史逐条恢复，并补齐新增的 HTTPS 相关中文；
> 校验脚本会统计「不含中日韩字符的中文条目」，当前只剩品牌名、`HTTPS`、`English` 这类本来就不翻译的项。

### 为什么是 OpenSSL 1.0.2

XP SP3 自带的 SChannel 最高只到 **TLS 1.0**，而官方 LocalSend 基于 rustls，**只支持 TLS 1.2+**，
两者根本握不上手。所以 HTTPS 不依赖系统 Schannel，而是自带一份能在 XP 上运行的
**OpenSSL 1.0.2u**（1.0.2 是最后一个支持 XP 的分支；1.1.0 起改用新 CRT/API，XP 直接不支持）。

### 编译 OpenSSL（一次性）

```bat
:: 源码取自 codeload（GitHub release 直链在国内常被墙）
::   https://codeload.github.com/openssl/openssl/tar.gz/refs/tags/OpenSSL_1_0_2u
:: 用 VS2008 + Git 自带 Perl 配置（no-asm 免 NASM）：
perl Configure VC-WIN32 no-asm no-idea no-mdc2 no-rc5 --openssldir=<dir> --prefix=<dir>
call ms\do_ms.bat
:: 关键一步：把 ms\ntdll.mak 里的 /MD 全部改成 /MT（否则 DLL 依赖 MSVCR90，XP 上要装运行库）
nmake -f ms\ntdll.mak
```

产物：`libeay32.dll`（1.31 MB）+ `ssleay32.dll`（338 KB），**与 LocalSendXP.exe 放在同一个目录**即可，
不需要用户额外安装 OpenSSL。已实测 DLL 只导入 `WS2_32/GDI32/ADVAPI32/USER32/KERNEL32`，
**没有 MSVCR90、没有任何 Vista+ API**。

注意：把 Git 的 `usr\bin` 追加到 PATH **末尾**，否则 nmake 调用的 `link` 会变成 GNU coreutils 的 `link`。

### 代码结构（协议层不依赖 TLS 实现）

```
include/lsxp/tls.h            TlsContext / TlsStream / 验证模式（不含任何 OpenSSL 声明）
include/lsxp/network.h        IStream（TcpSocket 与 TlsStream 的共同接口）
src/crypto/openssl_api.{h,cpp} 私有函数指针表：动态 LoadLibrary + GetProcAddress，Win32 锁回调
src/crypto/tls_context.cpp     自签证书生成/加载、指纹、CA bundle、client/server 两套 SSL_CTX
src/network/tls_socket.cpp     SSL_connect/SSL_accept、SNI、指纹固定 / CA+主机名校验、超时读写
```

`HttpClient` / `HttpServer` 只认 `IStream`，**看不到 `SSL_CTX*`、`SSL*`**；加密与否由
`HttpConnectionOptions`（secure / hostName / verifyMode / expectedFingerprint / allowLegacyTls）描述。

### 设置项（默认开启）

设置对话框底部新增「HTTPS / TLS 加密」分组，写入 `LocalSendXP.ini` 的 `[security]` 段：

| 键 | 默认 | 含义 |
| --- | --- | --- |
| `https` | **1（开）** | 启用 HTTPS 加密传输；关闭则退回纯 HTTP（XP 兼容模式） |
| `allowLegacyTls` | 0 | 额外允许 TLS 1.0/1.1 回退（默认只允许 TLS 1.2） |
| `allowInsecureHttps` | 0 | **明确**允许不校验证书（除用户手动开启外，程序不会使用 `SSL_VERIFY_NONE`） |
| `requireClientCertificate` | 0 | 要求对方出示客户端证书 |
| `certificate` / `caBundle` | 空 | 自定义证书 / CA bundle 路径，空则用程序目录下的 `LocalSendXP.pem`、`certs\ca-bundle.crt` |

证书首次运行自动生成（RSA-2048 + SHA-256 自签，有效期 10 年）存为 `LocalSendXP.pem`；
**HTTPS 模式下公告的 `fingerprint` 就是该证书的 SHA-256**（HTTP 模式下仍是随机串，符合协议）。
状态栏会显示「· HTTPS」，关于对话框显示 OpenSSL 版本与证书指纹。

### 服务端同时兼容明文与加密

HTTPS 开启时，同一个端口既服务 TLS 客户端也服务明文客户端：收到连接后先 peek 首字节，
`0x16` 是 TLS 握手记录 → 走 `SSL_accept`，否则按普通 HTTP 处理。这样即使对端忽略了
我们公告的 `protocol`，也仍能连上。

### 已实测（本机 Windows 10，真实运行）

| 项目 | 结果 |
| --- | --- |
| OpenSSL DLL 导入表 | 仅 5 个 XP 自带 DLL，无 MSVCR90、无 Vista+ API |
| TLS 版本 / 套件（外部站点） | TLS 1.2 + `ECDHE-RSA-AES128/256-GCM-SHAxxx`，SNI 生效 |
| 证书验证（自带 CA bundle） | 对 `www.bing.com`：`Verify return code: 0 (ok)` |
| 本程序 HTTPS 服务端 | `curl -k https://127.0.0.1:53317/api/localsend/v2/info` 返回正确 JSON |
| 同端口明文兼容 | `curl http://127.0.0.1:53317/...` 同样返回正确 JSON |
| 握手细节 | 自带 openssl 客户端连本程序：`TLSv1.2` + 证书 `CN=DESKTOP-B01E684` |
| HTTPS 上传文件 | 自研客户端向自研服务端经 HTTPS 上传 5 MB，成功 |
| 指纹固定（正向） | 指纹正确时握手通过、传输成功 |
| 指纹固定（负向） | 指纹不匹配时被拒绝并给出明确错误 |
| 反向传输走 HTTPS（CA 校验） | 自签证书被握手阶段拒绝（`certificate verify failed`），证明不会静默跳过校验 |
| 反向传输走 HTTPS（允许不安全） | 用户显式开启后成功拉取 5 MB，SHA-256 与源文件一致 |
| 公告内容 | `"protocol":"https"` + `"fingerprint":"<证书SHA256>"`（抓包验证） |
| 设置项持久化 | 退出后 ini 出现 `[security] https=1` |

服务端日志会记录每次 TLS 连接的结果，例如：
`HTTPS accepted from 127.0.0.1: TLSv1.2 / AES256-GCM-SHA384 (client certificate none)`。

### HTTPS 相关限制

- **XP 上只能到 TLS 1.2**（OpenSSL 1.0.2 不支持 TLS 1.3）；对端若强制 TLS 1.3 则无法连接。
- 与官方 LocalSend 的 HTTPS 互通依赖**双方的证书指纹交换**：需要对方处于加密模式且能被发现到；
  这一条建议在真机上与手机端各测一次。
- 「从网址接收」对 `https://` 的公开站点使用 CA bundle 校验；对 LocalSend 的自签证书，
  要么走指纹固定（通过设备列表发送），要么由用户显式打开「允许不安全的 HTTPS」。

## 接收历史（History）

每接收完成一个文件就记一条历史，方便事后找回「刚收到的那张图存到哪去了」。

- **文件结构**：新增 `include/lsxp/history.h`、`src/core/history.cpp`（存储）、
  `src/ui/dialog_history.cpp`（对话框）；资源里新增 `IDD_HISTORY`、`IDI_TOOL_HISTORY`
  （`icons\history.ico`）以及中英双份字符串。
- **入口**：`工具 → 历史记录(&H)...`（快捷键 `Ctrl+H`）、工具栏第 4 个按钮（历史图标）、
  托盘右键菜单。工具栏按钮的命令 ID 与菜单项相同（`IDM_TOOLS_HISTORY`），因此三处共用一份处理逻辑。
- **存储**：程序目录下 `LocalSendXP.history`，UTF-8 纯文本，一行一条，字段用 Tab 分隔：
  `时间 / 来源设备 / 来源 IP / 字节数 / 文件名 / 保存路径`。用记事本就能看，最多保留 500 条
  （超出后丢弃最旧的）。写入用 `CreateFile` / `WriteFile`，与其余文件操作一致；没有引入
  JSON、数据库或任何新依赖。
- **列表**：`SysListView32`（报表模式、整行选中、网格线），列为「接收时间 / 来源设备 /
  文件名 / 大小 / 保存位置」；文件已经被删掉时在「保存位置」后面追加「（文件已不存在）」。
- **操作**：
  | 操作 | 行为 |
  | --- | --- |
  | 打开所在文件夹（双击 / 右键 / 按钮） | `explorer.exe /select,"<文件>"`，在资源管理器中选中该文件；文件已删则退化为打开上级目录 |
  | 删除记录（支持多选） | 只删除历史条目，**磁盘上的文件不动**，确认框里写明这一点 |
  | 清空历史 | 同上，清掉全部条目 |
- **线程**：接收完成发生在 HTTP 线程，历史存储内部用 `CRITICAL_SECTION` 互斥，写盘后立即返回；
  对话框只在 UI 线程读写。
- **XP 兼容**：`explorer /select` 与 `SysListView32` 在 XP 上都可用；对话框按钮宽度按各自
  字体测量后再布局，英文不再被裁掉；窗口可缩放，最小尺寸在 `WM_GETMINMAXINFO` 里限制。

已实测（本机，走完整的 LocalSend v2 流程）：

1. 用 `POST /api/localsend/v2/prepare-upload` + `/upload` 真实发送一个文件 → 文件落盘 32 字节，
   `LocalSendXP.history` 出现一条
   `2026-09-20 14:51:44 | TestSender | 127.0.0.1 | 32 | history_test.txt | <完整路径>`；
2. 打开「历史记录」：标题、说明、4 个按钮、表头都是当前语言，列表 1 行，摘要显示「共 1 条记录」；
3. 选中该行 → 删除记录 → 确认框文案正确 → 列表变 0 行、摘要变「还没有接收过文件。」、
   `LocalSendXP.history` 变空，**而接收到的文件仍在磁盘上**；
4. 英文界面下重跑一遍：按钮宽度分别为 171 / 114 / 108 / 75 px，逐控件量文本宽度均未裁剪。

## 打包与安装程序（installer/）

### 设计决定

| 决定 | 取值 | 原因 |
| --- | --- | --- |
| 安装范围 | 机器级（`PrivilegesRequired=admin`） | 要写 Program Files、要加防火墙规则，二者都需要管理员；纯每用户安装会失去自动加规则的能力 |
| 安装目录 | `{pf}\LocalSendXP` | 传统 2000 年代软件的位置 |
| 数据目录 | 程序自己回落到 `%APPDATA%\LocalSendXP` | 见上一节，避免提权运行也避免写不进去 |
| 防火墙 | 安装时可选勾选（默认勾） | XP 上 `netsh firewall`，Vista+ 上 `netsh advfirewall`，只放行局域网 |
| 卸载 | 结束进程 → 删 Run 键 → 删防火墙规则 → 可选删用户数据 | 收到的文件**永不**自动删除 |
| 界面语言 | 简体中文 + English | 与程序一致；缺 `ChineseSimplified.isl` 时自动降级为纯英文 |

### 文件

| 文件 | 作用 |
| --- | --- |
| `installer/LocalSendXP.iss` | Inno Setup 脚本（`[Setup]`~`[Code]` 全部逻辑） |
| `installer/build_installer.bat` | 打包脚本：取版本号 → 校验载荷 → 找 ISCC → 编译 |
| `installer/prepare_isl.ps1` | 把中文语言文件转成编译器需要的代码页（见下） |
| `installer/THIRD-PARTY-NOTICES.txt` | 第三方声明（OpenSSL / Mozilla CA / 协议出处） |
| `installer/licenses/LICENSE-OpenSSL.txt` | OpenSSL 双许可证全文（**刻意放进仓库**：`third_party/` 被 .gitignore 忽略，而许可证必须随二进制一起分发） |
| `NOTICE` + `LICENSE`（仓库根目录） | Apache-2.0 §4(d) 要求的署名文件，以及填好版权行的许可证全文 |

### 安装包里装了什么

```
{app}\LocalSendXP.exe             主程序
{app}\libeay32.dll / ssleay32.dll OpenSSL 1.0.2u 运行时（HTTPS 必需，XP 自带 TLS 只到 1.0）
{app}\certs\ca-bundle.crt         Mozilla 根证书包（121 张根证书，来自 curl 的 caextract）
{app}\licenses\*                  Apache-2.0 全文 + OpenSSL 许可证 + 第三方声明
{app}\docs\README.md              英文说明（GitHub 用的那份）
{app}\docs\architecture.md        本文件
```

**不装**：`LocalSendXP.pdb`（调试符号）、`LocalSendXP.pem`（每台机器首次运行自己生成，
预置会让所有用户共用同一张证书，指纹固定就失去意义）、`LocalSendXP.ini` /
`.log` / `.history`（用户数据）。

### 关键片段说明

- `AppMutex=LocalSendXP_SingleInstance_Mutex`：直接用程序自己的单实例互斥体名，
  程序在托盘里运行时安装/升级会提示先退出，而不是覆盖到一半失败。
- 防火墙命令放在 `[Run]` 里、**不带** `postinstall` 标记，因此它们在安装阶段执行，
  必然早于最后那条 `postinstall` 的"立即运行程序"；`Check: IsModernWindows` /
  `IsLegacyWindows`（`GetWindowsVersion >= $06000000`）在两种系统上走不同分支。
- 开机启动写的是 `HKCU\...\Run` 值名 `LocalSendXP`、数据 `"<exe>" -silent`，
  与程序里 `SetAutoStart()` 完全一致，两边不会打架；卸载时 `[Code]` 里再
  `RegDeleteValue` 一次，即使用户没勾这个任务、而是后来在设置里自己开的也能清掉。
- 卸载时询问是否删除 `%APPDATA%\LocalSendXP`（`CustomMessage('RemoveData')`，中英各一份），
  **不提供**删除"我的文档\LocalSendXP"的选项，避免误删用户文件。
- 中英双语的实现：`[Languages]` 里英文用 `compiler:Default.isl`，中文用外部
  `ChineseSimplified.isl`；由于 Inno Setup 本身不带中文语言文件，
  `build_installer.bat` 会依次找 `installer\ChineseSimplified.isl` 和
  `<Inno>\Languages\ChineseSimplified.isl`，找到就通过 `/DZH_ISL_FILE=...` 传给编译器，
  找不到就只出英文包并打印提示。脚本里用 `#ifdef HAVE_ZH` 包住中文语言行与中文
  `[Messages]`，只用 `#ifdef/#ifndef/#if/#endif`（不用 `#elif`、不依赖 `CompilerPath`）。

### 编译安装包

```bat
cd installer
build_installer.bat              :: 双击也行；缺 Release 时会自动先编程序
build_installer.bat rebuild      :: 先全量重编程序，再打包
```

脚本流程：读 `src/util/common.cpp` 的 `LSXP_CLIENT_VERSION` 作为版本号 →
逐个校验载荷文件是否存在 → 在 `INNO_SETUP_DIR`、`%ProgramFiles%\Inno Setup 5`、
`C:\Program Files (x86)\Inno Setup 5`（另含 D:/E: 与 Inno 6 的常见路径）、以及 PATH 中
查找 `ISCC.exe` → 调 `ISCC /DMyAppVersion=<版本> [ /DZH_ISL_FILE=<中文语言文件> ] LocalSendXP.iss`
→ 产物落在 `installer\output\LocalSendXP-<版本>-setup.exe`（该目录已加入 .gitignore）。

为什么锁定**Inno Setup 5.6.1**：它是最后一代编译器和生成的安装包都支持 Windows
2000/XP 的版本；Inno Setup 6.x 抬高了编译器的系统要求与默认 `MinVersion`，
在 XP 上需要额外验证。脚本里已经写明 `MinVersion=5.1`（XP SP3 及以上）。

### 踩过的坑（都已在脚本里修掉）

1. **自定义消息必须放 `[CustomMessages]`**。第一版写进了 `[Messages]`，ISCC 只把它当作"覆盖内置消息"，
   对不存在的名字**静默忽略**，直到解析 `[Tasks]` 才报
   `A custom message named "FirewallTask" has not been defined.`。
2. **Inno Setup 5 读 `.isl` 用的是 ANSI 代码页，且不接受 BOM**：
   - UTF-8（无 BOM）→ 中文按 GBK 误解，向导里全是乱码；
   - UTF-8 **带** BOM → 直接报 `Error on line 1 ...: Text is not inside a section`（首个字符成了 U+FEFF）。
   所以 `prepare_isl.ps1` 把语言文件解码后**按代码页 936 重写一份**到 `output\`，再用
   `/DZH_ISL_FILE=<那份副本>` 交给编译器；仓库里的原始 `.isl` 保持不动（UTF-8 或 ANSI 版本都能处理）。
3. **`LanguageName` 的乱码警告**：非 Unicode 的 Setup 会把 `LanguageName=简体中文` 当作 ISO-8859-1。
   `prepare_isl.ps1` 会把该行的非 ASCII 字符改写成 Inno 建议的 `<nnnn>` 转义
   （如 `<7B80><4F53><4E2D><6587>`），警告消失、语言选择框显示正常。
   顺便一提：`LanguageName` 属于 `[LangOptions]`，**不能**用 `[Messages]` 覆盖（会被忽略并另报一条警告）。
4. **6.x 的中文语言文件配 5.6.1**：会有一批 "message name ... is not recognized" 警告（无害，被忽略），
   以及 7 条 5.6 内置消息缺中文而回落英文（`FileExists`、`FileAbortRetryIgnore` 等，只在覆盖文件、
   磁盘错误这类极少出现的对话框里用到）。打包脚本把前者收敛成一行提示，避免刷屏。
5. **`PrivilegesRequired=admin` 与 per-user 区域的警告**：脚本会写 `HKCU\...\Run`（与程序自身的
   开机启动开关保持一致）并使用 `{userappdata}` 的快速启动图标，Inno 会提示"管理安装却改 per-user 区域"。
   这是有意为之：开机启动的值必须和程序读写的 HKCU 一致，否则设置页的勾选状态会和实际行为不符。
   桌面图标已经改用 `{commondesktop}`（机器级），快速启动栏任务默认**不勾选**。
6. **许可证署名**：仓库根的 `LICENSE` 原本是 Apache 全文的裸文本（末尾还是 `Copyright [yyyy] [name...]`
   占位符），安装程序的许可证页因此看不到软件名和作者。现在在 `LICENSE` 顶部加了产品名/作者/年份
   与第三方组件说明、填好附录里的版权行，并新增 Apache-2.0 §4(d) 要求的 `NOTICE`；
   两者都会随安装包进到 `{app}\licenses\`（`LICENSE.txt`、`NOTICE.txt`），
   安装包自身的版本资源也写上了 `ProductName`/`CompanyName`/`LegalCopyright`
   （用 `FileVersionInfo` 读 `LocalSendXP-1.0.0-setup.exe` 实测：
   `ProductName=LocalSendXP`、`CompanyName=daisukiKaffuChino`、`LegalCopyright=Copyright © 2026 daisukiKaffuChino`）。

### 已验证 / 未验证

- 已验证：`build_installer.bat` 能正确取到版本号（1.0.0）、载荷校验、ISCC 定位与
  找不到时的报错退出（返回码 1）、双击时窗口停留；
  另有一个 .iss 静态检查脚本，确认 11 个 `Source:`/`LicenseFile`/`SetupIconFile` 路径
  全部存在、节名合法、`#if/#endif` 配平、`[Messages]` 自定义消息与 `{cm:}`/`CustomMessage()`
  引用一一对应。
- **未验证**：本机没有安装 Inno Setup，所以 `.iss` 还没有真正编译过一次。
  装上 Inno Setup 5.6.1（然后把 `ChineseSimplified.isl` 放进它的 `Languages` 目录，
  或直接放到 `installer\`）后跑一次 `build_installer.bat` 即可；
  首次编译若有报错，多半是语言文件路径或 Inno 版本差异，按提示改一行即可。
- 待做（需要真机）：在干净的 XP SP3 虚拟机里走一遍安装 → 快捷方式/防火墙规则/自启 →
  与手机互传 → 卸载；再验证受限账户下能正常启动并写 `%APPDATA%`。

## XP 兼容注意事项汇总

1. **不调用任何 XP 之后才出现的 API**：没有 `GetTickCount64`、`InetPton`、`GetAdaptersAddresses`（Vista+）、
   没有 `StrCpyNW` 之外的安全字符串新接口，`shell32` 只用 `Shell_NotifyIcon` / `ShellExecute`。
2. **PE 头实测值**（`dumpbin /headers`）：`operating system version 5.00`、`subsystem version 5.00`、
   `subsystem 2 (Windows GUI)`。5.00 表示"Windows 2000 及以上"，XP SP3 完全可以加载
   （子系统版本高于本机才会被拒绝）。注意 VS2008 的 `vcbuild` 不会把工程里的
   "最低版本 5.01" 写进 PE 头，所以这里以实测值为准。
3. **静态 CRT**：不要改成 `/MD`，否则 XP 上需要安装 VC2008 运行库。
4. **清单里的 Common Controls 6.0**：这是 Luna 主题的关键；若移除，控件会退回 Win95 外观。
5. **GDI 资源**：字体、图标、菜单都在销毁时释放，长时间挂托盘不会泄漏 GDI 对象。
6. **导入表实测**（`dumpbin /imports`）：只依赖 `KERNEL32 / USER32 / GDI32 / COMDLG32 / ADVAPI32 /
   SHELL32 / ole32 / WS2_32 / COMCTL32 / IPHLPAPI` 十个系统 DLL，且函数全部是 XP SP3 自带
   （如 `SHGetFolderPathW`、`GetAdaptersInfo`、`ImageList_AddMasked`、`GetPrivateProfileStringW`）；
   Winsock 走的是稳定序数导入，与官方 ws2_32 一致。没有任何 Vista+ 独占 API。
7. **不要用 `std::thread` / `std::mutex`**：全部使用 `CreateThread` + `CRITICAL_SECTION` + `Event`。
8. **UI 线程只做界面**：所有网络 IO 在工作线程，UI 更新通过 `PostMessage(WM_APP_UI_EVENT)` 投递到主线程。
9. **端口冲突**：XP 上 `bind` 失败会给出中文提示，请在设置里换端口（默认 53317 与官方相同）。

## 验证记录（本次实际执行）

1. **编译**：`build_release.bat Win32 Release` 与 `Win32 Debug` 均 0 错误通过（VS2008 真实工具链）。
2. **启动**：exe 启动后 `netstat` 可见 `TCP 0.0.0.0:53317 LISTENING` 与 `UDP 0.0.0.0:53317`，
   日志记录 `HTTP server listening` 与 `discovery: listening on UDP 53317`。
3. **注册**：`GET /api/localsend/v2/info`、`POST /api/localsend/v2/register`（v2 与 v1）均返回
   协议要求格式的 JSON；`GET /` 返回下载索引页。
4. **接收**：用 curl 按协议 `prepare-upload` → `upload`，含中文文件名与中文内容，
   落盘后 SHA-256 与发送方声明一致（`4e51e40b…`）。
5. **发送**：用我们自己的发送端代码（`proto::PrepareUpload` + `proto::UploadFile`）向本机接收端发送
   5MB 二进制文件，进度回调 1%→100%，耗时 32ms，接收端 SHA-256 校验一致（`5a661b67…`）。
6. **错误分支**：校验和不符 → `422`；token 错误 → `403`；会话不存在 → `403`；取消 → `200`；未知路由 → `404`。
7. **PIN**：设置 PIN 后，未带 PIN → `401`，PIN 错误 → `401`，PIN 正确 → `200` 且上传成功。
8. **重名处理**：再次接收同名文件生成 `bigfile (1).dat`，未覆盖原文件。
9. **文件夹枚举**：多层目录递归枚举正确（3 个文件，含 2 层子目录）。
10. **反向传输（客户端侧）**：进程内测试——自己起共享服务端 → `ParseShareUrl` 解析
    `http://127.0.0.1:53510/?sessionId=…` → `prepare-download` 取清单 → `download` 流式落盘，
    5MB 二进制文件 SHA-256 与源文件一致（`5a661b67…`）。
11. **反向传输（应用内 UI 路径）**：外部脚本驱动真实控件——菜单「从网址接收」弹出对话框
    （`#32770` 标题「从网址接收」）、写入网址、点击「确定」，程序成功从 53511 端口的共享端
    拉取 5MB 文件，落盘大小与 SHA-256 均正确。
12. **PE 审计**：`dumpbin /headers` + `/imports` 确认子系统版本 5.00，且仅导入 XP SP3 自带的
    10 个系统 DLL（无任何 Vista+ 独占 API）。
13. **开机自启（走真实 UI）**：脚本驱动设置对话框勾选「开机时自动启动」并确定后，
    `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` 出现
    `LocalSendXP = "…\LocalSendXP.exe" -silent`，ini 同步为 `autoStart=1`；取消勾选后该值被删除
    （测试结束后注册表已恢复原状）。
14. **`-silent` 启动**：进程运行、`IsWindowVisible=False`（窗口不显示），日志出现
    `tray icon created`，HTTP/UDP 服务照常监听。
15. **窗口位置记忆**：把窗口拖到 140,90 / 700×560 后关闭，ini 写入
    `[window] x=140 y=90 width=700 height=560 maximized=0`；再次启动时窗口矩形逐像素一致。

本机实际运行的抓图（真实窗口，非设计稿）：

- 主窗口：`docs/screenshots/main_window.png`
- 传输列表（含自绘进度条）：`docs/screenshots/transfer_list.png`
- 接收确认对话框：`docs/screenshots/receive_dialog.png`
- 从网址接收对话框：`docs/screenshots/url_dialog.png`

自动化界面与资源检查（脚本枚举子窗口 + UI Automation）：

| 检查 | 结果 |
| --- | --- |
| 主窗口子控件 | `ToolbarWindow32`, `Button`(分组框)×2, `SysListView32`×2, `SysHeader32`×2, `msctls_progress32`, `Button`×3, `msctls_statusbar32` |
| 顶部工具栏 | 5 个按钮；首个按钮实测 104×37，使用 32×32 图标并保留文字 |
| 工具栏图标 | 5 个 ICO 均含 32×32 BGRA 帧，Alpha 范围 0–255，四角透明 |
| 使用说明 | 原生 `IDD_HELP` 对话框成功打开，只读编辑框已载入内置说明文本 |
| 关于 | 作者显示为普通文本，点击“开源仓库”按钮会调用系统浏览器 |
| 传输进度条渲染 | 进度条蓝 `RGB(49,106,197)` 像素 963（自绘进度条已绘制） |

## 已知限制

- **不支持加密（HTTPS）**：协议 v2.2 允许对端用 HTTPS 通信，本客户端只实现了明文 HTTP。
  如果对端开启了加密，设备列表会在类型后显示"（加密）"，发送时会给出中文提示；
  请在官方 LocalSend 的设置里关闭加密后再传输。
- **不支持剪贴板/文本消息**：LocalSend 的"发送消息"不属于 v2.2 文档定义的端点，未实现。
- **文件夹不保留目录结构**：协议本身不携带相对路径，接收端只保留文件名。
- **x64 配置未验证**：工程里提供了 `x64` 配置（目标子系统 5.02、`TargetMachine=17`），
  但开发机上没有安装 VS2008 的"x64 编译器工具"组件（`VC\bin\x86_amd64` 不存在），
  因此 x64 只做了配置层面的对齐，未实际编译过。安装该组件后执行
  `build_release.bat x64 Release` 即可验证。
- **TreeView / TabControl 未使用**：这两者属于允许的控件范围，但当前主窗口布局按你给的草图
  只用到 ListView / 菜单 / 工具栏 / 状态栏 / 进度条 / 按钮 / 编辑框 / 对话框；
  如果以后要做"按设备类型分组的树形设备视图"或"传输记录分页签"，可以再补上。

## 与官方 LocalSend 互通测试步骤

1. 确认双方在同一网段；如果对端是官方客户端，先在它的设置里**关闭加密**。
2. 两端都启动后，本机「附近设备」列表应出现对端（若没有，点「刷新」触发 /24 扫描）。
3. 选中设备 → 点「发送文件」或直接把文件拖进窗口。
4. 对端接受后，本机「文件传输」列表显示实时进度、速度与剩余时间。
5. 反向测试：让对端向你发送文件，本机会弹出接收确认框（可逐文件勾选），
   文件保存到设置中的接收目录，托盘弹出完成通知。
