#include "pch.h"
#include "intro_skip.h"
#include "config.h"
#include <MinHook.h>
#include <ctype.h>
#include <string.h>

#define VER_105 105
#define VER_201 201
#define STEAM105_COMEON_INTRO_NAME_RVA 0x152C4

// 1.05 非 Steam：按路径启动 DirectShow 视频播放的 thiscall 包装函数。
// 状态机播放 dhp\begin.dhp 时会进入这里，其它剧情视频也可能复用同一函数。
#define ADDR_PLAY_MOVIE_PATH_105 0x0050A8E0
// 2.01 非 Steam：同构的 DirectShow 视频播放包装函数。
#define ADDR_PLAY_MOVIE_PATH_201 0x00522730

typedef int(__fastcall *tPlayMoviePath)(void *pThis, void *_edx, const char *path, int arg);
typedef HMODULE(WINAPI *tLoadLibraryA)(LPCSTR lpLibFileName);
typedef HMODULE(WINAPI *tLoadLibraryW)(LPCWSTR lpLibFileName);
typedef HMODULE(WINAPI *tLoadLibraryExA)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef HMODULE(WINAPI *tLoadLibraryExW)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
typedef HANDLE(WINAPI *tCreateThread)(
    LPSECURITY_ATTRIBUTES lpThreadAttributes,
    SIZE_T dwStackSize,
    LPTHREAD_START_ROUTINE lpStartAddress,
    LPVOID lpParameter,
    DWORD dwCreationFlags,
    LPDWORD lpThreadId);

static tPlayMoviePath fpPlayMoviePath = NULL;
static tLoadLibraryA fpLoadLibraryA = NULL;
static tLoadLibraryW fpLoadLibraryW = NULL;
static tLoadLibraryExA fpLoadLibraryExA = NULL;
static tLoadLibraryExW fpLoadLibraryExW = NULL;
static tCreateThread fpSteam105ComeOnCreateThread = NULL;
static LPTHREAD_START_ROUTINE g_steam105_comeon_window_thread = NULL;
static LPTHREAD_START_ROUTINE g_steam105_comeon_play_thread = NULL;
static HWND *g_steam105_comeon_hwnd = NULL;
static volatile LONG *g_steam105_comeon_state = NULL;
static volatile LONG g_steam105_comeon_hook_installed = 0;

// 视频路径可能使用 / 或 \，比较时统一按 Windows 路径分隔符处理，并忽略大小写。
static int IntroPathCharEqual(char left, char right)
{
    if (left == '/')
        left = '\\';
    if (right == '/')
        right = '\\';

    return tolower((unsigned char)left) == tolower((unsigned char)right);
}

static int IntroPathEndsWith(const char *path, const char *suffix)
{
    size_t path_len;
    size_t suffix_len;
    const char *tail;
    size_t i;

    if (path == NULL || path[0] == '\0' || IsBadReadPtr(path, 1))
        return 0;

    path_len = strlen(path);
    suffix_len = strlen(suffix);
    if (path_len < suffix_len)
        return 0;

    tail = path + path_len - suffix_len;
    for (i = 0; i < suffix_len; ++i)
    {
        if (!IntroPathCharEqual(tail[i], suffix[i]))
            return 0;
    }

    return tail == path || tail[-1] == '\\' || tail[-1] == '/';
}

static int IntroPathContainsDir(const char *path, const char *dir)
{
    size_t dir_len;
    size_t i;

    if (path == NULL || dir == NULL)
        return 0;

    dir_len = strlen(dir);
    if (dir_len == 0)
        return 0;

    for (i = 0; path[i] != '\0'; ++i)
    {
        size_t j = 0;
        while (j < dir_len && path[i + j] != '\0' && IntroPathCharEqual(path[i + j], dir[j]))
            ++j;

        if (j == dir_len)
        {
            int left_ok = i == 0 || path[i - 1] == '\\' || path[i - 1] == '/';
            int right_ok = path[i + j] == '\\' || path[i + j] == '/';
            if (left_ok && right_ok)
                return 1;
        }
    }

    return 0;
}

// 只匹配开场动画 begin.dhp。非 Steam 版本使用 dhp\begin.dhp；
// Steam 1.05 的发行方 DLL 会播放 bdh\<language>\begin.dhp，部分路径仍可能回落到 dhp。
// 不按完整安装目录匹配，避免用户安装路径不同导致功能失效；
// 也不匹配 end.dhp、其它 dhp/bdh 文件或剧情视频，避免误跳过结局/过场动画。
static int HasIntroMovieSuffix(const char *path)
{
    if (path == NULL || path[0] == '\0' || IsBadReadPtr(path, 1))
        return 0;

    if (!IntroPathEndsWith(path, "begin.dhp"))
        return 0;

    return IntroPathContainsDir(path, "dhp") || IntroPathContainsDir(path, "bdh");
}

