#include "pch.h"
#include "gem_insert.h"
#include "config.h"
#include "MinHook.h" // 确保引用了 MinHook 头文件
#include <windows.h>
#include <stdio.h>

// 全局变量：用于存储不同版本的跳转目标地址
DWORD g_jump_target_addr = 0;

// MinHook 需要的原函数指针占位（虽然我们这里不打算跳回去，但 API 需要）
void *pOriginalInsertGem = NULL;

/**
 * 这是一个 "裸函数" (Naked Function)。
 * 编译器不会为它生成任何函数头(prologue)和函数尾(epilogue)，
 * 这允许我们完全控制寄存器和堆栈，就像直接写汇编一样。
 */
void __declspec(naked) HookedInsertGemCondLogic()
{
    __asm {
        // 1. 补上被 MinHook 覆盖掉的原指令
        // 原指令是：mov ecx, dword ptr ds:[esi+0xF0]
        // 无论哪个版本，这条指令都是一样的
        mov ecx, dword ptr [esi + 0xF0]

        // 2. 强制跳转到目标地址
        // 相当于 Golang 里的: goto TargetLabel
        jmp [g_jump_target_addr]
    }
}

void Mod_Gem_Insert_Init(int game_version)
{
    // 1. 检查配置
    if (!g_pk_config.enable_insert_gem)
    {
        return;
    }

    DWORD hookAddress = 0;

    // 2. 根据版本设置地址
    if (game_version == 105)
    {
        // --- 1.05 版本 ---
        // Hook点：mov ecx, [esi+F0] 的位置
        hookAddress = 0x004BD5D9;

        // 目标点：jne 跳转到的位置 (comeon.4BD685)
        g_jump_target_addr = 0x004BD685;
    }
    else if (game_version == 201)
    {
        // --- 2.01 版本 ---
        // Hook点：mov ecx, [esi+F0] 的位置
        hookAddress = 0x004D1599;

        // 目标点：jne 跳转到的位置 (comeon.4D1645)
        g_jump_target_addr = 0x004D1645;
    }
    else
    {
        return; // 不支持的版本
    }

    // 3. 执行 MinHook 安装
    // 类似于 Golang 的 router.Use(Middleware)

    // 初始化 MinHook (如果在 DLL 入口或其他地方初始化过，这里可以省略)
    if (MH_Initialize() != MH_OK)
    {
        // 即使初始化失败（通常是因为已经初始化过），我们也尝试继续创建 Hook
    }

    // 创建 Hook
    // 参数1: 游戏原来的地址
    // 参数2: 我们的裸函数
    // 参数3: 接收原函数蹦床的指针（这里我们不用它，因为我们要强行跳转）
    if (MH_CreateHook((LPVOID)hookAddress, &HookedInsertGemCondLogic, &pOriginalInsertGem) == MH_OK)
    {
        // 启用 Hook
        MH_EnableHook((LPVOID)hookAddress);
    }
}