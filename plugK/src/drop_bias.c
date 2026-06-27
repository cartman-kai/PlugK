#include "pch.h"
#include "drop_bias.h"
#include "config.h"
#include "show_tips.h"
#include <MinHook.h>
#include <intrin.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma intrinsic(_ReturnAddress)

extern int g_StashPageB[50];
extern int g_InvPageB[50];

#define VER_105 105
#define VER_201 201

// 1.05/2.01 的 SelectDropItem 函数：原版根据“掉落档位 + 类型”从 PropMdl 表中选出具体物品 ID。
#define ADDR_105_SELECT_DROP_ITEM 0x00452200
#define ADDR_105_ENEMY_DROP 0x00452420
#define ADDR_105_RAND 0x0050E17A
#define ADDR_105_CHAR_PTR 0x00558AC4
#define ADDR_105_PROPMDL_TABLE 0x00548340
#define ADDR_105_RANDPROP_TABLE 0x00548760

// 1.05 中敌人死亡基础掉落调用点，以及敌人随机物品掉落判定用的 rand() 返回点。
#define RET_105_PRIMARY_ENEMY_DROP 0x00435BE6
#define RET_105_ITEM_DROP_RAND 0x00452576

// 只处理敌人随机掉落生成函数里的 4 个调用点，避免影响敌人背包吐出、脚本、商店等其他选物路径。
#define RET_105_DROP_SELECT_1 0x00452630
#define RET_105_DROP_SELECT_2 0x00452641
#define RET_105_DROP_SELECT_3 0x00452652
#define RET_105_DROP_SELECT_4 0x00452663

#define ADDR_201_SELECT_DROP_ITEM 0x0045E390
#define ADDR_201_ENEMY_DROP 0x0045E5A0
#define ADDR_201_RAND 0x005264DF
#define ADDR_201_CHAR_PTR 0x00589A44
#define ADDR_201_PROPMDL_TABLE 0x005788D0
#define ADDR_201_RANDPROP_TABLE 0x00578D18

#define RET_201_PRIMARY_ENEMY_DROP 0x0043F6B6
#define RET_201_ITEM_DROP_RAND 0x0045E6F6

#define RET_201_DROP_SELECT_1 0x0045E7B0
#define RET_201_DROP_SELECT_2 0x0045E7C1
#define RET_201_DROP_SELECT_3 0x0045E7D2
#define RET_201_DROP_SELECT_4 0x0045E7E3

// 角色对象中的物品容器布局。槽位里保存的是物品池索引，空位为 -1。
#define OFFSET_ITEM_POOL 0xA0
#define OFFSET_INV 0xA4
#define OFFSET_QUICK 0x184
#define OFFSET_SOCKET 0x1B4
#define OFFSET_CHARM 0x1E4
#define OFFSET_STASH 0x1FC

// PropMdl 表字段号：ID、类型，以及原版掉落查询使用的“掉落档位”。
// 这里的等级不是玩家界面显示的随机物品等级。
#define COL_ITEM_ID 1
#define COL_ITEM_TYPE 2
#define COL_ITEM_LEVEL 8

#define MAX_ITEM_ID_TRACK 1024
#define MAX_CANDIDATES 128
#define MAX_ITEM_NAME_BYTES 64
// 近期掉落计数的封顶，用来避免同一种缺少物品连续被补太多；不是“20 个以内才倾斜”的阈值。
#define RECENT_COUNT_CAP 20

typedef int(__cdecl *tSelectDropItem)(int level, int type_hint);
typedef int(__cdecl *tEnemyDrop)(int enemy_id, int position, int score);
typedef int(__cdecl *tGameRand)(void);

typedef struct DropBiasVersionConfig
{
    DWORD select_drop_item;
    DWORD enemy_drop;
    DWORD rand_func;
    DWORD char_ptr;
    DWORD propmdl_table;
    DWORD randprop_table;
    DWORD primary_enemy_drop_return;
    DWORD item_drop_rand_return;
    DWORD returns[4];
} DropBiasVersionConfig;

typedef struct DropExpectationState
{
    // 概率债务：本敌人连续未掉落时累加基础掉率，下一次一次掉落判定时提高命中率。
    int debt;
} DropExpectationState;

typedef struct DropExpectationContext
{
    // 当前线程正在执行敌人/宝箱掉落主函数时才设置，用来让 rand hook 知道这次判定属于谁。
    int active;
    int primary_death_drop;
    int enemy_id;
    int base_rate;
    DWORD thread_id;
} DropExpectationContext;