static int __fastcall Detour_PlayMoviePath(void *pThis, void *_edx, const char *path, int arg)
{
    // 返回 0 会让原状态机认为本次视频没有启动成功，随后清空待播放 CString，
    // 下一帧自然进入后续启动流程。这里仅对 begin.dhp 生效。
    if (g_pk_config.skip_intro_movie && HasIntroMovieSuffix(path))
    {
        return 0;
    }

    return fpPlayMoviePath(pThis, _edx, path, arg);
}

static int IsComeOnDllNameA(const char *path)
{
    const char *name = path;
    const char *p;

    if (path == NULL || path[0] == '\0')
        return 0;

    for (p = path; *p; ++p)
    {
        if (*p == '\\' || *p == '/')
            name = p + 1;
    }

    return _stricmp(name, "ComeOn.dll") == 0;
}

static int IsComeOnDllNameW(const WCHAR *path)
{
    const WCHAR *name = path;
    const WCHAR *p;

    if (path == NULL || path[0] == L'\0')
        return 0;

    for (p = path; *p; ++p)
    {
        if (*p == L'\\' || *p == L'/')
            name = p + 1;
    }

    return lstrcmpiW(name, L"ComeOn.dll") == 0;
}

static int PatchOneImport(HMODULE module, const char *dll_name, const char *func_name, PROC replacement, PROC *original)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_DATA_DIRECTORY import_dir;
    IMAGE_IMPORT_DESCRIPTOR *desc;

    if (module == NULL)
        return 0;

    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_dir.VirtualAddress == 0 || import_dir.Size == 0)
        return 0;

    desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + import_dir.VirtualAddress);
    for (; desc->Name; ++desc)
    {
        const char *name = (const char *)(base + desc->Name);
        IMAGE_THUNK_DATA *orig_thunk;
        IMAGE_THUNK_DATA *first_thunk;

        if (_stricmp(name, dll_name) != 0)
            continue;

        orig_thunk = (IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
        first_thunk = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
        if (desc->OriginalFirstThunk == 0)
            orig_thunk = first_thunk;

        for (; orig_thunk->u1.AddressOfData; ++orig_thunk, ++first_thunk)
        {
            IMAGE_IMPORT_BY_NAME *import_name;
            DWORD old_protect = 0;
            PROC old_function;

            if (orig_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)
                continue;

            import_name = (IMAGE_IMPORT_BY_NAME *)(base + orig_thunk->u1.AddressOfData);
            if (lstrcmpA((LPCSTR)import_name->Name, func_name) != 0)
                continue;

            if ((PROC)first_thunk->u1.Function == replacement)
                return 1;

            if (!VirtualProtect(&first_thunk->u1.Function, sizeof(PROC), PAGE_EXECUTE_READWRITE, &old_protect))
                return 0;

            old_function = (PROC)first_thunk->u1.Function;
            if (original != NULL && *original == NULL)
                *original = old_function;
            first_thunk->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&first_thunk->u1.Function, sizeof(PROC), old_protect, &old_protect);
            FlushInstructionCache(GetCurrentProcess(), &first_thunk->u1.Function, sizeof(PROC));
            return 1;
        }
    }

    return 0;
}

static BYTE *FindWideStringInModule(HMODULE module, const WCHAR *needle)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *section;
    DWORD needle_bytes;
    WORD i;

    if (module == NULL || needle == NULL)
        return NULL;

    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;

    needle_bytes = (DWORD)((lstrlenW(needle) + 1) * sizeof(WCHAR));
    section = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        BYTE *start = base + section->VirtualAddress;
        DWORD size = section->Misc.VirtualSize;
        DWORD offset;

        if (size < needle_bytes)
            continue;

        for (offset = 0; offset + needle_bytes <= size; ++offset)
        {
            if (memcmp(start + offset, needle, needle_bytes) == 0)
                return start + offset;
        }
    }

    return NULL;
}

static int RangeContainsDword(BYTE *start, size_t size, DWORD value)
{
    size_t i;

    if (start == NULL)
        return 0;

    for (i = 0; i + sizeof(DWORD) <= size; ++i)
    {
        if (*(DWORD *)(start + i) == value)
            return 1;
    }

    return 0;
}

