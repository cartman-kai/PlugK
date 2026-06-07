#include "pch.h"
#include "skill_respec.h"
#include "config.h"
#include "show_tips.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

// 当前已完成动态验证：正传 v1.05 与外传 v2.01。
#define VER_105 105
#define VER_201 201

// ---- 正传 v1.05 地址 ----
#define ADDR_GET_PLAYER_105 0x004892D0
#define ADDR_PLAYER_MGR_105 0x005585C0
#define ADDR_SKILL_TABLE_105 0x00548C10
#define ADDR_TABLE_FIND_FIRST_105 0x004D09A0
#define ADDR_TABLE_FIND_NEXT_105 0x004D09C0
#define ADDR_TABLE_GET_INT_105 0x004D0210
#define ADDR_REMOVE_SKILL_105 0x00481C60
#define ADDR_ADD_RAGE_MAX_105 0x00480D70

// ---- 外传 v2.01 地址 ----
#define ADDR_GET_PLAYER_201 0x004987F0
#define ADDR_PLAYER_MGR_201 0x00589540
#define ADDR_SKILL_TABLE_201 0x005791C8
#define ADDR_TABLE_FIND_FIRST_201 0x004E5690
#define ADDR_TABLE_FIND_NEXT_201 0x004E56B0
#define ADDR_TABLE_GET_INT_201 0x004E4EF0
#define ADDR_REMOVE_SKILL_201 0x00490A90
#define ADDR_ADD_RAGE_MAX_201 0x0048FBA0

// 角色对象内存字段。v1.05 与 v2.01 已动态验证一致。
#define PLAYER_SKILL_POINTS_OFFSET 0x34C
#define PLAYER_SKILL_COUNT_OFFSET 0x360
#define PLAYER_SKILL_HEAD_OFFSET 0x364
#define PLAYER_PASSIVE_RATE_OFFSET 0x2C8

// skill.txt 表列号。游戏内部表函数按这些列读取技能 ID、系号、组号、等级和消耗。
#define SKILL_COL_ID 1
#define SKILL_COL_SERIES 3
#define SKILL_COL_GROUP 4
#define SKILL_COL_LEVEL 5
#define SKILL_COL_COST 6

#define MAX_SKILLS_TO_RESET 128

typedef int(__fastcall *tGetPlayer)(void *pThis, void *_edx);
typedef int(__fastcall *tTableFindFirst)(void *pThis, void *_edx, int key, int column, int start);
typedef int(__fastcall *tTableFindNext)(void *pThis, void *_edx, int key, int column);
typedef int(__fastcall *tTableGetInt)(void *pThis, void *_edx, int column);
typedef int(__fastcall *tRemoveSkill)(void *pThis, void *_edx, int skill_id);
typedef void(__fastcall *tAddRageMax)(void *pThis, void *_edx, int amount);

static int g_enabled = 0;
static tGetPlayer Game_GetPlayer = NULL;
static tTableFindFirst Game_TableFindFirst = NULL;
static tTableFindNext Game_TableFindNext = NULL;
static tTableGetInt Game_TableGetInt = NULL;
static tRemoveSkill Game_RemoveSkill = NULL;
static tAddRageMax Game_AddRageMax = NULL;
static void *g_player_mgr = NULL;
static void *g_skill_table = NULL;

typedef struct SkillRespecAddressConfig
{
    DWORD get_player;
    DWORD player_mgr;
    DWORD skill_table;
    DWORD table_find_first;
    DWORD table_find_next;
    DWORD table_get_int;
    DWORD remove_skill;
    DWORD add_rage_max;
} SkillRespecAddressConfig;

static int LoadAddressConfig(int game_version, SkillRespecAddressConfig *config)
{
    if (!config)
        return 0;

    memset(config, 0, sizeof(*config));

    if (game_version == VER_105)
    {
        config->get_player = ADDR_GET_PLAYER_105;
        config->player_mgr = ADDR_PLAYER_MGR_105;
        config->skill_table = ADDR_SKILL_TABLE_105;
        config->table_find_first = ADDR_TABLE_FIND_FIRST_105;
        config->table_find_next = ADDR_TABLE_FIND_NEXT_105;
        config->table_get_int = ADDR_TABLE_GET_INT_105;
        config->remove_skill = ADDR_REMOVE_SKILL_105;
        config->add_rage_max = ADDR_ADD_RAGE_MAX_105;
    }
    else if (game_version == VER_201)
    {
        config->get_player = ADDR_GET_PLAYER_201;
        config->player_mgr = ADDR_PLAYER_MGR_201;
        config->skill_table = ADDR_SKILL_TABLE_201;
        config->table_find_first = ADDR_TABLE_FIND_FIRST_201;
        config->table_find_next = ADDR_TABLE_FIND_NEXT_201;
        config->table_get_int = ADDR_TABLE_GET_INT_201;
        config->remove_skill = ADDR_REMOVE_SKILL_201;
        config->add_rage_max = ADDR_ADD_RAGE_MAX_201;
    }
    else
    {
        return 0;
    }

    return config->get_player &&
           config->player_mgr &&
           config->skill_table &&
           config->table_find_first &&
           config->table_find_next &&
           config->table_get_int &&
           config->remove_skill &&
           config->add_rage_max;
}

static DWORD ReadDword(DWORD address)
{
    if (IsBadReadPtr((void *)address, sizeof(DWORD)))
        return 0;
    return *(DWORD *)address;
}

static int GetSkillField(int skill_record, int column)
{
    if (!skill_record || !Game_TableGetInt)
        return 0;
    return Game_TableGetInt((void *)skill_record, NULL, column);
}

