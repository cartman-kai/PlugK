#pragma once
#include <windows.h>

// 2.01 版本的关键地址定义
#define DROP_ADDR_HOOK_POINT_201 0x0048E501              // call comeon.49AB70 的位置
#define DROP_ADDR_FUNC_CREATE_201 0x0049AB70             // 创建掉落物 sub_49AB70
#define DROP_ADDR_FUN_ADDPROP_201 0x0041A2F0             // 添加属性 sub_41A2F0
#define DROP_ADDR_ITEM_TABLE_201 0x00578E74              // 物品信息表基址
#define DROP_ADDR_RET_201 (DROP_ADDR_HOOK_POINT_201 + 5) // 0x0048E506

// 物品类型定义
#define ITEM_TYPE_GEM_A 30
#define ITEM_TYPE_GEM_B 35

// 模块初始化函数
void Mod_Gem_SafeDrop_Init(int game_version);