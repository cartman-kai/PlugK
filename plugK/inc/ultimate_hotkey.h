#ifndef ULTIMATE_HOTKEY_H
#define ULTIMATE_HOTKEY_H

// 初始化必杀技快捷释放模块。当前支持正传 v1.05 与外传 v2.01。
void Mod_Ultimate_Hotkey_Init(int game_version);

// 由 InputMgr 调用。slot_index 为 0..3，对应 Alt+1..Alt+4。
void ExecuteUltimateHotkeySlot(int slot_index);

#endif