static int PatchWideText(BYTE *text, const WCHAR *from, const WCHAR *to)
{
    DWORD old_protect = 0;
    DWORD bytes;

    if (text == NULL || from == NULL || to == NULL || lstrlenW(from) != lstrlenW(to))
        return 0;

    bytes = (DWORD)((lstrlenW(from) + 1) * sizeof(WCHAR));
    if (memcmp(text, from, bytes) != 0)
        return 0;

    if (!VirtualProtect(text, bytes, PAGE_EXECUTE_READWRITE, &old_protect))
        return 0;

    memcpy(text, to, bytes);
    VirtualProtect(text, bytes, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), text, bytes);
    return 1;
}

static int PatchSteam105ComeOnIntroMovieName(HMODULE comeon)
{
    BYTE *format;
    const WCHAR *from = L"begin.dhp";
    const WCHAR *to = L"degin.dhp";
    BYTE *fixed_name;
    int format_len;
    int from_len;
    int i;

    // Steam 1.05 ComeOn.dll: aSBdhSBeginDhp + 0x14, RVA 0x152C4.
    // 优先固定 RVA；写入前校验原始 UTF-16LE 文本，失败再扫描格式串兜底。
    fixed_name = (BYTE *)comeon + STEAM105_COMEON_INTRO_NAME_RVA;
    if (PatchWideText(fixed_name, from, to))
        return 1;

    format = FindWideStringInModule(comeon, L"%s\\bdh\\%s\\begin.dhp");
    if (format == NULL)
        return 0;

    format_len = lstrlenW((const WCHAR *)format);
    from_len = lstrlenW(from);
    for (i = 0; i + from_len <= format_len; ++i)
    {
        WCHAR *cursor = (WCHAR *)format + i;
        if (memcmp(cursor, from, (from_len + 1) * sizeof(WCHAR)) == 0)
            return PatchWideText((BYTE *)cursor, from, to);
    }

    return 0;
}

static volatile LONG *FindSteam105ComeOnIntroState(BYTE *thread_proc)
{
    size_t i;

    if (thread_proc == NULL)
        return NULL;

    // Steam 1.05 ComeOn.dll 视频窗口线程结束前：
    //   C7 05 xx xx xx xx 03 00 00 00    mov dword_1001EE68, 3
    for (i = 0; i + 10 <= 0x400; ++i)
    {
        if (thread_proc[i] == 0xC7 && thread_proc[i + 1] == 0x05 &&
            *(DWORD *)(thread_proc + i + 6) == 3)
        {
            return (volatile LONG *)(ULONG_PTR)(*(DWORD *)(thread_proc + i + 2));
        }
    }

    return NULL;
}

static HWND *FindSteam105ComeOnVideoWindowHandle(BYTE *window_thread)
{
    size_t i;

    if (window_thread == NULL)
        return NULL;

    // sub_100041F0 创建窗口后保存句柄：
    //   A3 xx xx xx xx    mov hWnd, eax
    for (i = 0; i + 5 <= 0x120; ++i)
    {
        if (window_thread[i] == 0xA3)
            return (HWND *)(ULONG_PTR)(*(DWORD *)(window_thread + i + 1));
    }

    return NULL;
}

