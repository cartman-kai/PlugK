#include "pch.h"
#include "stash_ext.h"
#include "config.h"
#include "inv_auto_sort.h" // 复用 GetCharacterBase
#include "drop_bias.h"
#include "show_tips.h"
#include <stdio.h>

// ---------------------------------------------------------
// 全局变量与定义
// ---------------------------------------------------------
int g_StashPageB[50];
int g_InvPageB[50];

// 当前状态 (0=A面, 1=B面)
static int g_CurrentStashPage = 0;
static int g_CurrentInvPage = 0;

static int g_GameVersion = 0;

// 跳转返回地址
static DWORD g_RetAddr_Save = 0;
static DWORD g_RetAddr_Load = 0;
static DWORD g_RetAddr_Exit = 0;

// 1.05 地址
#define ADDR_105_SAVE_HOOK 0x004815BA
#define ADDR_105_EXIT_HOOK 0x004AD015

// 2.01 地址 (根据您的调研)
#define ADDR_201_SAVE_HOOK 0x00461B80
#define ADDR_201_EXIT_HOOK 0x004BFDC5

// 内存偏移
#define OFFSET_STASH_ARR 0x1FC
#define OFFSET_INV_ARR 0xA4

// 标记
static BOOL g_IsStashExtReady = FALSE;

// 文件头结构
typedef struct
{
    char magic[4]; // "PKS2"
    int stashB[50];
    int invB[50];
} PksFileV2;

// ---------------------------------------------------------
// 辅助函数
// ---------------------------------------------------------

BOOL ExtractRoleName(const char *fullPath, char *outName, int maxLen)
{
    if (!fullPath || !outName || maxLen <= 0)
        return FALSE;
    const char *pSlash = strrchr(fullPath, '\\');
    const char *pStart = (pSlash) ? pSlash + 1 : fullPath;
    const char *pDot = strchr(pStart, '.');
    int len = (pDot) ? (int)(pDot - pStart) : (int)strlen(pStart);
    if (len <= 0 || len >= maxLen)
        return FALSE;
    strncpy_s(outName, maxLen, pStart, len);
    outName[len] = '\0';
    return TRUE;
}

BOOL GetExtSavePath(const char *originalPath, char *outPath, int maxLen)
{
    char roleName[64] = {0};
    if (!ExtractRoleName(originalPath, roleName, 64))
        return FALSE;
    char dllPath[MAX_PATH];
    GetModuleFileNameA(NULL, dllPath, MAX_PATH);
    char *lastSlash = strrchr(dllPath, '\\');
    if (lastSlash)
        *lastSlash = 0;
    _snprintf_s(outPath, maxLen, _TRUNCATE, "%s\\Save\\%s.pks", dllPath, roleName);
    return TRUE;
}

// 初始化缓存
void ResetCache()
{
    for (int i = 0; i < 50; i++)
    {
        g_StashPageB[i] = -1;
        g_InvPageB[i] = -1;
    }
    g_CurrentStashPage = 0;
    g_CurrentInvPage = 0;
    g_IsStashExtReady = FALSE;
    DropBias_ResetRecent();
}

// ---------------------------------------------------------
// 业务逻辑: Save / Load
// ---------------------------------------------------------

