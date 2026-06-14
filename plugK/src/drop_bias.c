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
#define ADDR_105_CHAR_PTR 0x00558AC4
#define ADDR_105_PROPMDL_TABLE 0x00548340

// 只处理敌人随机掉落生成函数里的 4 个调用点，避免影响敌人背包吐出、脚本、商店等其他选物路径。
#define RET_105_DROP_SELECT_1 0x00452630
#define RET_105_DROP_SELECT_2 0x00452641
#define RET_105_DROP_SELECT_3 0x00452652
#define RET_105_DROP_SELECT_4 0x00452663

#define ADDR_201_SELECT_DROP_ITEM 0x0045E390
#define ADDR_201_CHAR_PTR 0x00589A44
#define ADDR_201_PROPMDL_TABLE 0x005788D0

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

typedef struct DropBiasVersionConfig
{
    DWORD select_drop_item;
    DWORD char_ptr;
    DWORD propmdl_table;
    DWORD returns[4];
} DropBiasVersionConfig;

static tSelectDropItem fpSelectDropItem = NULL;
static DropBiasVersionConfig g_drop_bias_config;
static int g_recent_drop_count[MAX_ITEM_ID_TRACK];
static DWORD g_last_char_base = 0;
static DWORD g_local_rng_state = 0;
static int g_drop_bias_debug = 0;

static DWORD FindPropRecordById(int item_id);

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

void DropBias_ResetRecent(void)
{
    memset(g_recent_drop_count, 0, sizeof(g_recent_drop_count));
}

static int InitVersionConfig(int game_version, DropBiasVersionConfig *config)
{
    memset(config, 0, sizeof(*config));

    if (game_version == VER_105)
    {
        config->select_drop_item = ADDR_105_SELECT_DROP_ITEM;
        config->char_ptr = ADDR_105_CHAR_PTR;
        config->propmdl_table = ADDR_105_PROPMDL_TABLE;
        config->returns[0] = RET_105_DROP_SELECT_1;
        config->returns[1] = RET_105_DROP_SELECT_2;
        config->returns[2] = RET_105_DROP_SELECT_3;
        config->returns[3] = RET_105_DROP_SELECT_4;
        return 1;
    }

    if (game_version == VER_201)
    {
        config->select_drop_item = ADDR_201_SELECT_DROP_ITEM;
        config->char_ptr = ADDR_201_CHAR_PTR;
        config->propmdl_table = ADDR_201_PROPMDL_TABLE;
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

    hModule = GetModuleHandleA("PlugK.dll");
    if (!hModule)
        hModule = GetModuleHandleA(NULL);
    if (!GetModuleFileNameA(hModule, dll_path, MAX_PATH))
        return;

    last_slash = strrchr(dll_path, '\\');
    if (last_slash)
        *(last_slash + 1) = '\0';
    snprintf(ini_path, MAX_PATH, "%sPlugK.ini", dll_path);

    GetPrivateProfileStringA("Debug", "DropBiasDebug", "MISSING", value, sizeof(value), ini_path);
    if (strcmp(value, "MISSING") == 0)
        WritePrivateProfileStringA("Debug", "DropBiasDebug", "0", ini_path);

    g_drop_bias_debug = GetPrivateProfileIntA("Debug", "DropBiasDebug", 0, ini_path) != 0;
}

// 初始化版本地址并安装 Hook。配置关闭时不安装，降低对游戏流程的影响。
void Mod_Drop_Bias_Init(int game_version)
{
    if (!InitVersionConfig(game_version, &g_drop_bias_config))
        return;
    LoadDropBiasDebugConfig();
    if (!g_pk_config.enable_drop_bias)
        return;

    MH_CreateHook((LPVOID)g_drop_bias_config.select_drop_item,
                  &Detour_SelectDropItem,
                  (LPVOID *)&fpSelectDropItem);
    MH_EnableHook((LPVOID)g_drop_bias_config.select_drop_item);
}
