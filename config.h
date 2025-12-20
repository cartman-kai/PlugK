#ifndef CONFIG_H
#define CONFIG_H

#include <windows.h>

typedef struct
{
    // 商店功能
    BOOL shop_no_vanish;

    // [新增] 分辨率功能
    BOOL res_enabled;
    int res_width;
    int res_height;

    // 1 = 开启回复类道具自动堆叠，且不占用暗器栏
    BOOL stack_potion;

    // 道具排序
    BOOL inventory_sort;

    // [新增] 界面修正功能
    // 打开窗口时保持画面居中，不进行平移
    BOOL ui_keep_center;

} PK_CONFIG;

extern PK_CONFIG g_pk_config;

void pk_config_load();

#endif