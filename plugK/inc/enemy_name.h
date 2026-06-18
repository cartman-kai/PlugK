#ifndef ENEMY_NAME_H
#define ENEMY_NAME_H

typedef int(__fastcall *PK_DrawTextFn)(void *pThis, void *_edx, int surface, int x, int y, const char *text, int mode);

void EnemyName_AfterDrawText(void *caller, PK_DrawTextFn draw_text, void *pThis, void *_edx, int surface, int x, int y, const char *text, int mode);
void Mod_Enemy_Name_Init(int game_version);

#endif // ENEMY_NAME_H