void ProcessSaveExt(const char *savePath)
{
    if (!savePath)
        return;
    char pkPath[MAX_PATH];
    if (!GetExtSavePath(savePath, pkPath, MAX_PATH))
        return;

    // 无论当前显示的是A面还是B面，内存中的 g_StashPageB 始终保存着 B 面的数据。
    // 但是！如果当前显示的是 B 面 (g_CurrentStashPage == 1)，
    // 那么角色的真实内存 (OFFSET_STASH_ARR) 里其实是 B 面数据，
    // 而 g_StashPageB 里暂存的是 A 面数据。
    // 保存时，我们只想保存 B 面数据到文件。

    // 策略：我们总是把 g_StashPageB (缓存区) 视为 "非当前显示的那一面"。
    // 但文件需要固定保存 "第二套装备"。
    // 这种逻辑有点绕。

    // 简化策略：
    // 文件只保存 g_StashPageB 和 g_InvPageB。
    // 在切换逻辑 Toggle 中，我们保证 g_StashPageB 永远持有 "另一面" 的数据。
    // 那么，如果玩家在 B 面存档，g_StashPageB 里其实是 A 面数据。
    // 这会导致 "存档里的扩展数据" 其实是 "A面数据"。
    // 这对于 "扩展存储" 来说是可以接受的：它就是一个交换缓冲区。

    PksFileV2 data;
    memcpy(data.magic, "PKS2", 4);

    // 直接保存当前缓存中的数据 (即"后台"数据)
    memcpy(data.stashB, g_StashPageB, sizeof(int) * 50);
    memcpy(data.invB, g_InvPageB, sizeof(int) * 50);

    FILE *fp = NULL;
    errno_t err = fopen_s(&fp, pkPath, "wb");
    if (err == 0 && fp != NULL)
    {
        fwrite(&data, sizeof(PksFileV2), 1, fp);
        fclose(fp);
        g_IsStashExtReady = TRUE;
    }
}

void ProcessLoadExt(const char *loadPath)
{
    ResetCache(); // 先清空
    if (!loadPath)
        return;

    char pkPath[MAX_PATH];
    if (!GetExtSavePath(loadPath, pkPath, MAX_PATH))
        return;

    FILE *fp = fopen(pkPath, "rb");
    if (fp)
    {
        PksFileV2 data;
        size_t read = fread(&data, sizeof(PksFileV2), 1, fp);
        fclose(fp);

        if (read == 1 && strncmp(data.magic, "PKS2", 4) == 0)
        {
            // 版本 2 格式
            memcpy(g_StashPageB, data.stashB, sizeof(int) * 50);
            memcpy(g_InvPageB, data.invB, sizeof(int) * 50);
        }
        else
        {
            // 尝试兼容旧版本 (只存了 StashB 50个int)
            // 重新打开读取头部
            FILE *fp = NULL;
            // fopen_s 返回 0 表示成功，否则返回错误代码
            errno_t err = fopen_s(&fp, pkPath, "rb");
            if (fp)
            {
                fseek(fp, 0, SEEK_END);
                long size = ftell(fp);
                fseek(fp, 0, SEEK_SET);
                if (size == 50 * sizeof(int))
                {
                    fread(g_StashPageB, sizeof(int), 50, fp);
                    // InvB 保持 -1
                }
                fclose(fp);
            }
        }
        g_IsStashExtReady = TRUE;
    }
    else
    {
        // 文件不存在，但也标记就绪，允许创建新数据
        g_IsStashExtReady = TRUE;
    }
}

// ---------------------------------------------------------
// 切换逻辑
// ---------------------------------------------------------

void SwapData(DWORD offset, int *cachePage, int *pageIndex)
{
    if (!g_IsStashExtReady)
    {
        Beep(200, 100);
        return;
    }

    DWORD charBase = GetCharacterBase();
    if (charBase == 0)
        return;

    int *gameArr = (int *)(charBase + offset);

    // 交换 内存数组 <-> 缓存数组
    for (int i = 0; i < 50; i++)
    {
        int temp = gameArr[i];
        gameArr[i] = cachePage[i];
        cachePage[i] = temp;
    }

    *pageIndex = !(*pageIndex); // 切换状态
}

void ToggleStash()
{
    SwapData(OFFSET_STASH_ARR, g_StashPageB, &g_CurrentStashPage);
    SendGameTips("[储物箱] 切换成功");
}

void ToggleInventory()
{
    SwapData(OFFSET_INV_ARR, g_InvPageB, &g_CurrentInvPage);
    SendGameTips("[背包] 切换成功");
}

// ---------------------------------------------------------
// Hooks - 1.05
// ---------------------------------------------------------

