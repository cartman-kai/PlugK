#include "pch.h"
#include "input_mgr.h"
#include "config.h"
#include "stash_ext.h"
#include "inv_auto_sort.h"
#include "item_stack.h"
#include "item.h"
#include "shop_optimization.h"
#include "skill_respec.h"
#include "ultimate_hotkey.h"

static WNDPROC g_OriginalWndProc = NULL;
static HWND g_hGameWindow = NULL;
static BOOL g_swallow_ultimate_keyup[4] = {FALSE, FALSE, FALSE, FALSE};

static int GetUltimateHotkeySlot(WPARAM wParam)
{
    int key = (int)wParam;

    if (key >= '1' && key <= '4')
        return key - '1';
    if (key >= VK_NUMPAD1 && key <= VK_NUMPAD4)
        return key - VK_NUMPAD1;

    return -1;
}

// 自定义消息处理函数
LRESULT CALLBACK PlugK_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // Alt+1..4 与游戏原有数字键吃药冲突，按下消息始终吞掉；重复消息只拦截不再次释放。
    if (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN)
    {
        if (GetKeyState(VK_MENU) & 0x8000)
        {
            int slot = GetUltimateHotkeySlot(wParam);
            if (slot >= 0)
            {
                g_swallow_ultimate_keyup[slot] = TRUE;
                if (!(lParam & 0x40000000))
                    ExecuteUltimateHotkeySlot(slot);
                return 0;
            }
        }
    }
    // 对应的释放和系统字符消息也吞掉，避免游戏继续收到数字键并触发回复药快捷键。
    else if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP || uMsg == WM_SYSCHAR)
    {
        int slot = GetUltimateHotkeySlot(wParam);
        if (slot >= 0 && (g_swallow_ultimate_keyup[slot] || (GetKeyState(VK_MENU) & 0x8000)))
        {
            if (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP)
                g_swallow_ultimate_keyup[slot] = FALSE;
            return 0;
        }
    }

    // 监听按键按下消息
    if (uMsg == WM_KEYDOWN)
    {
        // 检查 Ctrl 是否按下
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
            int key = (int)wParam;
            BOOL handled = FALSE;

            if (key == g_pk_config.key_stash_swap)
            { // Ctrl + ;
                if (g_pk_config.stash_ext_enabled)
                {
                    ToggleStash(); // 储物箱切换
                    handled = TRUE;
                }
            }
            else if (key == g_pk_config.key_inv_swap)
            { // Ctrl + <
                if (g_pk_config.stash_ext_enabled)
                {
                    ToggleInventory(); // 上一页
                    handled = TRUE;
                }
            }
            else if (key == g_pk_config.key_inv_sort)
            { // Ctrl + /
                if (g_pk_config.inventory_sort)
                {
                    ExecuteInventorySortFlow(); // 背包整理
                    handled = TRUE;
                }
            }
            else if (key == g_pk_config.key_stash_sort)
            {
                if (g_pk_config.inventory_sort)
                {
                    ExecuteStashSortFlow(); // 储物箱整理
                    handled = TRUE;
                }
            }
            else if (key == g_pk_config.key_inv_sort_current)
            {
                if (g_pk_config.inventory_sort)
                {
                    ExecuteCurrentInventorySortFlow(); // 仅整理当前背包页
                    handled = TRUE;
                }
            }
            else if (key == g_pk_config.key_switch_gem_stack)
            {
                if (g_pk_config.enable_gem_stack)
                {
                    ToggleChangeGemStackProp();
                    ToggleItemStackState();
                    handled = TRUE;
                }
            }
            else if (key == g_pk_config.key_switch_show_item_name)
            {
                ToggleShowItemNameSwitch();
                handled = TRUE;
            }
            else if (key == g_pk_config.key_skill_respec)
            {
                if (g_pk_config.enable_skill_respec)
                {
                    ExecuteSkillRespecFlow();
                    handled = TRUE;
                }
            }

            // ... 其他按键 (StashSort, StackToggle) ...

            // 如果是我们处理的快捷键，可以选择不传递给游戏，防止游戏误触
            // 但对于 Ctrl 组合键，通常游戏本身没有冲突，可以返回 0 吞掉，或者继续传递
            if (handled)
            {
                // MessageBeep(MB_OK); // 可以加个反馈
                // return 0; // 吞掉消息 (可选)
            }
        }
    }

    // 调用游戏原本的处理函数
    return CallWindowProc(g_OriginalWndProc, hwnd, uMsg, wParam, lParam);
}

// 寻找游戏窗口并 Hook 的临时线程 (只运行一次)
DWORD WINAPI HookWindowThread(LPVOID lpParam)
{
    int attempts = 0;
    while (g_hGameWindow == NULL && attempts < 100)
    {
        g_hGameWindow = FindWindowA(NULL, "daojian"); // 需确认窗口标题，或用类名
        if (!g_hGameWindow)
            g_hGameWindow = FindWindowA("daojian", NULL);
        // 也可以通过 GetCurrentProcessId() + EnumWindows 找到主窗口，这样最稳

        Sleep(200);
        attempts++;
    }

    if (g_hGameWindow)
    {
        g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_hGameWindow, GWLP_WNDPROC, (LONG_PTR)PlugK_WndProc);
    }
    return 0;
}

void Mod_Input_Mgr_Init()
{
    // 启动一个临时线程去 Hook 窗口，因为 DLL 加载时窗口可能还没创建
    CreateThread(NULL, 0, HookWindowThread, NULL, 0, NULL);
}
