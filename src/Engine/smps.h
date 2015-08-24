#ifndef __SMPS_H__
#define __SMPS_H__

// --- SMPS functions for external usage ---

#include <stdtype.h>
#include "smps_structs.h"

void InitDriver(void);
void DeinitDriver(void);

#define UPDATEEVT_VINT	0x00
#define UPDATEEVT_TIMER	0x01
void UpdateAll(UINT8 Event);

void PlayMusic(SMPS_SET* SmpsFileSet);
void PlaySFX(SMPS_SET* SmpsFileSet, UINT8 SpecialSFX);

void FadeOutMusic(void);
void FadeOutMusic_Custom(UINT8 StepCnt, UINT8 DelayFrames);

void StopAllSound(void);

void SetDACState(UINT8 DacOn);
UINT8 SmpsIsRunning(void);

#define SMPSVAR_COMMUNICATION	0x00
#define SMPSVAR_CONDIT_JUMP		0x01
#define SMPSVAR_RESTORE_REQ		0x02
#define SMPSVAR_MUSSTATE_USE	0x03
UINT8* SmpsGetVariable(UINT8 Type);


// for smps_extra.c
typedef void (*SMPS_CB_SIGNAL)(void);

#define SMPSCB_START	0x00	// called when starting a song
#define SMPSCB_STOP		0x01	// called when stopping a song
#define SMPSCB_LOOP		0x02	// called when loop counter changes
#define SMPSCB_CNTDOWN	0x03	// for user-defined countdown (controlled by Sound.c)
#define SMPSCB_COMM_VAR	0x10	// called when communication variable changes
#define SMPSCB_OFF		0xFF	// special: disables all callback routines
void SMPSExtra_SetCallbacks(UINT8 cbType, SMPS_CB_SIGNAL cbFunc);


#endif	// __SMPS_H__
