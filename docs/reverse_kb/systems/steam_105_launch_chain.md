# Steam 1.05 启动链与 Hook 边界

本文记录 Steam 版 1.05 的官方启动链、`ComeOn.dll` 中的 Steam 初始化位置，以及 plugK 后续应使用的 hook 边界。

来源：

- 静态分析：IDA Pro，目标为 Steam 版 `launcher.exe` 与 `ComeOn.dll`。
- 动态调试：x32dbg，目标为 Steam 版 `launcher.exe`，点击“启动简体中文游戏”后观察 `CreateProcessW` 参数。
- 静态分析：IDA Pro，目标为 Steam 版 1.05 `ComeOn.dll`，确认开场动画链路。

## 官方 launcher 启动 `ComeOn.exe`

Steam 版 `launcher.exe` 是内嵌 WKE/HTML/JS 的启动器。IDA 静态分析确认：

| 位置 | 说明 |
| --- | --- |
| `launcher.exe:sub_447DD0` / `0x00447DD0` | JS/native `CreateProcess` 包装函数，最终调用 Win32 `CreateProcessW` |
| `0x00447E9E` | `sub_447DD0` 内部 `call ds:CreateProcessW` |
| `0x004658A5` | `_WinMain@16` 附近注册 native binding，`push offset sub_447DD0` |
| `0x004658AA` | 注册名 `"CreateProcess"` |

动态调试确认，点击“启动简体中文游戏”后命中 `kernel32!CreateProcessW`：

```text
lpApplicationName   = <SteamLibrary>\steamapps\common\BladeSword\ComeOn.exe
lpCommandLine       = 空
lpCurrentDirectory  = <SteamLibrary>\steamapps\common\BladeSword
dwCreationFlags     = 0x00080000
lpEnvironment       = NULL
lpStartupInfo       = STARTUPINFOEXW, cb = 0x48
```

`0x00080000` 是 `EXTENDED_STARTUPINFO_PRESENT`。官方 launcher 没有使用 `CREATE_SUSPENDED`。

动态调试中 `CreateProcessW` 返回地址位于 `acgenral.dll`，说明 Windows appcompat shim 参与了调用链。后续 hook 不应依赖返回地址判断调用方，应依赖目标路径过滤。

## `ComeOn.dll` 的 Steam 初始化

Steam 版 `ComeOn.dll` 基址按 IDA 默认加载为 `0x10000000`。

| 位置 | 说明 |
| --- | --- |
| `_DllMain@12` / `0x10005770` | DLL 入口 |
| `0x1000586A` | attach 时调用 `sub_10005400(hModule)` |
| `0x1000587E` | detach 时调用 `SteamAPI_Shutdown()` |
| `sub_10005400` / `0x10005400` | 初始化函数，读取 DLL 同名 `.ini` 的语言配置，安装一批运行时 hook/补丁 |
| `0x10005623` | 读取 IAT 中的 `CreateFileW` |
| `0x10005629` | 将 `sub_100042E0` 作为 `CreateFileW` 包装函数参与安装 |
| `sub_100042E0` / `0x100042E0` | `CreateFileW` 包装函数；第一次处理 `.ini` 路径时触发 Steam 初始化 |

`sub_100042E0` 中确认的 Steam 初始化流程：

```text
DeleteFileA("steam_appid.txt")
SteamAPI_RestartAppIfNecessary(1792820)
SteamAPI_Init()
```

如果 `SteamAPI_Init()` 失败，会弹出：

```text
Steam must be running to play this game (SteamAPI_Init() failed).
```

随后进程退出。

`sub_100042E0` 只在全局状态 `dword_1001EE68 == 0` 且处理路径后缀为 `.ini` 时执行 Steam 初始化。初始化成功后会创建 `sub_100041F0` 线程；该线程注册隐藏 `VideoWindow` 窗口并运行消息循环。

## launcher 阶段开场动画

Steam 1.05 的开场动画由 `ComeOn.dll` 内部线程播放。该 DLL 可能在 `launcher.exe` 阶段加载，也可能在 `ComeOn.exe` 子进程内加载；不要只把处理边界限定在 `launcher.exe`。该版本没有 Steam 2.01 的 `dll_DirectShow_play_media` 导出函数，而是在 Steam 初始化成功后由内部线程播放：

