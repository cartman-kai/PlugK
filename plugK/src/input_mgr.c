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
#include "item_split.h"
#include <MinHook.h>

#define VER_105 105
#define VER_201 201

// 1.05 / 2.01 的键盘状态刷新函数；函数内会维护上一帧状态并调用 GetKeyboardState。
#define ADDR_UPDATE_KEYBOARD_STATE_105 0x004CE3D0
#define ADDR_UPDATE_KEYBOARD_STATE_201 0x004E3280

// 游戏键盘状态对象中，当前帧 VK 状态和上一帧/已按下标记的偏移。
#define GAME_KEY_CURRENT_OFFSET 0x08
#define GAME_KEY_PREVIOUS_OFFSET 0x108

static WNDPROC g_OriginalWndProc = NULL;
static HWND g_hGameWindow = NULL;
static BOOL g_swallow_ultimate_keyup[4] = {FALSE, FALSE, FALSE, FALSE};
// 组合键处理后持续屏蔽冲突键，直到玩家物理松开该键，避免松开 Alt/Ctrl 后补触发游戏逻辑。
static BOOL g_block_key_until_release[256] = {FALSE};

typedef BOOL(__fastcall *tUpdateKeyboardState)(BYTE *keyboard_state, void *_edx);

static tUpdateKeyboardState Original_UpdateKeyboardState = NULL;

static BOOL IsVkDown(int vk)
{
    return (GetKeyState(vk) & 0x8000) || (GetAsyncKeyState(vk) & 0x8000);
}

static BOOL IsAltDownForMessage(UINT uMsg, LPARAM lParam)
{
    if (IsVkDown(VK_MENU))
        return TRUE;

    // WM_SYSKEY* / WM_SYSCHAR 的 bit 29 表示该消息生成时 Alt 处于按下状态。
    if ((uMsg == WM_SYSKEYDOWN || uMsg == WM_SYSKEYUP || uMsg == WM_SYSCHAR) && (lParam & 0x20000000))
        return TRUE;

    return FALSE;
}

static int GetUltimateHotkeySlot(WPARAM wParam)
{
    int key = (int)wParam;

    if (key >= '1' && key <= '4')
        return key - '1';
    if (key >= VK_NUMPAD1 && key <= VK_NUMPAD4)
        return key - VK_NUMPAD1;

    return -1;
}

// 清理游戏状态表中的某个 VK，使原版逻辑在本帧看不到该按键。
static void ClearGameKeyState(BYTE *keyboard_state, int vk)
{
    if (!keyboard_state || vk < 0 || vk > 0xFF)
        return;

    keyboard_state[GAME_KEY_CURRENT_OFFSET + vk] = 0;
    keyboard_state[GAME_KEY_PREVIOUS_OFFSET + vk] = 0;
}

// 判断游戏刚刷新出的当前帧状态里，某个 VK 是否处于按下状态。
static BOOL IsGameKeyCurrentlyDown(BYTE *keyboard_state, int vk)
{
    if (!keyboard_state || vk < 0 || vk > 0xFF)
        return FALSE;

    return (keyboard_state[GAME_KEY_CURRENT_OFFSET + vk] & 0xF0) != 0;
}