static tSelectDropItem fpSelectDropItem = NULL;
static tEnemyDrop fpEnemyDrop = NULL;
static tGameRand fpGameRand = NULL;
static DropBiasVersionConfig g_drop_bias_config;
static int g_recent_drop_count[MAX_ITEM_ID_TRACK];
// 按 RandProp 角色 ID 独立累计，避免一个敌人的未掉落次数影响另一个敌人。
static DropExpectationState g_drop_expectation_state[MAX_ITEM_ID_TRACK];
static DropExpectationContext g_drop_expectation_context;
static DWORD g_last_char_base = 0;
static DWORD g_local_rng_state = 0;
static int g_drop_bias_debug = 0;
static int g_drop_bias_trace = 0;

static DWORD FindPropRecordById(int item_id);
static DWORD FindRandPropRecordById(int enemy_id);
static void DropBias_ResetExpectation(void);

typedef struct DropCandidate
{
    int item_id;
    int count;
} DropCandidate;

static int IsDropSelectReturn(void *return_address)
{
    DWORD ret = (DWORD)return_address;
    int i;

    for (i = 0; i < 4; ++i)
    {
        if (g_drop_bias_config.returns[i] == ret)
            return 1;
    }

    return 0;
}

// 本功能只优化投掷物品、基础宝石，以及显式 Type 35 宝石。药品、任务道具、护身石、金钱、特殊物品保持原版。
static int IsSupportedItemType(int item_type)
{
    return item_type == 20 ||
           item_type == 30 ||
           item_type == 35;
}

// 使用独立的本地伪随机数，避免为了倾向重抽额外消耗游戏 rand() 序列，降低对原版随机流程的扰动。
static int NextLocalRandom(int max_value)
{
    DWORD seed;

    if (max_value <= 1)
        return 0;

    if (g_local_rng_state == 0)
    {
        seed = GetTickCount();
        seed ^= (DWORD)&g_local_rng_state;
        seed ^= (DWORD)GetCurrentThreadId();
        if (seed == 0)
            seed = 0x13579BDF;
        g_local_rng_state = seed;
    }

    g_local_rng_state = g_local_rng_state * 1103515245u + 12345u;
    return (int)((g_local_rng_state >> 16) % (DWORD)max_value);
}

// 读取游戏表记录中的整数字段。表记录结构为 [未知, 字段数, 字段数组指针, ...]。
static int ReadRecordInt(DWORD record, int column)
{
    DWORD values;
    int count;

    if (record == 0 || IsBadReadPtr((void *)record, 0x10))
        return 0xFFFF;

    count = *(int *)(record + 4);
    if (column < 0 || column >= count)
        return 0xFFFF;

    values = *(DWORD *)(record + 8);
    if (values == 0 || IsBadReadPtr((void *)values, sizeof(int) * (column + 1)))
        return 0xFFFF;

    return *(int *)(values + column * 4);
}

static int SafeStringLength(const char *text, int max_len)
{
    int i;

    if (!text || max_len <= 0)
        return 0;

    for (i = 0; i < max_len; ++i)
    {
        if (IsBadReadPtr(text + i, 1))
            return i;
        if (text[i] == '\0')
            return i;
    }

    return max_len;
}

// 游戏表记录第 4 个 dword 指向一组字符串指针；第一个字符串是物品显示名称，编码为 GBK。
static const char *ReadRecordNameGbk(DWORD record)
{
    DWORD text_ptrs;
    DWORD name_ptr;

    if (record == 0 || IsBadReadPtr((void *)record, 0x10))
        return NULL;

    text_ptrs = *(DWORD *)(record + 0x0C);
    if (text_ptrs == 0 || IsBadReadPtr((void *)text_ptrs, sizeof(DWORD)))
        return NULL;

    name_ptr = *(DWORD *)text_ptrs;
    if (name_ptr == 0 || SafeStringLength((const char *)name_ptr, MAX_ITEM_NAME_BYTES) <= 0)
        return NULL;

    return (const char *)name_ptr;
}

static const char *GetItemNameGbkById(int item_id)
{
    DWORD record = FindPropRecordById(item_id);
    if (!record)
        return NULL;

    return ReadRecordNameGbk(record);
}

static const char *GetEnemyNameGbkById(int enemy_id)
{
    DWORD record = FindRandPropRecordById(enemy_id);
    if (!record)
        return NULL;

    return ReadRecordNameGbk(record);
}