static LPTHREAD_START_ROUTINE FindSteam105ComeOnPlayThread(HMODULE comeon, BYTE *window_thread)
{
    BYTE *format;
    size_t i;

    if (comeon == NULL || window_thread == NULL)
        return NULL;

    format = FindWideStringInModule(comeon, L"%s\\bdh\\%s\\begin.dhp");
    if (format == NULL)
        return NULL;

    // sub_100041F0 内部会 CreateThread(StartAddress, 0)。StartAddress 引用
    // "%s\\bdh\\%s\\begin.dhp"，只跳过这一层 DirectShow 播放线程。
    for (i = 0; i + 5 <= 0x200; ++i)
    {
        BYTE *candidate;
        MEMORY_BASIC_INFORMATION mbi;

        if (window_thread[i] != 0x68)
            continue;

        candidate = (BYTE *)(ULONG_PTR)(*(DWORD *)(window_thread + i + 1));
        if (candidate == window_thread)
            continue;

        memset(&mbi, 0, sizeof(mbi));
        if (VirtualQuery(candidate, &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.AllocationBase != comeon)
            continue;

        if (RangeContainsDword(candidate, 0x400, (DWORD)(ULONG_PTR)format))
            return (LPTHREAD_START_ROUTINE)candidate;
    }

    return NULL;
}

static int ResolveSteam105ComeOnIntroThread(HMODULE comeon,
                                            LPTHREAD_START_ROUTINE *out_window_proc,
                                            LPTHREAD_START_ROUTINE *out_play_proc,
                                            HWND **out_hwnd,
                                            volatile LONG **out_state)
{
    BYTE *video_window;
    BYTE *base = (BYTE *)comeon;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    IMAGE_SECTION_HEADER *section;
    WORD i;

    if (comeon == NULL || out_window_proc == NULL || out_play_proc == NULL ||
        out_hwnd == NULL || out_state == NULL)
        return 0;

    video_window = FindWideStringInModule(comeon, L"VideoWindow");
    if (video_window == NULL)
        return 0;

    dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return 0;

    nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return 0;

    section = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section)
    {
        BYTE *start;
        DWORD size;
        DWORD offset;

        if ((section->Characteristics & IMAGE_SCN_CNT_CODE) == 0)
            continue;

        start = base + section->VirtualAddress;
        size = section->Misc.VirtualSize;
        for (offset = 0; offset + 5 <= size; ++offset)
        {
            BYTE *candidate;
            MEMORY_BASIC_INFORMATION mbi;
            volatile LONG *state;
            LPTHREAD_START_ROUTINE play_thread;
            HWND *hwnd;

            // 查找 push imm32；两个 CreateThread 候选里，只有 sub_100041F0
            // 会引用 "VideoWindow" 并写 dword_1001EE68 = 3。
            if (start[offset] != 0x68)
                continue;

            candidate = (BYTE *)(ULONG_PTR)(*(DWORD *)(start + offset + 1));
            memset(&mbi, 0, sizeof(mbi));
            if (VirtualQuery(candidate, &mbi, sizeof(mbi)) != sizeof(mbi) ||
                mbi.AllocationBase != comeon)
                continue;

            if (!RangeContainsDword(candidate, 0x200, (DWORD)(ULONG_PTR)video_window))
                continue;

            state = FindSteam105ComeOnIntroState(candidate);
            if (state == NULL)
                continue;

            play_thread = FindSteam105ComeOnPlayThread(comeon, candidate);
            hwnd = FindSteam105ComeOnVideoWindowHandle(candidate);
            if (play_thread == NULL || hwnd == NULL)
                continue;

            *out_window_proc = (LPTHREAD_START_ROUTINE)candidate;
            *out_play_proc = play_thread;
            *out_hwnd = hwnd;
            *out_state = state;
            return 1;
        }
    }

    return 0;
}

static DWORD WINAPI SkippedSteam105ComeOnPlayThread(LPVOID parameter)
{
    HWND hwnd = NULL;

    (void)parameter;

    // 保留 sub_100041F0 视频窗口线程和结尾清理，只跳过内部 DirectShow 播放线程。
    // 原 StartAddress 播放完成后也是向 VideoWindow 投递 WM_CLOSE。
    if (g_steam105_comeon_hwnd != NULL)
        hwnd = *g_steam105_comeon_hwnd;

    if (hwnd != NULL)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    else if (g_steam105_comeon_state != NULL)
        InterlockedExchange(g_steam105_comeon_state, 3);

    return 0;
}

static HANDLE WINAPI Detour_Steam105ComeOnCreateThread(
    LPSECURITY_ATTRIBUTES thread_attributes,
    SIZE_T stack_size,
    LPTHREAD_START_ROUTINE start_address,
    LPVOID parameter,
    DWORD creation_flags,
    LPDWORD thread_id)
{
    if (start_address == g_steam105_comeon_play_thread)
    {
        start_address = SkippedSteam105ComeOnPlayThread;
        parameter = NULL;
    }

    return fpSteam105ComeOnCreateThread(thread_attributes, stack_size, start_address, parameter,
                                       creation_flags, thread_id);
}

static int InstallSteam105ComeOnDllIntroSkip(void)
{
    HMODULE comeon;

    if (InterlockedCompareExchange(&g_steam105_comeon_hook_installed, 0, 0) != 0)
        return 1;

    comeon = GetModuleHandleW(L"ComeOn.dll");
    if (comeon == NULL)
        return 0;

    // 试验方案：Steam 1.05 不替换 ComeOn.dll 的播放线程入口，只把内部格式字符串
    // "%s\\bdh\\%s\\begin.dhp" 改成同长度的 degin.dhp，观察原失败路径是否能自然跳过。
    if (PatchSteam105ComeOnIntroMovieName(comeon))
    {
        InterlockedExchange(&g_steam105_comeon_hook_installed, 1);
        return 1;
    }

    return 0;
}

static HMODULE WINAPI Detour_LoadLibraryA(LPCSTR lpLibFileName)
{
    HMODULE module = fpLoadLibraryA(lpLibFileName);
    if (module != NULL && IsComeOnDllNameA(lpLibFileName))
        InstallSteam105ComeOnDllIntroSkip();
    return module;
}

