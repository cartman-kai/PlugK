#ifndef STASH_EXT_H
#define STASH_EXT_H

#include <windows.h>

// =========================================================
// 常量定义
// =========================================================
#define MAX_PAGES 10
#define ITEMS_PER_PAGE 50

// =========================================================
// 全局数据导出
// =========================================================

// 当前所在的页面索引 (0-9)
extern int g_CurrentStashIdx;
extern int g_CurrentInvIdx;

// 扩展页面缓存 (用于存储非当前页面的数据)
// g_InvPages[0] 对应 索引 1 的页面数据
// g_InvPages[8] 对应 索引 9 的页面数据
extern int g_StashPages[MAX_PAGES - 1][ITEMS_PER_PAGE];
extern int g_InvPages[MAX_PAGES - 1][ITEMS_PER_PAGE];

// 第 0 页 (原版页面) 的备份缓存
// 当玩家切换到第 1-9 页时，第 0 页的数据存放在这里
extern int g_StashPageZero[ITEMS_PER_PAGE];
extern int g_InvPageZero[ITEMS_PER_PAGE];

// =========================================================
// 函数接口
// =========================================================

// 初始化与清理
void Mod_Stash_Ext_Init(int ver);

// 切换逻辑
// direction: 1 (下一页), -1 (上一页)
void ToggleStashEx(int direction);
void ToggleInventoryEx(int direction);

// 强制切换到指定索引页面 (用于自动填充和整理)
// type: 0=Inv, 1=Stash
void ForceSwitchPage(int type, int targetIdx);

// 获取指定逻辑页面的数据指针 (辅助函数)
// 自动处理 "在内存中" 还是 "在缓存中" 的判断
int *GetInvPagePtr(int logicalIdx);
int *GetStashPagePtr(int logicalIdx);

#endif