#include <windows.h>
#include <shlwapi.h>
#include <string.h>

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

static HMODULE g_module = NULL;
static WCHAR g_game_dir[MAX_PATH] = {0};
static WCHAR g_plugk_path[MAX_PATH] = {0};
static CreateProcessWProc g_real_create_process_w = NULL;

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

static bool PatchOneImport(HMODULE module, const char *dll_name, const char *func_name, PROC replacement, PROC *original)
{
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
            if (original && !*original)
                *original = old_function;
            if (!g_real_create_process_w)
                g_real_create_process_w = (CreateProcessWProc)old_function;
            first_thunk->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&first_thunk->u1.Function, sizeof(PROC), old_protect, &old_protect);
            FlushInstructionCache(GetCurrentProcess(), &first_thunk->u1.Function, sizeof(PROC));
            return true;
        }
    }
    return false;
}

static DWORD WINAPI InstallHookThread(LPVOID)
{
    WCHAR dll_path[MAX_PATH] = {0};
    GetModuleFileNameW(g_module, dll_path, MAX_PATH);
    CopyString(g_game_dir, MAX_PATH, dll_path);
    PathRemoveFileSpecW(g_game_dir);
    BuildPath(g_plugk_path, MAX_PATH, g_game_dir, L"PlugK.dll");

    HMODULE exe = GetModuleHandleW(NULL);
    PROC original = NULL;
    if (PatchOneImport(exe, "KERNEL32.dll", "CreateProcessW", (PROC)HookCreateProcessW, &original) ||
        PatchOneImport(exe, "kernel32.dll", "CreateProcessW", (PROC)HookCreateProcessW, &original)) {
        g_real_create_process_w = (CreateProcessWProc)original;
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
