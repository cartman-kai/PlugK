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

} PK_CONFIG;

extern PK_CONFIG g_pk_config;

void pk_config_load();

#endif