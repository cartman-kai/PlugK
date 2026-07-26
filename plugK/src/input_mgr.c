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
#include "auto_pickup.h"
#include <MinHook.h>
#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

#define VER_105 105
#define VER_201 201

// 1.05 / 2.01 的键盘状态刷新函数；函数内会维护上一帧状态并调用 GetKeyboardState。
#define ADDR_UPDATE_KEYBOARD_STATE_105 0x004CE3D0
#define ADDR_UPDATE_KEYBOARD_STATE_201 0x004E3280

// 快捷栏槽位执行函数。Alt+1..4 的原版吃药冲突在这里按槽位过滤，避免每帧改写数字键状态。
#define ADDR_QUICK_SLOT_EXEC_105 0x004C3CA0
#define ADDR_QUICK_SLOT_EXEC_201 0x004D8260

// 键盘分发调用快捷栏执行后的返回地址。1.05 已用 IDA 静态确认；2.01 按同构函数迁移。
#define RET_QUICK_SLOT_KEYBOARD_105 0x004C45DA
#define RET_QUICK_SLOT_KEYBOARD_201 0x004D8BBA

// 游戏键盘状态对象中，当前帧 VK 状态和上一帧/已按下标记的偏移。
#define GAME_KEY_CURRENT_OFFSET 0x08
#define GAME_KEY_PREVIOUS_OFFSET 0x108

static WNDPROC g_OriginalWndProc = NULL;
static HWND g_hGameWindow = NULL;
static BOOL g_swallow_ultimate_keyup[4] = {FALSE, FALSE, FALSE, FALSE};
// 组合键处理后持续屏蔽冲突键，直到玩家物理松开该键，避免松开 Alt/Ctrl 后补触发游戏逻辑。
static BOOL g_block_key_until_release[256] = {FALSE};
static BYTE g_latched_keys[256] = {0};
static int g_latched_key_count = 0;
static BOOL g_input_frame_alt_down = FALSE;
static BOOL g_input_hotkeys_available = FALSE;
static BOOL g_game_hotkey_latched[256] = {FALSE};

typedef BOOL(__fastcall *tUpdateKeyboardState)(BYTE *keyboard_state, void *_edx);
typedef int(__fastcall *tQuickSlotExec)(void *quick_slot_mgr, void *_edx, int slot);

static tUpdateKeyboardState Original_UpdateKeyboardState = NULL;
static tQuickSlotExec Original_QuickSlotExec = NULL;
static DWORD g_quick_slot_keyboard_return = 0;

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

static BOOL IsAnyGameKeyDown(BYTE *keyboard_state, int vk, int left_vk, int right_vk)
{
    return IsGameKeyCurrentlyDown(keyboard_state, vk) ||
           IsGameKeyCurrentlyDown(keyboard_state, left_vk) ||
           IsGameKeyCurrentlyDown(keyboard_state, right_vk);
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
    {
        if (!g_block_key_until_release[vk] && g_latched_key_count < 256)
            g_latched_keys[g_latched_key_count++] = (BYTE)vk;
        g_block_key_until_release[vk] = TRUE;
    }

    ClearGameKeyState(keyboard_state, vk);
}

// 对已建立 latch 的冲突键继续清状态，直到游戏状态表显示该键已释放。
static void ApplyLatchedGameKeyBlocks(BYTE *keyboard_state)
{
    int i;

    if (!keyboard_state)
        return;

    for (i = 0; i < g_latched_key_count;)
    {
        int vk = g_latched_keys[i];

        if (!g_block_key_until_release[vk])
        {
            g_latched_keys[i] = g_latched_keys[--g_latched_key_count];
            continue;
        }

        if (IsGameKeyCurrentlyDown(keyboard_state, vk))
        {
            ClearGameKeyState(keyboard_state, vk);
            ++i;
        }
        else
        {
            g_block_key_until_release[vk] = FALSE;
            g_latched_keys[i] = g_latched_keys[--g_latched_key_count];
        }
    }
}

static BOOL IsGameCtrlDown(BYTE *keyboard_state)
{
    return IsAnyGameKeyDown(keyboard_state, VK_CONTROL, VK_LCONTROL, VK_RCONTROL);
}

static BOOL IsGameShiftDown(BYTE *keyboard_state)
{
    return IsAnyGameKeyDown(keyboard_state, VK_SHIFT, VK_LSHIFT, VK_RSHIFT);
}

