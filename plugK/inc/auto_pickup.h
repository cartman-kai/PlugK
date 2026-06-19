#ifndef AUTO_PICKUP_H
#define AUTO_PICKUP_H

#include <windows.h>

void Mod_Auto_Pickup_Init(int game_version);
void AutoPickup_OnInputFrame(void);
void AutoPickup_Toggle(void);
void AutoPickup_CycleMode(void);

#endif
