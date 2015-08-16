// Extra SMPS Routines
// -------------------
// Written by Valley Bell, 2014-2015
// Additional functions not necessary for SMPS, but for VGM logging and loop detection.
//
// Note: Loop detection is still far from perfect and still fails quite often.

#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stddef.h>	// for NULL
#include <stdlib.h>
#include <string.h>

#include <stdtype.h>
#include "smps.h"
#include "smps_structs_int.h"
#include "dac.h"
#ifdef ENABLE_VGM_LOGGING
#include "../vgmwrite.h"
#endif

void Extra_SongStart(UINT8 isRestore);
void Extra_SongStop(UINT8 fromInit);
#ifdef ENABLE_LOOP_DETECTION
static void LoopStartSignal(void);
static void LoopEndSignal(void);

void Extra_StopCheck(void);
void Extra_LoopStartCheck(TRK_RAM* Trk);
void Extra_LoopEndCheck(TRK_RAM* Trk);
#endif
#ifdef ENABLE_VGM_LOGGING
static void DumpDACSounds(DAC_CFG* DACDrv);
#endif


extern volatile UINT32 SMPS_PlayingTimer;
volatile INT32 SMPS_LoopCntr;
extern volatile INT32 SMPS_StoppedTimer;

static SMPS_CB_SIGNAL CB_Start = NULL;
static SMPS_CB_SIGNAL CB_Stop = NULL;
static SMPS_CB_SIGNAL CB_Loop = NULL;
SMPS_CB_SIGNAL CB_Signal = NULL;	// not static due to being used by Sound.c
SMPS_CB_SIGNAL CB_CommVar = NULL;

#ifdef ENABLE_VGM_LOGGING
extern UINT8 VGM_DataBlkCompress;
extern UINT8 VGM_NoLooping;
#endif

extern SND_RAM SmpsRAM;
#ifdef ENABLE_LOOP_DETECTION
static struct loop_state
{
	UINT8 Activated;
	UINT8 LoopTrk;	// ID of track with longest loop
	UINT16 TrkMaskI;	// Track Mask (inital value)
	UINT16 TrkMaskE;	// Track Mask (loop end check)
	SMPS_LOOPPTR TrkPos[MUS_TRKCNT];
} LoopState;
#endif


void SMPSExtra_SetCallbacks(UINT8 cbType, SMPS_CB_SIGNAL cbFunc)
{
	switch(cbType)
	{
	case SMPSCB_START:
		CB_Start = cbFunc;
		break;
	case SMPSCB_STOP:
		CB_Stop = cbFunc;
		break;
	case SMPSCB_LOOP:
		CB_Loop = cbFunc;
		break;
	case SMPSCB_CNTDOWN:
		CB_Signal = cbFunc;
		break;
	case SMPSCB_COMM_VAR:
		CB_CommVar = cbFunc;
		break;
	case SMPSCB_OFF:
		CB_Start = NULL;
		CB_Stop = NULL;
		CB_Loop = NULL;
		CB_Signal = NULL;
		CB_CommVar = NULL;
		break;
	}
	
	return;
}

void Extra_SongStart(UINT8 isRestore)
{
#ifdef ENABLE_VGM_LOGGING
	const DRUM_LIB* DrumLib;
	UINT8 CurIdx;
	UINT8 VgmChipMask;
#endif
	
	SMPS_LoopCntr = 0;
	SMPS_StoppedTimer = -1;
	if (isRestore)
	{
		SMPS_PlayingTimer = 0;	// we're already within the initial frame
		return;	// no callback, no VGM starting when restoring from saves
	}
	SMPS_PlayingTimer = -1;
	if (CB_Start != NULL)
		CB_Start();
	
#ifdef ENABLE_VGM_LOGGING
	VgmChipMask = 0x00;
	for (CurIdx = 0; CurIdx < MUS_TRKCNT; CurIdx ++)
	{
		if ((SmpsRAM.MusicTrks[CurIdx].ChannelMask & 0xF8) == 0x18)
			VgmChipMask |= 0;	//VGM_CEN_32X_PWM;	// Right now I still don't have true PWM support.
	}
	if (SmpsRAM.MusSet != NULL && SmpsRAM.MusSet->Cfg != NULL)
	{
		DrumLib = &SmpsRAM.MusSet->Cfg->DrumLib;
		for (CurIdx = 0; CurIdx < DrumLib->DrumCount; CurIdx ++)
		{
#ifndef DISABLE_NECPCM
			if (DrumLib->DrumData[CurIdx].Type == DRMTYPE_NECPCM)
				VgmChipMask |= VGM_CEN_PICOPCM;
#endif
		}
	}
	vgm_set_chip_enable(VgmChipMask);
	
	vgm_dump_start();
	if (SmpsRAM.MusSet != NULL)
		DumpDACSounds((DAC_CFG*)&SmpsRAM.MusSet->Cfg->DACDrv);
#endif	// ENABLE_VGM_LOGGING
	
	return;
}