static BOOL IsGameAltDown(BYTE *keyboard_state)
{
    return IsAnyGameKeyDown(keyboard_state, VK_MENU, VK_LMENU, VK_RMENU);
}

static BOOL ConsumeGameHotkeyPress(BYTE *keyboard_state, int vk)
{
    if (!keyboard_state || vk < 0 || vk > 0xFF)
        return FALSE;

    if (!IsGameKeyCurrentlyDown(keyboard_state, vk))
    {
        g_game_hotkey_latched[vk] = FALSE;
        return FALSE;
    }

    if (g_game_hotkey_latched[vk])
        return FALSE;

    g_game_hotkey_latched[vk] = TRUE;
    return TRUE;
}

static void ReleaseGameHotkeyLatch(BYTE *keyboard_state, int vk)
{
    if (!keyboard_state || vk < 0 || vk > 0xFF)
        return;

    if (!IsGameKeyCurrentlyDown(keyboard_state, vk))
        g_game_hotkey_latched[vk] = FALSE;
}

static void HandleCtrlGameHotkey(BYTE *keyboard_state, int vk, void (*handler)(void))
{
    if (!handler)
        return;

    if (ConsumeGameHotkeyPress(keyboard_state, vk))
        handler();
}

static void HandlePlugKHotkeysFromGameState(BYTE *keyboard_state)
{
    int i;

    if (!keyboard_state)
        return;

    if (IsGameKeyCurrentlyDown(keyboard_state, 'Z'))
    {
        if (IsGameCtrlDown(keyboard_state) && ConsumeGameHotkeyPress(keyboard_state, 'Z'))
            AutoPickup_Toggle();
        else if (IsGameShiftDown(keyboard_state) && ConsumeGameHotkeyPress(keyboard_state, 'Z'))
            AutoPickup_CycleMode();
    }
    else
    {
        g_game_hotkey_latched['Z'] = FALSE;
    }

    if (g_pk_config.enable_ultimate_hotkey && IsGameAltDown(keyboard_state))
    {
        for (i = 0; i < 4; ++i)
        {
            int vk = '1' + i;
            if (ConsumeGameHotkeyPress(keyboard_state, vk))
                ExecuteUltimateHotkeySlot(i);
        }
    }
    else
    {
        for (i = 0; i < 4; ++i)
            ReleaseGameHotkeyLatch(keyboard_state, '1' + i);
    }

    if (IsGameCtrlDown(keyboard_state))
    {
        if (g_pk_config.stash_ext_enabled)
        {
            HandleCtrlGameHotkey(keyboard_state, g_pk_config.key_stash_swap, ToggleStash);
            HandleCtrlGameHotkey(keyboard_state, g_pk_config.key_inv_swap, ToggleInventory);
        }

        if (g_pk_config.inventory_sort)
        {
            HandleCtrlGameHotkey(keyboard_state, g_pk_config.key_inv_sort, ExecuteInventorySortFlow);
            HandleCtrlGameHotkey(keyboard_state, g_pk_config.key_stash_sort, ExecuteStashSortFlow);
            HandleCtrlGameHotkey(keyboard_state, g_pk_config.key_inv_sort_current, ExecuteCurrentInventorySortFlow);
        }

        if (g_pk_config.enable_gem_stack && ConsumeGameHotkeyPress(keyboard_state, g_pk_config.key_switch_gem_stack))
        {
            ToggleChangeGemStackProp();
            ToggleItemStackState();
        }

        if (g_pk_config.enable_skill_respec)
            HandleCtrlGameHotkey(keyboard_state, g_pk_config.key_skill_respec, ExecuteSkillRespecFlow);

        HandleCtrlGameHotkey(keyboard_state, g_pk_config.key_split_stack, ExecuteFirstInventoryItemSplitFlow);
    }
    else
    {
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_stash_swap);
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_inv_swap);
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_inv_sort);
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_stash_sort);
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_inv_sort_current);
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_switch_gem_stack);
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_skill_respec);
        ReleaseGameHotkeyLatch(keyboard_state, g_pk_config.key_split_stack);
    }
}
static void BlockConfiguredCtrlHotkeys(BYTE *keyboard_state)
{
    if (!IsGameCtrlDown(keyboard_state))
        return;

    BlockGameKeyUntilRelease(keyboard_state, 'Z');

    if (g_pk_config.stash_ext_enabled)
    {
        BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_stash_swap);
        BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_inv_swap);
    }

    if (g_pk_config.inventory_sort)
    {
        BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_inv_sort);
        BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_stash_sort);
        BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_inv_sort_current);
    }

    if (g_pk_config.enable_gem_stack)
        BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_switch_gem_stack);

    if (g_pk_config.enable_skill_respec)
        BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_skill_respec);

    BlockGameKeyUntilRelease(keyboard_state, g_pk_config.key_split_stack);
}

