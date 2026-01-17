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

    // [新增] 实验性功能
    int enable_autofill_ext; // 开启自动写入扩展页

    // 宝石插入条件修改
    int enable_insert_gem;

    // 屏幕震动效果禁用
    int disable_screen_shake;

    // 炼化物品数量优化
    int enable_fuse_opt;

    // [新增] 快捷键配置 (存储 Virtual Key Code)
    int key_inv_sort;         // 背包整理
    int key_stash_sort;       // 储物箱整理
    int key_inv_prev;         // 背包上一页
    int key_inv_next;         // 背包下一页
    int key_stash_swap;       // 储物箱切换 (A/B)
    int key_stack_toggle;     // 叠加开关
    int key_inv_sort_current; // 仅整理当前背包页

    // 废弃
    BOOL combo_score_fix; // 1.05 程序连击得分补正 *2

    // 废弃 物品叠加功能
    BOOL enable_item_stack;

} PK_CONFIG;

extern PK_CONFIG g_pk_config;

void pk_config_load();

#endif