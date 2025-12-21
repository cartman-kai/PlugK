#ifndef INVENTORY_H
#define INVENTORY_H

#include "pch.h"
#include <windows.h>

// 物品对象结构 (根据您的逆向信息)
// 注意：未使用的字段用 padding 填充
typedef struct
{
    // 0x00 - 0x17
    BYTE _pad0[0x18];

    // 0x18: ID
    DWORD ItemID;

    // 0x1C: 数量
    DWORD Count;

    // 0x20 - 0x2B
    BYTE _pad1[0x10];

    // 0x2C: 等级
    DWORD Level;

    // 0x30: 价格
    DWORD Price;

} ItemObject;

// 开启背包整理功能的监听线程
void Mod_inv_auto_sort_init(int game_version);

#endif