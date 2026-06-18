#include "pch.h"
#include "item.h"
#include "config.h"
#include "enemy_name.h"
#include "show_tips.h"
#include <MinHook.h>
#include <intrin.h>
#include <string.h>

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

typedef void *(__fastcall *tCreateTextNode)(void *pThis, void *_edx, const char *text, int x, int y, int style, int arg6, int arg7);
static tCreateTextNode fpCreateTextNode = NULL;

static PK_DrawTextFn fpDrawText = NULL;

static int g_item_name_game_version = 0;
static char g_pending_item_color = 0;

#define ITEM_COLOR_MARKER '\x1F'

#pragma intrinsic(_ReturnAddress)

// =============================================================
// Hook 处理函数
// =============================================================

static BOOL IsShowItemNameActive()
{
    if (g_pk_config.show_item_name)
        return TRUE;

    if (g_pk_config.hold_show_item_name &&
        (GetAsyncKeyState(g_pk_config.key_hold_show_item_name) & 0x8000))
        return TRUE;

    return FALSE;
}

static BOOL PatchCodeBytes(void *address, const BYTE *bytes, SIZE_T size)
{
    DWORD oldProtect = 0;
    DWORD restoreProtect = 0;

    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return FALSE;

    memcpy(address, bytes, size);
    FlushInstructionCache(GetCurrentProcess(), address, size);
    VirtualProtect(address, size, oldProtect, &restoreProtect);
    return TRUE;
}

static void PatchItemNameTextStyle(int game_version)
{
    const BYTE push1[] = {0x6A, 0x01};

    if (game_version == 105 && g_pk_config.optimize_drop_item_name_color)
    {
        // 1.05: sub_4127D0 writes this arg to text node +0x0C.
        // Runtime verification: value 1 removes the item-name background.
        PatchCodeBytes((void *)0x004128A3, push1, sizeof(push1));
    }
    else if (game_version == 201 && g_pk_config.optimize_drop_item_name_color)
    {
        // 2.01: sub_41A500 writes this arg to text node +0x0C.
        // Static IDA match with 1.05; runtime verification pending.
        PatchCodeBytes((void *)0x0041A5D3, push1, sizeof(push1));
    }
}

static int ReadRecordInt(DWORD record, int column)
{
    int count = 0;
    DWORD values = 0;

    if (record == 0 || IsBadReadPtr((void *)record, 0x0C))
        return 0xFFFF;

    count = *(int *)(record + 0x04);
    if (column < 0 || column >= count)
        return 0xFFFF;

    values = *(DWORD *)(record + 0x08);
    if (values == 0 || IsBadReadPtr((void *)values, sizeof(int) * (column + 1)))
        return 0xFFFF;

    return *(int *)(values + column * 4);
}

static BOOL IsColorizedItemType(int type)
{
    if (type == 30 || type == 50)
        return TRUE;

    if (g_item_name_game_version == 201 && type == 35)
        return TRUE;

    return FALSE;
}

static char GetItemNameColorCode(int quality)
{
    if (quality <= 0 || quality == 0xFFFF)
        return 0;
    if (quality <= 2)
        return '1';
    if (quality <= 4)
        return '2';
    if (quality <= 6)
        return '3';
    if (quality == 7)
        return '4';
    if (quality == 8)
        return '5';
    return '6';
}

static DWORD GetColorRefByCode(char code)
{
    switch (code)
    {
    case '1':
        return RGB(80, 120, 255);
    case '2':
        return RGB(80, 240, 80);
    case '3':
        return RGB(255, 128, 32);
    case '4':
        return RGB(255, 224, 64);
    case '5':
        return RGB(224, 64, 48);
    case '6':
        return RGB(192, 80, 255);
    default:
        return 0xFFFFFFFF;
    }
}

static char GetItemColorFromRecord(DWORD record)
{
    int type = ReadRecordInt(record, 2);
    int quality = ReadRecordInt(record, 8);

    if (!IsColorizedItemType(type))
        return 0;

    return GetItemNameColorCode(quality);
}

static void __fastcall Detour_ShowItemName(void *pThis, void *_edx, int mode)
{
    char previous_color = g_pending_item_color;

    g_pending_item_color = 0;
    if (g_pk_config.optimize_drop_item_name_color &&
        (g_item_name_game_version == 105 || g_item_name_game_version == 201) &&
        pThis)
    {
        DWORD record = 0;
        if (!IsBadReadPtr((BYTE *)pThis + 0x81, sizeof(DWORD)))
        {
            record = *(DWORD *)((BYTE *)pThis + 0x81);
            g_pending_item_color = GetItemColorFromRecord(record);
        }
    }

    fpShowItemName(pThis, _edx, mode);
    g_pending_item_color = previous_color;
}

