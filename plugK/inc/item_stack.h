#ifndef ITEM_STACK_H
#define ITEM_STACK_H

void Mod_item_stack_init(int game_version);
void ToggleItemStackState(); // [新增] 供 InputMgr 调用
int GetItemStackLimit();

#endif
