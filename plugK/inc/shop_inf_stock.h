#ifndef PLUGK_SHOP_H
#define PLUGK_SHOP_H

#include <windows.h>

// 由汇编传入商店地址和格子索引。
int ShouldKeepItemPreCall(DWORD shopAddress, DWORD slotIndex);

void Mod_shop_inf_stock_init(int game_version);

#endif
