#include "pch.h"
#include "stash_ext.h"
#include "config.h"
#include "inv_auto_sort.h"
#include "auto_fill_ext.h"
#include <stdio.h>
#include <MinHook.h> // 一定要引入 MinHook

// =========================================================
// 全局状态管理
// =========================================================

static int g_TempPageA[50];      // A 面备份 (临时存储)
static BOOL g_IsSwapped = FALSE; // 全局标记：当前游戏内存是否为 B 面

// 定义函数指针类型
typedef int(__fastcall *tFindEmptySlot)(void *thisPtr);
static tFindEmptySlot fpOriginalFindEmptySlot = NULL;

// 1.05 和 2.01 的 ItemIn 函数结尾地址 (用于 Hook 返回)
static void *g_Addr_ItemIn_Exit = NULL;
// 用于保存 ItemIn 函数结尾的原始指令的 Trampoline
static void *fpOriginalItemInExit = NULL;

// =========================================================
// 核心逻辑: 交换与还原
// =========================================================

// 切换到 B 面 (仅当 A 面满且 B 面有空时调用)
void SwapToPageB(void *pCharBase)
{
    if (g_IsSwapped)
        return; // 已经在 B 面了，禁止重复交换！

    int *gameInv = (int *)((DWORD)pCharBase + 0xA4);

    // 1. 备份 A 面 (Game -> Temp)
    memcpy(g_TempPageA, gameInv, 50 * sizeof(int));

    // 2. 写入 B 面 (B -> Game)
    memcpy(gameInv, g_InvPageB, 50 * sizeof(int));

    g_IsSwapped = TRUE;
}

void DirectSwapToPageB(void *pCharBase)
{
    if (g_IsSwapped)
        return;

    int *gameInv = (int *)((DWORD)pCharBase + 0xA4);

    // 1. 直接进行原地交换 (In-place Swap)
    // 这样即使后面 Restore 失败，数据也只是“互换了位置”
    for (int i = 0; i < 50; i++)
    {
        int temp = gameInv[i];
        gameInv[i] = g_InvPageB[i];
        g_InvPageB[i] = temp;
    }

    g_IsSwapped = TRUE;
}

void DirectRestoreToPageA(void *pCharBase)
{
    if (!g_IsSwapped)
        return;

    int *gameInv = (int *)((DWORD)pCharBase + 0xA4);

    // 2. 再次交换回来
    // 如果之前成功放入了新物品，新物品现在在 gameInv 里，交换后会进入 g_InvPageB
    for (int i = 0; i < 50; i++)
    {
        int temp = gameInv[i];
        gameInv[i] = g_InvPageB[i];
        g_InvPageB[i] = temp;
    }

    g_IsSwapped = FALSE;
}

// 还原回 A 面 (在函数彻底结束时调用)
void RestoreToPageA(void *pCharBase)
{
    if (!g_IsSwapped)
        return; // 没交换过，不需要还原

    int *gameInv = (int *)((DWORD)pCharBase + 0xA4);

    // 1. 保存 B 面 (Game -> B)
    // 注意：此时 Game 内存里包含着刚刚捡起来的物品
    memcpy(g_InvPageB, gameInv, 50 * sizeof(int));

    // 2. 还原 A 面 (Temp -> Game)
    memcpy(gameInv, g_TempPageA, 50 * sizeof(int));

    g_IsSwapped = FALSE;
}