void Extra_SongStop(UINT8 fromInit)
{
#ifdef ENABLE_VGM_LOGGING
	vgm_set_loop(0);
	vgm_dump_stop();
#endif
	SMPS_LoopCntr = -1;
	SMPS_StoppedTimer = 0;
	if (fromInit)
		return;
	
	if (CB_Stop != NULL && SmpsRAM.MusSet != NULL)
		CB_Stop();
	
	return;
}

#ifdef ENABLE_LOOP_DETECTION
static void LoopStartSignal(void)
{
#ifdef ENABLE_VGM_LOGGING
	if (! VGM_NoLooping)
		vgm_set_loop(1);
#endif
	
	if (CB_Loop != NULL)
		CB_Loop();
	SMPS_LoopCntr = 1;
	
	return;
}

static void LoopEndSignal(void)
{
#ifdef ENABLE_VGM_LOGGING
	if (! VGM_NoLooping)
		vgm_dump_stop();
#endif
	
	if (CB_Loop != NULL)
		CB_Loop();
	SMPS_LoopCntr ++;
	
	return;
}
#endif	// ENABLE_LOOP_DETECTION


void Extra_StopCheck(void)
{
	UINT8 CurTrk;
	UINT16 TrkMask;
	
	if (SmpsRAM.TrkMode != TRKMODE_MUSIC)
		return;
	
	TrkMask = 0x0000;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		if (SmpsRAM.MusicTrks[CurTrk].PlaybkFlags & PBKFLG_ACTIVE)
			TrkMask |= (1 << CurTrk);
	}
	
	if (! TrkMask)
		Extra_SongStop(0x00);
	
	return;
}

#ifdef ENABLE_LOOP_DETECTION
void Extra_LoopInit(void)
{
	UINT8 CurTrk;
	
	LoopState.Activated = 0xFF;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		if (SmpsRAM.MusicTrks[CurTrk].LoopOfs.Ptr)
		{
			LoopState.Activated = 0x00;
			break;
		}
	}
	LoopState.LoopTrk = 0xFF;
	LoopState.TrkMaskI = 0x0000;
	
	return;
}

void Extra_LoopStartCheck(TRK_RAM* Trk)
{
	UINT8 TrkID;
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	
	if (SmpsRAM.TrkMode != TRKMODE_MUSIC)
		return;
	if (LoopState.Activated)
		return;
	if (! Trk->LoopOfs.Ptr)
		return;
	
	TrkID = (UINT8)(Trk - SmpsRAM.MusicTrks);
	if (Trk->Pos != Trk->LoopOfs.Ptr)
		return;
	
	LoopState.TrkMaskI |= (1 << TrkID);
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		if (! (TempTrk->PlaybkFlags & PBKFLG_ACTIVE) || ! TempTrk->LoopOfs.Ptr)
			continue;
		
		if (! (LoopState.TrkMaskI & (1 << CurTrk)))
			return;	// One channel is still outside of a loop
		
		LoopState.TrkPos[CurTrk].Ptr = TempTrk->Pos;
		LoopState.TrkPos[CurTrk].SrcOfs = TempTrk->LoopOfs.SrcOfs;
	}
	
	LoopState.Activated = 0x01;
	LoopState.LoopTrk = TrkID;
	LoopState.TrkMaskE = LoopState.TrkMaskI;
	LoopStartSignal();
	
	return;
}

void Extra_LoopEndCheck(TRK_RAM* Trk)
{
	UINT8 TrkID;
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	
	if (SmpsRAM.TrkMode != TRKMODE_MUSIC)
		return;
	if (LoopState.Activated < 0x01 || LoopState.Activated == 0xFF)
		return;
	
	TrkID = (UINT8)(Trk - SmpsRAM.MusicTrks);
	if (Trk->LastJmpPos == LoopState.TrkPos[TrkID].SrcOfs)
		LoopState.TrkMaskE &= ~(1 << TrkID);	// need to pass the "loop jump"
	if (TrkID != LoopState.LoopTrk)
		return;
	
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		if (! (TempTrk->PlaybkFlags & PBKFLG_ACTIVE) || ! TempTrk->LoopOfs.Ptr)
			continue;
		
		if (TempTrk->Pos != LoopState.TrkPos[CurTrk].Ptr)
			return;
	}
	// If I omit this check, I get false positives due to "F7 Loops",
	// which can jump back to the loop offset. (-> Chou Yakyuu Miracle Nine)
	if (LoopState.TrkMaskE)
		return;
	
	LoopState.Activated = 0x02;
	LoopState.TrkMaskE = LoopState.TrkMaskI;
	LoopEndSignal();
	
	return;
}
#endif	// ENABLE_LOOP_DETECTION

