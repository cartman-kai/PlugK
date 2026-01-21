#include "pch.h"
#include "item.h"
#include "config.h"
#include "show_tips.h"
#include <MinHook.h> // 假设你已经集成了 MinHook.h

// =============================================================
// 函数指针定义
// =============================================================

// 原始的物品更新函数 (0041A860)
// 使用 __fastcall 模拟 __thiscall:
// pThis -> ECX, _edx -> EDX (占位)
typedef void(__fastcall *tItemUpdate)(void *pThis, void *_edx);
tItemUpdate fpItemUpdate = NULL;

// 显示物品名称的函数 (0041A500)
// 汇编调用是 push 1; mov ecx, this; call 41A500;
// 对应的 C 定义: pThis -> ECX, _edx -> EDX (占位), mode -> 堆栈
typedef void(__fastcall *tShowItemName)(void *pThis, void *_edx, int mode);
tShowItemName fpShowItemName = (tShowItemName)0x0041A500;

// =============================================================
// Hook 处理函数
// =============================================================

/**
 * @brief 拦截后的物品更新函数
 * * @param pThis 物品对象指针 (ECX)
 * @param _edx  未使用，__fastcall 的第二个参数通常是 edx
 */
void __fastcall Detour_ItemUpdate(void *pThis, void *_edx)
{
    // 1. 先执行游戏原始的渲染/更新逻辑
    // 如果不执行这个，物品本身可能不会显示，或者物理逻辑会停止
    if (fpItemUpdate)
    {
        fpItemUpdate(pThis, _edx);
    }

    // 2. 检查功能开关
    if (g_pk_config.show_item_name)
    {
        // 3. 强制调用显示名称
        // 传入 pThis，参数 1 (对应汇编中的 push 1)
        if (fpShowItemName && pThis)
        {
            fpShowItemName(pThis, NULL, 1);
        }
    }
}

void ToggleShowItemNameSwitch()
{
    if (g_pk_config.show_item_name)
    {
        g_pk_config.show_item_name = 0;
        ShowGameLog("[提示]关闭地面物品名称显示");
        return;
    }
    g_pk_config.show_item_name = 1;
    ShowGameLog("[提示]开启地面物品名称显示");
    return;
}

// =============================================================
// 初始化
// =============================================================

void Mod_Show_Item_Name_Init(int game_version)
{
    void *targetAddr_Update = NULL;

    // ---------------------------------------------------------
    // 针对 2.01 版本的地址配置
    // ---------------------------------------------------------
    if (game_version == 201)
    {
        // 原始更新函数地址 (MinHook 目标)
        targetAddr_Update = (void *)0x0041A860;

        // 显示名称函数地址 (直接调用)
        fpShowItemName = (tShowItemName)0x0041A500;
    }
    // ---------------------------------------------------------
    // 针对 1.05 版本的地址配置 [修复点]
    // ---------------------------------------------------------
    else if (game_version == 105)
    {
        // 根据你的调研结果: 00412B30
        targetAddr_Update = (void *)0x00412B30;

        // 根据你的调研结果: 004127D0
        fpShowItemName = (tShowItemName)0x004127D0;
    }
    // ---------------------------------------------------------
    // 其他版本不支持
    // ---------------------------------------------------------
    else
    {
        return;
    }

    // 创建 Hook
    if (MH_CreateHook(targetAddr_Update, &Detour_ItemUpdate, (LPVOID *)&fpItemUpdate) != MH_OK)
    {
        // Hook 创建失败处理 (如输出日志)
        // OutputDebugStringA("PlugK: Failed to create ItemName hook.");
        return;
    }

    // 启用 Hook
    if (MH_EnableHook(targetAddr_Update) != MH_OK)
    {
        // Hook 启用失败处理
        return;
    }
}