static void GbkToUtf8(const char *gbk_text, char *utf8_text, int utf8_size)
{
    wchar_t wide_text[MAX_ITEM_NAME_BYTES];
    int wide_len;

    if (!utf8_text || utf8_size <= 0)
        return;

    utf8_text[0] = '\0';
    if (!gbk_text)
        return;

    wide_len = MultiByteToWideChar(936, 0, gbk_text, -1, wide_text, MAX_ITEM_NAME_BYTES);
    if (wide_len <= 0)
        return;

    WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, utf8_text, utf8_size, NULL, NULL);
}

// 读取当前版本的 PropMdl 表头，表头形态为 [count, base]，每条记录 16 字节。
static DWORD GetPropMdlBase(int *out_count)
{
    int count;
    DWORD base;

    if (g_drop_bias_config.propmdl_table == 0)
        return 0;
    if (IsBadReadPtr((void *)g_drop_bias_config.propmdl_table, 8))
        return 0;

    count = *(int *)g_drop_bias_config.propmdl_table;
    base = *(DWORD *)(g_drop_bias_config.propmdl_table + 4);

    if (count <= 0 || count > 4096 || base == 0)
        return 0;
    if (IsBadReadPtr((void *)base, count * 16))
        return 0;

    if (out_count)
        *out_count = count;
    return base;
}

static DWORD GetTableBase(DWORD table, int max_count, int *out_count)
{
    int count;
    DWORD base;

    if (table == 0)
        return 0;
    if (IsBadReadPtr((void *)table, 8))
        return 0;

    count = *(int *)table;
    base = *(DWORD *)(table + 4);

    if (count <= 0 || count > max_count || base == 0)
        return 0;
    if (IsBadReadPtr((void *)base, count * 16))
        return 0;

    if (out_count)
        *out_count = count;
    return base;
}

static DWORD GetRandPropBase(int *out_count)
{
    return GetTableBase(g_drop_bias_config.randprop_table, 4096, out_count);
}

// 按物品 ID 在 PropMdl 中反查记录，用于获知原版已经抽中物品的类型和掉落档位。
static DWORD FindPropRecordById(int item_id)
{
    int count;
    DWORD base = GetPropMdlBase(&count);
    int i;

    if (!base)
        return 0;

    for (i = 0; i < count; ++i)
    {
        DWORD record = base + i * 16;
        if (ReadRecordInt(record, COL_ITEM_ID) == item_id)
            return record;
    }

    return 0;
}

static DWORD FindRandPropRecordById(int enemy_id)
{
    int count;
    DWORD base = GetRandPropBase(&count);
    int i;

    if (!base)
        return 0;

    for (i = 0; i < count; ++i)
    {
        DWORD record = base + i * 16;
        if (ReadRecordInt(record, 1) == enemy_id)
            return record;
    }

    return 0;
}

static int GetEnemyBaseDropRate(int enemy_id)
{
    DWORD record = FindRandPropRecordById(enemy_id);
    int rate;

    if (!record)
        return -1;

    rate = ReadRecordInt(record, 2);
    if (rate < 0 || rate > 100)
        return -1;

    return rate;
}

// 获取当前角色对象。切换角色或重新读档导致角色地址变化时，清理近期掉落计数。
static DWORD GetCharBase(void)
{
    DWORD char_base;

    if (g_drop_bias_config.char_ptr == 0)
        return 0;
    if (IsBadReadPtr((void *)g_drop_bias_config.char_ptr, sizeof(DWORD)))
        return 0;

    char_base = *(DWORD *)g_drop_bias_config.char_ptr;
    if (char_base == 0 || IsBadReadPtr((void *)char_base, 0x300))
        return 0;

    if (g_last_char_base != char_base)
    {
        DropBias_ResetRecent();
        g_last_char_base = char_base;
    }

    return char_base;
}

// 槽位数组保存的是物品池索引；物品对象 +0x18 是物品 ID，+0x1C 是数量。
static int GetItemIdFromPoolIndex(DWORD *item_pool, int index, int *out_count)
{
    DWORD item;
    int count = 1;

    if (index < 0 || !item_pool)
        return -1;

    if (IsBadReadPtr(item_pool + index, sizeof(DWORD)))
        return -1;

    item = item_pool[index];
    if (item == 0 || IsBadReadPtr((void *)item, 0x20))
        return -1;

    count = *(int *)(item + 0x1C);
    if (count <= 0)
        count = 1;

    if (out_count)
        *out_count = count;

    return *(int *)(item + 0x18);
}

