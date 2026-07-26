#include <windows.h>
#include <shlwapi.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "shlwapi.lib")

typedef BOOL(WINAPI *CreateProcessWProc)(
    LPCWSTR,
    LPWSTR,
    LPSECURITY_ATTRIBUTES,
    LPSECURITY_ATTRIBUTES,
    BOOL,
    DWORD,
    LPVOID,
    LPCWSTR,
    LPSTARTUPINFOW,
    LPPROCESS_INFORMATION);

typedef HMODULE(WINAPI *LoadLibraryWProc)(LPCWSTR);

static HMODULE g_module = NULL;
static WCHAR g_game_dir[MAX_PATH] = {0};
static WCHAR g_plugk_path[MAX_PATH] = {0};
static CreateProcessWProc g_real_create_process_w = NULL;
static LoadLibraryWProc g_real_load_library_w = NULL;

typedef HANDLE(WINAPI *CreateThreadProc)(
    LPSECURITY_ATTRIBUTES,
    SIZE_T,
    LPTHREAD_START_ROUTINE,
    LPVOID,
    DWORD,
    LPDWORD);

static CreateThreadProc g_real_create_thread = NULL;
static LPTHREAD_START_ROUTINE g_directshow_thread_proc = NULL;
static LPTHREAD_START_ROUTINE g_steam105_window_thread_proc = NULL;
static LPTHREAD_START_ROUTINE g_steam105_play_thread_proc = NULL;
static HWND *g_steam105_video_window_hwnd = NULL;
static volatile LONG *g_steam105_intro_state = NULL;
static volatile LONG g_directshow_hook_state = 0;

#define COMEON_HOOK_NONE 0
#define COMEON_HOOK_INSTALLING 1
#define COMEON_HOOK_INSTALLED 2
#define STEAM105_COMEON_INTRO_NAME_RVA 0x152C4

static void BuildPath(WCHAR *out, size_t out_count, const WCHAR *dir, const WCHAR *file);

static void CopyString(WCHAR *dst, size_t dst_count, const WCHAR *src)
{
    if (dst_count == 0)
        return;
    dst[0] = 0;
    if (src)
        lstrcpynW(dst, src, (int)dst_count);
}

static bool PathEquals(const WCHAR *a, const WCHAR *b)
{
    return a && b && lstrcmpiW(a, b) == 0;
}

static bool FileExistsW(const WCHAR *path)
{
    return path && PathFileExistsW(path) == TRUE;
}