void __declspec(naked) Hook_105_Save()
{
    __asm {
        pushad
        pushfd
        lea eax, [esp + 0x60] // 0x3C(ret) + 0x24(pushad)
        mov eax, [eax]
        push eax
        call ProcessSaveExt
        add esp, 4
        popfd
        popad;
        mov eax, 1
        jmp g_RetAddr_Save
    }
}

void __declspec(naked) Hook_105_Exit()
{
    __asm {
        pushad
        pushfd
        call ResetCache
        popfd
        popad
        mov eax, 0x005483A8
        mov dword ptr [eax], 2
        jmp g_RetAddr_Exit
    }
}

// ---------------------------------------------------------
// Hooks - 2.01 (新实现)
// ---------------------------------------------------------

// 2.01 Save Hook @ 00461B80
// 覆盖：
// 00461B80 | 8B 56 1C   | mov edx, dword ptr ds:[esi+1C]
// 00461B83 | 83 C4 04   | add esp, 4
// 总计 6 字节

void __declspec(naked) Hook_201_Save()
{
    __asm {
        // 1. 保存现场
        pushad
        pushfd

                // 2. 获取路径
                // 原始 esp+24 处是路径指针。
                // 经过 pushad(32) + pushfd(4) = 36 (0x24) 字节
                // 所以现在在 esp + 0x24 + 0x24 = 0x48
        mov eax, [esp + 0x48]
        push eax
        call ProcessSaveExt
        add esp, 4

        // 3. 恢复现场
        popfd
        popad

                // 4. 补偿被覆盖的指令
        mov edx, dword ptr ds:[esi+0x1C] // 原指令 1
        add esp, 4 // 原指令 2

                   // 5. 跳转回 00461B86
        jmp g_RetAddr_Save
    }
}

// Hook @ 004BFDC5 (Exit)
// 原指令: mov dword ptr ds:[578938], 2 (10 bytes: C7 05 38 89 57 00 02 00 00 00)
void __declspec(naked) Hook_201_Exit()
{
    __asm {
        pushad
        pushfd
        call ResetCache
        popfd
        popad

                            // 恢复原指令
        mov eax, 0x00578938
        mov dword ptr [eax], 2
        
        jmp g_RetAddr_Exit
    }
}

// ---------------------------------------------------------
// MinHook 定义与实现
// ---------------------------------------------------------

// ---------------------------------------------------------
// Load Hook - 调用点替换方式
// ---------------------------------------------------------

// 原始 Load 函数地址 (版本相关)
static DWORD g_OriginalLoadFunc = 0;

// 调用点 Hook 地址 (需要修补 call 指令的位置)
#define ADDR_105_LOAD_CALL 0x004558AC // call 4815D0 的位置
#define ADDR_201_LOAD_CALL 0x00461D1C // call 490400 的位置

// 原始函数地址
#define ADDR_105_LOAD_FUNC 0x004815D0
#define ADDR_201_LOAD_FUNC 0x00490400