// 统计一个槽位区域内指定物品 ID 的数量。
static int CountItemInSlots(DWORD *item_pool, int *slots, int slot_count, int item_id)
{
    int total = 0;
    int i;

    if (!item_pool || !slots || slot_count <= 0)
        return 0;
    if (IsBadReadPtr(slots, sizeof(int) * slot_count))
        return 0;

    for (i = 0; i < slot_count; ++i)
    {
        int count = 0;
        int id = GetItemIdFromPoolIndex(item_pool, slots[i], &count);
        if (id == item_id)
            total += count;
    }

    return total;
}

// 统计角色当前实际持有的数量：背包、快捷栏、镶嵌位、护身石位、仓库，以及启用扩展存储时的 B 面。
static int CountOwnedItem(int item_id)
{
    DWORD char_base = GetCharBase();
    DWORD pool_addr;
    DWORD *item_pool;
    int total = 0;

    if (!char_base)
        return 0;

    pool_addr = *(DWORD *)(char_base + OFFSET_ITEM_POOL);
    if (pool_addr == 0 || IsBadReadPtr((void *)pool_addr, sizeof(DWORD)))
        return 0;
    item_pool = (DWORD *)pool_addr;

    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_INV), 50, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_QUICK), 12, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_SOCKET), 12, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_CHARM), 6, item_id);
    total += CountItemInSlots(item_pool, (int *)(char_base + OFFSET_STASH), 50, item_id);

    if (g_pk_config.stash_ext_enabled)
    {
        total += CountItemInSlots(item_pool, g_InvPageB, 50, item_id);
        total += CountItemInSlots(item_pool, g_StashPageB, 50, item_id);
    }

    return total;
}

// 统计用于掉落权重的数量。近期由本模块改出的掉落也会临时计入，避免连续掉落过度偏向同一个 ID。
static int CountExistingItem(int item_id)
{
    int total = CountOwnedItem(item_id);

    if (item_id >= 0 && item_id < MAX_ITEM_ID_TRACK)
        total += g_recent_drop_count[item_id];

    return total;
}

// 构造候选池：只在“原版抽中物品的同类型 + 同 PropMdl 掉落档位”范围内做倾向。
// 这保证不会把原版本次决定的药品/投掷物/宝石方向改成另一类，也不会主动跨掉落档位。
static int CollectCandidates(int item_type, int item_level, DropCandidate *candidates, int max_candidates)
{
    int count;
    DWORD base = GetPropMdlBase(&count);
    int candidate_count = 0;
    int i;

    if (!base || !candidates || max_candidates <= 0)
        return 0;

    for (i = 0; i < count && candidate_count < max_candidates; ++i)
    {
        DWORD record = base + i * 16;
        int id = ReadRecordInt(record, COL_ITEM_ID);
        int type = ReadRecordInt(record, COL_ITEM_TYPE);
        int level = ReadRecordInt(record, COL_ITEM_LEVEL);

        if (id < 0 || level != item_level)
            continue;
        if (!IsSupportedItemType(type))
            continue;
        if (type != item_type)
            continue;

        candidates[candidate_count].item_id = id;
        candidates[candidate_count].count = CountExistingItem(id);
        ++candidate_count;
    }

    return candidate_count;
}

// 权重公式：
//   max_count = 候选池中拥有数量最多的数量
//   weight    = max_count - 当前候选拥有数量 + 1
// 拥有越少，权重越高；拥有最多的候选仍保留 1 点权重，所以不是强制补齐。
static int PickBiasedCandidate(DropCandidate *candidates, int candidate_count, int original_item_id)
{
    int i;
    int max_count = 0;
    int total_weight = 0;
    int roll;

    if (!candidates || candidate_count <= 1)
        return original_item_id;

    for (i = 0; i < candidate_count; ++i)
    {
        if (candidates[i].count > max_count)
            max_count = candidates[i].count;
    }

    for (i = 0; i < candidate_count; ++i)
        total_weight += (max_count - candidates[i].count) + 1;

    if (total_weight <= 0)
        return original_item_id;

    roll = NextLocalRandom(total_weight);
    for (i = 0; i < candidate_count; ++i)
    {
        int weight = (max_count - candidates[i].count) + 1;
        if (roll < weight)
            return candidates[i].item_id;
        roll -= weight;
    }

    return original_item_id;
}

