#include "pch.h"
#include "ultimate_hotkey.h"
#include "config.h"
#include "show_tips.h"
#include <windows.h>
#include <string.h>
#include <MinHook.h>

// 当前已完成静态/动态验证的游戏版本。
#define VER_105 105
#define VER_201 201

// ---- 正传 v1.05 地址 ----
#define ADDR_RELEASE_METHOD_105 0x004752C0
#define ADDR_RIGHT_CLICK_RELEASE_105 0x004743D0
#define ADDR_GET_PLAYER_105 0x004892D0
#define ADDR_PLAYER_MGR_105 0x005585C0

// ---- 外传 v2.01 地址 ----
#define ADDR_RELEASE_METHOD_201 0x00483B40
#define ADDR_RIGHT_CLICK_RELEASE_201 0x00482C50
#define ADDR_GET_PLAYER_201 0x004987F0
#define ADDR_PLAYER_MGR_201 0x00589540

// 角色技能链表字段。v1.05 与 v2.01 已在洗技能功能中共用验证。
#define PLAYER_SKILL_HEAD_OFFSET 0x364

// 技能链节点字段：node+0 为 next，node+8 为 skill_id。
#define SKILL_NODE_NEXT_OFFSET 0x0
#define SKILL_NODE_ID_OFFSET 0x8

// 每个角色固定四个必杀技槽位，对应 Alt+1 到 Alt+4。
#define ULTIMATE_SLOT_COUNT 4
#define MAX_SKILL_CHAIN_NODES 256

typedef int(__fastcall *tGetPlayer)(void *pThis, void *_edx);

// 原函数是 thiscall。用 __fastcall 包一层可让第一个参数进入 ECX，第二个 dummy 进入 EDX。
typedef int(__fastcall *tReleaseMethod)(void *controller, void *_edx, int method_id, int flag, void *target);

// 右键释放路径是 thiscall 且 ret 0x0C；detour 使用 __fastcall 接住 ECX 和 3 个栈参数。
typedef int(__fastcall *tRightClickRelease)(void *controller, void *_edx, int arg1, int arg2, int arg3);

typedef struct UltimateRoleMap
{
    int role_id;
    int skill_ids[ULTIMATE_SLOT_COUNT];
    int method_ids[ULTIMATE_SLOT_COUNT];
} UltimateRoleMap;

typedef struct UltimateHotkeyAddressConfig
{
    DWORD release_method;
    DWORD right_click_release;
    DWORD get_player;
    DWORD player_mgr;
} UltimateHotkeyAddressConfig;

// 1.05：role_id 仅用于维护数据含义；运行时按 skill_id 判定当前角色。
static const UltimateRoleMap g_role_maps_105[] = {
    {4, {1, 2, 3, 4}, {285, 45, 46, 268}},
    {30, {43, 44, 45, 46}, {348, 349, 157, 158}},
    {40, {85, 86, 87, 88}, {147, 215, 225, 219}},
};

// 2.01 在 1.05 三个角色基础上新增两个角色，每个角色仍为四个必杀技。
static const UltimateRoleMap g_role_maps_201[] = {
    {4, {1, 2, 3, 4}, {285, 45, 46, 268}},
    {30, {43, 44, 45, 46}, {348, 349, 157, 158}},
    {40, {85, 86, 87, 88}, {147, 215, 225, 219}},
    {223, {127, 128, 129, 130}, {613, 617, 623, 631}},
    {1, {169, 170, 171, 172}, {650, 654, 658, 662}},
};

static int g_enabled = 0;
static int g_game_version = 0;
static void *g_cached_controller = NULL;
static void *g_player_mgr = NULL;
static tGetPlayer Game_GetPlayer = NULL;
static tReleaseMethod Game_ReleaseMethod = NULL;
static tRightClickRelease Original_RightClickRelease = NULL;

static int LoadAddressConfig(int game_version, UltimateHotkeyAddressConfig *config)
{
    if (!config)
        return 0;

    memset(config, 0, sizeof(*config));

    if (game_version == VER_105)
    {
        config->release_method = ADDR_RELEASE_METHOD_105;
        config->right_click_release = ADDR_RIGHT_CLICK_RELEASE_105;
        config->get_player = ADDR_GET_PLAYER_105;
        config->player_mgr = ADDR_PLAYER_MGR_105;
    }
    else if (game_version == VER_201)
    {
        config->release_method = ADDR_RELEASE_METHOD_201;
        config->right_click_release = ADDR_RIGHT_CLICK_RELEASE_201;
        config->get_player = ADDR_GET_PLAYER_201;
        config->player_mgr = ADDR_PLAYER_MGR_201;
    }
    else
    {
        return 0;
    }

    return config->release_method &&
           config->right_click_release &&
           config->get_player &&
           config->player_mgr;
}

