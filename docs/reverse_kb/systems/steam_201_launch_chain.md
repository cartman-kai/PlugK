# Steam 2.01 启动链与 Hook 边界

本文记录 Steam 版 2.01 外传启动器的官方启动链，以及与 1.05 launcher hook 方案的复用边界。

来源：

- 静态分析：IDA Pro，目标为 Steam 版 2.01 `launcher.exe`。
- 动态调试：x32dbg，目标为 Steam 版 2.01 `launcher.exe`，点击“启动简体中文”后观察 `CreateProcessW` 调用点参数。
- 动态调试：x32dbg，目标为 Steam 版 2.01 `launcher.exe`，观察开场动画期间的进程、窗口标题与 `ComeOn.dll` 基址。
- 文件验证：Steam appmanifest `appmanifest_2924510.acf`，`appid = 2924510`，`installdir = 刀剑封魔录外传：上古传说`。

## 官方 launcher 启动 `ComeOn.exe`

Steam 版 2.01 `launcher.exe` 仍是内嵌 WKE/HTML/JS 的启动器。IDA 静态分析确认：

| 位置 | 说明 |
| --- | --- |
| `launcher.exe:sub_4475B0` / `0x004475B0` | JS/native `CreateProcess` 包装函数，最终调用 Win32 `CreateProcessW` |
| `0x0044767E` | `sub_4475B0` 内部 `call ds:CreateProcessW` |
| `0x00465EEF` | `_WinMain@16` 附近注册 native binding，`push offset sub_4475B0` |
| `0x00465EF4` | 注册名 `"CreateProcess"` |

动态调试中，`launcher.exe` 映像基址为 `0x00F90000`，`.text` 起始为 `0x00F91000`。IDA `.text` 起始为 `0x00401000`，因此运行时地址偏移为 `+0x00B90000`，`0x0044767E` 对应运行时 `0x00FD767E`。

点击“启动简体中文”后在 `0x00FD767E` 命中断点，调用点栈参数为：

```text
lpApplicationName   = <SteamLibrary>\steamapps\common\刀剑封魔录外传：上古传说\ComeOn.exe
lpCommandLine       = 空
lpCurrentDirectory  = <SteamLibrary>\steamapps\common\刀剑封魔录外传：上古传说
bInheritHandles     = TRUE
dwCreationFlags     = 0
lpEnvironment       = NULL
lpStartupInfo       = STARTUPINFO, cb = 0
```

调用返回成功，`PROCESS_INFORMATION` 中观察到：

```text
hProcess    = 0x00000648
hThread     = 0x00000644
dwProcessId = 0x00005970
dwThreadId  = 0x00004D08
```

2.01 官方 launcher 没有使用 `CREATE_SUSPENDED`，也没有使用 1.05 中动态观察到的 `EXTENDED_STARTUPINFO_PRESENT`。因此 hook 时必须保留官方传入的 `STARTUPINFO` 指针和原始 flag，只追加 `CREATE_SUSPENDED` 用于注入窗口。

## launcher 阶段开场动画

Steam 2.01 的开场动画发生在 `launcher.exe` 进程内，而不是 `ComeOn.exe` 启动后的游戏主循环内。动态观察到动画播放时：

```text
ProcessName      = launcher
MainWindowTitle  = ComeOn-begin-dhp
Path             = <SteamLibrary>\steamapps\common\刀剑封魔录外传：上古传说\launcher.exe
```

同一目录下 `dhp\begin.dhp` 文件大小为 0 字节，说明该动画不是非 Steam 路径中的 `ComeOn.exe -> dhp\begin.dhp` 状态机播放。

IDA 中 `ComeOn.dll` 有唯一导出：

| 位置 | 说明 |
| --- | --- |
| `dll_DirectShow_play_media` / `0x10003E00` | 外部调用入口，接收宽字符串参数 |
| `0x10003E0B` | 压入 `sub_10003A80` 作为 `CreateThread lpStartAddress` |
| `sub_10003A80` / `0x10003A80` | 解析 `"RenderFile"`、`"put_Caption"`，创建 DirectShow graph，播放并等待结束 |