// 记录本模块刚刚改出的掉落 ID。它不写入存档，只影响本次游戏进程中的后续权重。
static void NoteRecentDrop(int item_id)
{
    if (item_id < 0 || item_id >= MAX_ITEM_ID_TRACK)
        return;
    if (g_recent_drop_count[item_id] < RECENT_COUNT_CAP)
        ++g_recent_drop_count[item_id];
}

static int ClampInt(int value, int min_value, int max_value)
{
    if (value < min_value)
        return min_value;
    if (value > max_value)
        return max_value;
    return value;
}

static void DropBias_ResetExpectation(void)
{
    memset(g_drop_expectation_state, 0, sizeof(g_drop_expectation_state));
    memset(&g_drop_expectation_context, 0, sizeof(g_drop_expectation_context));
}

static void DebugDropExpectationMiss(int enemy_id, int next_rate)
{
    char enemy_name[96];
    char text[256];

    if (!g_drop_bias_debug)
        return;

    GbkToUtf8(GetEnemyNameGbkById(enemy_id), enemy_name, sizeof(enemy_name));
    if (enemy_name[0] == '\0')
        snprintf(enemy_name, sizeof(enemy_name), "Unknown");

    snprintf(text, sizeof(text),
             "[PlugK][DropBias] enemy=%s(%d) next_drop_rate=%d%%\n",
             enemy_name, enemy_id, next_rate);
    OutputDebugStringA(text);

    snprintf(text, sizeof(text),
             "[掉落优化] %s(%d) 掉落概率提升至%d%%",
             enemy_name, enemy_id, next_rate);
    SendGameTips(text);
}

// DropBiasTrace 是排查 hook 边界用的详细日志，只写 OutputDebugString，不打扰测试人员的游戏提示。
static void DebugDropExpectationContext(int enemy_id, int base_rate, int primary_death_drop, int score, void *return_address)
{
    char enemy_name[96];
    char text[256];

    if (!g_drop_bias_trace)
        return;

    GbkToUtf8(GetEnemyNameGbkById(enemy_id), enemy_name, sizeof(enemy_name));
    if (enemy_name[0] == '\0')
        snprintf(enemy_name, sizeof(enemy_name), "Unknown");

    snprintf(text, sizeof(text),
             "[PlugK][DropBias] EnemyDrop enemy=%s(%d) rate=%d primary=%d score=%d ret=0x%08X\n",
             enemy_name, enemy_id, base_rate, primary_death_drop, score, (DWORD)return_address);
    OutputDebugStringA(text);
}

static void DebugDropExpectationGate(int enemy_id, int base_rate, int debt, int chance, int roll, int success)
{
    char enemy_name[96];
    char text[256];

    if (!g_drop_bias_trace)
        return;

    GbkToUtf8(GetEnemyNameGbkById(enemy_id), enemy_name, sizeof(enemy_name));
    if (enemy_name[0] == '\0')
        snprintf(enemy_name, sizeof(enemy_name), "Unknown");

    snprintf(text, sizeof(text),
             "[PlugK][DropBias] Gate enemy=%s(%d) base=%d debt=%d chance=%d roll=%d success=%d\n",
             enemy_name, enemy_id, base_rate, debt, chance, roll, success);
    OutputDebugStringA(text);
}

// 只改写物品掉落 gate 的随机返回值，不额外调用游戏 rand()。
// 成功时返回 0，让原版的 rand()%100 判定必定通过；失败时返回 99，让本次 gate 保持失败。
static int ApplyDropExpectationRoll(int original_random)
{
    DropExpectationState *state;
    int enemy_id;
    int base_rate;
    int chance;
    int roll;
    int success;
    int next_rate;

    if (!g_pk_config.enable_drop_bias)
        return original_random;
    if (!g_drop_expectation_context.active || !g_drop_expectation_context.primary_death_drop)
        return original_random;
    if (g_drop_expectation_context.thread_id != GetCurrentThreadId())
        return original_random;

    enemy_id = g_drop_expectation_context.enemy_id;
    base_rate = g_drop_expectation_context.base_rate;
    if (enemy_id < 0 || enemy_id >= MAX_ITEM_ID_TRACK || base_rate <= 0 || base_rate >= 100)
    {
        if (g_drop_bias_trace)
        {
            char text[160];
            snprintf(text, sizeof(text),
                     "[PlugK][DropBias] Gate skipped enemy=%d base=%d\n",
                     enemy_id, base_rate);
            OutputDebugStringA(text);
        }
        return original_random;
    }

    state = &g_drop_expectation_state[enemy_id];
    chance = ClampInt(base_rate + state->debt, 0, 100);
    roll = NextLocalRandom(100);
    success = roll < chance;
    DebugDropExpectationGate(enemy_id, base_rate, state->debt, chance, roll, success);

    if (success)
    {
        state->debt -= (100 - base_rate);
        state->debt = ClampInt(state->debt, 0, 100 - base_rate);
        return 0;
    }

    state->debt += base_rate;
    state->debt = ClampInt(state->debt, 0, 100 - base_rate);
    next_rate = ClampInt(base_rate + state->debt, 0, 100);
    DebugDropExpectationMiss(enemy_id, next_rate);

    return 99;
}