| 位置 | 说明 |
| --- | --- |
| `sub_100042E0` / `0x100042E0` | `CreateFileW` 包装；首次处理 `.ini` 时完成 Steam 初始化，然后创建 `sub_100041F0` 线程并等待状态为 3 |
| `sub_100041F0` / `0x100041F0` | 视频窗口线程；注册 `VideoWindow` 类，创建隐藏窗口，创建 `StartAddress` 播放线程，消息循环结束后写 `dword_1001EE68 = 3` |
| `StartAddress` / `0x10003F20` | DirectShow/COM 播放线程；拼接 `%s\bdh\%s\begin.dhp` 并调用 `IGraphBuilder::RenderFile` |
| `dword_1001EE68` / `0x1001EE68` | 初始化/动画状态；`sub_100042E0` 等待其变成 3 |

播放线程内确认的路径格式：

```text
%s\bdh\%s\begin.dhp
```

当前实现边界：

1. `PlugKLauncherHook.dll` 仍安装在官方 `launcher.exe` 中。
2. 当配置 `UI.SkipIntroMovie=1` 时，patch `launcher.exe` 自身 IAT 的 `LoadLibraryW`，在 JS 加载 `ComeOn.dll` 返回后同步安装 `ComeOn.dll` Hook；后台等待 `ComeOn.dll` 继续保留作为兜底。
3. 安装 `ComeOn.dll` Hook 时 patch `ComeOn.dll` 自身 IAT 的 `CreateThread`。
4. 通过扫描 `VideoWindow` 字符串引用定位 `sub_100041F0`，再从函数中解析内部 `StartAddress` 播放线程、全局 `hWnd` 和 `mov dword_1001EE68, 3` 的状态变量地址。
5. 仅当 `lpStartAddress == StartAddress` 时，把线程入口替换为向 `VideoWindow` 投递 `WM_CLOSE` 后立即返回的空线程；保留 `sub_100041F0` 视频窗口线程与结尾清理，让它按原流程写 `dword_1001EE68 = 3`。
6. 不跳过 `SteamAPI_RestartAppIfNecessary` / `SteamAPI_Init`，不移除 `ComeOn.dll`，不影响后续 `ComeOn.exe` 创建与 `PlugK.dll` 注入。
7. `PlugK.dll` 在 1.05 `ComeOn.exe` 内也覆盖同一 DLL 播放链：先尝试 patch 已加载的 `ComeOn.dll`；如果尚未加载，则 hook `LoadLibraryA/W/ExA/ExW`，在动态加载 `ComeOn.dll` 返回后同步安装其 `CreateThread` IAT Hook，并保留短轮询兜底。

Steam 1.05 `launcher.exe` 静态补充：

| 位置 | 说明 |
| --- | --- |
| `sub_4480E0` / `0x004480E0` | JS native `LoadLibrary` 包装，参数来自 JS 字符串 |
| `0x004480FF` | `sub_4480E0` 内部调用 `LoadLibraryW(jsArg0)` |
| `0x004658CE` | `_WinMain@16` 注册 `"LoadLibrary"` -> `sub_4480E0` |
| `sub_447DD0` / `0x00447DD0` | JS native `CreateProcess` 包装 |
| `0x00447E9E` | `sub_447DD0` 内部调用 `CreateProcessW` |

`launcher.exe` 本体未发现 `begin.dhp` 明文；`dhp` / `bdh` 搜索只有常量或 CRT 符号误命中。当前判断是动画文件路径仍来自 `ComeOn.dll`。如果该 DLL 由 `launcher.exe` 的 JS `LoadLibrary` 加载，必须在这一同步边界尽快完成 DLL 内部 Hook；如果 DLL 在 `ComeOn.exe` 内加载，则由 `PlugK.dll` 的本进程 `LoadLibrary*` Hook 或已加载模块检查完成同样的 IAT patch。

2026-07-08 补充：人工验证发现通过 plugK 启动 Steam 1.05 仍会出现动画，因此保留上述 `ComeOn.dll` Hook，同时增加 `ComeOn.exe` 子进程兜底。IDA 切换到 Steam 1.05 `ComeOn.exe` 后确认：

