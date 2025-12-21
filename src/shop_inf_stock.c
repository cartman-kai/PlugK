/**
 * @file shop_infinite_stock.c
 * @brief 商店内回复和道具卖出不消失功能实现
 * @details 拦截商店卖出逻辑的内存地址，执行库存减量操作。
 */

#include "pch.h"
#include "config.h"
#include "shop_inf_stock.h"
#include <stdio.h>

// ---------------------------------------------------------
// 判定逻辑函数 (C语言编写，接收 Item Object Pointer的地址)
// ---------------------------------------------------------
// 默认 __cdecl 调用约定，返回结果在 EAX
int ShouldKeepItemPreCall(DWORD ItemPtrPtr)
{
    if (!g_pk_config.shop_no_vanish)
    {
        return 0; // 配置为 0，返回 0 (执行销毁)
    }

    // 1. 读取物品指针
    DWORD item_obj_ptr = *(DWORD *)ItemPtrPtr;
    if (item_obj_ptr == 0)
    {
        return 0;
    }

    // 2. 读取 ID
    DWORD item_id = *(DWORD *)(item_obj_ptr + 0x18);

    // 3. 判定逻辑
    if ((item_id >= 4 && item_id <= 15) ||
        (item_id >= 60 && item_id <= 77) ||
        (item_id >= 22 && item_id <= 23))
    {
        return 1; // 保留
    }

    return 0; // 销毁
}

// ---------------------------------------------------------
// Hook 变量与地址
// ---------------------------------------------------------
static DWORD g_OriginalCallAddr = 0;
static DWORD g_SkipAllAddr = 0;
static DWORD g_ClearSlotAddr = 0;

// ---------------------------------------------------------
// Naked Hook Trampoline (已修正栈平衡)
// ---------------------------------------------------------
__declspec(naked) void ShopHook_Trampoline()
{
    __asm {
        // [ 1. 模拟原版 'test ecx, ecx' ]
        test ecx, ecx

                    // [ 2. 模拟原版 'je 0x453515' ]
        jz label_jump_to_clear_slot

                        // [ 3. 运行 C 语言判定 ]

                        // 保存 volatile 寄存器 (ECX, EDX)
                        // EAX 不需要保存，因为它是返回值寄存器，马上会被C函数覆盖
        push edx
        push ecx

                            // 准备参数: [ESI + EDI*4 + 4] 的地址
        lea eax, [esi + edi * 4 + 4]
        push eax // 参数入栈

            // 调用 C 函数 (返回值在 EAX)
        call ShouldKeepItemPreCall

                // [关键修正] 平栈：清除参数 (push eax 占用的 4 字节)
                // 使用 add esp, 4 而不是 pop eax，避免覆盖 EAX 中的返回值！
        add esp, 4

        // 恢复 volatile 寄存器 (注意顺序：后进先出)
        pop ecx
        pop edx

            // 检查返回值 (EAX)
        test eax, eax
        jnz label_keep_flow // 如果返回 1，跳转到保留逻辑

            // ---------------------------------------------------------------------
            // [ Vanishing Flow - 执行原版销毁流程 (返回 0 的情况) ]
            // ---------------------------------------------------------------------

            // 恢复被 Hook 覆盖的原指令
        mov edx, dword ptr ds:[ecx]
        push 1

        // 跳转回原版 Call 指令 (执行销毁)
        jmp [g_OriginalCallAddr]

        // ---------------------------------------------------------------------
        // [ Keep Flow - 跳过 Call 和 清空槽位 (返回 1 的情况) ]
        // ---------------------------------------------------------------------
    label_keep_flow:
        // 直接跳过销毁和清空逻辑
        jmp [g_SkipAllAddr]

        // ---------------------------------------------------------------------
        // [ Clear Slot Flow (原版 je 的目标) ]
        // ---------------------------------------------------------------------
    label_jump_to_clear_slot:
        // 跳转到清空槽位指令
        jmp dword ptr [g_ClearSlotAddr]
    }
}

// ... (InstallJmpHook 和 pk_shop_init 函数保持不变，直接使用您提供的代码即可) ...
// ---------------------------------------------------------
// 内存 Patch 工具函数
// ---------------------------------------------------------
void InstallJmpHook(DWORD hookAddress, DWORD targetFunction, int len)
{
    DWORD oldProtect;
    VirtualProtect((LPVOID)hookAddress, len, PAGE_EXECUTE_READWRITE, &oldProtect);
    *(BYTE *)hookAddress = 0xE9;
    *(DWORD *)(hookAddress + 1) = targetFunction - hookAddress - 5;
    for (int i = 5; i < len; i++)
    {
        *(BYTE *)(hookAddress + i) = 0x90;
    }
    VirtualProtect((LPVOID)hookAddress, len, oldProtect, &oldProtect);
}

// ---------------------------------------------------------
// 商店回复类、暗器道具 无限购买 初始化函数
// ---------------------------------------------------------
void Mod_shop_inf_stock_init(int game_version)
{
    if (game_version == 105)
    {
        DWORD hookAddr = 0x0045350B;
        g_OriginalCallAddr = 0x00453513;
        g_ClearSlotAddr = 0x00453515;
        g_SkipAllAddr = 0x0045351D;

        if (*(BYTE *)hookAddr != 0x85 || *(BYTE *)(hookAddr + 2) != 0x74)
            return;
        InstallJmpHook(hookAddr, (DWORD)ShopHook_Trampoline, 8);
    }
    else if (game_version == 201)
    {
        DWORD hookAddr = 0x0045F63B;
        g_OriginalCallAddr = 0x0045F643;
        g_ClearSlotAddr = 0x0045F645;
        g_SkipAllAddr = 0x0045F64D;

        if (*(BYTE *)hookAddr != 0x85 || *(BYTE *)(hookAddr + 2) != 0x74)
            return;
        InstallJmpHook(hookAddr, (DWORD)ShopHook_Trampoline, 8);
    }
}