static int DetectGameVersion()
{
    WCHAR exe_path[MAX_PATH] = {0};
    BuildPath(exe_path, MAX_PATH, g_game_dir, L"ComeOn.exe");

    HANDLE file = CreateFileW(exe_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 0;

    int version = 0;
    DWORD value = 0;
    DWORD read = 0;

    if (SetFilePointer(file, 0x6ED18, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(file, &value, sizeof(value), &read, NULL) && read == sizeof(value) &&
        value == 0x3F866666) {
        version = 105;
    }

    value = 0;
    read = 0;
    if (version == 0 &&
        SetFilePointer(file, 0x7CF28, NULL, FILE_BEGIN) != INVALID_SET_FILE_POINTER &&
        ReadFile(file, &value, sizeof(value), &read, NULL) && read == sizeof(value) &&
        value == 0x4000A3D7) {
        version = 201;
    }

    CloseHandle(file);
    return version;
}

static bool IsSkipIntroMovieEnabled()
{
    // launcher hook DLL 注入到官方 launcher.exe 中，不能依赖 PlugK.dll 的配置全局变量。
    // 这里直接读取游戏目录下的 PlugK.ini，与 ComeOn.exe 内的跳过动画功能共用同一配置项。
    WCHAR ini_path[MAX_PATH] = {0};
    BuildPath(ini_path, MAX_PATH, g_game_dir, L"PlugK.ini");
    return GetPrivateProfileIntW(L"UI", L"SkipIntroMovie", 1, ini_path) != 0;
}

static void BuildPath(WCHAR *out, size_t out_count, const WCHAR *dir, const WCHAR *file)
{
    CopyString(out, out_count, dir);
    PathAppendW(out, file);
}

static bool GetFirstCommandLineArg(LPCWSTR cmd, WCHAR *out, size_t out_count)
{
    CopyString(out, out_count, L"");
    if (!cmd)
        return false;

    while (*cmd == L' ' || *cmd == L'\t')
        ++cmd;
    if (*cmd == 0)
        return false;

    WCHAR quote = 0;
    if (*cmd == L'"') {
        quote = L'"';
        ++cmd;
    }

    size_t len = 0;
    while (*cmd && len + 1 < out_count) {
        if (quote) {
            if (*cmd == quote)
                break;
        } else if (*cmd == L' ' || *cmd == L'\t') {
            break;
        }
        out[len++] = *cmd++;
    }
    out[len] = 0;
    return len > 0;
}

static bool ResolveCandidatePath(LPCWSTR app, LPWSTR cmd, LPCWSTR cwd, WCHAR *out, size_t out_count)
{
    WCHAR candidate[MAX_PATH] = {0};
    if (app && app[0]) {
        CopyString(candidate, MAX_PATH, app);
    } else if (!GetFirstCommandLineArg(cmd, candidate, MAX_PATH)) {
        return false;
    }

    WCHAR base_dir[MAX_PATH] = {0};
    CopyString(base_dir, MAX_PATH, cwd && cwd[0] ? cwd : g_game_dir);

    if (PathIsRelativeW(candidate)) {
        WCHAR combined[MAX_PATH] = {0};
        if (!PathCombineW(combined, base_dir, candidate))
            return false;
        CopyString(candidate, MAX_PATH, combined);
    }

    DWORD copied = GetFullPathNameW(candidate, (DWORD)out_count, out, NULL);
    return copied > 0 && copied < out_count;
}

static bool IsTargetComeOn(LPCWSTR app, LPWSTR cmd, LPCWSTR cwd)
{
    WCHAR resolved[MAX_PATH] = {0};
    WCHAR expected[MAX_PATH] = {0};

    if (!ResolveCandidatePath(app, cmd, cwd, resolved, MAX_PATH))
        return false;

    BuildPath(expected, MAX_PATH, g_game_dir, L"ComeOn.exe");
    return PathEquals(resolved, expected);
}

static bool IsComeOnDllName(LPCWSTR path)
{
    if (!path || !path[0])
        return false;

    const WCHAR *name = PathFindFileNameW(path);
    return name && lstrcmpiW(name, L"ComeOn.dll") == 0;
}

static WCHAR LowerAsciiWide(WCHAR ch)
{
    if (ch >= L'A' && ch <= L'Z')
        return (WCHAR)(ch - L'A' + L'a');
    return ch;
}

static bool ContainsWideAsciiInsensitive(const WCHAR *text, const WCHAR *needle)
{
    if (!text || !needle || !needle[0])
        return false;

    __try {
        for (size_t i = 0; text[i] && i < 4096; ++i) {
            size_t j = 0;
            while (needle[j]) {
                WCHAR ch = text[i + j];
                if (!ch || i + j >= 4096)
                    return false;
                if (LowerAsciiWide(ch) != LowerAsciiWide(needle[j]))
                    break;
                ++j;
            }
            if (!needle[j])
                return true;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return false;
}

static bool IsSteamIntroMovieParam(LPVOID param)
{
    const WCHAR *text = (const WCHAR *)param;

    // Steam 2.01 的开场动画参数是宽字符串 JSON 片段，包含 RenderFile 与窗口标题。
    // 仅匹配 begin 开场动画，避免误跳过后续剧情视频或其它 DirectShow 播放。
    if (!ContainsWideAsciiInsensitive(text, L"\"RenderFile\":\""))
        return false;

    return ContainsWideAsciiInsensitive(text, L"begin.dhp") ||
           ContainsWideAsciiInsensitive(text, L"begin-dhp") ||
           ContainsWideAsciiInsensitive(text, L"ComeOn-begin-dhp");
}

static DWORD WINAPI SkippedDirectShowThread(LPVOID)
{
    // 保留 CreateThread/CloseHandle 的调用语义，但线程入口立即返回，从而跳过等待视频播放完成。
    return 0;
}

static DWORD WINAPI SkippedSteam105IntroThread(LPVOID)
{
    HWND hwnd = NULL;

    // 保留 Steam 1.05 的视频窗口线程和结尾清理，只跳过内部 DirectShow 播放线程。
    // 原 StartAddress 播放完成后也是向 VideoWindow 投递 WM_CLOSE。
    if (g_steam105_video_window_hwnd)
        hwnd = *g_steam105_video_window_hwnd;

    if (hwnd)
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    else if (g_steam105_intro_state)
        InterlockedExchange(g_steam105_intro_state, 3);

    return 0;
}

static HANDLE WINAPI HookCreateThread(
    LPSECURITY_ATTRIBUTES thread_attributes,
    SIZE_T stack_size,
    LPTHREAD_START_ROUTINE start_address,
    LPVOID parameter,
    DWORD creation_flags,
    LPDWORD thread_id)
{
    if (!g_real_create_thread) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }

    // 只拦截 ComeOn.dll 导出的 DirectShow 播放包装创建的线程。
    // 其它 launcher 线程、WKE 线程和后续游戏启动流程必须原样透传。
    if (start_address == g_directshow_thread_proc && IsSteamIntroMovieParam(parameter)) {
        start_address = SkippedDirectShowThread;
        parameter = NULL;
    } else if (start_address == g_steam105_play_thread_proc) {
        start_address = SkippedSteam105IntroThread;
        parameter = NULL;
    }

    return g_real_create_thread(thread_attributes, stack_size, start_address, parameter,
                                creation_flags, thread_id);
}

static bool InjectPlugK(HANDLE process)
{
    if (!FileExistsW(g_plugk_path))
        return false;

    SIZE_T bytes = (lstrlenW(g_plugk_path) + 1) * sizeof(WCHAR);
    LPVOID remote = VirtualAllocEx(process, NULL, bytes, MEM_COMMIT, PAGE_READWRITE);
    if (!remote)
        return false;

    BOOL wrote = WriteProcessMemory(process, remote, g_plugk_path, bytes, NULL);
    if (!wrote) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    FARPROC load_library = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    if (!load_library) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    HANDLE thread = CreateRemoteThread(process, NULL, 0, (LPTHREAD_START_ROUTINE)load_library, remote, 0, NULL);
    if (!thread) {
        VirtualFreeEx(process, remote, 0, MEM_RELEASE);
        return false;
    }

    DWORD exit_code = 0;
    WaitForSingleObject(thread, INFINITE);
    GetExitCodeThread(thread, &exit_code);
    CloseHandle(thread);
    VirtualFreeEx(process, remote, 0, MEM_RELEASE);
    return exit_code != 0;
}

static BOOL WINAPI HookCreateProcessW(
    LPCWSTR app,
    LPWSTR cmd,
    LPSECURITY_ATTRIBUTES process_attrs,
    LPSECURITY_ATTRIBUTES thread_attrs,
    BOOL inherit_handles,
    DWORD flags,
    LPVOID env,
    LPCWSTR cwd,
    LPSTARTUPINFOW startup_info,
    LPPROCESS_INFORMATION process_info)
{
    if (!g_real_create_process_w) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return FALSE;
    }

    if (!IsTargetComeOn(app, cmd, cwd)) {
        return g_real_create_process_w(app, cmd, process_attrs, thread_attrs, inherit_handles,
                                       flags, env, cwd, startup_info, process_info);
    }

    // 官方 launcher 启动 ComeOn.exe 时追加 CREATE_SUSPENDED，注入 PlugK.dll 后再恢复。
    // 原始 flags、STARTUPINFO、环境和当前目录都保留，避免改变 Steam 官方启动链。
    DWORD original_flags = flags;
    BOOL ok = g_real_create_process_w(app, cmd, process_attrs, thread_attrs, inherit_handles,
                                      flags | CREATE_SUSPENDED, env, cwd, startup_info, process_info);
    if (!ok)
        return FALSE;

    if (!InjectPlugK(process_info->hProcess)) {
        TerminateProcess(process_info->hProcess, 0);
        CloseHandle(process_info->hThread);
        CloseHandle(process_info->hProcess);
        ZeroMemory(process_info, sizeof(*process_info));
        SetLastError(ERROR_DLL_INIT_FAILED);
        return FALSE;
    }

    if ((original_flags & CREATE_SUSPENDED) == 0)
        ResumeThread(process_info->hThread);

    return TRUE;
}

static bool InstallComeOnDirectShowHook();

static HMODULE WINAPI HookLoadLibraryW(LPCWSTR file_name)
{
    if (!g_real_load_library_w) {
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }

    HMODULE module = g_real_load_library_w(file_name);

    // Steam 1.05 的 JS 会通过 launcher 暴露的 LoadLibrary 加载 ComeOn.dll，
    // 随后很快触发 DLL 内部 .ini/Steam 初始化路径。同步安装可以避开轮询窗口。
    if (module && IsSkipIntroMovieEnabled() && IsComeOnDllName(file_name))
        InstallComeOnDirectShowHook();

    return module;
}

static bool PatchOneImport(HMODULE module, const char *dll_name, const char *func_name, PROC replacement, PROC *original)
{
    // 只 patch 指定模块自己的 IAT，不做全局 inline hook。
    // 这样可以把 launcher.exe 的 CreateProcessW 和 ComeOn.dll 的 CreateThread 分别限制在各自模块内。
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    IMAGE_DATA_DIRECTORY import_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!import_dir.VirtualAddress || !import_dir.Size)
        return false;

    IMAGE_IMPORT_DESCRIPTOR *desc = (IMAGE_IMPORT_DESCRIPTOR *)(base + import_dir.VirtualAddress);
    for (; desc->Name; ++desc) {
        const char *name = (const char *)(base + desc->Name);
        if (_stricmp(name, dll_name) != 0)
            continue;

        IMAGE_THUNK_DATA *orig_thunk = (IMAGE_THUNK_DATA *)(base + desc->OriginalFirstThunk);
        IMAGE_THUNK_DATA *first_thunk = (IMAGE_THUNK_DATA *)(base + desc->FirstThunk);
        if (!desc->OriginalFirstThunk)
            orig_thunk = first_thunk;

        for (; orig_thunk->u1.AddressOfData; ++orig_thunk, ++first_thunk) {
            if (orig_thunk->u1.Ordinal & IMAGE_ORDINAL_FLAG)
                continue;

            IMAGE_IMPORT_BY_NAME *import_name = (IMAGE_IMPORT_BY_NAME *)(base + orig_thunk->u1.AddressOfData);
            if (lstrcmpA((LPCSTR)import_name->Name, func_name) != 0)
                continue;

            DWORD old_protect = 0;
            if (!VirtualProtect(&first_thunk->u1.Function, sizeof(PROC), PAGE_EXECUTE_READWRITE, &old_protect))
                return false;

            PROC old_function = (PROC)first_thunk->u1.Function;
            if (old_function == replacement)
                return true;

            if (original && !*original)
                *original = old_function;
            first_thunk->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&first_thunk->u1.Function, sizeof(PROC), old_protect, &old_protect);
            FlushInstructionCache(GetCurrentProcess(), &first_thunk->u1.Function, sizeof(PROC));
            return true;
        }
    }
    return false;
}