static void DebugDropBiasDecision(int original_item_id, int selected_item_id, int item_type, int item_level, DropCandidate *candidates, int candidate_count)
{
    char original_name[96];
    char selected_name[96];
    char text[256];
    int i;

    if (!g_drop_bias_debug)
        return;

    GbkToUtf8(GetItemNameGbkById(original_item_id), original_name, sizeof(original_name));
    GbkToUtf8(GetItemNameGbkById(selected_item_id), selected_name, sizeof(selected_name));

    if (original_name[0] == '\0')
        snprintf(original_name, sizeof(original_name), "Unknown");
    if (selected_name[0] == '\0')
        snprintf(selected_name, sizeof(selected_name), "Unknown");

    snprintf(text, sizeof(text),
             "[PlugK][DropBias] type=%d level=%d candidates=%d original=%s(%d) selected=%s(%d)\n",
             item_type, item_level, candidate_count, original_name, original_item_id, selected_name, selected_item_id);
    OutputDebugStringA(text);

    for (i = 0; candidates && i < candidate_count; ++i)
    {
        char name[96];

        GbkToUtf8(GetItemNameGbkById(candidates[i].item_id), name, sizeof(name));
        if (name[0] == '\0')
            snprintf(name, sizeof(name), "Unknown");

        snprintf(text, sizeof(text),
                 "[PlugK][DropBias] candidate[%d]=%s(%d) owned=%d\n",
                 i, name, candidates[i].item_id, candidates[i].count);
        OutputDebugStringA(text);
    }

    if (selected_item_id != original_item_id)
    {
        snprintf(text, sizeof(text),
                 "[掉落优化] %s(%d) -> %s(%d)",
                 original_name, original_item_id, selected_name, selected_item_id);
        SendGameTips(text);
    }
}

// 对原版选出的物品 ID 进行倾向替换。无法识别、类型不支持或候选不足时，直接返回原 ID。
static int ApplyDropBias(int original_item_id)
{
    DWORD record;
    int item_type;
    int item_level;
    DropCandidate candidates[MAX_CANDIDATES];
    int candidate_count;
    int original_owned_count;
    int selected_item_id;

    record = FindPropRecordById(original_item_id);
    if (!record)
        return original_item_id;

    item_type = ReadRecordInt(record, COL_ITEM_TYPE);
    item_level = ReadRecordInt(record, COL_ITEM_LEVEL);
    if (!IsSupportedItemType(item_type))
        return original_item_id;

    original_owned_count = CountOwnedItem(original_item_id);
    if (original_owned_count <= 0)
        return original_item_id;

    candidate_count = CollectCandidates(item_type, item_level, candidates, MAX_CANDIDATES);
    selected_item_id = PickBiasedCandidate(candidates, candidate_count, original_item_id);
    NoteRecentDrop(selected_item_id);
    DebugDropBiasDecision(original_item_id, selected_item_id, item_type, item_level, candidates, candidate_count);

    return selected_item_id;
}

// Hook 入口：先调用原版 SelectDropItem，保留原版掉落类型、档位和失败重试逻辑，再按返回地址过滤随机掉落路径。
int __cdecl Detour_SelectDropItem(int level, int type_hint)
{
    void *return_address = _ReturnAddress();
    int original_item_id;

    if (!fpSelectDropItem)
        return -1;

    original_item_id = fpSelectDropItem(level, type_hint);

    if (!g_pk_config.enable_drop_bias)
        return original_item_id;
    if (!IsDropSelectReturn(return_address))
        return original_item_id;
    if (original_item_id < 0 || original_item_id == 0xFFFF)
        return original_item_id;

    return ApplyDropBias(original_item_id);
}

