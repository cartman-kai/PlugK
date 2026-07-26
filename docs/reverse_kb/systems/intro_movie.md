# 开场动画

## 1.05 非 Steam

来源：IDA 静态分析与 x32dbg 动态调试。

开场动画文件位于游戏目录：

```text
dhp/begin.dhp
```

`dhp` 是“动画片”的拼音缩写。该文件可用 VLC 直接播放，但 `comeon.exe` 中没有明文 `dhp` 或 `begin` 字符串；运行时路径来自状态机中的 CString。

已确认播放链路：

```text
sub_401090
  sub_4043E0                主循环状态机
    state 0: sub_405300     初始化资源与 DirectShow 播放对象
      sub_50A6F0
        sub_50BAA0
          sub_50B9D0        CoInitialize / CoCreateInstance / QueryInterface
    state 6:
      this+0x284 CString -> "...\dhp\begin.dhp"
      sub_50A8E0(path, 0)
        sub_50A800
          sub_50BC10        MultiByteToWideChar + DirectShow RenderFile/Run
```

动态验证记录：

- 版本：1.05 非 Steam，`E:\Games\刀剑封魔录\comeon.exe`。
- 断点：`0x00404518`，状态 6 播放调用前。
- 命中时：`EAX = 0x1A0BDD70`，内存为 `E:\GAMES\刀剑封魔录\\dhp\begin.dhp`。
- 不应影响：其它剧情/系统视频，尤其是非 `dhp\begin.dhp` 路径。

plugK 当前实现：

- Hook 目标：`sub_50A8E0` / `0x0050A8E0`。
- 条件：配置 `UI.SkipIntroMovie=1`，路径尾部匹配 `begin.dhp`，且路径目录中包含 `dhp` 或 `bdh`。
- 行为：在 `sub_50A8E0` 阶段返回未播放，让原状态机清空待播放 CString，并在下一轮自然进入后续状态。

## 2.01 非 Steam

来源：IDA 静态分析与 x32dbg 动态调试。

开场动画文件仍为：

```text
dhp/begin.dhp
```

已确认播放链路：

```text
sub_40B340                主循环状态机
  state 0: sub_40C290     初始化资源与 DirectShow 播放对象
    sub_522710
      sub_523A80
        sub_523890        CoInitialize / CoCreateInstance / QueryInterface
  state 6:
    this+0x284 CString -> "...\dhp\begin.dhp"
    sub_522730(path, 0)
      sub_523AD0          MultiByteToWideChar + DirectShow RenderFile/Run
```

动态验证记录：

- 版本：2.01 非 Steam，`E:\GAMES\刀剑封魔录：上古传说\ComeOn.exe`。
- 断点：`0x0040B473`，状态 6 调用 `sub_522730` 前。
- 命中时：`EAX = 0x1581EE30`，栈参数 `[ESP] = 0x1581EE30`，`[ESP+4] = 0`。
- 路径内存为 `E:\GAMES\刀剑封魔录：上古传说\\dhp\begin.dhp`。
- 不应影响：其它剧情/系统视频，尤其是非 `dhp\begin.dhp` 路径；同一包装也可能被音频/其它媒体路径复用，例如动态调试曾在 `sub_522730` 入口观察到 `yy\25_HERO.mp3`。

plugK 当前实现：

- Hook 目标：`sub_522730` / `0x00522730`。
- 条件：配置 `UI.SkipIntroMovie=1`，路径尾部匹配 `begin.dhp`，且路径目录中包含 `dhp` 或 `bdh`。
- 行为：返回未播放，让原状态机清空待播放 CString，并在下一轮自然进入后续状态。

## 2.01 Steam

来源：IDA 静态分析、x32dbg 动态调试与运行进程观察。

Steam 2.01 的开场动画不走 `ComeOn.exe` 中的非 Steam `dhp\begin.dhp` 状态机。动态观察动画播放期间只有 `launcher.exe` 进程，主窗口标题为：

```text
ComeOn-begin-dhp
```