static BOOL ShouldBlockAutoPickupZHotkey(BYTE *keyboard_state)
{
    if (!IsGameKeyCurrentlyDown(keyboard_state, 'Z'))
        return FALSE;

    return IsGameCtrlDown(keyboard_state) ||
           IsAnyGameKeyDown(keyboard_state, VK_SHIFT, VK_LSHIFT, VK_RSHIFT);
}

// 汇总所有 plugK 快捷键规则，只清理冲突键，不清理 Alt/Ctrl/Shift 等修饰键。
static void ApplyPlugKInputBlockRules(BYTE *keyboard_state)
{
    if (!keyboard_state)
        return;

    g_input_frame_alt_down = IsAnyGameKeyDown(keyboard_state, VK_MENU, VK_LMENU, VK_RMENU);

    BlockConfiguredCtrlHotkeys(keyboard_state);

    if (ShouldBlockAutoPickupZHotkey(keyboard_state))
        BlockGameKeyUntilRelease(keyboard_state, 'Z');

    ApplyLatchedGameKeyBlocks(keyboard_state);
}

// 游戏完成 GetKeyboardState 后再修正状态表，兼容窗口消息被吞但游戏轮询仍能看到按键的情况。
static BOOL __fastcall Detour_UpdateKeyboardState(BYTE *keyboard_state, void *_edx)
{
    BOOL result = Original_UpdateKeyboardState(keyboard_state, _edx);
    HandlePlugKHotkeysFromGameState(keyboard_state);
    ApplyPlugKInputBlockRules(keyboard_state);
    AutoPickup_OnInputFrame();
    return result;
}

static BOOL ShouldBlockQuickSlotByAlt(int slot, DWORD return_address)
{
    if (!g_pk_config.enable_ultimate_hotkey)
        return FALSE;

    if (slot < 0 || slot >= 4)
        return FALSE;

    if (g_quick_slot_keyboard_return && return_address != g_quick_slot_keyboard_return)
        return FALSE;

    return g_input_frame_alt_down;
}

// 阻止 Alt+1..4 继续触发原版快捷栏 0..3；WndProc 仍负责执行 plugK 必杀技。
static int __fastcall Detour_QuickSlotExec(void *quick_slot_mgr, void *_edx, int slot)
{
    DWORD return_address = (DWORD)_ReturnAddress();

    if (ShouldBlockQuickSlotByAlt(slot, return_address))
        return 0;

    return Original_QuickSlotExec(quick_slot_mgr, _edx, slot);
}

static void InitQuickSlotBlockerHook(int game_version)
{
    LPVOID target = NULL;

    if (game_version == VER_105)
    {
        target = (LPVOID)ADDR_QUICK_SLOT_EXEC_105;
        g_quick_slot_keyboard_return = RET_QUICK_SLOT_KEYBOARD_105;
    }
    else if (game_version == VER_201)
    {
        target = (LPVOID)ADDR_QUICK_SLOT_EXEC_201;
        g_quick_slot_keyboard_return = RET_QUICK_SLOT_KEYBOARD_201;
    }

    if (!target)
        return;

    if (MH_CreateHook(target, &Detour_QuickSlotExec, (LPVOID *)&Original_QuickSlotExec) != MH_OK)
        return;

    MH_EnableHook(target);
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

    if (MH_EnableHook(target) == MH_OK)
        g_input_hotkeys_available = TRUE;
}