int __cdecl Detour_GameRand(void)
{
    void *return_address = _ReturnAddress();
    int original_random;

    if (!fpGameRand)
        return NextLocalRandom(RAND_MAX);

    original_random = fpGameRand();
    if ((DWORD)return_address != g_drop_bias_config.item_drop_rand_return)
        return original_random;

    return ApplyDropExpectationRoll(original_random);
}

int __cdecl Detour_EnemyDrop(int enemy_id, int position, int score)
{
    void *return_address = _ReturnAddress();
    DropExpectationContext previous_context;
    int result;

    if (!fpEnemyDrop)
        return 0;

    previous_context = g_drop_expectation_context;

    g_drop_expectation_context.active = 1;
    // 固定返回地址只能覆盖部分路径；敌人、宝箱等都会进入同一掉落主函数。
    // 一次掉落调用会传入 score=0，连招二次掉落才会传入实际得分。
    // 不依赖“超过多少分触发二次掉落”的阈值，避免未来阈值调整时误判。
    g_drop_expectation_context.primary_death_drop =
        ((DWORD)return_address == g_drop_bias_config.primary_enemy_drop_return) ||
        (score == 0);
    g_drop_expectation_context.enemy_id = enemy_id;
    g_drop_expectation_context.base_rate = GetEnemyBaseDropRate(enemy_id);
    g_drop_expectation_context.thread_id = GetCurrentThreadId();

    DebugDropExpectationContext(enemy_id,
                                g_drop_expectation_context.base_rate,
                                g_drop_expectation_context.primary_death_drop,
                                score,
                                return_address);

    result = fpEnemyDrop(enemy_id, position, score);

    g_drop_expectation_context = previous_context;
    return result;
}

void DropBias_ResetRecent(void)
{
    memset(g_recent_drop_count, 0, sizeof(g_recent_drop_count));
    DropBias_ResetExpectation();
}

static int InitVersionConfig(int game_version, DropBiasVersionConfig *config)
{
    memset(config, 0, sizeof(*config));

    if (game_version == VER_105)
    {
        config->select_drop_item = ADDR_105_SELECT_DROP_ITEM;
        config->enemy_drop = ADDR_105_ENEMY_DROP;
        config->rand_func = ADDR_105_RAND;
        config->char_ptr = ADDR_105_CHAR_PTR;
        config->propmdl_table = ADDR_105_PROPMDL_TABLE;
        config->randprop_table = ADDR_105_RANDPROP_TABLE;
        config->primary_enemy_drop_return = RET_105_PRIMARY_ENEMY_DROP;
        config->item_drop_rand_return = RET_105_ITEM_DROP_RAND;
        config->returns[0] = RET_105_DROP_SELECT_1;
        config->returns[1] = RET_105_DROP_SELECT_2;
        config->returns[2] = RET_105_DROP_SELECT_3;
        config->returns[3] = RET_105_DROP_SELECT_4;
        return 1;
    }

    if (game_version == VER_201)
    {
        config->select_drop_item = ADDR_201_SELECT_DROP_ITEM;
        config->enemy_drop = ADDR_201_ENEMY_DROP;
        config->rand_func = ADDR_201_RAND;
        config->char_ptr = ADDR_201_CHAR_PTR;
        config->propmdl_table = ADDR_201_PROPMDL_TABLE;
        config->randprop_table = ADDR_201_RANDPROP_TABLE;
        config->primary_enemy_drop_return = RET_201_PRIMARY_ENEMY_DROP;
        config->item_drop_rand_return = RET_201_ITEM_DROP_RAND;
        config->returns[0] = RET_201_DROP_SELECT_1;
        config->returns[1] = RET_201_DROP_SELECT_2;
        config->returns[2] = RET_201_DROP_SELECT_3;
        config->returns[3] = RET_201_DROP_SELECT_4;
        return 1;
    }

    return 0;
}

