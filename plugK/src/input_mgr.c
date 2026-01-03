#include "pch.h"
#include "input_mgr.h"
#include "config.h"

// 引入各功能模块的头文件
#include "stash_ext.h"     // 储物箱/背包扩展
#include "inv_auto_sort.h" // 一键整理
#include "item_stack.h"    // [本期新增] 物品叠加

// 全局线程句柄
static HANDLE g_hInputThread = NULL;
static BOOL g_bInputThreadRunning = TRUE;

// 统一的按键监听线程
DWORD WINAPI InputMonitorThread(LPVOID lpParam)
{
    while (g_bInputThreadRunning)
    {
        Sleep(50); // 适当的休眠，避免占用 CPU

        // ---------------------------------------------------------
        // 所有快捷键都依赖 CTRL 键
        // ---------------------------------------------------------
        if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
        {
            BOOL actionTriggered = FALSE;

            // 1. 切换储物箱 A/B 面 (Ctrl + < / ,)
            // VK_OEM_COMMA 是逗号键，即 <
            if (GetAsyncKeyState(VK_OEM_COMMA) & 0x8000)
            {
                if (g_pk_config.stash_ext_enabled)
                {
                    ToggleStash(); // 需在 stash_ext.h 中暴露
                    actionTriggered = TRUE;
                }
            }
            // 2. 切换背包 A/B 面 (Ctrl + > / .)
            // VK_OEM_PERIOD 是句号键，即 >
            else if (GetAsyncKeyState(VK_OEM_PERIOD) & 0x8000)
            {
                if (g_pk_config.stash_ext_enabled)
                {
                    ToggleInventory(); // 需在 stash_ext.h 中暴露
                    actionTriggered = TRUE;
                }
            }
            // 3. 储物箱一键整理 (Ctrl + [)
            else if (GetAsyncKeyState(VK_OEM_4) & 0x8000)
            {
                if (g_pk_config.inventory_sort)
                {
                    // 调用储物箱整理
                    ExecuteStashSortFlow();
                    actionTriggered = TRUE;
                }
            }
            // 4. 背包一键整理 (Ctrl + \)
            else if (GetAsyncKeyState(VK_OEM_5) & 0x8000)
            {
                if (g_pk_config.inventory_sort)
                {
                    // 调用背包整理流程 (整理 -> 清理快捷栏 -> 整理)
                    ExecuteInventorySortFlow(); // 需在 inv_auto_sort.c 中封装此逻辑
                    actionTriggered = TRUE;
                }
            }
            // 5. [新增] 物品叠加开关 (Ctrl + ')
            // VK_OEM_7 是单引号/双引号键
            else if (GetAsyncKeyState(VK_OEM_7) & 0x8000)
            {
                // 注意：这里不再判断 g_pk_config.enable_item_stack
                // 因为配置项变成了“是否允许使用此功能”，而按键负责实时开关
                ToggleItemStackState(); // 需在 item_stack.c 中实现
                actionTriggered = TRUE;
            }

            // 如果触发了任意动作，进行防抖处理
            if (actionTriggered)
            {
                Sleep(300); // 300ms 防抖

                // 可选：为了防止连续误触，可以等待 Ctrl 抬起
                // while (GetAsyncKeyState(VK_CONTROL) & 0x8000) Sleep(10);
            }
        }
    }
    return 0;
}

void Mod_Input_Mgr_Init()
{
    g_bInputThreadRunning = TRUE;
    g_hInputThread = CreateThread(NULL, 0, InputMonitorThread, NULL, 0, NULL);
}