static bool ResolveDirectShowThreadProc(HMODULE comeon, LPTHREAD_START_ROUTINE *out_proc)
{
    // ComeOn.dll!dll_DirectShow_play_media 中固定形态为：
    //   push offset sub_10003A80
    //   call ds:CreateThread
    // 运行时解析 push 立即数，避免依赖 ComeOn.dll 的固定加载基址。
    BYTE *play_media = (BYTE *)GetProcAddress(comeon, "dll_DirectShow_play_media");
    if (!play_media)
        return false;

    __try {
        for (size_t i = 0; i + 5 < 32; ++i) {
            if (play_media[i] != 0x68)
                continue;

            LPTHREAD_START_ROUTINE proc = *(LPTHREAD_START_ROUTINE *)(play_media + i + 1);
            MEMORY_BASIC_INFORMATION mbi = {0};
            if (VirtualQuery((LPCVOID)proc, &mbi, sizeof(mbi)) == sizeof(mbi) &&
                mbi.AllocationBase == comeon) {
                *out_proc = proc;
                return true;
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }

    return false;
}

static BYTE *FindWideStringInModule(HMODULE module, const WCHAR *needle)
{
    BYTE *base = (BYTE *)module;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return NULL;

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return NULL;

    size_t needle_bytes = (lstrlenW(needle) + 1) * sizeof(WCHAR);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        BYTE *start = base + section->VirtualAddress;
        DWORD size = section->Misc.VirtualSize;
        if (size < needle_bytes)
            continue;

        for (DWORD offset = 0; offset + needle_bytes <= size; ++offset) {
            if (memcmp(start + offset, needle, needle_bytes) == 0)
                return start + offset;
        }
    }

    return NULL;
}

static bool RangeContainsDword(BYTE *start, size_t size, DWORD value)
{
    for (size_t i = 0; i + sizeof(DWORD) <= size; ++i) {
        if (*(DWORD *)(start + i) == value)
            return true;
    }
    return false;
}

static bool PatchWideText(BYTE *text, const WCHAR *from, const WCHAR *to)
{
    size_t bytes = (lstrlenW(from) + 1) * sizeof(WCHAR);
    DWORD old_protect = 0;

    if (!text || !from || !to || lstrlenW(from) != lstrlenW(to))
        return false;

    if (memcmp(text, from, bytes) != 0)
        return false;

    if (!VirtualProtect(text, bytes, PAGE_EXECUTE_READWRITE, &old_protect))
        return false;

    memcpy(text, to, bytes);
    VirtualProtect(text, bytes, old_protect, &old_protect);
    FlushInstructionCache(GetCurrentProcess(), text, bytes);
    return true;
}

static bool PatchSteam105IntroMovieName(HMODULE comeon)
{
    BYTE *format = FindWideStringInModule(comeon, L"%s\\bdh\\%s\\begin.dhp");
    const WCHAR *from = L"begin.dhp";
    const WCHAR *to = L"degin.dhp";
    BYTE *fixed_name = (BYTE *)comeon + STEAM105_COMEON_INTRO_NAME_RVA;
    size_t format_len = 0;
    size_t from_len = 0;

    // Steam 1.05 ComeOn.dll: aSBdhSBeginDhp + 0x14, RVA 0x152C4.
    // Prefer the direct RVA and validate the original UTF-16LE text before writing.
    if (PatchWideText(fixed_name, from, to))
        return true;

    if (!format)
        return false;

    format_len = (size_t)lstrlenW((const WCHAR *)format);
    from_len = (size_t)lstrlenW(from);
    for (size_t i = 0; i + from_len <= format_len; ++i) {
        WCHAR *cursor = (WCHAR *)format + i;
        if (memcmp(cursor, from, (from_len + 1) * sizeof(WCHAR)) == 0)
            return PatchWideText((BYTE *)cursor, from, to);
    }

    return false;
}

static volatile LONG *FindSteam105IntroState(BYTE *thread_proc)
{
    // Steam 1.05 的视频窗口线程结束前会执行：
    //   mov dword_1001EE68, 3
    // 这里从线程函数中解析状态变量地址，避免依赖固定 DLL 基址。
    for (size_t i = 0; i + 10 <= 0x400; ++i) {
        if (thread_proc[i] == 0xC7 && thread_proc[i + 1] == 0x05 &&
            *(DWORD *)(thread_proc + i + 6) == 3) {
            return (volatile LONG *)(*(DWORD *)(thread_proc + i + 2));
        }
    }

    return NULL;
}

static HWND *FindSteam105VideoWindowHandle(BYTE *window_thread)
{
    if (!window_thread)
        return NULL;

    // sub_100041F0 创建窗口后保存句柄：
    //   A3 xx xx xx xx    mov hWnd, eax
    for (size_t i = 0; i + 5 <= 0x120; ++i) {
        if (window_thread[i] == 0xA3)
            return (HWND *)(*(DWORD *)(window_thread + i + 1));
    }

    return NULL;
}

static LPTHREAD_START_ROUTINE FindSteam105PlayThreadProc(HMODULE comeon, BYTE *window_thread)
{
    if (!comeon || !window_thread)
        return NULL;

    BYTE *format = FindWideStringInModule(comeon, L"%s\\bdh\\%s\\begin.dhp");
    if (!format)
        return NULL;

    // sub_100041F0 内部会 CreateThread(StartAddress, 0)。StartAddress 引用
    // "%s\\bdh\\%s\\begin.dhp"，只跳过这一层 DirectShow 播放线程。
    for (size_t i = 0; i + 5 <= 0x200; ++i) {
        if (window_thread[i] != 0x68)
            continue;

        BYTE *candidate = (BYTE *)(*(DWORD *)(window_thread + i + 1));
        if (candidate == window_thread)
            continue;

        MEMORY_BASIC_INFORMATION mbi = {0};
        if (VirtualQuery(candidate, &mbi, sizeof(mbi)) != sizeof(mbi) ||
            mbi.AllocationBase != comeon)
            continue;

        if (RangeContainsDword(candidate, 0x400, (DWORD)format))
            return (LPTHREAD_START_ROUTINE)candidate;
    }

    return NULL;
}

static bool ResolveSteam105IntroThreadProc(HMODULE comeon,
                                           LPTHREAD_START_ROUTINE *out_window_proc,
                                           LPTHREAD_START_ROUTINE *out_play_proc,
                                           HWND **out_hwnd,
                                           volatile LONG **out_state)
{
    BYTE *video_window = FindWideStringInModule(comeon, L"VideoWindow");
    if (!video_window)
        return false;

    BYTE *base = (BYTE *)comeon;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)base;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;

    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;

    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if ((section->Characteristics & IMAGE_SCN_CNT_CODE) == 0)
            continue;

        BYTE *start = base + section->VirtualAddress;
        DWORD size = section->Misc.VirtualSize;
        for (DWORD offset = 0; offset + 5 <= size; ++offset) {
            // 查找 CreateThread 调用前的 push imm32。Steam 1.05 有两个候选：
            // 一个是视频窗口线程，一个是窗口线程内部的实际播放线程。
            if (start[offset] != 0x68)
                continue;

            BYTE *candidate = (BYTE *)(*(DWORD *)(start + offset + 1));
            MEMORY_BASIC_INFORMATION mbi = {0};
            if (VirtualQuery(candidate, &mbi, sizeof(mbi)) != sizeof(mbi) ||
                mbi.AllocationBase != comeon)
                continue;

            if (!RangeContainsDword(candidate, 0x200, (DWORD)video_window))
                continue;

            volatile LONG *state = FindSteam105IntroState(candidate);
            if (!state)
                continue;

            LPTHREAD_START_ROUTINE play_proc = FindSteam105PlayThreadProc(comeon, candidate);
            HWND *hwnd = FindSteam105VideoWindowHandle(candidate);
            if (!play_proc || !hwnd)
                continue;

            *out_window_proc = (LPTHREAD_START_ROUTINE)candidate;
            *out_play_proc = play_proc;
            *out_hwnd = hwnd;
            *out_state = state;
            return true;
        }
    }

    return false;
}