static int FindSkillRecordById(int skill_id)
{
    if (!Game_TableFindFirst || !g_skill_table)
        return 0;
    return Game_TableFindFirst(g_skill_table, NULL, skill_id, SKILL_COL_ID, 0);
}

static int IsInitialSkill(int skill_record)
{
    // 主角初始技能来自 Init.txt，进入角色技能链表后仍可通过技能表识别：
    // 系号为 0 且组号为 -1 的技能应保留，不参与洗点。
    return GetSkillField(skill_record, SKILL_COL_SERIES) == 0 &&
           GetSkillField(skill_record, SKILL_COL_GROUP) == -1;
}

static int CalcRefundCost(int series, int group, int current_level)
{
    int total = 0;
    int record;

    if (!Game_TableFindFirst || !Game_TableFindNext || !g_skill_table)
        return 0;

    record = Game_TableFindFirst(g_skill_table, NULL, group, SKILL_COL_GROUP, 0);
    while (record)
    {
        int record_series = GetSkillField(record, SKILL_COL_SERIES);
        int level = GetSkillField(record, SKILL_COL_LEVEL);
        int cost = GetSkillField(record, SKILL_COL_COST);

        if (record_series == series && level > 0 && level <= current_level && cost > 0)
            total += cost;

        // 按组号遍历同一组技能，把当前等级及以下所有等级的消耗累加返还。
        record = Game_TableFindNext(g_skill_table, NULL, group, SKILL_COL_GROUP);
    }

    return total;
}

static int CollectResetSkills(DWORD player, int *skill_ids, int max_count)
{
    DWORD node = ReadDword(player + PLAYER_SKILL_HEAD_OFFSET);
    int count = 0;

    // 先收集 ID，再删除。直接遍历时删除链表节点会破坏 next/prev，容易跳过节点。
    while (node && count < max_count)
    {
        DWORD next = ReadDword(node);
        int skill_id = (int)ReadDword(node + 8);
        int record = FindSkillRecordById(skill_id);

        if (record && !IsInitialSkill(record))
            skill_ids[count++] = skill_id;

        node = next;
    }

    return count;
}

static void ResetPassiveRates(DWORD player)
{
    int i;
    float *rates = (float *)(player + PLAYER_PASSIVE_RATE_OFFSET);

    if (IsBadWritePtr(rates, sizeof(float) * 6))
        return;

    // 0x481B40 添加“特别数据3 == -1”的被动技能时，会直接写这 6 个倍率。
    // 删除技能函数不会反向恢复，所以洗点后统一回到默认 1.0。
    for (i = 0; i < 6; ++i)
        rates[i] = 1.0f;
}

void ExecuteSkillRespecFlow(void)
{
    DWORD player;
    int skill_ids[MAX_SKILLS_TO_RESET];
    int count;
    int i;
    int refund = 0;
    int removed = 0;
    char msg[128];

    if (!g_enabled || !g_pk_config.enable_skill_respec)
        return;

    if (!Game_GetPlayer || !Game_RemoveSkill || !Game_AddRageMax)
        return;

    player = (DWORD)Game_GetPlayer(g_player_mgr, NULL);
    if (!player)
    {
        SendGameTips("[提示]洗技能失败：未找到角色");
        return;
    }

    count = CollectResetSkills(player, skill_ids, MAX_SKILLS_TO_RESET);
    if (count <= 0)
    {
        SendGameTips("[提示]没有可重置的技能");
        return;
    }

    for (i = 0; i < count; ++i)
    {
        int skill_id = skill_ids[i];
        int record = FindSkillRecordById(skill_id);

        if (record)
        {
            int series = GetSkillField(record, SKILL_COL_SERIES);
            int group = GetSkillField(record, SKILL_COL_GROUP);
            int level = GetSkillField(record, SKILL_COL_LEVEL);
            int cost = GetSkillField(record, SKILL_COL_COST);

            if (series > 0 && group > 0)
            {
                if (level > 0 && cost > 0)
                {
                    // 普通技能只记录当前最高等级在链表中；需要把同组较低等级消耗一并返还。
                    refund += CalcRefundCost(series, group, level);
                }
                else if (level == -1 && cost < 0)
                {
                    // 一排技能学满后自动获得的必杀技会增加 1000 怒气上限，洗点时扣回。
                    Game_AddRageMax((void *)player, NULL, -1000);
                }
            }
        }

        Game_RemoveSkill((void *)player, NULL, skill_id);
        ++removed;
    }

    if (refund > 0)
        *(int *)(player + PLAYER_SKILL_POINTS_OFFSET) += refund;

    ResetPassiveRates(player);

    snprintf(msg, sizeof(msg), "[提示]已重置技能 %d 个，返还技能点 %d", removed, refund);
    SendGameTips(msg);
}

void Mod_Skill_Respec_Init(int game_version)
{
    SkillRespecAddressConfig config;

    if (!LoadAddressConfig(game_version, &config))
        return;

    Game_GetPlayer = (tGetPlayer)config.get_player;
    Game_TableFindFirst = (tTableFindFirst)config.table_find_first;
    Game_TableFindNext = (tTableFindNext)config.table_find_next;
    Game_TableGetInt = (tTableGetInt)config.table_get_int;
    Game_RemoveSkill = (tRemoveSkill)config.remove_skill;
    Game_AddRageMax = (tAddRageMax)config.add_rage_max;
    g_player_mgr = (void *)config.player_mgr;
    g_skill_table = (void *)config.skill_table;
    g_enabled = 1;
}
