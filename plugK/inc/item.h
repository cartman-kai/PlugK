#ifndef MOD_SHOW_ITEM_NAME_H
#define MOD_SHOW_ITEM_NAME_H

// 初始化函数，在插件入口调用
void Mod_Show_Item_Name_Init(int game_version);

// 调用共享文字函数的原始 trampoline；移动敌人血量时复用，避免重复安装文字 Hook。
int Item_DrawText(void *pThis, void *_edx, int surface, int x, int y, const char *text, int mode);

void ToggleShowItemNameSwitch();

#endif
