// plugk_shop.c
#include "pch.h"
#include "config.h"
#include "shop.h"
#include <stdio.h>

// ---------------------------------------------------------
// 判定逻辑函数 (C语言编写，接收 Item Object Pointer的地址)
// ---------------------------------------------------------
// 作用：如果返回 1，则阻止销毁流程；如果返回 0，则执行销毁流程。
int ShouldKeepItemPreCall(DWORD ItemPtrPtr)
{
    if (!g_pk_config.shop_no_vanish)
    {
        return 0; // 配置未开启，执行销毁
    }

    // ... (读取 Item ID 的逻辑保持不变) ...
    DWORD item_obj_ptr = *(DWORD *)ItemPtrPtr;
    if (item_obj_ptr == 0)
    {
        return 0;
    }
    DWORD item_id = *(DWORD *)(item_obj_ptr + 0x18);

    // 4 <= ID <= 15 或 60 <= ID <= 77 或 22 <= ID <= 23
    if ((item_id >= 4 && item_id <= 15) ||
        (item_id >= 60 && item_id <= 77) ||
        (item_id >= 22 && item_id <= 23))
    {
        return 1; // 保留物品
    }

    return 0; // 销毁物品
}

// ---------------------------------------------------------
// Hook 变量与地址 (v1.05 为例)
// ---------------------------------------------------------
// Hook 点: 0x0045350B (test ecx, ecx)
// Return 地址: 0x00453513 (call dword ptr ds:[edx])
static DWORD g_OriginalCallAddr = 0; // 0x00453513 或 0x0045F643
static DWORD g_SkipAllAddr = 0;      // 0x0045351D 或 0x0045F64D
static DWORD g_ClearSlotAddr = 0;    // [新增] 存储清空槽位指令的地址：0x00453515 或 0x0045F645

// ---------------------------------------------------------
// Naked Hook Trampoline (重建原版判断逻辑)
// ---------------------------------------------------------
__declspec(naked) void ShopHook_Trampoline()
{
    __asm {
        // [ 1. 模拟原版 'test ecx, ecx' ]
        test ecx, ecx

                    // [ 2. 模拟原版 'je 0x453515' ]
                    // 如果 ECX 是 0 (槽位为空)，则跳过销毁 Call，直接跳到 0x453515 (清空槽位)
        jz label_jump_to_clear_slot

                        // [ 3. 运行 C 语言判定 ]
                        // 保存 volatile 寄存器 (EAX, EDX, ECX)
        push edx
        push ecx

                            // 调用 C 函数，参数是 [ESI + EDI*4 + 4] 的地址
        lea eax, [esi + edi * 4 + 4]
        push eax
        call ShouldKeepItemPreCall // EAX 返回 1 (Keep) 或 0 (Vanish)

                // 恢复 volatile 寄存器 (ECX, EDX)
        pop eax // 弹出 ItemPtrPtr
        pop ecx
        pop edx

        test eax, eax
        jnz label_keep_flow // 如果 C 函数返回 1 (Keep)，跳转到保留逻辑

                // ---------------------------------------------------------------------
                // [ Vanishing Flow - 执行原版销毁流程 ]
                // ---------------------------------------------------------------------

                // 重新执行被我们 Hook 覆盖的指令 (0x45350F 到 0x453511)
                // 0x45350F: mov edx, dword ptr ds:[ecx]
        mov edx, dword ptr ds : [ecx]

        // 0x453511: push 1 
        push 1

        // 跳转到 Call 指令 (0x453513)
        jmp[g_OriginalCallAddr]

        // ---------------------------------------------------------------------
        // [ Keep Flow - 跳过 Call 和 清空槽位 ]
        // ---------------------------------------------------------------------
    label_keep_flow:
        // 跳过销毁 Call (0x453513) 和 清空槽位 (0x453515)
            jmp[g_SkipAllAddr] // JMP 到 0x45351D (pop edi)

        // ---------------------------------------------------------------------
        // [ Clear Slot Flow (原版 je 的目标) ]
        // ---------------------------------------------------------------------
            label_jump_to_clear_slot:
        // 如果是 NULL，原版是跳到 0x453515 (清空槽位)
                jmp dword ptr[g_ClearSlotAddr] // JMP 到 mov dword ptr ... 0
    }
}