static void *__fastcall Detour_CreateTextNode(void *pThis, void *_edx, const char *text, int x, int y, int style, int arg6, int arg7)
{
    char colored_text[512];

    if (g_pending_item_color && text && text[0] != ITEM_COLOR_MARKER)
    {
        colored_text[0] = ITEM_COLOR_MARKER;
        colored_text[1] = g_pending_item_color;
        strncpy_s(colored_text + 2, sizeof(colored_text) - 2, text, _TRUNCATE);
        return fpCreateTextNode(pThis, _edx, colored_text, x, y, style, arg6, arg7);
    }

    return fpCreateTextNode(pThis, _edx, text, x, y, style, arg6, arg7);
}

static int __fastcall Detour_DrawText(void *pThis, void *_edx, int surface, int x, int y, const char *text, int centered)
{
    void *caller = _ReturnAddress();
    DWORD old_color = 0;
    DWORD color = 0;
    int result = 0;

    if (text && text[0] == ITEM_COLOR_MARKER)
    {
        color = GetColorRefByCode(text[1]);
        if (color != 0xFFFFFFFF && !IsBadReadPtr((BYTE *)pThis + 0x60, sizeof(DWORD)))
        {
            old_color = *(DWORD *)((BYTE *)pThis + 0x60);
            *(DWORD *)((BYTE *)pThis + 0x60) = color;
            result = fpDrawText(pThis, _edx, surface, x, y, text + 2, centered);
            *(DWORD *)((BYTE *)pThis + 0x60) = old_color;
            EnemyName_AfterDrawText(caller, fpDrawText, pThis, _edx, surface, x, y, text + 2, centered);
            return result;
        }

        result = fpDrawText(pThis, _edx, surface, x, y, text + 2, centered);
        EnemyName_AfterDrawText(caller, fpDrawText, pThis, _edx, surface, x, y, text + 2, centered);
        return result;
    }

    result = fpDrawText(pThis, _edx, surface, x, y, text, centered);
    EnemyName_AfterDrawText(caller, fpDrawText, pThis, _edx, surface, x, y, text, centered);
    return result;
}

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
    if (IsShowItemNameActive())
    {
        // 3. 强制调用显示名称
        // 传入 pThis，参数 1 (对应汇编中的 push 1)
        if (fpShowItemName && pThis)
        {
            Detour_ShowItemName(pThis, NULL, 1);
        }
    }
}

void ToggleShowItemNameSwitch()
{
    if (g_pk_config.show_item_name)
    {
        g_pk_config.show_item_name = 0;
        SendGameTips("[提示]关闭地面物品名称显示");
        return;
    }
    g_pk_config.show_item_name = 1;
    SendGameTips("[提示]开启地面物品名称显示");
    return;
}

// =============================================================
// 初始化
// =============================================================

void Mod_Show_Item_Name_Init(int game_version)
{
    void *targetAddr_Update = NULL;
    void *targetAddr_ShowName = NULL;
    void *targetAddr_CreateTextNode = NULL;
    void *targetAddr_DrawText = NULL;

    g_item_name_game_version = game_version;
    // ---------------------------------------------------------
    // 针对 2.01 版本的地址配置
    // ---------------------------------------------------------
    if (game_version == 201)
    {
        // 原始更新函数地址 (MinHook 目标)
        targetAddr_Update = (void *)0x0041A860;

        // 显示名称函数地址 (直接调用)
        targetAddr_ShowName = (void *)0x0041A500;
        fpShowItemName = (tShowItemName)targetAddr_ShowName;

        targetAddr_CreateTextNode = (void *)0x004E1270;
        targetAddr_DrawText = (void *)0x004C58F0;
    }
    // ---------------------------------------------------------
    // 针对 1.05 版本的地址配置 [修复点]
    // ---------------------------------------------------------
    else if (game_version == 105)
    {
        // 根据你的调研结果: 00412B30
        targetAddr_Update = (void *)0x00412B30;

        // 根据你的调研结果: 004127D0
        targetAddr_ShowName = (void *)0x004127D0;
        fpShowItemName = (tShowItemName)targetAddr_ShowName;

        targetAddr_CreateTextNode = (void *)0x004CC4B0;
        targetAddr_DrawText = (void *)0x004B25A0;
    }
    // ---------------------------------------------------------
    // 其他版本不支持
    // ---------------------------------------------------------
    else
    {
        return;
    }

    PatchItemNameTextStyle(game_version);

    if (g_pk_config.optimize_drop_item_name_color)
    {
        if (MH_CreateHook(targetAddr_ShowName, &Detour_ShowItemName, (LPVOID *)&fpShowItemName) == MH_OK)
            MH_EnableHook(targetAddr_ShowName);

        if (MH_CreateHook(targetAddr_CreateTextNode, &Detour_CreateTextNode, (LPVOID *)&fpCreateTextNode) == MH_OK)
            MH_EnableHook(targetAddr_CreateTextNode);
    }

    if (g_pk_config.optimize_drop_item_name_color || g_pk_config.show_enemy_hp)
    {
        if (MH_CreateHook(targetAddr_DrawText, &Detour_DrawText, (LPVOID *)&fpDrawText) == MH_OK)
            MH_EnableHook(targetAddr_DrawText);
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
