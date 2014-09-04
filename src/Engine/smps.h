#ifndef __SMPS_H__
#define __SMPS_H__

// --- SMPS functions for external usage ---

#include "smps_structs.h"

void InitDriver(void);
void DeinitDriver(void);

#define UPDATEEVT_VINT	0x00
#define UPDATEEVT_TIMER	0x01
void UpdateAll(UINT8 Event);

void PlayMusic(SMPS_SET* SmpsFileSet);
void PlaySFX(SMPS_SET* SmpsFileSet, UINT8 SpecialSFX);

void FadeOutMusic(void);

void StopAllSound(void);

void SetDACState(UINT8 DacOn);
UINT8 SmpsIsRunning(void);

#define SMPSVAR_COMMUNICATION	0x00
#define SMPSVAR_CONDIT_JUMP		0x01
UINT8* SmpsGetVariable(UINT8 Type);

#endif	// __SMPS_H__
