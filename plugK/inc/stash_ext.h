#ifndef STASH_EXT_H
#define STASH_EXT_H

#include <windows.h>

// 初始化扩展储物箱模块
void Mod_Stash_Ext_Init(int ver);
// 切换储物箱
void ToggleStash(void);
// 切换背包
void ToggleInventory(void);

// 清理资源
void Mod_Stash_Ext_Cleanup();

extern int g_StashPageB[50];
extern int g_InvPageB[50];

#endif