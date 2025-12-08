#ifndef PLUGK_SHOP_H
#define PLUGK_SHOP_H

#include <windows.h>

// C函数签名不变，但现在由汇编调用，且不需要 __stdcall
// ItemPtrPtr: [ESI+EDI*4+4] 的地址
int ShouldKeepItemPreCall(DWORD ItemPtrPtr);

void pk_shop_init(int game_version);

#endif