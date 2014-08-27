// Extra SMPS Routines
// -------------------
// Written by Valley Bell, 2014
// Additional functions not necessary for SMPS, but for VGM logging and loop detection.
//
// Note: Loop detection is still far from perfect and still fails quite often.

#include <stddef.h>	// for NULL
#include <malloc.h>
#include <memory.h>
#include "stdtype.h"
#include "smps_structs_int.h"
#include "dac.h"
#include "../vgmwrite.h"

//void StartSignal(void);
//void StopSignal(void);
//void LoopStartSignal(void);
//void LoopEndSignal(void);

//void Extra_StopCheck(void);
//void Extra_LoopStartCheck(TRK_RAM* Trk);
//void Extra_LoopEndCheck(TRK_RAM* Trk);
static void DumpDACSounds(DAC_CFG* DACDrv);

void FinishedSongSignal(void);	// from main.c

extern UINT32 PlayingTimer;
INT32 LoopCntr;
extern INT32 StoppedTimer;

extern UINT8 VGM_DataBlkCompress;

extern SND_RAM SmpsRAM;
static struct loop_state
{
	UINT8 Activated;
	UINT16 TrkMask;
	UINT16 TrkPos[MUS_TRKCNT];
} LoopState;


void StartSignal(void)
{
	PlayingTimer = -1;
	LoopCntr = 0;
	StoppedTimer = -1;
	
	vgm_dump_start();
	DumpDACSounds(&SmpsRAM.MusCfg.DACDrv);
	
	return;
}

void StopSignal(void)
{
	vgm_set_loop(0);
	vgm_dump_stop();
	LoopCntr = -1;
	StoppedTimer = 0;
	
	return;
}

void LoopStartSignal(void)
{
	vgm_set_loop(1);
	LoopCntr = 1;
	
	return;
}

void LoopEndSignal(void)
{
	vgm_dump_stop();
	if (LoopCntr >= 2)
		FinishedSongSignal();
	LoopCntr ++;
	
	return;
}


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
		StopSignal();
	
	return;
}

void Extra_LoopInit(void)
{
	UINT8 CurTrk;
	
	LoopState.Activated = 0xFF;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		if (SmpsRAM.MusicTrks[CurTrk].LoopOfs)
		{
			LoopState.Activated = 0x00;
			break;
		}
	}
	LoopState.TrkMask = 0x0000;
	
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
	if (! Trk->LoopOfs)
		return;
	
	TrkID = (UINT8)(Trk - SmpsRAM.MusicTrks);
	if (Trk->Pos != Trk->LoopOfs)
		return;
	
	LoopState.TrkMask |= (1 << TrkID);
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		if (! (TempTrk->PlaybkFlags & PBKFLG_ACTIVE) || ! TempTrk->LoopOfs)
			continue;
		
		if (! (LoopState.TrkMask & (1 << CurTrk)))
			return;	// One channel is still outside of a loop
		
		LoopState.TrkPos[CurTrk] = TempTrk->Pos;
	}
	
	LoopState.Activated = 0x01;
	LoopState.TrkMask = TrkID;
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
	if (TrkID != LoopState.TrkMask)
		return;
	
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		if (! (TempTrk->PlaybkFlags & PBKFLG_ACTIVE) || ! TempTrk->LoopOfs)
			continue;
		
		if (TempTrk->Pos != LoopState.TrkPos[CurTrk])
			return;
	}
	
	LoopState.Activated = 0x02;
	LoopEndSignal();
	
	return;
}

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
	CurDPCMTbl = NULL;
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
				OldDPCMTbl = CurDPCMTbl;
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