// 修饰键本身要继续交给游戏，只用于判断组合键条件，不作为被屏蔽的冲突键。
static BOOL IsModifierKey(int vk)
{
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

// 屏蔽冲突键并建立释放前 latch，避免组合键结束时游戏补消费一次。
static void BlockGameKeyUntilRelease(BYTE *keyboard_state, int vk)
{
    if (!keyboard_state || vk < 0 || vk > 0xFF || IsModifierKey(vk))
        return;

    if (IsGameKeyCurrentlyDown(keyboard_state, vk))
        g_block_key_until_release[vk] = TRUE;

    ClearGameKeyState(keyboard_state, vk);
}

// 对已建立 latch 的冲突键继续清状态，直到游戏状态表显示该键已释放。
static void ApplyLatchedGameKeyBlocks(BYTE *keyboard_state)
{
    int vk;

    if (!keyboard_state)
        return;

    for (vk = 0; vk <= 0xFF; ++vk)
    {
        if (!g_block_key_until_release[vk])
            continue;

        if (IsGameKeyCurrentlyDown(keyboard_state, vk))
            ClearGameKeyState(keyboard_state, vk);
        else
            g_block_key_until_release[vk] = FALSE;
    }
}

// 根据当前配置判断 Ctrl 组合里的主键是否需要阻止游戏继续消费。
static BOOL ShouldBlockCtrlHotkey(int vk)
{
    if (!IsVkDown(VK_CONTROL))
        return FALSE;

    if (g_pk_config.stash_ext_enabled &&
        (vk == g_pk_config.key_stash_swap || vk == g_pk_config.key_inv_swap))
        return TRUE;

    if (g_pk_config.inventory_sort &&
        (vk == g_pk_config.key_inv_sort ||
         vk == g_pk_config.key_stash_sort ||
         vk == g_pk_config.key_inv_sort_current))
        return TRUE;

    if (g_pk_config.enable_gem_stack && vk == g_pk_config.key_switch_gem_stack)
        return TRUE;

    if (g_pk_config.enable_skill_respec && vk == g_pk_config.key_skill_respec)
        return TRUE;

    if (vk == g_pk_config.key_split_stack)
        return TRUE;

    return FALSE;
}

// 汇总所有 plugK 快捷键规则，只清理冲突键，不清理 Alt/Ctrl/Shift 等修饰键。
static void ApplyPlugKInputBlockRules(BYTE *keyboard_state)
{
    int vk;

    if (!keyboard_state)
        return;

    if (g_pk_config.enable_ultimate_hotkey && IsVkDown(VK_MENU))
    {
        BlockGameKeyUntilRelease(keyboard_state, '1');
        BlockGameKeyUntilRelease(keyboard_state, '2');
        BlockGameKeyUntilRelease(keyboard_state, '3');
        BlockGameKeyUntilRelease(keyboard_state, '4');
        BlockGameKeyUntilRelease(keyboard_state, VK_NUMPAD1);
        BlockGameKeyUntilRelease(keyboard_state, VK_NUMPAD2);
        BlockGameKeyUntilRelease(keyboard_state, VK_NUMPAD3);
        BlockGameKeyUntilRelease(keyboard_state, VK_NUMPAD4);
    }

    for (vk = 0; vk <= 0xFF; ++vk)
    {
        if (ShouldBlockCtrlHotkey(vk))
            BlockGameKeyUntilRelease(keyboard_state, vk);
    }

    ApplyLatchedGameKeyBlocks(keyboard_state);
}

// 游戏完成 GetKeyboardState 后再修正状态表，兼容窗口消息被吞但游戏轮询仍能看到按键的情况。
static BOOL __fastcall Detour_UpdateKeyboardState(BYTE *keyboard_state, void *_edx)
{
    BOOL result = Original_UpdateKeyboardState(keyboard_state, _edx);
    ApplyPlugKInputBlockRules(keyboard_state);
    return result;
}

// 按版本安装共享的键盘状态刷新 Hook；1.05 和 2.01 的结构一致，只是函数地址不同。
static void InitInputBlockerHook(int game_version)
{
    LPVOID target = NULL;

    if (game_version == VER_105)
        target = (LPVOID)ADDR_UPDATE_KEYBOARD_STATE_105;
    else if (game_version == VER_201)
        target = (LPVOID)ADDR_UPDATE_KEYBOARD_STATE_201;

    if (!target)
        return;

    if (MH_CreateHook(target, &Detour_UpdateKeyboardState, (LPVOID *)&Original_UpdateKeyboardState) != MH_OK)
        return;

    MH_EnableHook(target);
}

// 自定义消息处理函数
LRESULT CALLBACK PlugK_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // Alt+1..4 与游戏原有数字键吃药冲突，只吞数字键消息；Alt 本身继续交给游戏。
    if (g_pk_config.enable_ultimate_hotkey && (uMsg == WM_KEYDOWN || uMsg == WM_SYSKEYDOWN))
    {
        if (IsAltDownForMessage(uMsg, lParam))
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
    // 对应的数字键释放和系统字符消息也吞掉，避免游戏继续收到数字键并触发回复药快捷键。
    else if (g_pk_config.enable_ultimate_hotkey && (uMsg == WM_KEYUP || uMsg == WM_SYSKEYUP || uMsg == WM_SYSCHAR))
    {
        int slot = GetUltimateHotkeySlot(wParam);
        if (slot >= 0 && (g_swallow_ultimate_keyup[slot] || IsAltDownForMessage(uMsg, lParam)))
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
            /*
            else if (key == g_pk_config.key_switch_show_item_name)
            {
                ToggleShowItemNameSwitch();
                handled = TRUE;
            }
            */
            else if (key == g_pk_config.key_skill_respec)
            {
                if (g_pk_config.enable_skill_respec)
                {
                    ExecuteSkillRespecFlow();
                    handled = TRUE;
                }
            }
            else if (key == g_pk_config.key_split_stack)
            {
                ExecuteFirstInventoryItemSplitFlow();
                handled = TRUE;
            }

            // ... 其他按键 (StashSort, StackToggle) ...

            // 如果是我们处理的快捷键，不传递冲突键，防止游戏误触；Ctrl 本身继续交给游戏。
            if (handled)
            {
                // MessageBeep(MB_OK); // 可以加个反馈
                return 0; // 吞掉消息，游戏键盘状态表会在输入屏蔽 hook 中同步清理。
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

void Mod_Input_Mgr_Init(int game_version)
{
    InitInputBlockerHook(game_version);

    // 启动一个临时线程去 Hook 窗口，因为 DLL 加载时窗口可能还没创建
    CreateThread(NULL, 0, HookWindowThread, NULL, 0, NULL);
}