static HMODULE WINAPI Detour_LoadLibraryW(LPCWSTR lpLibFileName)
{
    HMODULE module = fpLoadLibraryW(lpLibFileName);
    if (module != NULL && IsComeOnDllNameW(lpLibFileName))
        InstallSteam105ComeOnDllIntroSkip();
    return module;
}

static HMODULE WINAPI Detour_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE module = fpLoadLibraryExA(lpLibFileName, hFile, dwFlags);
    if (module != NULL && IsComeOnDllNameA(lpLibFileName))
        InstallSteam105ComeOnDllIntroSkip();
    return module;
}

static HMODULE WINAPI Detour_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
    HMODULE module = fpLoadLibraryExW(lpLibFileName, hFile, dwFlags);
    if (module != NULL && IsComeOnDllNameW(lpLibFileName))
        InstallSteam105ComeOnDllIntroSkip();
    return module;
}

static void InstallSteam105ComeOnLoadLibraryHooks(void)
{
    HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");

    if (kernel32 == NULL)
        return;

    if (fpLoadLibraryA == NULL)
    {
        LPVOID target = (LPVOID)GetProcAddress(kernel32, "LoadLibraryA");
        if (target != NULL &&
            MH_CreateHook(target, &Detour_LoadLibraryA, (LPVOID *)&fpLoadLibraryA) == MH_OK)
            MH_EnableHook(target);
    }

    if (fpLoadLibraryW == NULL)
    {
        LPVOID target = (LPVOID)GetProcAddress(kernel32, "LoadLibraryW");
        if (target != NULL &&
            MH_CreateHook(target, &Detour_LoadLibraryW, (LPVOID *)&fpLoadLibraryW) == MH_OK)
            MH_EnableHook(target);
    }

    if (fpLoadLibraryExA == NULL)
    {
        LPVOID target = (LPVOID)GetProcAddress(kernel32, "LoadLibraryExA");
        if (target != NULL &&
            MH_CreateHook(target, &Detour_LoadLibraryExA, (LPVOID *)&fpLoadLibraryExA) == MH_OK)
            MH_EnableHook(target);
    }

    if (fpLoadLibraryExW == NULL)
    {
        LPVOID target = (LPVOID)GetProcAddress(kernel32, "LoadLibraryExW");
        if (target != NULL &&
            MH_CreateHook(target, &Detour_LoadLibraryExW, (LPVOID *)&fpLoadLibraryExW) == MH_OK)
            MH_EnableHook(target);
    }
}

static DWORD WINAPI Steam105ComeOnInstallThread(LPVOID parameter)
{
    DWORD start_tick;

    (void)parameter;

    start_tick = GetTickCount();
    while (InterlockedCompareExchange(&g_steam105_comeon_hook_installed, 0, 0) == 0 &&
           GetTickCount() - start_tick < 120000)
    {
        if (InstallSteam105ComeOnDllIntroSkip())
            break;
        Sleep(20);
    }

    return 0;
}

void Mod_Intro_Skip_Init(int game_version)
{
    LPVOID target;

    // 这里处理注入到 ComeOn.exe 之后的路径播放包装；Steam launcher 阶段的动画
    // 由 PlugKLauncherHook.dll 在 launcher.exe 内处理。
    if (!g_pk_config.skip_intro_movie)
        return;

    if (game_version == VER_105)
        target = (LPVOID)ADDR_PLAY_MOVIE_PATH_105;
    else if (game_version == VER_201)
        target = (LPVOID)ADDR_PLAY_MOVIE_PATH_201;
    else
        return;

    if (MH_CreateHook(target,
                      &Detour_PlayMoviePath,
                      (LPVOID *)&fpPlayMoviePath) != MH_OK)
        return;

    MH_EnableHook(target);

    if (game_version == VER_105)
    {
        // Steam 1.05 的发行方 ComeOn.dll 会在本进程内通过 CreateFileW 包装触发
        // Steam 初始化和 bdh\<language>\begin.dhp 播放。它可能已提前加载，也可能
        // 运行时动态加载；这里两条路径都覆盖。
        InstallSteam105ComeOnDllIntroSkip();
        InstallSteam105ComeOnLoadLibraryHooks();
        if (InterlockedCompareExchange(&g_steam105_comeon_hook_installed, 0, 0) == 0)
        {
            HANDLE thread = CreateThread(NULL, 0, Steam105ComeOnInstallThread, NULL, 0, NULL);
            if (thread != NULL)
                CloseHandle(thread);
        }
    }
}