// Naked 包装函数 - 替代原调用点的 call 目标
// 调用约定：__thiscall (ECX = this, 栈上有参数)
//
// 在 call 我们的函数时，栈布局：
//   [ESP+0]  = 返回地址 (004558B1 或 00461D21)
//   [ESP+4]  = pStream (由 push esi 压入)
//   [ESP+8]  = 调用者栈上的某个值
//   [ESP+C]  = 存档名称字符串指针 ← 我们需要的
// ECX = pThis (this 指针)
//
// 原函数是 ret 4，所以我们也要 ret 4 来平衡栈
void __declspec(naked) Wrapper_GameFile_Load(void)
{
    __asm {
        // === 1. 调用原始 Load 函数 ===
        // 原函数期望：ECX = this, [ESP+4] = pStream (在 call 之后)
        // 当前栈：[ret][pStream][...][savename]
        // 我们需要做的是：转发调用到原函数

        // 保存存档名指针 (从 ESP+C 读取，保存到 EDX 临时使用)
        // 注意：在进入此函数时 [ESP] = 返回地址
        mov edx, dword ptr [esp + 0x0C]
        push edx // 把存档名压栈保存

                   // 调用原函数
                   // 需要重新压入 pStream 参数给原函数
        mov eax, dword ptr [esp + 0x08] // [ESP+4+4] = 原始 pStream (因为我们压了 4 字节)
        push eax // 压入 pStream

            // ECX 已经是 this 指针，不需要修改
            // 调用原函数 (间接调用，通过全局变量)
        call g_OriginalLoadFunc
                // 原函数执行 ret 4，会清理我们 push 的 pStream
                // 返回值在 EAX

                // === 2. 检查返回值 ===
        cmp eax, 1
        jne skip_ext_load

            // === 3. EAX == 1，调用 ProcessLoadExt ===
            // 保存 EAX (返回值)
        push eax

                // 此时栈顶是 EAX，下面是我们保存的存档名指针
                // [ESP] = EAX (返回值)
                // [ESP+4] = 存档名指针

                // 获取存档名并调用 ProcessLoadExt
        mov eax, dword ptr [esp + 4] // 获取存档名指针
        push eax // 作为参数压栈
        call ProcessLoadExt
        add esp, 4 // 清理参数 (cdecl)

        // 恢复返回值
        pop eax
        
skip_ext_load:
        // === 4. 清理并返回 ===
        // 弹出我们保存的存档名指针
        add esp, 4

        // 模拟原函数行为：ret 4 (清理调用者压入的 pStream)
        ret 4
    }
}

// 安装 JMP Hook 的辅助函数 (用于 Save 和 Exit)
void InstallStashExtJmp(DWORD addr, void *func, DWORD *ret, int len)
{
    DWORD old;
    VirtualProtect((void *)addr, len, PAGE_EXECUTE_READWRITE, &old);
    *(BYTE *)addr = 0xE9;
    *(DWORD *)(addr + 1) = (DWORD)func - addr - 5;
    for (int i = 5; i < len; i++)
        *(BYTE *)(addr + i) = 0x90;
    *ret = addr + len;
    VirtualProtect((void *)addr, len, old, &old);
}

// 修补 call 指令的辅助函数 (用于 Load)
static void PatchCallInstruction(DWORD callAddr, void *newTarget)
{
    DWORD old;
    VirtualProtect((void *)callAddr, 5, PAGE_EXECUTE_READWRITE, &old);

    // call 指令格式：E8 [相对偏移32位]
    // 相对偏移 = 目标地址 - (call指令地址 + 5)
    DWORD relOffset = (DWORD)newTarget - (callAddr + 5);
    *(DWORD *)(callAddr + 1) = relOffset;

    VirtualProtect((void *)callAddr, 5, old, &old);
}

// ---------------------------------------------------------
// 初始化
// ---------------------------------------------------------

void Mod_Stash_Ext_Init(int ver)
{
    ResetCache();

    if (!g_pk_config.stash_ext_enabled)
        return;

    g_GameVersion = ver;

    if (ver == 105)
    {
        // Save & Exit Hooks (保持原有逻辑)
        InstallStashExtJmp(ADDR_105_SAVE_HOOK, Hook_105_Save, &g_RetAddr_Save, 5);
        InstallStashExtJmp(ADDR_105_EXIT_HOOK, Hook_105_Exit, &g_RetAddr_Exit, 10);

        // --- Load Hook: 调用点替换 ---
        g_OriginalLoadFunc = ADDR_105_LOAD_FUNC;
        PatchCallInstruction(ADDR_105_LOAD_CALL, Wrapper_GameFile_Load);
    }
    else if (ver == 201)
    {
        // Save & Exit Hooks (保持原有逻辑)
        InstallStashExtJmp(ADDR_201_SAVE_HOOK, Hook_201_Save, &g_RetAddr_Save, 6);
        InstallStashExtJmp(ADDR_201_EXIT_HOOK, Hook_201_Exit, &g_RetAddr_Exit, 10);

        // --- Load Hook: 调用点替换 ---
        g_OriginalLoadFunc = ADDR_201_LOAD_FUNC;
        PatchCallInstruction(ADDR_201_LOAD_CALL, Wrapper_GameFile_Load);
    }
}