#ifdef ENABLE_VGM_LOGGING
static void DumpDACSounds(DAC_CFG* DACDrv)
{
	UINT8 CurSnd;
	UINT32 CurSmpl;
	DAC_SAMPLE* TempSmpl;
	const UINT8* SmplPnt;
	UINT32 SndLen;
	UINT8* SndBuffer;
	UINT8 NibbleData;
	UINT8 DPCMState;
	UINT8 SmplID;
	const UINT8* OldDPCMTbl;
	const UINT8* CurDPCMTbl;
	
	if (! Enable_VGMDumping)
		return;
	
	// Check, if there is a sample used
	SmplID = 0x00;
	for (CurSnd = 0; CurSnd < DACDrv->SmplCount; CurSnd ++)
	{
		TempSmpl = &DACDrv->Smpls[CurSnd];
		if (TempSmpl->Size && DACDrv->Smpls[CurSnd].UsageID != 0xFF)
		{
			SmplID = 0x01;
			break;
		}
	}
	if (! SmplID)
		return;
	
	OldDPCMTbl = NULL;
	if (VGM_DataBlkCompress)
	{
		// Check, if there is a compressed sample
		for (CurSnd = 0; CurSnd < DACDrv->SmplCount; CurSnd ++)
		{
			TempSmpl = &DACDrv->Smpls[CurSnd];
			if (! TempSmpl->Size || DACDrv->Smpls[CurSnd].UsageID == 0xFF)
				continue;
			
			if (TempSmpl->Compr == COMPR_DPCM)
			{
				// write DPCM Delta-Table
				vgm_write_large_data(VGMC_YM2612, 0xFF, 0x10, 0, 0, TempSmpl->DPCMArr);
				OldDPCMTbl = TempSmpl->DPCMArr;
				break;
			}
		}
	}
	
	SmplID = 0x00;
	for (CurSnd = 0; CurSnd < DACDrv->SmplCount; CurSnd ++)
	{
		TempSmpl = &DACDrv->Smpls[CurSnd];
		if (! TempSmpl->Size || DACDrv->Smpls[CurSnd].UsageID == 0xFF)
			continue;
		
		if (DACDrv->Smpls[CurSnd].UsageID == 0xFE)
		{
			DACDrv->Smpls[CurSnd].UsageID = SmplID;
			SmplID ++;
		}
		
		CurDPCMTbl = TempSmpl->DPCMArr;
		if (TempSmpl->Compr == COMPR_PCM || VGM_DataBlkCompress)
		{
			if (TempSmpl->Compr == COMPR_PCM)
				vgm_write_large_data(VGMC_YM2612, 0x00, TempSmpl->Size, 0, 0, TempSmpl->Data);
			else
			{
				if (OldDPCMTbl == NULL || memcmp(CurDPCMTbl, OldDPCMTbl, 0x10))
				{
					// write new DPCM Delta-Table
					vgm_write_large_data(VGMC_YM2612, 0xFF, 0x10, 0, 0, CurDPCMTbl);
					OldDPCMTbl = CurDPCMTbl;
				}
				vgm_write_large_data(VGMC_YM2612, 0x01, TempSmpl->Size, TempSmpl->Size << 1, 0x80, TempSmpl->Data);
			}
		}
		else
		{
			SndLen = TempSmpl->Size << 1;
			SmplPnt = TempSmpl->Data;
			SndBuffer = (UINT8*)malloc(SndLen);
			
			DPCMState = 0x80;
			for (CurSmpl = 0; CurSmpl < SndLen; CurSmpl += 0x02)
			{
				NibbleData = (SmplPnt[CurSmpl >> 1] >> 4) & 0x0F;
				DPCMState += CurDPCMTbl[NibbleData];
				SndBuffer[CurSmpl + 0x00] = DPCMState;
				
				NibbleData = (SmplPnt[CurSmpl >> 1] >> 0) & 0x0F;
				DPCMState += CurDPCMTbl[NibbleData];
				SndBuffer[CurSmpl + 0x01] = DPCMState;
			}
			
			vgm_write_large_data(VGMC_YM2612, 0x00, SndLen, 0, 0, SndBuffer);
			free(SndBuffer);
		}
	}
	
	vgm_write_stream_data_command(0x00, 0x00, 0x02002A);
	vgm_write_stream_data_command(0x00, 0x01, 0x00);
	
	return;
}
#endif	// ENABLE_VGM_LOGGING