static DWORD ReadDword(DWORD address)
{
    if (IsBadReadPtr((void *)address, sizeof(DWORD)))
        return 0;
    return *(DWORD *)address;
}

static int PlayerHasSkill(DWORD player, int skill_id)
{
    DWORD node;
    int guard = 0;

    if (!player || skill_id <= 0)
        return 0;

    node = ReadDword(player + PLAYER_SKILL_HEAD_OFFSET);
    while (node && guard++ < MAX_SKILL_CHAIN_NODES)
    {
        int current_skill_id = (int)ReadDword(node + SKILL_NODE_ID_OFFSET);
        if (current_skill_id == skill_id)
            return 1;

        node = ReadDword(node + SKILL_NODE_NEXT_OFFSET);
    }

    return 0;
}

static const UltimateRoleMap *GetRoleMaps(int *count)
{
    if (!count)
        return NULL;

    if (g_game_version == VER_105)
    {
        *count = sizeof(g_role_maps_105) / sizeof(g_role_maps_105[0]);
        return g_role_maps_105;
    }

    if (g_game_version == VER_201)
    {
        *count = sizeof(g_role_maps_201) / sizeof(g_role_maps_201[0]);
        return g_role_maps_201;
    }

    *count = 0;
    return NULL;
}

static int FindUltimateMethodForSlot(DWORD player, int slot_index, int *skill_id, int *method_id)
{
    int i;
    int map_count = 0;
    const UltimateRoleMap *maps = GetRoleMaps(&map_count);

    if (!maps || slot_index < 0 || slot_index >= ULTIMATE_SLOT_COUNT)
        return 0;

    for (i = 0; i < map_count; ++i)
    {
        int candidate_skill_id = maps[i].skill_ids[slot_index];
        if (PlayerHasSkill(player, candidate_skill_id))
        {
            if (skill_id)
                *skill_id = candidate_skill_id;
            if (method_id)
                *method_id = maps[i].method_ids[slot_index];
            return 1;
        }
    }

    return 0;
}

static int __fastcall Detour_RightClickRelease(void *controller, void *_edx, int arg1, int arg2, int arg3)
{
    // 释放入口需要原 this 指针。右键路径稳定持有该 controller，缓存后供 Alt 快捷键复用。
    if (controller)
        g_cached_controller = controller;

    return Original_RightClickRelease(controller, _edx, arg1, arg2, arg3);
}

void ExecuteUltimateHotkeySlot(int slot_index)
{
    DWORD player;
    int skill_id = 0;
    int method_id = 0;

    if (!g_enabled || !g_pk_config.enable_ultimate_hotkey)
        return;

    if (slot_index < 0 || slot_index >= ULTIMATE_SLOT_COUNT)
        return;

    if (!g_cached_controller)
    {
        SendGameTips("[提示]必杀技快捷键未就绪：请先进入战斗并点击一次右键");
        return;
    }

    if (!Game_GetPlayer || !Game_ReleaseMethod)
        return;

    player = (DWORD)Game_GetPlayer(g_player_mgr, NULL);
    if (!player)
    {
        SendGameTips("[提示]必杀技释放失败：未找到角色");
        return;
    }

    if (!FindUltimateMethodForSlot(player, slot_index, &skill_id, &method_id))
    {
        SendGameTips("[提示]未学习对应必杀技");
        return;
    }

    // target 传 NULL，交给原释放函数使用 controller+0x3C 的上下文兜底。
    Game_ReleaseMethod(g_cached_controller, NULL, method_id, 1, NULL);
}

void Mod_Ultimate_Hotkey_Init(int game_version)
{
    UltimateHotkeyAddressConfig config;

    if (!g_pk_config.enable_ultimate_hotkey)
        return;

    if (!LoadAddressConfig(game_version, &config))
        return;

    Game_GetPlayer = (tGetPlayer)config.get_player;
    Game_ReleaseMethod = (tReleaseMethod)config.release_method;
    g_player_mgr = (void *)config.player_mgr;
    g_game_version = game_version;

    if (MH_CreateHook((LPVOID)config.right_click_release,
                      &Detour_RightClickRelease,
                      (LPVOID *)&Original_RightClickRelease) != MH_OK)
        return;

    if (MH_EnableHook((LPVOID)config.right_click_release) != MH_OK)
        return;

    g_enabled = 1;
}
