#ifndef FPS_LIMIT_H
#define FPS_LIMIT_H

#include <windows.h>

// 帧率限制：Hook 每帧函数入口，按目标帧间隔节流渲染节奏，
// 避免现代 CPU + ddraw wrapper 下无限快速刷新产生无效 blt。
// 调研结论见 docs/reverse_kb/systems/fps_limit_201.md
void Mod_FpsLimit_Init(int game_version);

#endif