static void LoadDropBiasDebugConfig(void)
{
    char ini_path[MAX_PATH];
    char dll_path[MAX_PATH];
    char value[32];
    HMODULE hModule;
    char *last_slash;

    g_drop_bias_debug = 0;
    g_drop_bias_trace = 0;

    hModule = GetModuleHandleA("PlugK.dll");
    if (!hModule)
        hModule = GetModuleHandleA(NULL);
    if (!GetModuleFileNameA(hModule, dll_path, MAX_PATH))
        return;

    last_slash = strrchr(dll_path, '\\');
    if (last_slash)
        *(last_slash + 1) = '\0';
    snprintf(ini_path, MAX_PATH, "%sPlugK.ini", dll_path);

    // DropBiasDebug：面向测试人员的可见提示，只显示概率提升和物品替换结果。
    GetPrivateProfileStringA("Debug", "DropBiasDebug", "MISSING", value, sizeof(value), ini_path);
    if (strcmp(value, "MISSING") == 0)
        WritePrivateProfileStringA("Debug", "DropBiasDebug", "0", ini_path);

    g_drop_bias_debug = GetPrivateProfileIntA("Debug", "DropBiasDebug", 0, ini_path) != 0;

    // DropBiasTrace：面向开发排查的详细路径日志，只输出到调试器。
    GetPrivateProfileStringA("Debug", "DropBiasTrace", "MISSING", value, sizeof(value), ini_path);
    if (strcmp(value, "MISSING") == 0)
        WritePrivateProfileStringA("Debug", "DropBiasTrace", "0", ini_path);

    g_drop_bias_trace = GetPrivateProfileIntA("Debug", "DropBiasTrace", 0, ini_path) != 0;
}

// 初始化版本地址并安装 Hook。配置关闭时不安装，降低对游戏流程的影响。
void Mod_Drop_Bias_Init(int game_version)
{
    MH_STATUS status;
    char text[256];

    if (!InitVersionConfig(game_version, &g_drop_bias_config))
        return;
    LoadDropBiasDebugConfig();
    if (g_drop_bias_trace)
    {
        snprintf(text, sizeof(text),
                 "[PlugK][DropBias] init version=%d enable=%d select=0x%08X enemy=0x%08X rand=0x%08X randprop=0x%08X\n",
                 game_version,
                 g_pk_config.enable_drop_bias,
                 g_drop_bias_config.select_drop_item,
                 g_drop_bias_config.enemy_drop,
                 g_drop_bias_config.rand_func,
                 g_drop_bias_config.randprop_table);
        OutputDebugStringA(text);
    }
    if (!g_pk_config.enable_drop_bias)
        return;

    status = MH_CreateHook((LPVOID)g_drop_bias_config.select_drop_item,
                           &Detour_SelectDropItem,
                           (LPVOID *)&fpSelectDropItem);
    if (g_drop_bias_trace)
    {
        snprintf(text, sizeof(text), "[PlugK][DropBias] hook SelectDropItem create=%d\n", status);
        OutputDebugStringA(text);
    }
    status = MH_EnableHook((LPVOID)g_drop_bias_config.select_drop_item);
    if (g_drop_bias_trace)
    {
        snprintf(text, sizeof(text), "[PlugK][DropBias] hook SelectDropItem enable=%d\n", status);
        OutputDebugStringA(text);
    }

    if (g_drop_bias_config.enemy_drop && g_drop_bias_config.rand_func)
    {
        status = MH_CreateHook((LPVOID)g_drop_bias_config.enemy_drop,
                               &Detour_EnemyDrop,
                               (LPVOID *)&fpEnemyDrop);
        if (g_drop_bias_trace)
        {
            snprintf(text, sizeof(text), "[PlugK][DropBias] hook EnemyDrop create=%d\n", status);
            OutputDebugStringA(text);
        }
        status = MH_EnableHook((LPVOID)g_drop_bias_config.enemy_drop);
        if (g_drop_bias_trace)
        {
            snprintf(text, sizeof(text), "[PlugK][DropBias] hook EnemyDrop enable=%d\n", status);
            OutputDebugStringA(text);
        }

        status = MH_CreateHook((LPVOID)g_drop_bias_config.rand_func,
                               &Detour_GameRand,
                               (LPVOID *)&fpGameRand);
        if (g_drop_bias_trace)
        {
            snprintf(text, sizeof(text), "[PlugK][DropBias] hook GameRand create=%d\n", status);
            OutputDebugStringA(text);
        }
        status = MH_EnableHook((LPVOID)g_drop_bias_config.rand_func);
        if (g_drop_bias_trace)
        {
            snprintf(text, sizeof(text), "[PlugK][DropBias] hook GameRand enable=%d\n", status);
            OutputDebugStringA(text);
        }
    }
}