static bool InstallComeOnDirectShowHook()
{
    HMODULE comeon = GetModuleHandleW(L"ComeOn.dll");
    LPTHREAD_START_ROUTINE directshow_proc = NULL;
    PROC original = NULL;
    LONG state = InterlockedCompareExchange(&g_directshow_hook_state,
                                            COMEON_HOOK_INSTALLING,
                                            COMEON_HOOK_NONE);

    if (state == COMEON_HOOK_INSTALLED)
        return true;
    if (state == COMEON_HOOK_INSTALLING)
        return false;

    if (!comeon) {
        InterlockedExchange(&g_directshow_hook_state, COMEON_HOOK_NONE);
        return false;
    }

    if (ResolveDirectShowThreadProc(comeon, &directshow_proc)) {
        // Steam 2.01 的开场动画发生在 launcher.exe 进程内加载的 ComeOn.dll 中，
        // 早于 ComeOn.exe 创建；因此该 hook 必须安装在 launcher hook DLL 里。
        g_directshow_thread_proc = directshow_proc;
    } else if (PatchSteam105IntroMovieName(comeon)) {
        // 试验方案：Steam 1.05 不替换线程入口，只把内部格式字符串中的 begin.dhp
        // 改为同长度的 degin.dhp，观察原 DirectShow 失败路径是否能自然跳过。
        InterlockedExchange(&g_directshow_hook_state, COMEON_HOOK_INSTALLED);
        return true;
    } else {
        InterlockedExchange(&g_directshow_hook_state, COMEON_HOOK_NONE);
        return false;
    }

    if (PatchOneImport(comeon, "KERNEL32.dll", "CreateThread", (PROC)HookCreateThread, &original) ||
        PatchOneImport(comeon, "kernel32.dll", "CreateThread", (PROC)HookCreateThread, &original)) {
        if (original)
            g_real_create_thread = (CreateThreadProc)original;
        InterlockedExchange(&g_directshow_hook_state, COMEON_HOOK_INSTALLED);
        return true;
    }

    InterlockedExchange(&g_directshow_hook_state, COMEON_HOOK_NONE);
    return false;
}