同一 Steam 目录下 `dhp\begin.dhp` 文件大小为 0 字节，因此 observed 开场动画来自 launcher 阶段的独立 DirectShow 播放窗口。

已确认播放链路位于 `launcher.exe` 进程内加载的 `ComeOn.dll`：

```text
launcher.exe
  LoadLibrary/GetProcAddress ComeOn.dll
    dll_DirectShow_play_media(param) / 0x10003E00
      CreateThread(sub_10003A80, param)
        sub_10003A80 / 0x10003A80
          parse "RenderFile" and "put_Caption"
          CoInitialize / CoCreateInstance
          IGraphBuilder::RenderFile
          IMediaControl::Run
          FindWindowExW(..., "ComeOn-begin-dhp")
          wait until playback finishes
```

动态验证记录：

- 版本：Steam 2.01，`E:\SteamLibrary\steamapps\common\刀剑封魔录外传：上古传说\launcher.exe`。
- 动画播放期间进程：`launcher.exe`。
- 动画窗口标题：`ComeOn-begin-dhp`。
- `ComeOn.dll` 运行时基址：`0x6E6C0000`。
- `dll_DirectShow_play_media` 运行时地址：`0x6E6C3E00`。
- `sub_10003A80` 运行时地址：`0x6E6C3A80`。

plugK 当前实现：

- Hook 归属：`PlugKLauncherHook.dll`，因为动画发生在 `ComeOn.exe` 创建前，`PlugK.dll` 注入 `ComeOn.exe` 后已无法拦截。
- Hook 边界：等待 `launcher.exe` 内 `ComeOn.dll` 加载后，解析导出 `dll_DirectShow_play_media` 中压入的线程入口，并 patch `ComeOn.dll` 自身 IAT 中的 `CreateThread`。
- 条件：配置 `UI.SkipIntroMovie=1`，`lpStartAddress == sub_10003A80`，且线程参数包含 `"RenderFile":"` 与 `begin.dhp`、`begin-dhp` 或 `ComeOn-begin-dhp`。
- 行为：把该次 `CreateThread` 的入口替换成立即返回的空线程，保留返回的线程句柄语义，让原导出函数继续 `CloseHandle`。
- 不应影响：后续 `launcher.exe` 启动 `ComeOn.exe`、Steam 初始化、`ComeOn.exe` 中其它 DHP/剧情视频。
- 2026-07-08 回归修正：2.01 保持后台轮询等待 `ComeOn.dll` 的 Hook 入口，不安装 `launcher.exe` 的 `LoadLibraryW` IAT Hook；`LoadLibraryW` 同步安装只用于 Steam 1.05。

## 1.05 Steam

来源：IDA 静态分析与人工验证。2026-07-08 已验证 Steam 1.05 跳过开场动画后可正常进入游戏画面。

Steam 1.05 与 Steam 2.01 不同，没有 `dll_DirectShow_play_media` 导出，也没有 `"RenderFile"` / `"put_Caption"` 参数解析。开场动画仍发生在 `launcher.exe` 阶段加载的 `ComeOn.dll` 内，但路径由 DLL 内部拼接：

```text
%s\bdh\%s\begin.dhp
```

已确认播放链路：

```text
sub_100042E0 / 0x100042E0       CreateFileW 包装；首次处理 .ini 时触发 Steam 初始化
  SteamAPI_RestartAppIfNecessary(1792820)
  SteamAPI_Init()
  CreateThread(sub_100041F0, 0)
  wait dword_1001EE68 == 3

sub_100041F0 / 0x100041F0       注册并创建隐藏 VideoWindow，运行消息循环
  RegisterClassW("VideoWindow")
  CreateWindowExW(..., "VideoWindow", WindowName, ...)
  ShowWindow(hWnd, 0)
  CreateThread(StartAddress, 0)
  GetMessageW / DispatchMessageW loop
  sub_10002870()
  dword_1001EE68 = 3

StartAddress / 0x10003F20       DirectShow/COM 播放线程
  CoInitialize / CoCreateInstance
  path = "%s\bdh\%s\begin.dhp"
  IGraphBuilder::RenderFile(path, 0)
  configure full-screen VideoWindow
  IMediaControl::Run
  IMediaEvent::WaitForCompletion(-1, ...)
  PostMessageW(hWnd, WM_CLOSE, 0, 0)
```