// =========================================================
// Hook 1: 拦截查找空位函数
// =========================================================
int __fastcall Detour_FindEmptySlot(void *pCharBase)
{

    // 如果发现 g_IsSwapped 依然为 TRUE，说明上一次操作可能异常中断了
    // 强制执行一次还原，清除错误状态
    if (g_IsSwapped)
    {
        DirectRestoreToPageA(pCharBase);
    }

    // 1. 先让游戏在当前内存中找 (可能是 A 面，也可能是已经被换过的 B 面)
    int slot = fpOriginalFindEmptySlot(pCharBase);

    // 如果找到了空位，或者功能没开，直接返回
    if (slot != -1 || !g_pk_config.enable_autofill_ext || !g_pk_config.stash_ext_enabled)
    {
        return slot;
    }

    // 2. 只有当当前是 A 面时，才尝试去检查 B 面
    // 如果已经在 B 面了还返回 -1，说明 B 面也满了，真的没地方放了
    if (!g_IsSwapped)
    {
        // 检查 B 面缓存是否有空位
        int slotB = -1;
        for (int i = 0; i < 50; i++)
        {
            if (g_InvPageB[i] == -1)
            {
                slotB = i;
                break;
            }
        }

        if (slotB != -1)
        {
            // B 面有空位！执行热交换
            DirectSwapToPageB(pCharBase);

            // 交换后，直接返回刚才找到的 slotB
            // 游戏接下来的指令会把物品写到 gameInv[slotB]
            // 因为我们 DirectSwapToPageB 修改了 gameInv 指向的内存，所以实际写入了 B 面
            return slotB;
        }
    }

    return -1;
}

// =========================================================
// Hook 2: 拦截 ItemIn 函数的返回 (RET)
// =========================================================
// 这是一个 Naked 函数，用于 Hook 函数末尾的 ret 18
// 我们在这里做 "最终清算"

// 辅助函数：在 C 环境下获取 CharBase 并还原
void __stdcall Cleanup_Helper()
{
    DWORD charBase = GetCharacterBase();
    if (charBase)
    {
        DirectRestoreToPageA((void *)charBase);
    }
}

__declspec(naked) void Detour_ItemIn_Exit()
{
    __asm {
        // 保存寄存器 (尤其是 EAX, 它是返回值!)
        pushad
        pushfd

                // 调用 C 逻辑进行还原
        call Cleanup_Helper

        popfd
        popad

                            // 执行原始指令 (由 MinHook 自动生成的 Trampoline)
                            // 注意：MinHook 对 JMP/CALL 支持很好，但对 RET 的 Hook 需要特殊处理。
                            // 因为我们 Hook 的是 "add esp, 18; ret 18"，我们需要跳回 Trampoline 去执行这些被覆盖的指令。
        jmp fpOriginalItemInExit
    }
}

// =========================================================
// 初始化 MinHook
// =========================================================
void Mod_Auto_Fill_Init(int ver)
{
    if (!g_pk_config.enable_autofill_ext || !g_pk_config.stash_ext_enabled)
        return;

    LPVOID targetFindSlot = NULL;
    LPVOID targetFuncExit = NULL;

    if (ver == 105)
    {
        // 1.05 FindEmptySlot (Call Address is 47F0C8, Target is 47F290)
        targetFindSlot = (LPVOID)0x0047F290;

        // 1.05 ItemIn Exit
        // 目标：0047EFBA | 83C4 18 | add esp, 18 (3 bytes)
        //       0047EFBD | C2 1800 | ret 18      (3 bytes)
        // 总共 6 字节，足够放一个 5 字节的 JMP Hook
        targetFuncExit = (LPVOID)0x0047EFBA;
    }
    else if (ver == 201)
    {
        // 2.01 FindEmptySlot
        targetFindSlot = (LPVOID)0x0048E0A0;

        // 2.01 ItemIn Exit
        // 目标：0048DDAA | 83C4 18 | add esp, 18
        //       0048DDAD | C2 1800 | ret 18
        targetFuncExit = (LPVOID)0x0048DDAA;
    }

    // 1. Hook FindEmptySlot
    if (MH_CreateHook(targetFindSlot, &Detour_FindEmptySlot, (LPVOID *)&fpOriginalFindEmptySlot) != MH_OK)
    {
        OutputDebugStringA("PlugK: Failed to hook FindEmptySlot");
    }

    // 2. Hook Function Exit
    if (MH_CreateHook(targetFuncExit, &Detour_ItemIn_Exit, (LPVOID *)&fpOriginalItemInExit) != MH_OK)
    {
        OutputDebugStringA("PlugK: Failed to hook ItemIn Exit");
    }

    // 启用所有 Hook
    MH_EnableHook(MH_ALL_HOOKS);
}