| 位置 | 说明 |
| --- | --- |
| `sub_404C80` / `0x00404C80` | 将路径写入 `this+0x284` 并切到 state 6 的视频排队函数；仅作为路径提交观察点，当前不作为 Hook 目标 |
| `sub_50A8E0` / `0x0050A8E0` | 与 1.05 非 Steam 同构的路径播放包装，作为 `PlugK.dll` 已入队后的兜底 Hook 目标 |
| `sub_4043E0` / `0x004043E0` | 状态机，`0x00404520` 调用 `sub_50A8E0` |
| `sub_408550` / `0x00408550` | 拼接 `"%s\dhp\begin.dhp"` |
| `0x004085CA` | `"%s\dhp\begin.dhp"` 字符串引用 |
| `0x004085E2` | `sub_408550` 调用 `sub_404C80(lpString)`，提交开场路径 |

`sub_408550` 不是 bool 函数，不应整体短路。曾尝试在 `sub_404C80` 阶段拦截：文件名为 `begin.dhp`，且路径目录中包含 `dhp` 或 `bdh` 时直接返回成功，不把路径提交到 state 6。人工验证显示该范围过早，Steam 1.05 会跳过动画但进入游戏后画面卡住；撤销该 Hook 后，保留 `ComeOn.dll` 播放链 Hook 与 `sub_50A8E0` 播放入口兜底，已验证可跳过动画并正常进入游戏画面。

## plugK Hook 边界

Steam 版 1.05 不应移除或绕过 `ComeOn.dll`。该 DLL 同时承担 Steam 初始化和若干运行时补丁职责。

推荐边界：

1. 在官方 `launcher.exe` 中 hook `CreateProcessW`。
2. 仅当目标路径解析为当前游戏目录下的 `ComeOn.exe` 时处理。
3. 在原始 `dwCreationFlags` 上追加 `CREATE_SUSPENDED`，保留 `EXTENDED_STARTUPINFO_PRESENT` 和原始 `STARTUPINFOEXW` 指针。
4. 原始 `CreateProcessW` 成功后，向返回的 `hProcess` 注入 `PlugK.dll`。
5. 如果官方调用原本没有 `CREATE_SUSPENDED`，注入成功后恢复 `hThread`。
6. 返回成功结果给官方 launcher，不关闭其 `PROCESS_INFORMATION` 句柄。

这种方式保留：

- 官方 `launcher.exe` UI 与语言选择。
- 官方 `ComeOn.exe` 创建路径。
- `ComeOn.dll` 的运行时补丁与 Steam 初始化。
- 游戏目录下 `steam_api.dll` 的加载与认证路径。

不应采用：

- patch 掉 `ComeOn.dll` 加载。
- 把现有 `PlugK.dll` 直接注入官方 `launcher.exe`。
- 依赖 `acgenral.dll` 或具体返回地址过滤 launcher 调用。
- 重建 `STARTUPINFO` 导致丢失 `STARTUPINFOEXW`/attribute list。

## 手工验证

v0.7.2 实现后，已验证 Steam 版 1.05 可以通过 `plugKLauncher.exe` 的 MOD 模式启动。启动流程只需要选择一次官方 launcher 语言，`ComeOn.dll` 与游戏目录下 `steam_api.dll` 均保持官方加载路径。

跳过开场动画：launcher 阶段后台等待 Hook 构建后人工验证仍见动画；2026-07-08 补充 `ComeOn.exe` 的 `dhp\begin.dhp` 兜底 Hook 后仍见动画；随后补充 `launcher.exe` IAT `LoadLibraryW` Hook，以及 `PlugK.dll` 在 `ComeOn.exe` 内对 `ComeOn.dll` 加载和 `CreateThread` IAT 的兜底 Hook。首次实现直接跳过 `sub_100041F0` 后观察到游戏进入后画面卡住，因此收窄为只跳过内部 `StartAddress` 播放线程。随后发现 `sub_404C80` 上游排队 Hook 同样会导致进入游戏后卡住；撤销 `sub_404C80` Hook，只保留 `ComeOn.dll` 播放链 Hook 和 `sub_50A8E0` 兜底后，已验证可跳过动画并正常进入游戏画面。