static DWORD WINAPI InstallHookThread(LPVOID)
{
    WCHAR dll_path[MAX_PATH] = {0};
    GetModuleFileNameW(g_module, dll_path, MAX_PATH);
    CopyString(g_game_dir, MAX_PATH, dll_path);
    PathRemoveFileSpecW(g_game_dir);
    BuildPath(g_plugk_path, MAX_PATH, g_game_dir, L"PlugK.dll");
    int game_version = DetectGameVersion();

    HMODULE exe = GetModuleHandleW(NULL);
    PROC original = NULL;
    if (PatchOneImport(exe, "KERNEL32.dll", "CreateProcessW", (PROC)HookCreateProcessW, &original) ||
        PatchOneImport(exe, "kernel32.dll", "CreateProcessW", (PROC)HookCreateProcessW, &original)) {
        g_real_create_process_w = (CreateProcessWProc)original;
    }

    if (game_version == 105) {
        original = NULL;
        if (PatchOneImport(exe, "KERNEL32.dll", "LoadLibraryW", (PROC)HookLoadLibraryW, &original) ||
            PatchOneImport(exe, "kernel32.dll", "LoadLibraryW", (PROC)HookLoadLibraryW, &original)) {
            if (original)
                g_real_load_library_w = (LoadLibraryWProc)original;
        }
    }

    if (IsSkipIntroMovieEnabled()) {
        // Steam 2.01 保持原有后台等待路径；Steam 1.05 额外由 LoadLibraryW hook
        // 在 JS 同步加载 ComeOn.dll 后立即 patch。这里仍作为两者的兜底。
        DWORD start = GetTickCount();
        while (InterlockedCompareExchange(&g_directshow_hook_state, 0, 0) != COMEON_HOOK_INSTALLED &&
               GetTickCount() - start < 30 * 60 * 1000) {
            if (InstallComeOnDirectShowHook())
                break;
            Sleep(50);
        }
    }

    return 0;
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        HANDLE thread = CreateThread(NULL, 0, InstallHookThread, NULL, 0, NULL);
        if (thread)
            CloseHandle(thread);
    }
    return TRUE;
}
