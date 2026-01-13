#ifndef UI_FIX_H
#define UI_FIX_H

// 打开背包、角色画面向右移动的问题 修正功能
// 根据版本号 (105 或 201) 和配置决定是否应用补丁
void Mod_UI_offset_fix_init(int game_version);

// 屏幕震动效果禁用功能
void Mod_Screen_shake_effect_init(int game_version);

#endif