#ifndef INVENTORY_H
#define INVENTORY_H

#include "pch.h"
#include <windows.h>

// [新增] 供 InputMgr 调用 整理背包
void ExecuteInventorySortFlow(void);
// 调用整理仓库
void ExecuteStashSortFlow(void);

// 整理背包仅当前页面
void ExecuteCurrentInventorySortFlow(void);

// 物品对象结构
// 根据您的逆向信息更新
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

// 供其他模块复用角色基址解析
DWORD GetCharacterBase();

// 供物品拆分复用可叠加判定
BOOL IsStackable(ItemObject *pItem);

// 初始化背包整理模块
void Mod_inv_auto_sort_init(int game_version);

#endif