动态样本中 `ComeOn.dll` 基址为 `0x6E6C0000`，因此运行时地址为：

```text
dll_DirectShow_play_media = 0x6E6C3E00
sub_10003A80              = 0x6E6C3A80
```

这个阶段早于 `launcher.exe` 调用 `CreateProcessW` 创建 `ComeOn.exe`。因此跳过 Steam 2.01 开场动画不能只放在 `PlugK.dll` 内；应由已注入 `launcher.exe` 的 `PlugKLauncherHook.dll` 处理。

当前实现边界：

1. `PlugKLauncherHook.dll` 仍保留原有 `CreateProcessW` hook，用于后续创建 `ComeOn.exe` 时注入 `PlugK.dll`。
2. 当配置 `UI.SkipIntroMovie=1` 时，`PlugKLauncherHook.dll` 轮询等待 `ComeOn.dll` 加载。
3. 加载后解析 `dll_DirectShow_play_media` 中的线程入口，并 patch `ComeOn.dll` 自身 IAT 的 `CreateThread`。
4. 仅当 `lpStartAddress == sub_10003A80` 且参数包含 `"RenderFile":"` 与 `begin.dhp`、`begin-dhp` 或 `ComeOn-begin-dhp` 时，把线程入口替换为立即返回的空线程。
5. 不拦截其它线程创建，不 patch 官方文件，不绕过 `ComeOn.dll` 或 `steam_api.dll`。

2026-07-08 回归修正：Steam 2.01 不安装 `launcher.exe` 的 `LoadLibraryW` IAT Hook，保持上述后台等待 `ComeOn.dll` 的方案。1.05 为了避开 `ComeOn.dll` 加载后的短窗口期才额外 hook `LoadLibraryW`；该同步路径不应扩展到 2.01。`ComeOn.dll` 的 `CreateThread` IAT patch 必须是幂等的，避免后台轮询与同步加载路径重复安装时把原始 `CreateThread` 指针覆盖成 detour 自身。

## `ComeOn.dll` 的 Steam 线索

2.01 游戏目录包含 `ComeOn.dll`、`steam_api.dll` 与 `launcher.exe`。`ComeOn.dll` 中可见 Steam 初始化相关字符串：

```text
SteamAPI_RestartAppIfNecessary
SteamAPI_Init
steam_appid
```

这表明 2.01 仍通过官方 `ComeOn.dll` 进入 Steam 初始化路径。后续不应通过移除 `ComeOn.dll` 的方式绕过该链路。

## plugK Hook 边界

2.01 可以复用 1.05 的 launcher hook 方案：

1. 通过 `steam://rungameid/2924510` 请求 Steam 启动官方 `launcher.exe`。
2. `plugKLauncher.exe` 监控同目录 `launcher.exe` 进程，并注入 `PlugKLauncherHook.dll`。
3. `PlugKLauncherHook.dll` 只 hook 当前进程 IAT 中的 `CreateProcessW`。
4. 仅当目标路径解析为当前游戏目录下的 `ComeOn.exe` 时处理。
5. 在原始 `dwCreationFlags` 上追加 `CREATE_SUSPENDED`，原样保留其它参数。
6. 原始 `CreateProcessW` 成功后，向 `ComeOn.exe` 注入 `PlugK.dll`。
7. 如果官方调用原本没有 `CREATE_SUSPENDED`，注入成功后恢复主线程。

不应采用：

- patch 掉 `ComeOn.dll` 加载。
- 把 `PlugK.dll` 直接注入官方 `launcher.exe`。
- 依赖 `kernelbase.dll`、`kernel32.dll` 或 appcompat shim 的具体地址断点。
- 依赖 1.05 的 `EXTENDED_STARTUPINFO_PRESENT` 行为。

## 手工验证

v0.7.2 实现后，已验证 Steam 版 2.01 可以通过 `plugKLauncher.exe` 的 MOD 模式启动。启动流程复用 1.05 的 launcher hook 方案，Steam appid 为 `2924510`。
