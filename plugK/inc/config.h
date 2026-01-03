#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

typedef struct
{
    // 商店功能
    BOOL shop_inf_stock;

    // [新增] 分辨率功能
    BOOL res_enabled;
    int res_width;
    int res_height;

    // 1 = 开启回复类道具自动堆叠
    BOOL stack_potion;

    // 道具排序
    BOOL inventory_sort;

    // [新增] 界面修正功能
    // 打开窗口时保持画面居中，不进行平移
    BOOL ui_keep_center;

    // [新增] 商店物品数量优化 (随机1-9 & 药水堆叠)
    BOOL shop_item_count;

    // 商店物品排序
    BOOL shop_sort;

    // [新增] 扩展储物箱 (大箱子)
    BOOL stash_ext_enabled;

    BOOL combo_score_fix; // 1.05 程序连击得分补正 *2

    // 物品叠加功能
    BOOL enable_item_stack;

} PK_CONFIG;

extern PK_CONFIG g_pk_config;

void pk_config_load();

#endif