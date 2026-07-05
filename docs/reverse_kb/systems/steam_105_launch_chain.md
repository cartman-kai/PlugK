# Steam 1.05 启动链与 Hook 边界

本文记录 Steam 版 1.05 的官方启动链、`ComeOn.dll` 中的 Steam 初始化位置，以及 plugK 后续应使用的 hook 边界。

来源：

- 静态分析：IDA Pro，目标为 Steam 版 `launcher.exe` 与 `ComeOn.dll`。
- 动态调试：x32dbg，目标为 Steam 版 `launcher.exe`，点击“启动简体中文游戏”后观察 `CreateProcessW` 参数。

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