plugK 当前实现：

- Hook 归属：`PlugKLauncherHook.dll` 与 `PlugK.dll` 均覆盖同一 `ComeOn.dll` 播放链，分别处理 `launcher.exe` 阶段加载 DLL 和 `ComeOn.exe` 子进程内加载 DLL 的情况。
- launcher 阶段边界：patch `launcher.exe` 自身 IAT 的 `LoadLibraryW`，在 JS 加载 `ComeOn.dll` 返回后同步安装 `ComeOn.dll` Hook；同时保留后台等待作为兜底。
- `ComeOn.exe` 阶段边界：`PlugK.dll` 在 1.05 初始化时先检查本进程是否已经加载 `ComeOn.dll`；若未加载，则 hook `LoadLibraryA/W/ExA/ExW`，在后续动态加载 `ComeOn.dll` 返回后同步安装 Hook，并保留短轮询兜底。
- 安装时扫描 `VideoWindow` 字符串引用，定位 `sub_100041F0` 视频窗口线程；再从该函数内部解析 `StartAddress` 播放线程、全局 `hWnd` 和 `dword_1001EE68`。
- 条件：配置 `UI.SkipIntroMovie=1`，且 `CreateThread` 的 `lpStartAddress == StartAddress`。
- 行为：保留 `sub_100041F0` 视频窗口线程和结尾清理，只把内部 DirectShow 播放线程替换为投递 `WM_CLOSE` 后立即返回的空线程，让 `sub_100041F0` 按原流程退出并写 `dword_1001EE68 = 3`。
- 不应影响：Steam 初始化、`CreateProcessW` 启动 `ComeOn.exe`、`ComeOn.exe` 中其它视频播放。

Steam 1.05 `launcher.exe` 本体未发现 `begin.dhp` 明文；`dhp` / `bdh` 搜索只有常量或 CRT 符号误命中。已确认 `_WinMain@16` 注册 JS native `"LoadLibrary"` -> `sub_4480E0`，该函数在 `0x004480FF` 调用 `LoadLibraryW(jsArg0)`；因此动画文件路径仍来自 `ComeOn.dll`，但必须在 JS 加载 DLL 的同步边界完成 `CreateThread` Hook，避免后台轮询窗口期。

补充兜底：

- Steam 1.05 `ComeOn.exe` 中仍存在 `sub_50A8E0` / `0x0050A8E0` 播放包装，`sub_4043E0` 在 `0x00404520` 调用它。
- Steam 1.05 `ComeOn.exe` 的 `sub_408550` 在 `0x004085CA` 引用 `"%s\dhp\begin.dhp"`。
- `sub_408550` 不是 bool 函数；它会先做状态初始化，再调用 `sub_404C80(lpString)` 把 `begin.dhp` 提交到 state 6。
- `PlugK.dll` 在 1.05 中只 Hook `0x0050A8E0` 作为 `ComeOn.exe` 层播放兜底。路径判定为文件名 `begin.dhp` 且目录包含 `dhp` 或 `bdh`。
- 曾尝试 Hook `sub_404C80` / `0x00404C80`，在路径为 `begin.dhp` 时直接返回成功以阻止进入 state 6；人工验证显示该范围过早，Steam 1.05 会跳过动画但进入游戏后画面卡住，因此该 Hook 已撤销。该函数只能作为定位路径提交的观察点，不应作为当前跳过实现边界。
- 不应影响：`end.dhp`、其它 DHP 剧情视频，以及非 `dhp`/`bdh` 目录下同名文件。

## 待移植版本

其它 Steam 变体尚未动态确认。移植时优先用以下特征定位：

- 唯一或少量 `CoCreateInstance` 调用点。
- 调用 `MultiByteToWideChar` 后通过 COM vtable 渲染路径的函数。
- 主循环状态机中持有 CString 视频路径并调用播放包装函数的状态。