// 自定义消息处理函数
LRESULT CALLBACK PlugK_WndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (g_input_hotkeys_available)
        return CallWindowProc(g_OriginalWndProc, hwnd, uMsg, wParam, lParam);

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
        int key = (int)wParam;
        BOOL key_repeat = (lParam & 0x40000000) != 0;

        if (key == 'Z' && !key_repeat)
        {
            if (GetKeyState(VK_CONTROL) & 0x8000)
            {
                AutoPickup_Toggle();
                return 0;
            }
            if (GetKeyState(VK_SHIFT) & 0x8000)
            {
                AutoPickup_CycleMode();
                return 0;
            }
        }

        // 检查 Ctrl 是否按下
        if (GetKeyState(VK_CONTROL) & 0x8000)
        {
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

// 窗口搜索上下文：只记录当前进程 PID 和最终选中的游戏主窗口。
typedef struct WindowSearchContext
{
    DWORD process_id;
    HWND selected;
    HWND fallback;
} WindowSearchContext;

// Steam 版启动视频和 DirectShow 会创建额外窗口，这些都不能安装 PlugK_WndProc。
static BOOL IsBlockedWindowClassOrTitle(const char *class_name, const char *title)
{
    return lstrcmpiA(class_name, "VideoWindow") == 0 ||
           lstrcmpiA(class_name, "VideoRenderer") == 0 ||
           lstrcmpiA(class_name, "FilterGraphWindow") == 0 ||
           lstrcmpiA(title, "ActiveMovie Window") == 0;
}

// 枚举顶层窗口时先按 PID 过滤，再匹配已确认的游戏主窗口，避免拦截到其它进程或视频窗口。
static BOOL CALLBACK EnumCurrentProcessWindows(HWND hwnd, LPARAM lParam)
{
    WindowSearchContext *ctx = (WindowSearchContext *)lParam;
    DWORD window_process_id = 0;
    char class_name[128] = {0};
    char title[256] = {0};

    // 只处理当前 ComeOn.exe 进程创建的窗口，影响范围比全局 FindWindowA 更小。
    GetWindowThreadProcessId(hwnd, &window_process_id);
    if (window_process_id != ctx->process_id)
        return TRUE;

    // 只接受可见且无 owner 的顶层窗口，跳过对话框、隐藏窗口和子窗口。
    if (GetWindow(hwnd, GW_OWNER) || !IsWindowVisible(hwnd))
        return TRUE;

    GetClassNameA(hwnd, class_name, sizeof(class_name));
    GetWindowTextA(hwnd, title, sizeof(title));

    if (IsBlockedWindowClassOrTitle(class_name, title))
        return TRUE;

    // 优先接受已确认的游戏主窗口；否则保留当前 PID 下第一个可见顶层窗口作为 fallback。
    if (lstrcmpiA(title, "daojian") == 0 ||
        lstrcmpiA(class_name, "daojian") == 0 ||
        lstrcmpiA(title, "DaojianServer") == 0)
    {
        ctx->selected = hwnd;
        return FALSE;
    }

    if (!ctx->fallback)
        ctx->fallback = hwnd;

    return TRUE;
}

// 查找当前进程内的游戏主窗口，窗口尚未创建时返回 NULL 交给线程重试。
static HWND FindCurrentProcessGameWindow(void)
{
    WindowSearchContext ctx;

    ZeroMemory(&ctx, sizeof(ctx));
    ctx.process_id = GetCurrentProcessId();
    EnumWindows(EnumCurrentProcessWindows, (LPARAM)&ctx);
    return ctx.selected ? ctx.selected : ctx.fallback;
}

// 寻找游戏窗口并 Hook 的临时线程 (只运行一次)
DWORD WINAPI HookWindowThread(LPVOID lpParam)
{
    int attempts = 0;
    // DLL 注入时窗口可能还没创建，最多等待约 30 秒。
    while (g_hGameWindow == NULL && attempts < 150)
    {
        g_hGameWindow = FindCurrentProcessGameWindow();
        if (g_hGameWindow)
            break;

        Sleep(200);
        attempts++;
    }

    if (g_hGameWindow)
    {
        // 只对子类化选中的游戏主窗口，所有热键消息最终仍可回到原 WndProc。
        g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_hGameWindow, GWLP_WNDPROC, (LONG_PTR)PlugK_WndProc);
    }
    return 0;
}

void Mod_Input_Mgr_Init(int game_version)
{
    InitQuickSlotBlockerHook(game_version);
    InitInputBlockerHook(game_version);

    // 启动一个临时线程去 Hook 窗口，因为 DLL 加载时窗口可能还没创建
    CreateThread(NULL, 0, HookWindowThread, NULL, 0, NULL);
}