// ---------------------------------------------------------
// 判定逻辑函数 (C语言编写，接收 Item Object Pointer的地址)
// ---------------------------------------------------------
// 参数: item_ptr_ptr = [ESI + EDI*4 + 4] (存放物品对象指针的内存地址)
// 返回: 1 表示要保留物品(不执行置0)，返回 0 表示正常消除
int __stdcall ShouldKeepItem(DWORD item_ptr_ptr)
{
    if (!g_pk_config.shop_no_vanish)
    {
        return 0; // 如果配置未开启，直接返回 0 (消失)
    }

    // 1. 从内存中读取物品对象指针 (Item Object Pointer)
    // *(DWORD*)item_ptr_ptr 等价于 MOV ECX, [EAX]
    DWORD item_obj_ptr = *(DWORD *)item_ptr_ptr;

    // 如果物品指针是 0，说明该槽位为空，应该执行消失逻辑（虽然不太可能走到这里）
    if (item_obj_ptr == 0)
    {
        return 0;
    }

    // 2. 读取物品 ID (ItemID)
    // Item ID = [Item Object Pointer + 0x18]
    // *(DWORD*)(item_obj_ptr + 0x18)
    DWORD item_id = *(DWORD *)(item_obj_ptr + 0x18);

    // 3. 应用自定义不消失逻辑
    // 4 <= ID <= 15
    // 60 <= ID <= 77
    // 22 <= ID <= 23

    if ((item_id >= 4 && item_id <= 15) ||
        (item_id >= 60 && item_id <= 77) ||
        (item_id >= 22 && item_id <= 23))
    {
        return 1; // 保留物品
    }

    return 0; // 消失物品
}

// ---------------------------------------------------------
// 内存 Patch 工具函数 (更新：接收长度参数)
// ---------------------------------------------------------
void InstallJmpHook(DWORD hookAddress, DWORD targetFunction, int len)
{
    DWORD oldProtect;
    // 允许写入指定长度
    VirtualProtect((LPVOID)hookAddress, len, PAGE_EXECUTE_READWRITE, &oldProtect);

    // 写入 JMP (E9)
    *(BYTE *)hookAddress = 0xE9;

    // 计算相对偏移量: Target - Source - 5
    *(DWORD *)(hookAddress + 1) = targetFunction - hookAddress - 5;

    // 填充剩余的字节为 NOP
    for (int i = 5; i < len; i++)
    {
        *(BYTE *)(hookAddress + i) = 0x90; // NOP
    }

    VirtualProtect((LPVOID)hookAddress, len, oldProtect, &oldProtect);
}

// ---------------------------------------------------------
// 初始化函数 (更新 Hook 地址)
// ---------------------------------------------------------
void pk_shop_init(int game_version)
{
    if (game_version == 105)
    {
        DWORD hookAddr = 0x0045350B;
        g_OriginalCallAddr = 0x00453513;
        g_ClearSlotAddr = 0x00453515; // 设置清空槽位地址
        g_SkipAllAddr = 0x0045351D;

        // 校验特征码 C9 74
        if (*(BYTE *)hookAddr != 0x85 || *(BYTE *)(hookAddr + 2) != 0x74)
            return;

        // Hook 8 bytes
        InstallJmpHook(hookAddr, (DWORD)ShopHook_Trampoline, 8); // InstallJmpHook需要更新以接收长度
    }
    else if (game_version == 201)
    {
        DWORD hookAddr = 0x0045F63B;
        g_OriginalCallAddr = 0x0045F643;
        g_ClearSlotAddr = 0x0045F645; // 设置清空槽位地址
        g_SkipAllAddr = 0x0045F64D;

        if (*(BYTE *)hookAddr != 0x85 || *(BYTE *)(hookAddr + 2) != 0x74)
            return;

        InstallJmpHook(hookAddr, (DWORD)ShopHook_Trampoline, 8);
    }
}