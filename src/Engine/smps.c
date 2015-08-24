// SMPS Engine
// -----------
// Written by Valley Bell, 2014-2015
//
// Originally based on a disassembly of Battletoads. (SMPS Z80 Type 1)
// This should support pretty much all MegaDrive variants of SMPS Z80 and 68k.

#define _CRTDBG_MAP_ALLOC
#include <stddef.h>	// for NULL
#include <stdio.h>
#include <string.h>
#include <stdlib.h>	// for SMPS_SET memory management

#include <common_def.h>
#include "smps_structs.h"
#include "smps_structs_int.h"
#include "smps.h"
#include "smps_int.h"
#include "../Sound.h"
#include "dac.h"
#ifndef DISABLE_NECPCM
#include "necpcm.h"
#endif

UINT8 ym2612_r(UINT8 ChipID, UINT32 offset);
#define ReadFM()				ym2612_r(0x00, 0x00)
#define WriteFMI(Reg, Data)		ym2612_fm_write(0x00, 0x00, Reg, Data)
#define WriteFMII(Reg, Data)	ym2612_fm_write(0x00, 0x01, Reg, Data)
#define WritePSG(Data)			sn76496_psg_write(0x00, Data)
int upd7759_busy_r(UINT8 ChipID);


#ifndef DISABLE_DEBUG_MSGS
extern UINT8 DebugMsgs;
#else
#define DebugMsgs	0
#endif

extern UINT32 SMPS_PlayingTimer;


// from smps_extra.c
void Extra_SongStart(UINT8 isRestore);
void Extra_SongStop(UINT8 fromInit);
#ifdef ENABLE_LOOP_DETECTION
void Extra_LoopInit(void);
void Extra_LoopStartCheck(TRK_RAM* Trk);
//void Extra_LoopEndCheck(TRK_RAM* Trk);
#else
#define Extra_LoopInit()
#define Extra_LoopStartCheck(x)
#define Extra_LoopEndCheck(x)
#endif


// Function Prototypes
// -------------------
INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT16 ReadRawPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_SET* SmpsSet);
INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_SET* SmpsSet);


#define VALID_FREQ(x)	((x & 0xFF00) != 0x8000)

// Variables
// ---------
SND_RAM SmpsRAM;
MUS_STATE MusicSaveState;

//static const UINT8 VolOperators_DEF[] = {0x40, 0x48, 0x44, 0x4C, 0x00};
//static const UINT8 VolOperators_HW[] = {0x40, 0x44, 0x48, 0x4C, 0x00};
static const UINT8 AlgoOutMask[0x08] =
	{0x08, 0x08, 0x08, 0x08, 0x0C, 0x0E, 0x0E, 0x0F};
// Note: bit order is 40 44 48 4C (Bit 0 = 40, Bit 3 = 4C)
static const UINT8 OpList_DEF[] = {0x00, 0x08, 0x04, 0x0C};	// default SMPS operator order
static const UINT8 OpList_HW[]  = {0x00, 0x04, 0x08, 0x0C};	// hardware operator order

static const UINT8 FreqReg_2Op[2][4] =
{	{0xAD, 0xA9, 0xAE, 0xAA},
	{0xAC, 0xA8, 0xA6, 0xA2}};

#define CHNMODE_DEF		0x00	// default (4 bytes per header)
#define CHNMODE_PSG		0x01	// PSG (6 bytes per header)
#define CHNMODE_DRM		0x10	// drum (can skip drum channels)


INLINE UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

INLINE UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

INLINE UINT16 ReadRawPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg)
{
	if ((SmpsCfg->PtrFmt & PTRFMT_EMASK) == PTRFMT_BE)
		return ReadBE16(Data);
	else
		return ReadLE16(Data);
}

INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_SET* SmpsSet)
{
	return ReadRawPtr(Data, SmpsSet->Cfg) - SmpsSet->SeqBase;
}

INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_SET* SmpsSet)
{
	UINT16 PtrVal;
	UINT8 Offset;
	
	PtrVal = ReadRawPtr(Data, SmpsSet->Cfg);
	Offset = SmpsSet->Cfg->PtrFmt & PTRFMT_OFSMASK;
	if (! Offset)
	{
		// absolute
		return PtrVal - SmpsSet->SeqBase;
	}
	else
	{
		// relative
		Offset --;
		return PtrPos + Offset + (INT16)PtrVal;
	}
}

void WriteFMMain(const TRK_RAM* Trk, UINT8 Reg, UINT8 Data)
{
	UINT8 FnlReg;
	
	if (Trk->ChannelMask & 0x80)
		return;	// PSG channel - return
	
	//WriteFMIorII:
	if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
		return;
	
	// The actual driver uses ADD instead of OR, but OR is always safe.
	FnlReg = Reg | (Trk->ChannelMask & 0x03);
	if (! (Trk->ChannelMask & 0x04))
		WriteFMI(FnlReg, Data);
	else
		WriteFMII(FnlReg, Data);
	
	return;
}



void InitDriver(void)
{
	memset(&SmpsRAM, 0x00, sizeof(SND_RAM));
	memset(&MusicSaveState, 0x00, sizeof(MUS_STATE));
	SmpsRAM.LockTimingMode = 0xFF;
	ym2612_timer_mask(0x00);
	StopAllSound();
	
	return;
}

void DeinitDriver(void)
{
	UINT8 CurSet;
	
	StopAllSound();
	
	FreeSMPSFileRef_Zero(&SmpsRAM.MusSet);
	for (CurSet = 0; CurSet < SFX_TRKCNT + SPCSFX_TRKCNT; CurSet ++)
		FreeSMPSFileRef_Zero(&SmpsRAM.SFXSet[CurSet]);
	if (MusicSaveState.MusSet != NULL)
	{
		MusicSaveState.InUse = 0x00;
		MusicSaveState.MusSet->UsageCounter = 0;
		FreeSMPSFileRef_Zero(&MusicSaveState.MusSet);
	}
	
	memset(&SmpsRAM, 0x00, sizeof(SND_RAM));
	memset(&MusicSaveState, 0x00, sizeof(MUS_STATE));
	SmpsRAM.LockTimingMode = 0xFF;
	ym2612_timer_mask(0x00);
	
	return;
}

static void FreeSMPSFileRef_Zero(SMPS_SET** SmpsSetRef)
{
	// Free an SMPS File structure, if the Usage Counter dropped to 0.
	if (SmpsSetRef == NULL || *SmpsSetRef == NULL)
		return;	// pointer invalid/unused
	if ((*SmpsSetRef)->UsageCounter)
		return;	// there are remaining references
	
	FreeSMPSFile(*SmpsSetRef);	// ensure that the internal data is free'd
	free(*SmpsSetRef);	// 0 references left - free data
	*SmpsSetRef = NULL;
	
	return;
}

static UINT8 CleanSmpsTrack(TRK_RAM* Trk)
{
	UINT8 ResVal;
	
	ResVal = 0;
	if (Trk->SmpsSet != NULL && ! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
	{
		Trk->SmpsSet->UsageCounter --;
		if (! Trk->SmpsSet->UsageCounter)
			ResVal = 1;
		Trk->SmpsSet = NULL;
	}
	
	return ResVal;
}

static void CleanSmpsFiles(void)
{
	UINT8 CurTrk;
	UINT8 CurSet;
	UINT8 MusTrkPlaying;
	
	CurSet = 0;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		for (CurSet = 0x00; CurSet < 0x02; CurSet ++)
		{
			if ((SmpsRAM.MusicTrks[CurTrk].PlaybkFlags & PBKFLG_ACTIVE) &&
				SmpsRAM.MusicTrks[CurTrk].SmpsSet == &SmpsRAM.DrumSet[CurSet])
				return;	// delay the cleanup until the drums finished playing (else they might hang)
		}
	}
	
	CurSet = 0;
	MusTrkPlaying = 0;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		CurSet += CleanSmpsTrack(&SmpsRAM.MusicTrks[CurTrk]);
		if (SmpsRAM.MusicTrks[CurTrk].PlaybkFlags & PBKFLG_ACTIVE)
			MusTrkPlaying ++;
	}
	for (CurTrk = 0; CurTrk < SFX_TRKCNT; CurTrk ++)
		CurSet += CleanSmpsTrack(&SmpsRAM.SFXTrks[CurTrk]);
	for (CurTrk = 0; CurTrk < SPCSFX_TRKCNT; CurTrk ++)
		CurSet += CleanSmpsTrack(&SmpsRAM.SpcSFXTrks[CurTrk]);
	
	if (! MusTrkPlaying && SmpsRAM.MusSet != NULL)
	{
		if (SmpsRAM.MusSet->UsageCounter > 0 && SmpsRAM.MusSet == MusicSaveState.MusSet)
			SmpsRAM.MusSet = NULL;	// remove main reference and keep Save State reference only
	}
	if (! CurSet)
		return;
	
	FreeSMPSFileRef_Zero(&SmpsRAM.MusSet);
	for (CurSet = 0; CurSet < SFX_TRKCNT + SPCSFX_TRKCNT; CurSet ++)
		FreeSMPSFileRef_Zero(&SmpsRAM.SFXSet[CurSet]);
	
	return;
}


static void ResetYMTimerA(void)
{
	WriteFMI(0x25, SmpsRAM.TimerAVal & 0x03);
	WriteFMI(0x24, SmpsRAM.TimerAVal >> 2);
	WriteFMI(0x27, SmpsRAM.SpcFM3Mode | 0x1F);
	
	return;
}

static void ResetYMTimerB(void)
{
	WriteFMI(0x26, SmpsRAM.TimerBVal);
	WriteFMI(0x27, SmpsRAM.SpcFM3Mode | 0x2F);
	
	return;
}


void UpdateAll(UINT8 Event)
{
	UINT8 TimerState;
	
	switch(Event)
	{
	case UPDATEEVT_VINT:
		if (SmpsRAM.TimingMode)
			break;
		
		UpdateMusic();
		UpdateSFX();
		break;
	case UPDATEEVT_TIMER:
		if (! SmpsRAM.TimingMode)
			break;
		
		TimerState = ReadFM();
		if (SmpsRAM.TimingMode & 0x80)
		{
			// The original driver updates SFX first, but it looks cleaner this way.
			if (TimerState & 0x01)
			{
				UpdateMusic();
				ResetYMTimerA();
			}
			if (TimerState & 0x02)
			{
				UpdateSFX();
				ResetYMTimerB();
			}
		}
		else if (SmpsRAM.TimingMode == 0x20)	// special Timing mode not present in the original SMPS Z80
		{
			if (TimerState & 0x01)
			{
				UpdateMusic();
				UpdateSFX();
				ResetYMTimerA();
			}
		}
		else	// Timing Mode 40
		{
			if (TimerState & 0x02)
			{
				UpdateMusic();
				UpdateSFX();
				ResetYMTimerB();
			}
		}
		break;
	}
	
	return;
}

void UpdateMusic(void)
{
	UINT8 CurTrk;
	
	if (SMPS_PlayingTimer == -1)
		SMPS_PlayingTimer = 0;
	
	DoPause();
	if (SmpsRAM.PauseMode)
		return;
	
	if (SmpsRAM.LoadSaveRequest)
	{
		SmpsRAM.LoadSaveRequest = 0x00;
		RestoreMusic(&MusicSaveState);
	}
	if (SmpsRAM.MusSet == NULL)
		return;
	
#ifndef DISABLE_NECPCM
	if (SmpsRAM.NecPcmOverride)
	{
		if (upd7759_busy_r(0x00))	// actually returns "ready"
			SmpsRAM.NecPcmOverride = 0x00;	// PCM sound finished - remove PCM SFX lock
	}
#endif
	
	SmpsRAM.MusMultUpdate = 1;
	DoTempo();
	if (! SmpsRAM.MusMultUpdate)
		return;
	
	DoSpecialFade();
	DoFade(0x01);	// FadeOut
	if (SmpsRAM.MusSet == NULL)	// The FadeOut routine calls StopAllSound, so it NULLs the music set.
		return;
	DoFade(0x00);	// FadeIn
	
	SmpsRAM.TrkMode = TRKMODE_MUSIC;
	while(SmpsRAM.MusMultUpdate)
	{
		SmpsRAM.MusMultUpdate --;
		for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
			UpdateTrack(&SmpsRAM.MusicTrks[CurTrk]);
		
		for (CurTrk = 0; CurTrk < 2; CurTrk ++)
			Update2OpDrumTrack(&SmpsRAM.MusDrmTrks[CurTrk]);
	}
	
	CleanSmpsFiles();
	return;
}

void UpdateSFX(void)
{
	UINT8 CurTrk;
	
	if (SmpsRAM.MusSet != NULL && SmpsRAM.MusSet->Cfg->TempoMode == TEMPO_TOUT_REV)
	{
		TRK_RAM* TempTrk;
		
		for (CurTrk = TRACK_MUS_PSG1; CurTrk < TRACK_MUS_PSG_END; CurTrk ++)
		{
			TempTrk = &SmpsRAM.MusicTrks[CurTrk];
			if (TempTrk->PlaybkFlags & PBKFLG_ACTIVE)
				UpdatePSGVolume(TempTrk, 0x00);
		}
	}
	
	SmpsRAM.TrkMode = TRKMODE_SFX;
	for (CurTrk = 0; CurTrk < SFX_TRKCNT; CurTrk ++)
		UpdateTrack(&SmpsRAM.SFXTrks[CurTrk]);
	
	SmpsRAM.TrkMode = TRKMODE_SPCSFX;
	for (CurTrk = 0; CurTrk < SPCSFX_TRKCNT; CurTrk ++)
		UpdateTrack(&SmpsRAM.SpcSFXTrks[CurTrk]);
	
	CleanSmpsFiles();
	return;
}


INLINE void UpdateTrack(TRK_RAM* Trk)
{
	if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
		return;
	
	if (Trk->ChannelMask & 0x80)
	{
		if (Trk->ChannelMask & 0x10)
			UpdatePSGNoiseTrack(Trk);
		else
			UpdatePSGTrack(Trk);
	}
	else if (Trk->ChannelMask & 0x10)
	{
		if (Trk->ChannelMask & 0x08)
			UpdatePWMTrack(Trk);
		else
			UpdateDrumTrack(Trk);
	}
	else
		UpdateFMTrack(Trk);
	
	return;
}

static void UpdateFMTrack(TRK_RAM* Trk)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	UINT16 Freq;
	UINT8 FreqUpdate;
	
	Trk->RemTicks --;
	if (! Trk->RemTicks)
	{
		TrkUpdate_Proc(Trk);
		if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
			return;	// return after TRK_END command (the driver POPs some addresses from the stack instead)
		if (Trk->PlaybkFlags & PBKFLG_ATREST)
			return;
		
		PrepareModulat(Trk);
		if (SmpsCfg->DelayFreq == DLYFREQ_RESET && ! Trk->Frequency)
		{
			Trk->PlaybkFlags |= PBKFLG_ATREST;
			return;
		}
		if (! SmpsCfg->FMOctWrap)
			Freq = Trk->Frequency + Trk->Detune;
		else
			Freq = DoPitchSlide(Trk);
		
		//if (SmpsCfg->ModAlgo == MODULAT_Z80)
		if (SmpsCfg->ModAlgo & 0x01)
		{
			DoModulation(Trk, &Freq);
		}
		else
		{
			// Ristar only, improves sound slightly
			if (Trk->ModEnv & 0x7F)
				Freq += Trk->ModEnvCache;
			if (Trk->ModEnv & 0x80)
				Freq += Trk->CstMod.Freq;
		}
		
		SendFMFrequency(Trk, Freq);
		DoNoteOn(Trk);
	}
	else
	{
		DoPanAnimation(Trk, 1);
		ExecLFOModulation(Trk);
		if (Trk->PlaybkFlags & PBKFLG_ATREST)
			return;
		
		DoFMVolEnv(Trk);
		
		if (DoNoteStop(Trk))
		{
			Trk->PlaybkFlags |= PBKFLG_ATREST;
			DoNoteOff(Trk);
			return;
		}
		
		Freq = DoPitchSlide(Trk);
		if (Trk->PlaybkFlags & PBKFLG_LOCKFREQ)
			return;
		
		FreqUpdate = DoModulation(Trk, &Freq);
		if ((Trk->PlaybkFlags & PBKFLG_PITCHSLIDE) || FreqUpdate == 0x01)
			SendFMFrequency(Trk, Freq);
	}
	
	return;
}

static void UpdatePSGTrack(TRK_RAM* Trk)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	UINT16 Freq;
	UINT8 FreqUpdate;
	UINT8 WasNewNote;
	
	Trk->RemTicks --;
	if (! Trk->RemTicks)
	{
		TrkUpdate_Proc(Trk);
		if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
			return;	// return after TRK_END command (the driver POPs some addresses from the stack instead)
		if (Trk->PlaybkFlags & PBKFLG_ATREST)
			return;
		
		PrepareModulat(Trk);
		PrepareADSR(Trk);
		if (SmpsCfg->DelayFreq == DLYFREQ_RESET && (Trk->Frequency & 0x8000))
		{
			Trk->PlaybkFlags |= PBKFLG_ATREST;
			return;
		}
		if (! SmpsCfg->FMOctWrap)
			Freq = Trk->Frequency + Trk->Detune;
		else
			Freq = DoPitchSlide(Trk);
		
		//if (SmpsCfg->ModAlgo == MODULAT_Z80)
		if (SmpsCfg->ModAlgo & 0x01)
		{
			FreqUpdate = DoModulation(Trk, &Freq);
		}
		else
		{
			// Ristar only, improves sound slightly
			FreqUpdate = 0x00;	// SMPS 68k
			if (Trk->ModEnv & 0x7F)
				Freq += Trk->ModEnvCache;
			if (Trk->ModEnv & 0x80)
				Freq += Trk->CstMod.Freq;
		}
		WasNewNote = 0x01;
	}
	else
	{
		// Note: Trying to return here causes wrong behaviour for unusual cases
		//       like 80 48 E7 0C (from Mega Man Wily Wars - 3E Introduction)
		//if (Trk->PlaybkFlags & PBKFLG_ATREST)
		//	return;
		
		if (DoNoteStop(Trk))
		{
			DoPSGNoteOff(Trk, 0x01);	// Master System SMPS
			return;
		}
		Freq = DoPitchSlide(Trk);
		FreqUpdate = DoModulation(Trk, &Freq);
		WasNewNote = 0x00;
	}
	
	if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
		return;
	
	if (WasNewNote || (Trk->PlaybkFlags & PBKFLG_PITCHSLIDE) || FreqUpdate == 0x01)
		SendPSGFrequency(Trk, Freq);
	
	UpdatePSGVolume(Trk, WasNewNote);
	return;
}

static void UpdatePSGVolume(TRK_RAM* Trk, UINT8 WasNewNote)
{
	UINT8 FinalVol;
	UINT8 EnvVol;
	
	FinalVol = Trk->Volume;
	if (Trk->Instrument)
	{
		if (Trk->Instrument & 0x80)
			EnvVol = DoADSR(Trk);
		else
			EnvVol = DoVolumeEnvelope(Trk, Trk->Instrument);
		if (EnvVol & 0x80)
		{
			if (EnvVol == 0x81)	// SMPS Z80 does it for 0x80 too, but that breaks my Note Stop effect implementation
			{
				Trk->PlaybkFlags |= PBKFLG_ATREST;
				return;
			}
			else if (! WasNewNote)
			{
				return;
			}
			EnvVol = Trk->VolEnvCache;
		}
		FinalVol += EnvVol;
	}
	else if (! WasNewNote)
	{
		return;
	}
	
	if (Trk->PlaybkFlags & PBKFLG_ATREST)
		return;
	
	if (FinalVol >= 0x10)
		FinalVol = 0x0F;
	FinalVol |= (Trk->ChannelMask & 0xE0) | 0x10;
	if (Trk->PlaybkFlags & PBKFLG_SPCMODE)
		FinalVol |= 0x20;
	WritePSG(FinalVol);
	
	return;
}

static void UpdateDrumTrack(TRK_RAM* Trk)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	UINT8 Note;
	
	Trk->RemTicks --;
	if (! Trk->RemTicks)
	{
		const UINT8* Data = Trk->SmpsSet->Seq.Data;
		
		if (Trk->Pos >= Trk->SmpsSet->Seq.Len)
		{
			Trk->PlaybkFlags &= ~PBKFLG_ACTIVE;
			return;
		}
		
		Trk->PlaybkFlags &= ~(PBKFLG_HOLD | PBKFLG_ATREST);
		
		while(Data[Trk->Pos] >= SmpsCfg->CmdList.FlagBase)
		{
			Extra_LoopStartCheck(Trk);
			cfHandler(Trk, Data[Trk->Pos]);
			if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
				return;
		}
		
		Note = 0x00;
		if (Data[Trk->Pos] >= SmpsCfg->NoteBase)
		{
			Extra_LoopStartCheck(Trk);
			Note = Data[Trk->Pos];
			Trk->Pos ++;
			
			if (Trk->SpcDacMode == DCHNMODE_GAXE3)
			{
				// Golden Axe III: Special 2-ch DAC mode (2 separate notes, one for each channel)
				Trk->DAC.Snd = Note;
				Trk->DAC.Unused = 0x00;
				if (Trk->GA3_DacMode & 0x01)
				{
					Trk->DAC.Unused = Data[Trk->Pos];
					Trk->Pos ++;
				}
				if (! (Trk->GA3_DacMode & 0x02))
					Trk->GA3_DacMode &= ~0x01;
			}
			else if (Trk->SpcDacMode == DCHNMODE_S2R)
			{
				Trk->DAC.Snd = Note - SmpsCfg->NoteBase;
				if (! Trk->DAC.Snd)
					Trk->PlaybkFlags |= PBKFLG_ATREST;
				else
					Trk->DAC.Snd += Trk->Transpose;
			}
			else if (Trk->SpcDacMode == DCHNMODE_SMGP2)
			{
				if ((Trk->ChannelMask & 0x01) && Note == SmpsCfg->NoteBase)
				{
					Trk->PlaybkFlags |= PBKFLG_ATREST;
					if (SmpsCfg->DelayFreq == DLYFREQ_RESET)
						Trk->DAC.Snd = Note;
				}
				else
				{
					Trk->DAC.Snd = Note;
				}
			}
			else
			{
				Trk->DAC.Snd = Note;
			}
		}
		
		// read Duration for 00..7F
		FinishTrkUpdate(Trk, (Data[Trk->Pos] < SmpsCfg->NoteBase));
		
		if (! (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN))
		{
			switch(Trk->SpcDacMode)
			{
			case DCHNMODE_NORMAL:
			case DCHNMODE_VRDLX:
				if (Trk->DAC.Snd >= SmpsCfg->NoteBase)
					PlayDrumNote(Trk, Trk->DAC.Snd);
				break;
			case DCHNMODE_PS4:
				if (Trk->DAC.Snd >= SmpsCfg->NoteBase)
					PlayPS4DrumNote(Trk, Trk->DAC.Snd);
				break;
			case DCHNMODE_GAXE3:
				if (Trk->DAC.Snd >= SmpsCfg->NoteBase)
					PlayDrumNote(Trk, Trk->DAC.Snd);
				if (Trk->DAC.Unused >= SmpsCfg->NoteBase)
					PlayDrumNote(Trk, Trk->DAC.Unused);
				break;
			case DCHNMODE_CYMN:
				SmpsRAM.DacChVol[0x00] = 0x80 | (Trk->Volume & 0x0F);
				RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, SmpsRAM.DacChVol[0x00]);
				if (Trk->DAC.Snd >= SmpsCfg->NoteBase)
				{
					if (! (Trk->PlaybkFlags & PBKFLG_HOLD))	// not in the driver, but seems to be intended
						PlayDrumNote(Trk, Trk->DAC.Snd);
				}
				break;
			case DCHNMODE_S2R:
				PrepareModulat(Trk);
				if (Trk->PlaybkFlags & (PBKFLG_ATREST | PBKFLG_OVERRIDDEN))
					break;
				
				RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, Trk->Volume);
				SendDACFrequency(Trk, Trk->Detune & 0xFF);
				DAC_SetFeature(0x00, DACFLAG_REVERSE, Trk->DAC.Unused & 0x01);
				DAC_SetFeature(0x00, DACFLAG_LOOP, Trk->DAC.Unused & 0x02);
				DAC_SetFeature(0x00, DACFLAG_FLIP_FLOP, Trk->DAC.Unused & 0x04);
				DAC_SetFeature(0x00, DACFLAG_FF_STATE, Trk->DAC.Unused & 0x40);
				if (! (Trk->PlaybkFlags & PBKFLG_HOLD))
				{
					if (Trk->DAC.Snd < SmpsCfg->DrumLib.DrumCount)
						PlayDrumNote(Trk, SmpsCfg->NoteBase + Trk->DAC.Snd);
					else
						DAC_Play(0x00, Trk->DAC.Snd - 0x01);
				}
				break;
			case DCHNMODE_SMGP2:
				if (Trk->ChannelMask & 0x01)
				{
					// melodic DAC mode
					if (Trk->DAC.Snd >= SmpsCfg->NoteBase)
						PlaySMGP2DACNote(Trk, Trk->DAC.Snd);
				}
				else
				{
					if (Trk->DAC.Snd & 0x0F)
						DAC_SetRate(0x00, 0x00, 0x00);
					if (Trk->DAC.Snd & 0x70)
						DAC_SetRate(0x01, 0x00, 0x00);
					if (Trk->DAC.Snd >= SmpsCfg->NoteBase)
						PlayDrumNote(Trk, Trk->DAC.Snd);
				}
				break;
			}
		}
	}
	else
	{
		if (Trk->PlaybkFlags & PBKFLG_ATREST)
			return;
		
		// Phantasy Star IV - special DAC handling
		if (DoNoteStop(Trk))
		{
			if (Trk->SpcDacMode == DCHNMODE_PS4 || Trk->SpcDacMode == DCHNMODE_S2R)
			{
				Trk->PlaybkFlags |= PBKFLG_ATREST;
				DoNoteOff(Trk);
				DAC_Stop(0x00);
			}
			return;
		}
		
		if (Trk->SpcDacMode == DCHNMODE_S2R)
		{
			UINT16 Freq;
			UINT8 FreqUpdate;
			
			Freq = Trk->Detune << 4;
			FreqUpdate = DoModulation(Trk, &Freq);
			Freq >>= 4;
			if (FreqUpdate == 0x01)
				SendDACFrequency(Trk, Freq & 0xFF);
		}
	}
	
	return;
}

static void Update2OpDrumTrack(DRUM_TRK_RAM* Trk)
{
	const UINT8* FreqList;
	
	if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
		return;
	if (Trk->Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)	// parent track has "overridden" bit set
		return;
	
	Trk->RemTicks --;
	if (! Trk->RemTicks)
	{
		//StopDrumNote:
		Trk->PlaybkFlags &= ~PBKFLG_ACTIVE;
		Do2OpNote();
		return;
	}
	
	FreqList = FreqReg_2Op[Trk->PlaybkFlags & 0x01];
	WriteFMI(FreqList[0x00], Trk->Freq1MSB);
	WriteFMI(FreqList[0x01], Trk->Freq1LSB);
	WriteFMI(FreqList[0x02], Trk->Freq2MSB);
	WriteFMI(FreqList[0x03], Trk->Freq2LSB);
	
	Trk->Freq1LSB += Trk->Freq1Inc;
	Trk->Freq2LSB += Trk->Freq2Inc;
	// Space Harrier II's code is slightly buggy and does this instead:
	//Trk->Freq2MSB = Trk->Freq2LSB + Trk->Freq2Inc;
	
	return;
}

static void UpdatePWMTrack(TRK_RAM* Trk)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	
	Trk->RemTicks --;
	if (! Trk->RemTicks)
	{
		const UINT8* Data = Trk->SmpsSet->Seq.Data;
		
		if (Trk->Pos >= Trk->SmpsSet->Seq.Len)
		{
			Trk->PlaybkFlags &= ~PBKFLG_ACTIVE;
			return;
		}
		
		Trk->PlaybkFlags &= ~(PBKFLG_HOLD | PBKFLG_ATREST);
		
		while(Data[Trk->Pos] >= SmpsCfg->CmdList.FlagBase)
		{
			Extra_LoopStartCheck(Trk);
			cfHandler(Trk, Data[Trk->Pos]);
			if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
				return;
		}
		
		if (Data[Trk->Pos] >= SmpsCfg->NoteBase)
		{
			Extra_LoopStartCheck(Trk);
			Trk->DAC.Snd = Data[Trk->Pos];
			Trk->Pos ++;
		}
		if (! (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN) && Trk->DAC.Snd >= 0x81)
		{
			UINT8 VolValue;
			
			VolValue  = 1 + ((Trk->Volume & 0x0F) >> 0);
			VolValue += 1 + ((Trk->Volume & 0xF0) >> 4);
			DAC_SetVolume((Trk->ChannelMask & 0x06) >> 1, VolValue * 0x10);
			if (! (Trk->PlaybkFlags & PBKFLG_HOLD))
				DAC_Play((Trk->ChannelMask & 0x06) >> 1, Trk->DAC.Snd - 0x81);
		}
		
		// read Duration for 00..7F
		FinishTrkUpdate(Trk, (Data[Trk->Pos] < SmpsCfg->NoteBase));
	}
	else
	{
		// not in the driver, but why not?
		if (DoNoteStop(Trk))
		{
			Trk->PlaybkFlags |= PBKFLG_ATREST;
			DoNoteOff(Trk);
			return;
		}
	}
	
	return;
}

static void UpdatePSGNoiseTrack(TRK_RAM* Trk)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	UINT16 Freq;
	UINT8 WasNewNote;
	
	Trk->RemTicks --;
	if (! Trk->RemTicks)
	{
		const UINT8* Data = Trk->SmpsSet->Seq.Data;
		UINT8 Note;
		
		if (Trk->Pos >= Trk->SmpsSet->Seq.Len)
		{
			Trk->PlaybkFlags &= ~PBKFLG_ACTIVE;
			return;
		}
		
		Trk->PlaybkFlags &= ~(PBKFLG_HOLD | PBKFLG_ATREST);
		
		while(Data[Trk->Pos] >= SmpsCfg->CmdList.FlagBase)
		{
			Extra_LoopStartCheck(Trk);
			cfHandler(Trk, Data[Trk->Pos]);
			if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
				return;
		}
		
		if (Data[Trk->Pos] >= SmpsCfg->NoteBase)
		{
			Extra_LoopStartCheck(Trk);
			Trk->DAC.Snd = Data[Trk->Pos];
			Trk->Pos ++;
		}
		Note = Trk->DAC.Snd;
		if (! (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN) && Note >= 0x80)
			PlayPSGDrumNote(Trk, Note);
		
		// read Duration for 00..7F
		FinishTrkUpdate(Trk, (Data[Trk->Pos] < SmpsCfg->NoteBase));
		PrepareADSR(Trk);
		
		Freq = Trk->Frequency;
		WasNewNote = 0x01;
	}
	else
	{
		// not in the driver, but why not?
		if (DoNoteStop(Trk))
		{
			DoPSGNoteOff(Trk, 0x01);
			return;
		}
		Freq = DoPitchSlide(Trk);
		WasNewNote = 0x00;
	}
	
	if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
		return;
	
	if (Trk->PlaybkFlags & PBKFLG_SPCMODE)
	{
		if (WasNewNote || (Trk->PlaybkFlags & PBKFLG_PITCHSLIDE))
			SendPSGFrequency(Trk, Freq);
	}
	
	UpdatePSGVolume(Trk, WasNewNote);
	return;
}

static void SendDACFrequency(TRK_RAM* Trk, UINT16 Freq)
{
	if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
		return;
	
	DAC_SetRate(0x00, Freq, 0x01);
	
	return;
}

static void SendFMFrequency(TRK_RAM* Trk, UINT16 Freq)
{
	if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
		return;
	
	if (! ((Trk->PlaybkFlags & PBKFLG_SPCMODE) && Trk->ChannelMask == 0x02))
	{
		WriteFMMain(Trk, 0xA4, Freq >> 8);
		WriteFMMain(Trk, 0xA0, Freq & 0xFF);
	}
	else
	{
		const UINT8 SpcFM3Regs[4] = {0xAD, 0xAE, 0xAC, 0xA6};
		UINT16* SpcFreqPtr;
		UINT16 FinalFrq;
		UINT8 CurFrq;
		
		SpcFreqPtr = GetFM3FreqPtr();
		for (CurFrq = 0; CurFrq < 4; CurFrq ++)
		{
			FinalFrq = Trk->Frequency + SpcFreqPtr[CurFrq];
			WriteFMI(SpcFM3Regs[CurFrq] - 0, FinalFrq >> 8);
			WriteFMI(SpcFM3Regs[CurFrq] - 4, FinalFrq & 0xFF);
		}
	}
	
	return;
}

INLINE UINT16* GetFM3FreqPtr(void)
{
	if (SmpsRAM.TrkMode == TRKMODE_MUSIC)
		return SmpsRAM.FM3Freqs_Mus;
	else if (SmpsRAM.TrkMode == TRKMODE_SPCSFX)
		return SmpsRAM.FM3Freqs_SpcSFX;
	else //if (SmpsRAM.TrkMode == TRKMODE_SSFX)
		return SmpsRAM.FM3Freqs_SFX;
}

static void SendPSGFrequency(TRK_RAM* Trk, UINT16 Freq)
{
	UINT8 ChnMask;
	
	if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
		return;
	
	ChnMask = Trk->ChannelMask;
	if (ChnMask == 0xF0)
		ChnMask = 0xC0;
	ChnMask &= 0xE0;
	
	WritePSG(ChnMask | (Freq & 0x0F));
	if (ChnMask != 0xE0)
		WritePSG((Freq >> 4) & 0x7F);
	
	return;
}

static void TrkUpdate_Proc(TRK_RAM* Trk)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	const UINT8* Data = Trk->SmpsSet->Seq.Data;
	UINT8 Note;
	UINT8 ReuseDelay;
	
	if (Trk->Pos >= Trk->SmpsSet->Seq.Len)
	{
		Trk->PlaybkFlags &= ~PBKFLG_ACTIVE;
		return;
	}
	
	Trk->PlaybkFlags &= ~(PBKFLG_HOLD | PBKFLG_ATREST);
	if (Trk->PlaybkFlags & PBKFLG_HOLD_LOCK)
		Trk->PlaybkFlags |= PBKFLG_HOLD;
	
	while(Data[Trk->Pos] >= /*0xE0*/SmpsCfg->CmdList.FlagBase)
	{
		Extra_LoopStartCheck(Trk);
		cfHandler(Trk, Data[Trk->Pos]);
		if (! (Trk->PlaybkFlags & PBKFLG_ACTIVE))
			return;
	}
	
	if (! (Trk->ChannelMask & 0x80))
	{
		DoNoteOff(Trk);
		DoPanAnimation(Trk, 0);
		InitLFOModulation(Trk);
	}
	
	ReuseDelay = 0x00;
	if (! (Trk->PlaybkFlags & PBKFLG_RAWFREQ))
	{
		Note = Data[Trk->Pos];
		if (Note >= SmpsCfg->NoteBase)
		{
			Extra_LoopStartCheck(Trk);
			Trk->Pos ++;
			if (Note == SmpsCfg->NoteBase)
			{
				DoPSGNoteOff(Trk, 0x00);
				if (SmpsCfg->DelayFreq == DLYFREQ_RESET)
					Trk->Frequency = (Trk->ChannelMask & 0x80) ? 0xFFFF : 0x0000;
			}
			else
			{
				Trk->ADSR.State &= 0x7F;
				Trk->Frequency = GetNote(Trk, Note);
			}
			
			if (! (Trk->PlaybkFlags & PBKFLG_PITCHSLIDE))
			{
				if (Data[Trk->Pos] >= SmpsCfg->NoteBase)
					ReuseDelay = 0x01;
			}
			else
			{
				Trk->Detune = (INT8)Data[Trk->Pos];
				Trk->Pos ++;
			}
		}
	}
	else
	{
		//DoRawFreqMode:
		Extra_LoopStartCheck(Trk);
		Trk->Frequency = ReadLE16(&Data[Trk->Pos]);
		Trk->Pos += 2;
		if (Trk->Frequency)
			Trk->Frequency += Trk->Transpose;
	}
	
	FinishTrkUpdate(Trk, ! ReuseDelay);
	return;
}

static void FinishTrkUpdate(TRK_RAM* Trk, UINT8 ReadDuration)
{
	const UINT8* Data = Trk->SmpsSet->Seq.Data;
	
	if (ReadDuration)
	{
		//SetDuration:
		Extra_LoopStartCheck(Trk);
		Trk->NoteLen = Data[Trk->Pos] * Trk->TickMult;
		Trk->Pos ++;
	}
	
	Trk->RemTicks = Trk->NoteLen;
	if (! (Trk->PlaybkFlags & PBKFLG_HOLD) || (Trk->NStopRevMode & 0x80))	// Mode 0x80 == execute always
	{
		if (! (Trk->NStopRevMode & 0x7F))
		{
			Trk->NStopTout = Trk->NStopInit;
			// Note: A few earlier SMPS Z80 driver have a bug and do
			//Trk->VolEnvIdx = Trk->NStopInit;
			// right here.
		}
		else
		{
			INT16 NewTout;
			
			NewTout = (INT16)Trk->NoteLen - Trk->NStopInit;
			switch(Trk->NStopRevMode & 0x7F)
			{
			case 0x01:
				if (NewTout <= 0)
					NewTout = 1;	// Chou Yakyuu Miracle Nine - make a lower limit of 1
				break;
			case 0x02:
				if (Data[Trk->Pos] == 0xE7)
					NewTout = 0;	// Ristar - the E7 flag disables the effect temporarily.
				// maybe TODO: improve this and check for CFlag[Data[Trk->Pos]].Type == CF_HOLD
				break;
			case 0x11:
				NewTout = Trk->NStopInit;
				break;
			case 0x12:
				NewTout = Trk->NoteLen - (Trk->NoteLen * Trk->NStopInit / 0x80);
				break;
			}
			Trk->NStopTout = (UINT8)NewTout;
		}
	}
	if (! (Trk->PlaybkFlags & PBKFLG_HOLD))
	{
		Trk->ModEnvIdx = 0x00;
		Trk->ModEnvMult = 0x00;
		Trk->ModEnvCache = 0;
		if (1)	// if (! BuggySmpsZ80)
			Trk->PlaybkFlags &= ~PBKFLG_LOCKFREQ;
		Trk->VolEnvIdx = 0x00;
		Trk->VolEnvCache = 0x00;
		if (0)	// if (BuggySmpsZ80)
			Trk->VolEnvIdx = Trk->NStopTout;
	}
	
	return;
}

static UINT8 DoNoteStop(TRK_RAM* Trk)
{
	if (! Trk->NStopTout)
		return 0x00;
	
	if (! (Trk->NStopRevMode & 0x10))
	{
		Trk->NStopTout --;
		if (! Trk->NStopTout)
			return 0x01;
	}
	else
	{
		if (Trk->RemTicks == Trk->NStopTout)
			return 0x01;
	}
	
	return 0x00;
}

static UINT16 GetNote(TRK_RAM* Trk, UINT8 NoteCmd)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	INT16 Note;
	UINT8 Octave;
	
	Note = NoteCmd - (SmpsCfg->NoteBase + 0x01);	// NoteCmd - 0x81
	Note += Trk->Transpose;
	if (Trk->ChannelMask & 0x80)
	{
		// Sonic 2 SMS does (NoteCmd - 0x80), too
		//if (SmpsCfg->PSGBaseNote == PSGBASEN_B)
		//	Note ++;
		Note += SmpsCfg->PSGBaseNote;
		
		if (Trk->Transpose == -36 && Note < -24)
			return 0x6000;	// workaround for crappy xm#smps conversions that use an invalid PSG frequency for noise
		if (Note < 0)
			Note = 0;
		else if (Note >= SmpsCfg->PSGFreqCnt)
			Note = SmpsCfg->PSGFreqCnt - 1;
		return SmpsCfg->PSGFreqs[Note];
	}
	else //if (! (Trk->ChannelMask & 0x70))
	{
		// SMPS 68k drivers do (NoteCmd - 0x80) instead, so add 1 again.
		//if (SmpsCfg->FMBaseNote == FMBASEN_B)
		//	Note ++;
		Note += SmpsCfg->FMBaseNote;
		
		if (SmpsCfg->FMFreqCnt == 12)
		{
			Note &= 0xFF;	// simulate SMPS Z80 behaviour
			if (SmpsCfg->FMBaseNote == FMBASEN_B)
				Note &= 0x7F;	// SMPS 68k strips the sign bit, too
			Octave = SmpsCfg->FMBaseOct + Note / 12;
			Note %= 12;
			return SmpsCfg->FMFreqs[Note] | (Octave << 11);
		}
		else
		{
			if (Note < 0)
				Note = 0;
			else if (Note >= SmpsCfg->FMFreqCnt)
				Note = SmpsCfg->FMFreqCnt - 1;
			return SmpsCfg->FMFreqs[Note];
		}
	}
}


static void DoPanAnimation(TRK_RAM* Trk, UINT8 Continue)
{
	const PAN_ANI_LIB* PAniLib = &Trk->SmpsSet->Cfg->PanAnims;
	UINT16 DataPtr;
	UINT8 PanData;
	
	// Continue == 0 - Start Pan Animation
	// Continue == 1 - Execute Pan Animation
	if (! Trk->PanAni.Type)
		return;	// Pan Animation disabled
	if (Trk->PanAni.Anim >= PAniLib->AniCount)
	{
		if (DebugMsgs & 0x02)
			printf("Warning: invalid Pan Animation 0x%02X\n", Trk->PanAni.Anim);
		Trk->PanAni.Type = 0x00;
		return;
	}
	
	// StartPanAnim:
	//	1	(return if Hold-Bit set), DoPanAnim
	//	2	Reset Index, DoPanAnim
	//	3	Reset Index, DoPanAnim
	// ExecutePanAnim:
	//	1	-- (return)
	//	2	DoPanAnim
	//	3	DoPanAnim
	if (! Continue)
	{
		//StartPanAnim:
		if (Trk->PanAni.Type == 0x01)
		{
			UINT8 Flags;
			
			// Like in DoNoteOn, SMPS 68k and Z80 check the same bit,
			// which does something different in both drivers.
			if (Trk->SmpsSet->Cfg->NoteOnPrevent == NONPREV_HOLD)
				Flags = PBKFLG_HOLD;	// SMPS Z80
			else //if (Trk->SmpsSet->Cfg->NoteOnPrevent == NONPREV_REST)
				Flags = PBKFLG_ATREST;	// SMPS 68k
			if (Trk->PlaybkFlags & Flags)
				return;
		}
		else //if (Trk->PanAni.Type >= 0x02)
		{
			Trk->PanAni.AniIdx = 0x00;
			if ((Trk->SmpsSet->Cfg->ModAlgo & 0xF0) == MODALGO_68K)
				Trk->PanAni.Timeout = 1;
		}
	}
	else
	{
		//ExecPanAnim:
		if (Trk->PanAni.Type == 0x01)
			return;
	}
	
	Trk->PanAni.Timeout --;
	if (Trk->PanAni.Timeout)
		return;
	
	Trk->PanAni.Timeout = Trk->PanAni.ToutInit;
	
	//PanData = AniData[Trk->PanAni.AniIdx];
	DataPtr = PAniLib->AniList[Trk->PanAni.Anim] - PAniLib->AniBase;
	if (DataPtr >= PAniLib->DataLen)
		return;
	DataPtr += Trk->PanAni.AniIdx;	// add Animation Index
	if (DataPtr >= PAniLib->DataLen)
	{
		if (DebugMsgs & 0x02)
			printf("Warning: invalid Pan Animation Index 0x%02X (Env. %02X)\n",
					Trk->PanAni.AniIdx, Trk->PanAni.Anim);
		DataPtr = PAniLib->DataLen - 1;	// prevent reading beyond EOF
	}
	PanData = PAniLib->Data[DataPtr];
	
	Trk->PanAni.AniIdx ++;
	if (Trk->PanAni.AniIdx == Trk->PanAni.AniLen)
	{
		if (Trk->PanAni.Type == 2)
			Trk->PanAni.AniIdx --;
		else
			Trk->PanAni.AniIdx = 0;
	}
	
	// inlined cfE0_Pan:
	Trk->PanAFMS &= 0x3F;
	Trk->PanAFMS |= PanData;
	WriteFMMain(Trk, 0xB4, Trk->PanAFMS);
	
	return;
}

static void InitLFOModulation(TRK_RAM* Trk)
{
	LFO_MOD* LFOMod = &Trk->LFOMod;
	
	if (Trk->PlaybkFlags & (PBKFLG_HOLD | PBKFLG_OVERRIDDEN))
		return;
	if (SmpsRAM.FadeOut.Steps)
		return;
	if (! Trk->LFOMod.MaxFMS)
		return;
	
	LFOMod->Delay = LFOMod->DelayInit;
	LFOMod->Timeout = LFOMod->ToutInit;
	LFOMod->CurFMS = Trk->PanAFMS & 0xF8;
	WriteFMMain(Trk, 0xB4, LFOMod->CurFMS);
	
	return;
}

static void ExecLFOModulation(TRK_RAM* Trk)
{
	LFO_MOD* LFOMod = &Trk->LFOMod;
	
	if (SmpsRAM.FadeOut.Steps)
		return;
	if (! LFOMod->MaxFMS)
		return;
	
	LFOMod->Delay --;
	if (LFOMod->Delay)
		return;
	LFOMod->Delay = 1;
	
	if ((LFOMod->CurFMS & 0x07) == LFOMod->MaxFMS)
		return;
	
	LFOMod->Timeout --;
	if (LFOMod->Timeout)
		return;
	LFOMod->Timeout = LFOMod->ToutInit;
	
	LFOMod->CurFMS ++;
	WriteFMMain(Trk, 0xB4, LFOMod->CurFMS);
	
	return;
}

static void DoFMVolEnv(TRK_RAM* Trk)
{
	UINT8 EnvVol;
	//const UINT8* OpPtr = (Trk->SmpsSet->Cfg->InsMode & 0x01) ? VolOperators_HW : VolOperators_DEF;
	const UINT8* OpPtr = Trk->SmpsSet->Cfg->InsReg_TL;
	const UINT8* VolPtr = Trk->VolOpPtr;
	UINT8 AlgoMask;
	UINT8 CurOp;
	UINT8 CurTL;
	
	if (! Trk->FMVolEnv.VolEnv || (Trk->FMVolEnv.VolEnv & 0x80))	// VolEnv 80+ is SSG-EG
		return;
	
	EnvVol = DoVolumeEnvelope(Trk, Trk->FMVolEnv.VolEnv);
	if (EnvVol & 0x80)
		return;
	if (OpPtr == NULL || VolPtr == NULL)
		return;
	// TODO: support interleaved mode (not important, because preSMPS games don't have this feature)
	
	AlgoMask = Trk->FMVolEnv.OpMask;
	for (CurOp = 0x00; CurOp < 0x04; CurOp ++, AlgoMask >>= 1)
	{
		if (AlgoMask & 0x01)
		{
			CurTL = VolPtr[CurOp] + EnvVol;
			CurTL &= 0x7F;
			WriteFMMain(Trk, OpPtr[CurOp], CurTL);
		}
	}
	
	return;
}

static void PrepareModulat(TRK_RAM* Trk)
{
	const UINT8* Data = Trk->SmpsSet->Seq.Data;
	const UINT8* ModData;
	
	if (Trk->PlaybkFlags & PBKFLG_HOLD)
		return;
	if (! (Trk->ModEnv & 0x80))
	{
		Trk->CstMod.Freq = 0;
		return;	// no Custom Modulation - reset cache
	}
	
	ModData = &Data[Trk->CstMod.DataPtr];
	SmpsRAM.ModData = ModData;	// save IY register value
	Trk->CstMod.Delay = ModData[0x00];
	Trk->CstMod.Rate = ModData[0x01];
	Trk->CstMod.Delta = ModData[0x02];
	Trk->CstMod.RemSteps = ModData[0x03] / 2;
	Trk->CstMod.Freq = 0;
	
	return;
}

static UINT8 DoModulation(TRK_RAM* Trk, UINT16* Freq)
{
	INT16 EnvFreq;
	INT16 CstFreq;
	INT16 FreqChange;
	UINT8 Executed;
	
	if (! Trk->ModEnv)
		return 0x00;
	if (Trk->ModEnv > 0x80 && (Trk->SmpsSet->Cfg->ModAlgo & 0xF0) == MODALGO_Z80)
	{
		if (DebugMsgs & 0x02)
			printf("Warning: Modulation Type %02X on Channel %02X, Pos 0x%04X!\n", Trk->ModEnv, Trk->ChannelMask, Trk->Pos);
		// Some games like Flicky and Sega Channel (J) use 81+ to read from an alternative Modulation set.
		// In case of Sega Channel (J) and other games of the Sega Game Toshokan series,
		// this happens to be the current set, so this has the correct effect.
		Trk->ModEnv &= 0x7F;
	}
	
	// Note: SMPS 68k can do Modulation Envelope and Custom Modulation simultaneously.
	//       SMPS Z80 can only do one of them at the same time, since they share some memory.
	//       Additionally, the SegaNet Z80 driver has a second ModEnv set for IDs 81-FF.
	
	EnvFreq = DoModulatEnvelope(Trk, Trk->ModEnv);
	CstFreq = DoCustomModulation(Trk);
	
	Executed = 0x00;
	FreqChange = 0;
	if (VALID_FREQ(EnvFreq))
	{
		Executed |= 0x01;
		FreqChange += EnvFreq;
	}
	else if ((EnvFreq & 0xFF) == 0x01)
	{
		Executed |= 0x10;
		FreqChange += Trk->ModEnvCache;	// hold at current level
	}
	if (VALID_FREQ(CstFreq))
	{
		Executed |= 0x02;
		FreqChange += CstFreq;
	}
	else if ((CstFreq & 0xFF) == 0x01)
	{
		Executed |= 0x20;
		FreqChange += Trk->CstMod.Freq;	// hold at current level
	}
	
	*Freq += FreqChange;
	if (Executed & 0x0F)
		return 0x01;	// has updated
	else if (Executed & 0xF0)
		return 0x02;	// in effect, but not changed
	else
		return 0x00;	// nothing done
}

static INT16 DoCustomModulation(TRK_RAM* Trk)
{
	const UINT8* Data = Trk->SmpsSet->Seq.Data;
	const UINT8* ModData = &Data[Trk->CstMod.DataPtr];
	
	if (! (Trk->ModEnv & 0x80))
		return 0x8000;
	
	if ((Trk->SmpsSet->Cfg->ModAlgo & 0xF0) == MODALGO_68K)
	{
		if (Trk->CstMod.Delay)
		{
			Trk->CstMod.Delay --;
			return 0x8001;
		}
		
		Trk->CstMod.Rate --;
		if (Trk->CstMod.Rate)
			return 0x8001;
		Trk->CstMod.Rate = ModData[0x01];
		
		if (! Trk->CstMod.RemSteps)
		{
			Trk->CstMod.RemSteps = ModData[0x03];
			Trk->CstMod.Delta = -Trk->CstMod.Delta;
			return 0x8001;
		}
		
		Trk->CstMod.RemSteps --;
		Trk->CstMod.Freq += Trk->CstMod.Delta;
		return Trk->CstMod.Freq;
	}
	else if ((Trk->SmpsSet->Cfg->ModAlgo & 0xF0) == MODALGO_Z80)
	{
		INT16 NewFreq;
		
		Trk->CstMod.Delay --;
		if (Trk->CstMod.Delay)
			return 0x8001;
		Trk->CstMod.Delay ++;
		
		//NewFreq = 0x8000;
		NewFreq = Trk->CstMod.Freq;
		
		Trk->CstMod.Rate --;
		if (! Trk->CstMod.Rate)
		{
			SmpsRAM.ModData = ModData;	// save IY register value
			Trk->CstMod.Rate = ModData[0x01];
			Trk->CstMod.Freq += Trk->CstMod.Delta;
			NewFreq = Trk->CstMod.Freq;
		}
		
		Trk->CstMod.RemSteps --;
		if (! Trk->CstMod.RemSteps)
		{
			if (Trk->SmpsSet->Cfg->ModAlgo & 0x02)
			{
				// emulate bug with uninitialized IY register, fixes Hyper Marbles: Round Clear
				// (SMPS Z80 Type 1 and Type 2 FM, fixed in Type 2 DAC)
				if (SmpsRAM.ModData != NULL)
					ModData = SmpsRAM.ModData;
			}
			Trk->CstMod.RemSteps = ModData[0x03];
			Trk->CstMod.Delta = -Trk->CstMod.Delta;
		}
		return NewFreq;
	}
	
	return 0x80FF;
}

static INT16 DoModulatEnvelope(TRK_RAM* Trk, UINT8 EnvID)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	const ENV_LIB* ModEnvLib = &SmpsCfg->ModEnvs;
	INT8 EnvVal;
	INT16 Multiplier;
	
	if (EnvID & 0x80)
	{
		//ModEnvLib = &SmpsCfg->ModEnvs2;
		EnvID &= 0x7F;
	}
	if (! EnvID)
		return 0x8000;
	EnvID --;
	if (EnvID >= ModEnvLib->EnvCount)
		return 0x8000;
	
	EnvVal = (INT8)DoEnvelope(&ModEnvLib->EnvData[EnvID], SmpsCfg->EnvCmds,
								&Trk->ModEnvIdx, &Trk->ModEnvMult);
	if (EnvVal & 0x80)
	{
		switch(SmpsCfg->EnvCmds[EnvVal & 0x7F])
		{
		case ENVCMD_HOLD:		// 81 - hold at current level
		case ENVCMD_VST_MHLD:	// 83 - hold [SMPS Z80]
			Trk->PlaybkFlags |= PBKFLG_LOCKFREQ;
			return 0x8001;
		case ENVCMD_STOP:		// 83 - stop [SMPS 68k]
			DoNoteOff(Trk);
			return 0x80FF;
		}
	}
	
	// preSMPS 68k formula: (Rent-A-Hero)
	//	if (EnvMult != 0)
	//		FreqDelta = (INT8)EnvVal * (UINT8)EnvMult
	//	else
	//		FreqDelta = (INT8)EnvVal
	//	Note: Whether EnvMult is signed or unsigned depends on how the register D3 was set before the
	//	      sound driver was called. Usually it is set to 0 though.
	//	      In Phantasy Star II, a multiplication with 0 is possible, EnvMult is ensured to be unsigned
	//	      and an invalid sign extention causes negative EnvVal to be always -1.
	
	// SMPS 68k formula:
	//	FreqDelta = (INT8)EnvVal * (INT8)EnvMult
	//	Note: EnvMult is 0 by default, nullifying the envelope until the first Change Multiplier command.
	
	// SMPS Z80 formula: (also preSMPS Z80, Space Harrier II)
	//	FreqDelta = (INT8)EnvVal * (UNT8)(EnvMult + 1)
	
	switch(SmpsCfg->EnvMult)
	{
	case ENVMULT_PRE:
		Multiplier = Trk->ModEnvMult ? Trk->ModEnvMult : 1;
		break;
	case ENVMULT_68K:
		Multiplier = (INT8)Trk->ModEnvMult;
		break;
	case ENVMULT_Z80:
		Multiplier = Trk->ModEnvMult + 1;
		break;
	}
	
	Trk->ModEnvCache = EnvVal * Multiplier;
	return Trk->ModEnvCache;
}

static UINT8 DoVolumeEnvelope(TRK_RAM* Trk, UINT8 EnvID)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	const ENV_LIB* VolEnvLib = &SmpsCfg->VolEnvs;
	UINT8 EnvVal;
	
	if (EnvID & 0x80)
	{
		//VolEnvLib = &SmpsCfg->VolEnvs2;
		EnvID &= 0x7F;
	}
	if (! EnvID)
		return 0x80;
	EnvID --;
	if (EnvID >= VolEnvLib->EnvCount)
		return 0x80;
	
	EnvVal = DoEnvelope(&VolEnvLib->EnvData[EnvID], SmpsCfg->EnvCmds, &Trk->VolEnvIdx, NULL);
	if (EnvVal & 0x80)
	{
		switch(SmpsCfg->EnvCmds[EnvVal & 0x7F])
		{
		case ENVCMD_HOLD:		// 81 - hold at current level
			return 0x80;
		case ENVCMD_VST_MHLD:	// 83 - stop [SMPS Z80]
			if (! (Trk->ChannelMask & 0x80))	// handle FM Volume Envelopes
			{
				Trk->PlaybkFlags |= PBKFLG_ATREST;
				return 0x80;
			}
			// fall through
		case ENVCMD_STOP:		// 83 - stop [SMPS 68k]
			DoNoteOff(Trk);
			return 0x81;
		}
	}
	
	Trk->VolEnvCache = EnvVal;
	return Trk->VolEnvCache;
}

static UINT8 DoEnvelope(const ENV_DATA* EnvData, const UINT8* EnvCmds, UINT8* EnvIdx, UINT8* EnvMult)
{
	UINT8 Data;
	UINT8 Finished;
	
	if (*EnvIdx >= EnvData->Len)
	{
		if (DebugMsgs & 0x02)
			printf("Warning: invalid Envelope Index 0x%02X (Env. Length %02X)\n", *EnvIdx, EnvData->Len);
		*EnvIdx = 0x00;
	}
	
	Finished = 0x00;
	do
	{
		Data = EnvData->Data[*EnvIdx];
		(*EnvIdx) ++;
		if (Data < 0x80)
			break;
		
		switch(EnvCmds[Data & 0x7F])
		{
		case ENVCMD_DATA:
			Finished = 0x01;
			break;
		case ENVCMD_RESET:		// 80 - reset Envelope
			*EnvIdx = 0x00;
			break;
		case ENVCMD_HOLD:		// 81 - hold at current level
		case ENVCMD_STOP:		// 83 - stop
		case ENVCMD_VST_MHLD:	// 83 - stop/hold
			(*EnvIdx) --;
			Finished = 0x01;
			break;
		case ENVCMD_LOOP:		// 82 xx - loop back to index xx
			*EnvIdx = EnvData->Data[*EnvIdx];
			break;
		case ENVCMD_CHGMULT:	// 84 xx - change Multiplier
			if (EnvMult != NULL)
				*EnvMult += EnvData->Data[*EnvIdx];
			(*EnvIdx) ++;
			break;
		}
	} while(! Finished);
	
	return Data;
}

static void PrepareADSR(TRK_RAM* Trk)
{
	if (Trk->PlaybkFlags & PBKFLG_HOLD)
		return;
	if (! (Trk->Instrument & 0x80))
		return;
	
	if (Trk->ADSR.State & 0x80)
		return;
	Trk->ADSR.Level = 0xFF;
	Trk->ADSR.State = 0x10 | Trk->ADSR.Mode;
	
	return;
}

static UINT8 DoADSR(TRK_RAM* Trk)
{
	// Note: The original Z80 ASM code uses the carry bit to check for 8-bit overflowing additions.
	//       But this can't be done in C and this is more readable anyway.
	INT16 NewLvl;
	
	if (Trk->ADSR.State & 0x10)
	{
		// Attack Phase
		NewLvl = Trk->ADSR.Level - Trk->ADSR.AtkRate;
		if (NewLvl <= 0)
		{
			NewLvl = 0;
			Trk->ADSR.State ^= 0x30;	// -> Decay Phase (10 -> 20)
		}
		Trk->ADSR.Level = (UINT8)NewLvl;
	}
	else if (Trk->ADSR.State & 0x20)
	{
		// Decay Phase
		//loc_8580:
		NewLvl = Trk->ADSR.Level + Trk->ADSR.DecRate;
		if (NewLvl >= Trk->ADSR.DecLvl)
		{
			NewLvl = Trk->ADSR.DecLvl;
			if (Trk->ADSR.State & 0x08)
				Trk->ADSR.State ^= 0x30;	// return to Attack Phase (20 -> 10)
			else
				Trk->ADSR.State ^= 0x60;	// -> Sustain Phase (20 -> 40)
		}
		Trk->ADSR.Level = (UINT8)NewLvl;
	}
	else if (Trk->ADSR.State & 0x40)
	{
		// Sustain Phase
		//loc_85B0:
		NewLvl = Trk->ADSR.Level + Trk->ADSR.SusRate;
		if (NewLvl >= 0xFF)
		{
			NewLvl = 0xFF;
			Trk->ADSR.State &= ~0x70;	// -> Release Phase (40 -> 00)
		}
		Trk->ADSR.Level = (UINT8)NewLvl;
	}
	else
	{
		// Release Phase
		//loc_85D2:
		NewLvl = Trk->ADSR.Level + Trk->ADSR.RelRate;
		if (NewLvl >= 0x100)
		{
			Trk->ADSR.State &= 0x0F;
			Trk->ADSR.Level = 0xFF;
			return 0x81;	// Note Off
		}
		Trk->ADSR.Level = (UINT8)NewLvl;
	}
	
	return Trk->ADSR.Level >> 4;
}


void DoNoteOn(TRK_RAM* Trk)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	UINT8 Flags;
	
	if (! Trk->Frequency)
		return;
	
	// SMPS 68k and Z80 both return when bit 1 or 2 is set. (AND 06h)
	// But for SMPS Z80, bit 1 is PBKFLG_HOLD, for SMPS 68k it is PBKFLG_ATREST.
	Flags = PBKFLG_OVERRIDDEN;
	if (SmpsCfg->NoteOnPrevent == NONPREV_HOLD)
		Flags |= PBKFLG_HOLD;	// SMPS Z80
	else //if (SmpsCfg->NoteOnPrevent == NONPREV_REST)
		Flags |= PBKFLG_ATREST;	// SMPS 68k
	if (Trk->PlaybkFlags & Flags)
		return;
	
	if (Trk->ChannelMask & 0x80)	// not in SMPS Z80 code
		return;
	
	// [not in driver] turn DAC off when playing a note on FM6
	if (SmpsCfg->FM6DACOff && Trk->ChannelMask == 0x06 && (SmpsRAM.DacState & 0x80))
		SetDACState(0x00);
	
	WriteFMI(0x28, Trk->ChannelMask | 0xF0);
	
	return;
}

void DoNoteOff(TRK_RAM* Trk)
{
	// Note: The difference present in DoNoteOn doesn't exist here.
	if (Trk->PlaybkFlags & (PBKFLG_HOLD | PBKFLG_OVERRIDDEN))
		return;
	
	if (Trk->ChannelMask & 0x80)
	{
		WritePSG((Trk->ChannelMask & 0xE0) | 0x10 | 0x0F);
		if (Trk->PlaybkFlags & PBKFLG_SPCMODE)
			WritePSG((Trk->ChannelMask & 0xE0 ^ 0x20) | 0x10 | 0x0F);	// mute Noise Channel or PSG 3
	}
	else
	{
		if (Trk->ChannelMask & 0x10)	// skip Drum Tracks
			return;
		WriteFMI(0x28, Trk->ChannelMask);
	}
	
	return;
}

static void DoPSGNoteOff(TRK_RAM* Trk, UINT8 OffByTimeout)
{
	// also known as SetRest
	UINT8 EnforceOff;
	
	if (Trk->SmpsSet->Cfg->TempoMode == TEMPO_TOUT_REV)	// Castle Of Illusion
		EnforceOff = OffByTimeout;
	else	// Master System SMPS
		EnforceOff = 0x00;
	if ((Trk->ChannelMask & 0x80) && (Trk->Instrument & 0x80) && ! EnforceOff)
	{
		Trk->ADSR.State &= 0x0F;
		Trk->ADSR.State |= 0x80;	// set Release Phase
	}
	else
	{
		Trk->PlaybkFlags |= PBKFLG_ATREST;	// doing this is important for cases like 80 0C E7 80 60
		if (Trk->PlaybkFlags & PBKFLG_HOLD)
			return;
		
		if (Trk->ChannelMask & 0x80)
			DoNoteOff(Trk);
	}
	
	return;
}

void Do2OpNote(void)
{
	UINT8 OffOnBits;
	
	OffOnBits = 0x02;
	if (SmpsRAM.MusDrmTrks[0].PlaybkFlags & PBKFLG_ACTIVE)
		OffOnBits |= 0x30;
	if (SmpsRAM.MusDrmTrks[1].PlaybkFlags & PBKFLG_ACTIVE)
		OffOnBits |= 0xC0;
	WriteFMI(0x28, OffOnBits);
	
	return;
}

static UINT16 DoPitchSlide(TRK_RAM* Trk)
{
	UINT16 NewFreq;
	
	NewFreq = Trk->Frequency + Trk->Detune;
	// Note: SMPS Z80 does the pitch wrap even if no Pitch Slide is active,
	//       but this breaks SMPS 68k songs.
	if (! (Trk->ChannelMask & 0x80))	// FM channels only
	{
		// make it slide smoothly through the octaves
		UINT16 BaseFreq;
		UINT16 OctFreq;	// frequency within octave
		
		BaseFreq = Trk->SmpsSet->Cfg->FMFreqs[0] & 0x7FF;
		OctFreq = NewFreq & 0x7FF;
		if (OctFreq < BaseFreq)
			NewFreq -= (0x7FF - BaseFreq);
		else if (OctFreq > BaseFreq * 2)
			NewFreq += (0x800 - BaseFreq);
		
		/* Original formula:
	SMPS Z80 (Type 1/2):
		if (OctFreq <= 0x283)		// 0x284-1
			NewFreq -= 0x57B;		// 0x800-1 - 0x284
		else if (OctFreq > 0x508)	// 0x284*2
			NewFreq += 0x57C;		// 0x800 - 0x284
		// A few game have different numbers:
	Ghostbusters:
		if (OctFreq <= 0x284)
			NewFreq -= 0x57C;
		else if (OctFreq > 0x508)
			NewFreq += 0x57C;
	Arnold Palmer Tournament Golf (early SMPS):
		if (OctFreq <= 0x27E)
			NewFreq -= 0x580;
		else if (OctFreq > 0x4FE)
			NewFreq += 0x580;
	*/
	}
	
	if (Trk->PlaybkFlags & PBKFLG_PITCHSLIDE)
		Trk->Frequency = NewFreq;
	
	return NewFreq;
}

const INS_LIB* GetSongInsLib(TRK_RAM* Trk, UINT8 SongID)
{
	// placeholder function
	// If it should be possible to define song IDs sometime, this will return the instrument table of another song.
	return &Trk->SmpsSet->Cfg->GblInsLib;
}

void SendFMIns(TRK_RAM* Trk, const UINT8* InsData)
{
	const UINT8* OpPtr = Trk->SmpsSet->Cfg->InsRegs;
	const UINT8* InsPtr = InsData;
	UINT8 HadB4;
	
	if (! (Trk->SmpsSet->Cfg->InsMode & INSMODE_INT))
	{
		if (OpPtr == NULL || InsData == NULL)
			return;
		
		HadB4 = 0x00;
		while(*OpPtr)
		{
			if (*OpPtr == 0xB0)
				Trk->FMAlgo = *InsPtr;
			else if (*OpPtr == 0xB4)
			{
				Trk->PanAFMS = *InsPtr;
				HadB4 = 0x01;
			}
			else if (*OpPtr == 0x40)
				Trk->VolOpPtr = InsPtr;
			
			if ((*OpPtr & 0xF0) != 0x40)	// exclude the TL operators - RefreshFMVolume will do them
			{
				//WriteInsReg:
				WriteFMMain(Trk, *OpPtr, *InsPtr);
			}
			*OpPtr ++;	*InsPtr ++;
		}
	}
	else
	{
		if (InsData == NULL)
			return;
		
		// preSMPS style (interleaved register/data)
		HadB4 = 0x00;
		Trk->VolOpPtr = InsPtr;
		while(*InsPtr)
		{
			if (*InsPtr == 0x83)
				break;	// terminator
			
			if (*InsPtr == 0xB0)
				Trk->FMAlgo = InsPtr[0x01];
			else if (*InsPtr == 0xB4)
			{
				Trk->PanAFMS = InsPtr[0x01];
				HadB4 = 0x01;
			}
			else if ((*InsPtr & 0xF0) == 0x40 && Trk->VolOpPtr == NULL)
				Trk->VolOpPtr = InsPtr;
			
			if ((*InsPtr & 0xF0) != 0x40)	// exclude the TL operators - RefreshFMVolume will do them
			{
				//WriteInsReg:
				WriteFMMain(Trk, InsPtr[0x00], InsPtr[0x01]);
			}
			InsPtr += 0x02;
		}
	}
	if (! HadB4)	// if it was in the list already, skip it
		WriteFMMain(Trk, 0xB4, Trk->PanAFMS);
	RefreshFMVolume(Trk);
	
	return;
}

void RefreshVolume(TRK_RAM* Trk)
{
	UINT8 FinalVol;
	
	if (Trk->ChannelMask & 0x80)	// 80/A0/C0/E0 - PSG
	{
		if (Trk->PlaybkFlags & PBKFLG_ATREST)
			return;
		
		FinalVol = Trk->Volume + Trk->VolEnvCache;
		if (FinalVol >= 0x10)
			FinalVol = 0x0F;
		FinalVol |= (Trk->ChannelMask & 0xE0) | 0x10;
		if (Trk->PlaybkFlags & PBKFLG_SPCMODE)
			FinalVol |= 0x20;	// Noise Channel
		WritePSG(FinalVol);
	}
	else if (Trk->ChannelMask & 0x10)	// 1x - FM drums
	{
		// do nothing
	}
	else	// 00/01/02/04/05/06 - FM
	{
		RefreshFMVolume(Trk);
	}
	
	return;
}

static UINT8 ApplyOutOperatorVol(TRK_RAM* Trk, UINT8 AlgoMask, UINT8 Reg, UINT8 CurTL)
{
	UINT8 IsOutputOp;
	
	if (Trk->SmpsSet->Cfg->VolMode & VOLMODE_BIT7)
	{
		IsOutputOp = (CurTL & 0x80);
	}
	else // VOLMODE_ALGO
	{
		IsOutputOp = (Reg & 0x0C) >> 2;	// 40/44/48/4C -> Bit 0/1/2/3
		IsOutputOp = AlgoMask & (1 << IsOutputOp);
	}
	if (! IsOutputOp)
		return CurTL;
	
	if (Trk->SmpsSet->Cfg->VolMode & VOLMODE_SETVOL)
		CurTL = Trk->Volume;
	else
		CurTL += Trk->Volume;
	CurTL &= 0x7F;
	
	return CurTL;
}

void RefreshFMVolume(TRK_RAM* Trk)
{
	const UINT8* OpPtr;
	const UINT8* VolPtr;
	UINT8 AlgoMask;
	UINT8 CurOp;
	UINT8 CurTL;
	
//	if (Trk->ChannelMask & 0x10)
//		return;	// don't refresh on DAC/Drum tracks
	
	AlgoMask = AlgoOutMask[Trk->FMAlgo & 0x07];
	if (! (Trk->SmpsSet->Cfg->InsMode & INSMODE_INT))
	{
		OpPtr = Trk->SmpsSet->Cfg->InsReg_TL;
		VolPtr = Trk->VolOpPtr;
		if (OpPtr == NULL || VolPtr == NULL)
			return;
		
		// normal mode - OpPtr has the Registers, VolPtr their values
		for (CurOp = 0x00; CurOp < 0x04; CurOp ++)
		{
			CurTL = ApplyOutOperatorVol(Trk, AlgoMask, OpPtr[CurOp], VolPtr[CurOp]);
			WriteFMMain(Trk, OpPtr[CurOp], CurTL);
		}
	}
	else
	{
		// interleaved mode - VolPtr has Reg, Data, Reg, Data, ...
		// Note that it the registers can be in a random order or, like for drum instruments, partly missing.
		// (preSMPS uses the Special FM3 mode to play 2x 2op drums)
		// So I iterate through the whole instrument and search for the volume data.
		VolPtr = Trk->VolOpPtr;
		while(*VolPtr && *VolPtr != 0x83)
		{
			CurOp = *VolPtr;	VolPtr ++;
			if ((CurOp & 0xF0) == 0x40)
			{
				CurTL = ApplyOutOperatorVol(Trk, AlgoMask, CurOp, *VolPtr);
				WriteFMMain(Trk, CurOp, CurTL);
			}
			VolPtr ++;
		}
	}
	
	return;
}

void SendSSGEG(TRK_RAM* Trk, const UINT8* Data, UINT8 ForceMaxAtk)
{
	const UINT8* OpPtr;
	UINT8 CurOp;
	
	if ((Trk->SmpsSet->Cfg->InsMode & 0x01) == INSMODE_DEF)
		OpPtr = OpList_DEF;
	else //if ((Trk->SmpsSet->Cfg->InsMode & 0x01) == INSMODE_HW)
		OpPtr = OpList_HW;
	
	for (CurOp = 0x00; CurOp < 0x04; CurOp ++)
	{
		WriteFMMain(Trk, 0x90 | OpPtr[CurOp], Data[CurOp]);
		if (ForceMaxAtk)
			WriteFMMain(Trk, 0x50 | OpPtr[CurOp], 0x1F);
	}
	
	return;
}



static void InitMusicPlay(const SMPS_CFG* SmpsCfg)
{
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	const SMPS_CFG_INIT* InitCfg;
	
	Extra_SongStop(1);
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		TempTrk->PlaybkFlags &= ~PBKFLG_HOLD;
		DoNoteOff(TempTrk);
		DisableSSGEG(TempTrk);
		
		if (TempTrk->SmpsSet != NULL)
			TempTrk->SmpsSet->UsageCounter --;
		TempTrk->PlaybkFlags = 0x00;
		TempTrk->SmpsSet = NULL;
	}
	for (CurTrk = 0; CurTrk < 2; CurTrk ++)
		SmpsRAM.MusDrmTrks[CurTrk].PlaybkFlags &= ~PBKFLG_ACTIVE;
	SmpsRAM.NoiseDrmVol = 0x00;
	ResetSpcFM3Mode();
	SmpsRAM.FadeOut.Steps = 0x00;
	if (SmpsRAM.FadeIn.Steps)
		SmpsRAM.FadeIn.Steps |= 0x80;	// allow for proper FadeIn when playing a new song
	SmpsRAM.ModData = NULL;
	
	InitCfg = &SmpsCfg->InitCfg;
	if (SmpsRAM.LockTimingMode == 0xFF)
	{
		SmpsRAM.LockTimingMode = InitCfg->Timing_Lock;
		SmpsRAM.TimingMode = InitCfg->Timing_DefMode;
		SmpsRAM.TimerAVal = InitCfg->Timing_TimerA;
		SmpsRAM.TimerBVal = InitCfg->Timing_TimerB;
	}
	else
	{
		SmpsRAM.LockTimingMode = InitCfg->Timing_Lock;
		if (InitCfg->Timing_DefMode != 0xFF)
			SmpsRAM.TimingMode = InitCfg->Timing_DefMode;
		if (InitCfg->Timing_TimerA)
			SmpsRAM.TimerAVal = InitCfg->Timing_TimerA;
		if (InitCfg->Timing_TimerB)
			SmpsRAM.TimerBVal = InitCfg->Timing_TimerB;
	}
	if (SmpsRAM.TimingMode == 0x00)
		ym2612_timer_mask(0x00);	// no YM2612 Timer
	else if (SmpsRAM.TimingMode == 0x20)
		ym2612_timer_mask(0x01);	// YM2612 Timer A
	else if (SmpsRAM.TimingMode == 0x40)
		ym2612_timer_mask(0x02);	// YM2612 Timer B
	else //if (SmpsRAM.TimingMode == 0x80)
		ym2612_timer_mask(0x03);	// YM2612 Timer A and B
	ResetYMTimerA();
	ResetYMTimerB();
	
	SetDACDriver((DAC_CFG*)&SmpsCfg->DACDrv);
#ifndef DISABLE_NECPCM
	SetNecPCMDriver((DAC_CFG*)&SmpsCfg->DACDrv);
#endif
	
	DAC_ResetOverride();
	
	return;
}

static UINT8 CheckTrkRange(UINT8 TrkID, UINT8 BestTrkID, UINT8 FirstTrk, UINT8 TrkEnd)
{
	UINT8 CurTrk;
	
	if (TrkID >= FirstTrk && TrkID < TrkEnd)
	{
		// within the wanted range - check for usage
		if (! (SmpsRAM.MusicTrks[TrkID].PlaybkFlags & PBKFLG_ACTIVE))
			return TrkID;	// free - return
		// else try the rest
	}
	
	if (! (SmpsRAM.MusicTrks[BestTrkID].PlaybkFlags & PBKFLG_ACTIVE))
		return BestTrkID;	// the best-fit one is free - accepted
	
	// else find the first free one
	for (CurTrk = FirstTrk; CurTrk < TrkEnd; CurTrk ++)
	{
		if (! (SmpsRAM.MusicTrks[CurTrk].PlaybkFlags & PBKFLG_ACTIVE))
			return CurTrk;
	}
	
	return TrkID;	// no free track - return original ID
}

static UINT8 CheckTrkID(UINT8 TrkID, UINT8 ChnBits)
{
	UINT8 BestTrkID;
	
	if (ChnBits & 0x80)
	{
		BestTrkID = (ChnBits & 0x60) >> 5;
		return CheckTrkRange(TrkID, TRACK_MUS_PSG1 + BestTrkID, TRACK_MUS_PSG1, TRACK_MUS_PSG_END);
	}
	else if ((ChnBits & 0xF8) == 0x18 && ChnBits != 0x1F)
	{
		return CheckTrkRange(TrkID, TrkID, TRACK_MUS_PWM1, TRACK_MUS_PWM_END);
	}
	
	return TrkID;
}

static void LoadChannelSet(UINT8 TrkIDStart, UINT8 ChnCount, UINT16* FilePos, UINT8 Mode,
						   UINT8 ChnListSize, const UINT8* ChnList, UINT8 TickMult, UINT8 TrkBase)
{
	SMPS_SET* SmpsSet = SmpsRAM.MusSet;
	const UINT8* Data = SmpsSet->Seq.Data;
	UINT16 HdrChnSize;
	UINT16 CurPos;
	UINT8 CurTrk;
	UINT8 TrkID;
	UINT8 NextTrkID;
	TRK_RAM* TempTrk;
	
	if (! ChnCount)
		return;
	
	HdrChnSize = 0x04;
	if (Mode & CHNMODE_PSG)
		HdrChnSize += 0x02;
	
	CurPos = *FilePos;
	NextTrkID = TrkIDStart;
	for (CurTrk = 0; CurTrk < ChnCount; CurTrk ++, CurPos += HdrChnSize)
	{
		if (Mode & CHNMODE_DRM)	// able to skip drum channels?
		{
			// skip Drum tracks unless Channel Bits are 0x10-0x17
			while(NextTrkID < TRACK_MUS_FM1 && ! (ChnList[CurTrk] & 0x10))
				NextTrkID ++;
		}
		TrkID = CheckTrkID(NextTrkID, ChnList[CurTrk]);
		if (TrkID >= MUS_TRKCNT || CurTrk >= ChnListSize)
			break;
		if (TrkID == NextTrkID)
			NextTrkID ++;
		
		TempTrk = &SmpsRAM.MusicTrks[TrkID];
		memset(TempTrk, 0x00, sizeof(TRK_RAM));
		TempTrk->SmpsSet = SmpsSet;
		SmpsSet->UsageCounter ++;
		TempTrk->PlaybkFlags = PBKFLG_ACTIVE;
		TempTrk->ChannelMask = ChnList[CurTrk];
		TempTrk->TickMult = TickMult;
		TempTrk->Pos = ReadPtr(&Data[CurPos + 0x00], SmpsSet);
		TempTrk->Transpose = Data[CurPos + 0x02];
		TempTrk->Volume = Data[CurPos + 0x03];
		if (Mode & CHNMODE_PSG)
		{
			TempTrk->ModEnv = Data[CurPos + 0x04];
			TempTrk->Instrument = Data[CurPos + 0x05];
		}
		else
		{
			//FinishFMTrkInit:
			TempTrk->ModEnv = 0x00;
			TempTrk->Instrument = 0x00;
		}
		//FinishTrkInit:
		TempTrk->StackPtr = TRK_STACK_SIZE;
		TempTrk->PanAFMS = 0xC0;
		TempTrk->RemTicks = 0x01;
#ifdef ENABLE_LOOP_DETECTION
		if (SmpsSet->LoopPtrs != NULL)
			TempTrk->LoopOfs = SmpsSet->LoopPtrs[TrkBase + CurTrk];
		else
			TempTrk->LoopOfs.Ptr = 0x0000;
#endif
		
		if ((TempTrk->ChannelMask & 0xF8) == 0x10)	// DAC drum channels
			TempTrk->SpcDacMode = SmpsSet->Cfg->DrumChnMode;
		if ((TempTrk->ChannelMask & 0xF8) == 0x18)	// PWM channels
			SmpsRAM.MusicTrks[TRACK_MUS_FM6].PlaybkFlags = 0x00;	// disable FM 6 for PWM simulation
		
		if (TrkID == TRACK_MUS_DRUM && ! (TempTrk->ChannelMask & 0x09))
			WriteFMMain(TempTrk, 0xB4, TempTrk->PanAFMS);	// force Pan bits to LR
		
		if (TempTrk->Pos >= SmpsSet->Seq.Len)
		{
			TempTrk->PlaybkFlags &= ~PBKFLG_ACTIVE;
			//printf("Track XX points after EOF!\n");
		}
	}
	// if we stopped early, go over all remaining channels
	for (; CurTrk < ChnCount; CurTrk ++, CurPos += HdrChnSize)
		;
	
	*FilePos = CurPos;
	return;
}

//static const UINT8 FMChnOrder[7] = {0x16, 0x00, 0x01, 0x02, 0x04, 0x05, 0x06};
//static const UINT8 FMChnOrder[7] = {0x12, 0x00, 0x01, 0x04, 0x05, 0x06, 0x02};
//static const UINT8 PSGChnOrder[3] = {0x80, 0xA0, 0xC0};

void PlayMusic(SMPS_SET* SmpsFileSet)
{
	SMPS_SET* SmpsSet;
	const SMPS_CFG* SmpsCfg = SmpsFileSet->Cfg;
	const UINT8* Data;
	UINT16 CurPos;
	UINT8 FMChnCnt;
	UINT8 PSGChnCnt;
	UINT8 TickMult;
	UINT8 TrkBase;
	
	if (SmpsRAM.MusSet != NULL && ! (SmpsRAM.MusSet->SeqFlags & SEQFLG_NEED_SAVE))
	{
		// Note: It makes a save state ONLY if:
		//	1. the song-to-be-played has the "need save" flag set and
		//	2. the current song does NOT have that flag set
		if (SmpsFileSet->SeqFlags & SEQFLG_NEED_SAVE)
			BackupMusic(&MusicSaveState);
	}
	
	//StopAllSound();	// in the driver, but I can do that in a better way
	InitMusicPlay(SmpsCfg);
	
	FreeSMPSFileRef_Zero(&SmpsRAM.MusSet);
	
	SmpsSet = (SMPS_SET*)malloc(sizeof(SMPS_SET));	// create a copy of the struct for the SMPS driver
	*SmpsSet = *SmpsFileSet;
	SmpsSet->UsageCounter = 0;
	
	SmpsRAM.MusSet = SmpsSet;
	Data = SmpsSet->Seq.Data;
	CurPos = 0x00;
	
	//InsLibPtr = ReadPtr(&Data[CurPos + 0x00], SmpsSet);	// done by the SMPS preparser
	FMChnCnt = Data[CurPos + 0x02];
	PSGChnCnt = Data[CurPos + 0x03];
	if (FMChnCnt + PSGChnCnt > SmpsCfg->FMChnCnt + SmpsCfg->PSGChnCnt)
		return;	// invalid file
	
	TickMult = Data[CurPos + 0x04];
	SmpsRAM.TempoInit = Data[CurPos + 0x05];
	SmpsRAM.TempoCntr = SmpsRAM.TempoInit;
	if (SmpsCfg->Tempo1Tick == T1TICK_NOTEMPO)
	{
		// DoTempo is called before PlayMusic and thus isn't executed during the first tick.
		// So we undo one DoTempo.
		switch(SmpsCfg->TempoMode)
		{
		case TEMPO_TIMEOUT:
			SmpsRAM.TempoCntr ++;
			break;
		case TEMPO_OVERFLOW:
			SmpsRAM.TempoCntr -= SmpsRAM.TempoInit;	// prevent overflow
			break;
		case TEMPO_OVERFLOW2:
			// This is not 100% correct, but all games with this algorithm execute PlayMusic first anyway.
			SmpsRAM.TempoCntr = (UINT8)(0x100 - SmpsRAM.TempoInit);
			break;
		case TEMPO_TOUT_OFLW:
			if (! (SmpsRAM.TempoInit & 0x80))
				SmpsRAM.TempoCntr ++;
			else
				SmpsRAM.TempoCntr -= (SmpsRAM.TempoInit & 0x7F);	// prevent overflow
			break;
		case TEMPO_OFLW_MULT:
			SmpsRAM.TempoCntr -= SmpsRAM.TempoInit;	// prevent overflow
			if (SmpsRAM.TempoInit & 0x80)
				SmpsRAM.MusMultUpdate --;	// The first tick won't overflow and add 1 to the counter.
			break;
		case TEMPO_TOUT_REV:
			SmpsRAM.TempoCntr = 1;
			break;
		}
	}
	CurPos += 0x06;
	
	Extra_SongStart(0);
	
	TrkBase = 0x00;
	// FM channels
	LoadChannelSet(TRACK_MUS_DRUM, FMChnCnt, &CurPos, CHNMODE_DEF | CHNMODE_DRM,
					SmpsCfg->FMChnCnt, SmpsCfg->FMChnList, TickMult, TrkBase);
	if (FMChnCnt == SmpsCfg->FMChnCnt && SmpsCfg->FMChnList[FMChnCnt - 1] == 0x06)
		//SetDACState(0x00);	// disable DAC forcefully to ensure FM 6 notes are sounding
		DAC_Stop(0x00);			// should have the same effect
	TrkBase += FMChnCnt;
	
	// PSG channels
	LoadChannelSet(TRACK_MUS_PSG1, PSGChnCnt, &CurPos, CHNMODE_PSG,
					SmpsCfg->PSGChnCnt, SmpsCfg->PSGChnList, TickMult, TrkBase);
	TrkBase += PSGChnCnt;
	
	// additional channels that don't have "channel count" values in the header (i.e. PWM channels)
	LoadChannelSet(TRACK_MUS_PWM1, SmpsCfg->AddChnCnt, &CurPos, CHNMODE_DEF,
					SmpsCfg->AddChnCnt, SmpsCfg->AddChnList, TickMult, TrkBase);
	TrkBase += SmpsCfg->AddChnCnt;
	
	Extra_LoopInit();
	
	//SetSFXOverrideBits();
	
	return;
}

void PlaySFX(SMPS_SET* SmpsFileSet, UINT8 SpecialSFX)
{
	SMPS_SET* SmpsSet;
	SMPS_SET** SfxSetPtr;
	const SMPS_CFG* SmpsCfg = SmpsFileSet->Cfg;
	const UINT8* Data;
	UINT16 CurPos;
	UINT8 ChnCnt;
	UINT8 TickMult;
	UINT8 CurTrk;
	UINT8 SFXTrkID;
	UINT8 SpcSFXTrkID;
	UINT8 MusTrkID;
	TRK_RAM* SFXTrk;
	
	if (SmpsFileSet->SeqFlags & SEQFLG_CONT_SFX)
	{
		//SFXTrkID = SFX_ID;
		SFXTrkID = (SmpsFileSet->SeqBase >> 4) & 0xFF;
		if (SmpsRAM.ContSfxID == SFXTrkID)
		{
			SmpsRAM.ContSfxFlag = 0x80;
			SmpsRAM.ContSfxLoop = SmpsFileSet->Seq.Data[0x03];
			return;	// don't process the SFX command further
		}
		else
		{
			SmpsRAM.ContSfxID = SFXTrkID;
			SmpsRAM.ContSfxFlag = 0x00;
			SmpsRAM.ContSfxLoop = SmpsFileSet->Seq.Data[0x03];
		}
	}
	
	SmpsSet = (SMPS_SET*)malloc(sizeof(SMPS_SET));	// create a copy of the struct for the SMPS driver
	*SmpsSet = *SmpsFileSet;
	SmpsSet->UsageCounter = 0;
	
	Data = SmpsSet->Seq.Data;
	CurPos = 0x00;
	
	//if (! (SmpsSet->SeqFlags & SEQFLG_SPINDASH))
	//	SmpsRAM.SpinDashRev = 0;
	
	//InsLibPtr = ReadPtr(&Data[CurPos + 0x00], SmpsSet);
	TickMult = Data[CurPos + 0x02];
	ChnCnt = Data[CurPos + 0x03];
	CurPos += 0x04;
	
	for (CurTrk = 0; CurTrk < ChnCnt; CurTrk ++, CurPos += 0x06)
	{
		GetSFXChnPtrs(Data[CurPos + 0x01], &MusTrkID, &SpcSFXTrkID, &SFXTrkID);
		
		if (SpecialSFX)
		{
			if (SpcSFXTrkID == 0xFF)
				continue;
			SFXTrk = &SmpsRAM.SpcSFXTrks[SpcSFXTrkID];
		}
		else
		{
			if (SFXTrkID == 0xFF)
				continue;
			SFXTrk = &SmpsRAM.SFXTrks[SFXTrkID];
			
			if (SpcSFXTrkID != 0xFF)
				SmpsRAM.SpcSFXTrks[SpcSFXTrkID].PlaybkFlags |= PBKFLG_OVERRIDDEN;
		}
		if (MusTrkID != 0xFF)
			SmpsRAM.MusicTrks[MusTrkID].PlaybkFlags |= PBKFLG_OVERRIDDEN;
		
		memset(SFXTrk, 0x00, sizeof(TRK_RAM));
		if (SpecialSFX)
			SfxSetPtr = &SmpsRAM.SFXSet[SFX_TRKCNT + SpcSFXTrkID];
		else
			SfxSetPtr = &SmpsRAM.SFXSet[SFXTrkID];
		FreeSMPSFile(*SfxSetPtr);
		FreeSMPSFileRef_Zero(SfxSetPtr);
		
		*SfxSetPtr = SmpsSet;		// write SFXSet Pointer
		SFXTrk->SmpsSet = SmpsSet;
		SmpsSet->UsageCounter ++;
		
		SFXTrk->PlaybkFlags = Data[CurPos + 0x00];
		SFXTrk->ChannelMask = Data[CurPos + 0x01];
		if (SFXTrk->ChannelMask == 0x02)
			ResetSpcFM3Mode();
		SFXTrk->TickMult = TickMult;
		SFXTrk->Pos = ReadPtr(&Data[CurPos + 0x00], SFXTrk->SmpsSet);
		SFXTrk->Transpose = Data[CurPos + 0x02];
		SFXTrk->Volume = Data[CurPos + 0x03];
		//FinishFMTrkInit:
		SFXTrk->ModEnv = 0x00;
		SFXTrk->Instrument = 0x00;
		//FinishTrkInit:
		SFXTrk->StackPtr = TRK_STACK_SIZE;
		SFXTrk->PanAFMS = 0xC0;
		SFXTrk->RemTicks = 0x01;
		
		if (SFXTrk->Pos >= SFXTrk->SmpsSet->Seq.Len)
			SFXTrk->PlaybkFlags &= ~PBKFLG_ACTIVE;
		
		if (SpcSFXTrkID != 0xFF && SFXTrkID != 0xFF)
		{
			if (SmpsRAM.SFXTrks[SFXTrkID].PlaybkFlags & PBKFLG_ACTIVE)
				SmpsRAM.SpcSFXTrks[SpcSFXTrkID].PlaybkFlags |= PBKFLG_OVERRIDDEN;
		}
		
		//SFXTrk->InsPtr = InsLibPtr;
		DoNoteOff(SFXTrk);
		DisableSSGEG(SFXTrk);
	}
	
	return;
}

void GetSFXChnPtrs(UINT8 ChannelMask, UINT8* MusicTrk, UINT8* SFXTrk, UINT8* SpcSFXTrk)
{
	if (MusicTrk != NULL)
		*MusicTrk = GetChannelTrack(ChannelMask, MUS_TRKCNT, SmpsRAM.MusicTrks);
	
	if (SFXTrk != NULL)
		*SFXTrk = GetChannelTrack(ChannelMask, SFX_TRKCNT, SmpsRAM.SFXTrks);
	
	if (SpcSFXTrk != NULL)
		*SpcSFXTrk = GetChannelTrack(ChannelMask, SPCSFX_TRKCNT, SmpsRAM.SpcSFXTrks);
	
	return;
}

UINT8 GetChannelTrack(UINT8 ChannelMask, UINT8 TrkCount, const TRK_RAM* Tracks)
{
	UINT8 CurTrk;
	UINT8 TrkID;
	
	TrkID = 0xFF;
	for (CurTrk = 0; CurTrk < TrkCount; CurTrk ++)
	{
		if (Tracks[CurTrk].ChannelMask == ChannelMask)
		{
			if (Tracks[CurTrk].PlaybkFlags & PBKFLG_ACTIVE)
				return CurTrk;	// active track found - take it
			else if (TrkID == 0xFF)
				TrkID = CurTrk;	// else search for active tracks with same Channel Mask
		}
	}
	
	return TrkID;
}


static void DoPause(void)
{
	if (! SmpsRAM.PauseMode)
		return;	// 00 - not paused
	
	if (! (SmpsRAM.PauseMode & 0x80))
	{
		if (SmpsRAM.PauseMode == 0x01)
		{
			SmpsRAM.PauseMode = 0x02;
			SilenceAll();
		}
	}
	else
	{
		//UnpauseMusic:
		UINT8 CurTrk;
		TRK_RAM* TempTrk;
		
		if (SmpsRAM.FadeOut.Steps)
		{
			StopAllSound();
			return;
		}
		
		for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
		{
			TempTrk = &SmpsRAM.MusicTrks[CurTrk];
			if (TempTrk->PlaybkFlags & PBKFLG_ACTIVE)
			{
				if (! (TempTrk->ChannelMask & 0x80))
					WriteFMMain(TempTrk, 0xB4, TempTrk->PanAFMS);
			}
		}
		
		for (CurTrk = 0; CurTrk < SPCSFX_TRKCNT; CurTrk ++)
		{
			TempTrk = &SmpsRAM.SpcSFXTrks[CurTrk];
			if (TempTrk->PlaybkFlags & PBKFLG_ACTIVE)
			{
				if (! (TempTrk->ChannelMask & 0x80))
					WriteFMMain(TempTrk, 0xB4, TempTrk->PanAFMS);
			}
		}
	}
	
	return;
}

static void DoTempo(void)
{
	UINT16 NewTempoVal;
	UINT8 CurTrk;
	
	switch(SmpsRAM.MusSet->Cfg->TempoMode)
	{
	case TEMPO_NONE:
		return;
	case TEMPO_TIMEOUT:
		// Note: (pre-)SMPS 68k checks TempoInit, SMPS Z80 checks TempoCntr
		if (! SmpsRAM.TempoInit)
			return;	// Tempo 00 - never delayed
		
		SmpsRAM.TempoCntr --;
		if (SmpsRAM.TempoCntr)
			return;
		// reached 00 - delay tracks
		SmpsRAM.TempoCntr = SmpsRAM.TempoInit;
		break;
	case TEMPO_OVERFLOW:
		NewTempoVal = SmpsRAM.TempoCntr + SmpsRAM.TempoInit;
		SmpsRAM.TempoCntr = (UINT8)NewTempoVal;
		if (NewTempoVal < 0x100)
			return;
		// calculation overflowed - delay tracks
		break;
	case TEMPO_OVERFLOW2:
		NewTempoVal = SmpsRAM.TempoCntr + SmpsRAM.TempoInit;
		SmpsRAM.TempoCntr = (UINT8)NewTempoVal;
		if (NewTempoVal >= 0x100)
			return;
		// calculation didn't overflow - delay tracks
		break;
	case TEMPO_TOUT_OFLW:
		if (! (SmpsRAM.TempoInit & 0x80))
		{
			// Tempo 00..7F - Timeout
			if (! SmpsRAM.TempoInit)
				return;	// Tempo 00 - never delayed
			
			SmpsRAM.TempoCntr --;
			if (SmpsRAM.TempoCntr)
				return;
			// reached 00 - delay tracks
			SmpsRAM.TempoCntr = SmpsRAM.TempoInit;
		}
		else
		{
			// Tempo 80..FF - Overflow
			NewTempoVal = SmpsRAM.TempoCntr + (SmpsRAM.TempoInit & 0x7F);
			SmpsRAM.TempoCntr = (UINT8)NewTempoVal;
			if (NewTempoVal < 0x100)
				return;
			// calculation overflowed - delay tracks
		}
		break;
	case TEMPO_OFLW_MULT:
		if (! (SmpsRAM.TempoInit & 0x80))
		{
			// Tempo 00..7F - delay on overflow
			NewTempoVal = SmpsRAM.TempoCntr + SmpsRAM.TempoInit;
			SmpsRAM.TempoCntr = (UINT8)NewTempoVal;
			if (NewTempoVal < 0x100)
				return;
			// calculation overflowed - delay tracks
			break;
		}
		else
		{
			// Tempo 80..FF - double-update if not overflowed
			NewTempoVal = SmpsRAM.TempoCntr + SmpsRAM.TempoInit;
			SmpsRAM.TempoCntr = (UINT8)NewTempoVal;
			if (NewTempoVal < 0x100)
				SmpsRAM.MusMultUpdate ++;	// make all tracks updated twice
			return;
		}
	case TEMPO_TOUT_REV:
		if (! SmpsRAM.TempoInit)
			return;	// Tempo 00 - never delayed
		
		SmpsRAM.TempoCntr --;
		if (! SmpsRAM.TempoCntr)
			SmpsRAM.TempoCntr = SmpsRAM.TempoInit;	// reset counter + update once
		else
			SmpsRAM.MusMultUpdate --;	// prevent update
		return;	// don't use the "inject delay" routine for this tempo
	}
	
	// Delay all tracks by 1 frame.
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
		SmpsRAM.MusicTrks[CurTrk].RemTicks ++;
	
	return;
}

void FadeOutMusic(void)
{
	const SMPS_CFG* SmpsCfg = (SmpsRAM.MusSet != NULL) ? SmpsRAM.MusSet->Cfg : NULL;
	FADE_INF* Fade = &SmpsRAM.FadeOut;
	
	if (SmpsCfg != NULL)
	{
		Fade->Steps = SmpsCfg->FadeOut.Steps | 0x80;
		Fade->DlyInit = SmpsCfg->FadeOut.Delay;
	}
	else
	{
		Fade->Steps = 0x28 | 0x80;
		Fade->DlyInit = 0x06;
	}
	Fade->DlyCntr = Fade->DlyInit;
	
	return;
}

void FadeOutMusic_Custom(UINT8 StepCnt, UINT8 DelayFrames)
{
	FADE_INF* Fade = &SmpsRAM.FadeOut;
	
	Fade->Steps = StepCnt | 0x80;
	Fade->DlyInit = DelayFrames;
	Fade->DlyCntr = Fade->DlyInit;
	
	return;
}

static void DoFade(UINT8 FadeMode)
{
	const SMPS_CFG* SmpsCfg = SmpsRAM.MusSet->Cfg;
	const FADE_CFG* FadeCfg = FadeMode ? &SmpsCfg->FadeOut : &SmpsCfg->FadeIn;
	FADE_INF* Fade = FadeMode ? &SmpsRAM.FadeOut : &SmpsRAM.FadeIn;
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	UINT8 PrevVol;
	INT8 FadeDir;
	
	if (! Fade->Steps)
		return;	// Fading disabled - return
	
	if (Fade->Steps & 0x80)
	{
		if (FadeMode)	// FadeOut
		{
			// Maybe a loop checking for Channel Bits 0x80 and 0x10 would be a better solution.
			//call StopDrumPSG:
			//SmpsRAM.MusicTrks[TRACK_MUS_DRUM].PlaybkFlags = 0x00;
			//SmpsRAM.MusicTrks[TRACK_MUS_DAC2].PlaybkFlags = 0x00;
			SmpsRAM.MusicTrks[TRACK_MUS_DRUM].PlaybkFlags |= PBKFLG_OVERRIDDEN;	// works better in combination with FadeIn
			SmpsRAM.MusicTrks[TRACK_MUS_DAC2].PlaybkFlags |= PBKFLG_OVERRIDDEN;
			//SmpsRAM.MusicTrks[TRACK_MUS_FM6].PlaybkFlags = 0x00;	// for SMPS Z80 with FM drums only
	#if 0
			SmpsRAM.MusicTrks[TRACK_MUS_PSG3].PlaybkFlags = 0x00;
			SmpsRAM.MusicTrks[TRACK_MUS_PSG1].PlaybkFlags = 0x00;
			SmpsRAM.MusicTrks[TRACK_MUS_PSG2].PlaybkFlags = 0x00;
			SilencePSG();
	#endif
			
			Fade->Steps &= ~0x80;	// Note: The driver clears this bit even if it wasn't set.
		}
		else	// FadeIn
		{
			Fade->Steps &= ~0x80;
			
			SmpsRAM.MusicTrks[TRACK_MUS_DRUM].PlaybkFlags |= PBKFLG_OVERRIDDEN;
			SmpsRAM.MusicTrks[TRACK_MUS_DAC2].PlaybkFlags |= PBKFLG_OVERRIDDEN;
			// Note: Similarly to the SMPS Z80 FadeOut routine,
			//       Sonic 3K sets the 'overridden' bit on the PSG channels instead of fading them.
			
			SmpsRAM.NoiseDrmVol += FadeCfg->AddPSG * Fade->Steps;
			for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
			{
				TempTrk = &SmpsRAM.MusicTrks[CurTrk];
				if (TempTrk->ChannelMask & 0x80)
					TempTrk->Volume += FadeCfg->AddPSG * Fade->Steps;
				else
					TempTrk->Volume += FadeCfg->AddFM * Fade->Steps;
			}
			
			// enforce volume refresh
			Fade->DlyCntr = (SmpsCfg->FadeMode == FADEMODE_68K) ? 0 : 1;
		}
	}
	
	if (SmpsCfg->FadeMode == FADEMODE_Z80)
	{
		// delay by (n-1) frames / execute every n-th frame
		Fade->DlyCntr --;
		if (Fade->DlyCntr)
			return;
	}
	else //if (SmpsCfg->FadeMode == FADEMODE_68K)
	{
		// delay by n frames / execute every (n+1)-th frame
		if (Fade->DlyCntr)
		{
			Fade->DlyCntr --;
			return;
		}
	}
	// Timeout expired
	
	//ApplyFading:
	Fade->DlyCntr = Fade->DlyInit;
	Fade->Steps --;
	if (! Fade->Steps)
	{
		if (FadeMode)	// FadeOut
		{
			StopAllSound();
			return;
		}
		else	// FadeIn
		{
			SmpsRAM.MusicTrks[TRACK_MUS_DRUM].PlaybkFlags &= ~PBKFLG_OVERRIDDEN;
			SmpsRAM.MusicTrks[TRACK_MUS_DAC2].PlaybkFlags &= ~PBKFLG_OVERRIDDEN;
			// and continue
		}
	}
	
	FadeDir = FadeMode ? +1 : -1;
	SmpsRAM.NoiseDrmVol += FadeCfg->AddPSG * FadeDir;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		
#if 0
		if (FadeMode)
		{
			TempTrk->Volume ++;
			if (TempTrk->Volume & 0x80)
				TempTrk->Volume --;	// prevent overflow
		}
		else
		{
			TempTrk->Volume --;
		}
#endif
		// This gets more complicated for the few idiotic homebrew SMPS files
		// which use negative volumes that overflow into a positive range.
		PrevVol = TempTrk->Volume;
		if (TempTrk->ChannelMask & 0x80)
			TempTrk->Volume += FadeCfg->AddPSG * FadeDir;
		else
			TempTrk->Volume += FadeCfg->AddFM * FadeDir;
		if (FadeMode)
		{
			if ((TempTrk->Volume & 0x80) && ! (PrevVol & 0x80))
				TempTrk->Volume = 0x7F;	// prevent overflow
		}
		else
		{
			// TODO: possible overflow check here
		}
		
		if ((TempTrk->PlaybkFlags & PBKFLG_ACTIVE) && ! (TempTrk->PlaybkFlags & PBKFLG_OVERRIDDEN))
			RefreshVolume(TempTrk);
	}
	
	return;
}

static void DoSpecialFade(void)
{
	const SMPS_CFG* SmpsCfg = SmpsRAM.MusSet->Cfg;
	FADE_SPC_INF* Fade = &SmpsRAM.FadeSpc;
	INT8 FadeDir;
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	UINT8 PrevVol;
	
	if (! Fade->Mode || Fade->Mode == 0x02)
		return;	// Fading disabled - return
	
	FadeDir = (Fade->Mode & 0x80) ? +1 : -1;
	
	SmpsRAM.NoiseDrmVol += Fade->AddPSG * FadeDir;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		
		PrevVol = TempTrk->Volume;
		if (TempTrk->ChannelMask & 0x80)
		{
			TempTrk->Volume += Fade->AddPSG * FadeDir;
		}
		else if (TempTrk->ChannelMask & 0x10)
		{
			if (TempTrk->ChannelMask & 0x08)
				TempTrk->Volume += Fade->AddPWM * FadeDir;
			else
				TempTrk->Volume += Fade->AddDAC * FadeDir;
		}
		else
		{
			TempTrk->Volume += Fade->AddFM * FadeDir;
		}
		if (! (TempTrk->Volume & 0x80) || (PrevVol & 0x80))
		{
			// execute if no overflow
			if ((TempTrk->PlaybkFlags & PBKFLG_ACTIVE) && ! (TempTrk->PlaybkFlags & PBKFLG_OVERRIDDEN))
				RefreshVolume(TempTrk);
		}
	}
	
	if (Fade->Mode & 0x80)
		Fade->Mode = 0x00;
	else
		Fade->Mode = 0x02;
	
	return;
}

#if 0
static void DoFadeOut_GoldenAxeIII(void)
{
	const UINT8 FADE_ARR[4] = {TRACK_MUS_FM1, TRACK_MUS_FM3, TRACK_MUS_FM5, TRACK_MUS_PSG1};
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	UINT8 FinalVol;
	UINT16 TempSht;
	UINT8 Fade004;	// Fade->Mode
	UINT8 Fade005;	// Fade->Steps
	UINT8 Fade006;	// Fade->Increment
	UINT8 Fade007;	// Fade->Counter
	
	if (! Fade004)
		return;	// Fading disabled - return
	
	TempSht = Fade006 + Fade007;
	Fade007 = (UINT8)TempSht;
	if (TempSht < 0x100)
		return;
	
	if (Fade004 & 0x01)
	{
		if (! Fade005)
		{
			Fade004 = 0x00;
			return;
		}
		Fade005 --;
	}
	else
	{
		if (Fade005 != 0x30)
		{
			if (Fade004 & 0x02)
				StopAllSound();
			else
				DoPauseMusic();	// same as FF 01 80
			Fade004 = 0x00;
			return;
		}
		Fade005 ++;
	}
	CurTrk = FADE_ARR[Fade005 & 0x03];
	
	if (CurTrk < TRACK_MUS_PSG1)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		if (TempTrk->PlaybkFlags & PBKFLG_ACTIVE)
			RefreshFMVolume(TempTrk);	// RefreshFMVolume sends (Trk->Volume + Fade005)
		CurTrk ++;
		
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		if (TempTrk->PlaybkFlags & PBKFLG_ACTIVE)
			RefreshFMVolume(TempTrk);
	}
	else
	{
		for (; CurTrk < MUS_TRKCNT; CurTrk ++)
		{
			TempTrk = &SmpsRAM.MusicTrks[CurTrk];
			if (TempTrk->PlaybkFlags & PBKFLG_ACTIVE)
			{
				if (! (TempTrk->PlaybkFlags & PBKFLG_OVERRIDDEN))
				{
					if (TempTrk->PlaybkFlags & PBKFLG_ATREST)
						continue;
					
					FinalVol = TempTrk->Volume + TempTrk->VolEnvCache + Fade005;
					if (FinalVol >= 0x10)
						FinalVol = 0x0F;
					FinalVol |= TempTrk->ChannelMask | 0x10;
					if (TempTrk->PlaybkFlags & PBKFLG_SPCMODE)
						FinalVol |= 0x20;
					WritePSG(FinalVol);
				}
			}
		}
	}
	
	return;
}
#endif


void StopAllSound(void)
{
	TRK_RAM TempTrack;
	UINT8 CurChn;
	UINT8 CurTrk;
	
	// TODO: Clear all memory?
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
		SmpsRAM.MusicTrks[CurTrk].PlaybkFlags = 0x00;
	for (CurTrk = 0; CurTrk < 2; CurTrk ++)
		SmpsRAM.MusDrmTrks[CurTrk].PlaybkFlags = 0x00;
	for (CurTrk = 0; CurTrk < SFX_TRKCNT; CurTrk ++)
		SmpsRAM.SFXTrks[CurTrk].PlaybkFlags = 0x00;
	for (CurTrk = 0; CurTrk < SPCSFX_TRKCNT; CurTrk ++)
		SmpsRAM.SpcSFXTrks[CurTrk].PlaybkFlags = 0x00;
	
	for (CurChn = 0; CurChn < 8; CurChn ++)
		DAC_Stop(CurChn);
#ifndef DISABLE_NECPCM
	NECPCM_Stop();
#endif
	for (CurChn = 0; CurChn < 7; CurChn ++)
	{
		if ((CurChn & 0x03) == 0x03)
			continue;
		TempTrack.ChannelMask = CurChn;
		SilenceFMChn(&TempTrack);
		DisableSSGEG(&TempTrack);
	}
	
	SmpsRAM.FadeOut.Steps = 0x00;
	SmpsRAM.FadeIn.Steps = 0x00;
	SmpsRAM.ModData = NULL;
	SilencePSG();
	ResetSpcFM3Mode();
	DAC_ResetOverride();
	
	Extra_SongStop(0);
	
	CleanSmpsFiles();
	if (MusicSaveState.MusSet != NULL)
	{
		MusicSaveState.InUse = 0x00;
		MusicSaveState.MusSet->UsageCounter = 0;
		FreeSMPSFileRef_Zero(&MusicSaveState.MusSet);
	}
	return;
}

void ResetSpcFM3Mode(void)
{
	SmpsRAM.SpcFM3Mode = 0x0F;
	WriteFMI(0x27, SmpsRAM.SpcFM3Mode);
	
	return;
}

void DisableSSGEG(TRK_RAM* Trk)
{
	UINT8 CurOp;
	
	for (CurOp = 0x00; CurOp < 0x10; CurOp += 0x04)
		WriteFMMain(Trk, 0x90 | CurOp, 0x00);
	
	return;
}

void SilenceFMChn(TRK_RAM* Trk)
{
	UINT8 CurOp;
	
	//SetMaxRelRate:
	for (CurOp = 0x00; CurOp < 0x10; CurOp += 0x04)
		WriteFMMain(Trk, 0x80 | CurOp, 0xFF);
	
	for (CurOp = 0x00; CurOp < 0x10; CurOp += 0x04)
		WriteFMMain(Trk, 0x40 | CurOp, 0x7F);
	
	WriteFMI(0x28, Trk->ChannelMask);
	
	return;
}

static void SilenceAll(void)
{
	UINT8 CurChn;
	
	for (CurChn = 0; CurChn < 3; CurChn ++)
		WriteFMI(0xB4 | CurChn, 0x00);
	for (CurChn = 0; CurChn < 3; CurChn ++)
		WriteFMII(0xB4 | CurChn, 0x00);
	
	for (CurChn = 0; CurChn < 7; CurChn ++)
		WriteFMI(0x28, CurChn);
	
	SilencePSG();
	
	return;
}

static void SilencePSG(void)
{
	UINT8 CurChn;
	UINT8 PSGVal;
	
	PSGVal = 0x9F;
	for (CurChn = 0x00; CurChn < 0x04; CurChn ++, PSGVal += 0x20)
		WritePSG(PSGVal);
	
	return;
}

void RestoreBGMChannel(TRK_RAM* Trk)
{
	UINT8 MusTrkID;
	UINT8 SFXTrkID;
	UINT8 SpcSFXTrkID;
	TRK_RAM* MusTrk;
	TRK_RAM* SFXTrk;
	TRK_RAM* SpcSFXTrk;
	TRK_RAM* RstTrk;	// Restored Track
	
	if (Trk >= SmpsRAM.MusicTrks && Trk < SmpsRAM.MusicTrks + MUS_TRKCNT)
	{
		RstTrk = Trk;
	}
	else
	{
		GetSFXChnPtrs(Trk->ChannelMask, &MusTrkID, &SpcSFXTrkID, &SFXTrkID);
		MusTrk = (MusTrkID == 0xFF) ? NULL : &SmpsRAM.MusicTrks[MusTrkID];
		SFXTrk = (SFXTrkID == 0xFF) ? NULL : &SmpsRAM.SFXTrks[SFXTrkID];
		SpcSFXTrk = (SpcSFXTrkID == 0xFF) ? NULL : &SmpsRAM.SpcSFXTrks[SpcSFXTrkID];
		
		if (SpcSFXTrk != NULL && SpcSFXTrk->PlaybkFlags & PBKFLG_ACTIVE)
			RstTrk = SpcSFXTrk;
		else
			RstTrk = MusTrk;
	}
	if (RstTrk == NULL)
		return;	// This should really not happen, because then MusTrk was NULL.
	RstTrk->PlaybkFlags &= ~PBKFLG_OVERRIDDEN;
	if (! (RstTrk->PlaybkFlags & PBKFLG_ACTIVE))
		return;
	if (RstTrk->SmpsSet == NULL)
		return;
	
	// restore a channel (heavily dependent on the channel type)
	if (RstTrk->ChannelMask & 0x80)
	{
		// restore PSG channel
		if (Trk->NoiseMode & 0x0)
			WritePSG(Trk->NoiseMode);
	}
	else if ((RstTrk->ChannelMask & 0xF8) == 0x00)
	{
		// FM channel - 00..07
		const INS_LIB* InsLib;
		const UINT8* InsPtr;
		UINT8 InsID;
		
		// restore FM channel
		if (RstTrk->ChannelMask == 0x02)	// is FM 3?
		{
			if (RstTrk->PlaybkFlags & PBKFLG_SPCMODE)
				SmpsRAM.SpcFM3Mode = 0x4F;
			else
				SmpsRAM.SpcFM3Mode = 0x0F;
			WriteFMI(0x27, SmpsRAM.SpcFM3Mode);
		}
		
		// restore Instrument
		//if (Trk->Instrument & 0x80)	// That's what the actual driver does.
		if (Trk->FMInsSong)	// But this is safer in our case, since we're supporting large instrument tables.
		{
			InsLib = GetSongInsLib(Trk, Trk->FMInsSong);
			InsID = Trk->Instrument & 0x7F;
		}
		else
		{
			InsLib = &Trk->SmpsSet->InsLib;
			InsID = Trk->Instrument;
		}
		if (InsLib != NULL && InsID < InsLib->InsCount)
		{
			InsPtr = InsLib->InsPtrs[InsID];
			SendFMIns(Trk, InsPtr);
		}
		
		if (Trk->SSGEG.Type & 0x80)
			SendSSGEG(Trk, &Trk->SmpsSet->Seq.Data[Trk->SSGEG.DataPtr], Trk->SSGEG.Type & 0x01);
	}
	else if ((RstTrk->ChannelMask & 0xF8) == 0x10)
	{
		// Drum/DAC channel - 10..17
		WriteFMMain(Trk, 0xB4, Trk->PanAFMS);
	}
	
	return;
}

void BackupMusic(MUS_STATE* MusState)
{
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	
	// We'll make a backup of SmpsRAM.MusSet, so we're going to have an additional reference.
	// By doing this here, we ensure to not free the memory in the block below,
	// if MusState->MusSet and SmpsRAM.MusSet are the same.
	if (SmpsRAM.MusSet != NULL)
		SmpsRAM.MusSet->UsageCounter ++;
	
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
		FreeSMPSFile(MusState->MusicTrks[CurTrk].SmpsSet);
	FreeSMPSFile(MusState->MusSet);
	FreeSMPSFileRef_Zero(&MusState->MusSet);
	
	MusState->MusSet = SmpsRAM.MusSet;
	MusState->TimerAVal = SmpsRAM.TimerAVal;
	MusState->TimerBVal = SmpsRAM.TimerBVal;
	MusState->TimingMode = SmpsRAM.TimingMode;
	MusState->LockTimingMode = SmpsRAM.LockTimingMode;
	memcpy(MusState->DacChVol, SmpsRAM.DacChVol, sizeof(UINT8) * 2);
	MusState->MusicPaused = SmpsRAM.MusicPaused;
	MusState->SpcFM3Mode = SmpsRAM.SpcFM3Mode;
	MusState->NoiseDrmVol = SmpsRAM.NoiseDrmVol;
	MusState->TempoCntr = SmpsRAM.TempoCntr;
	MusState->TempoInit = SmpsRAM.TempoInit;
	memcpy(MusState->FM3Freqs_Mus, SmpsRAM.FM3Freqs_Mus, sizeof(UINT16) * 4);
	memcpy(MusState->MusicTrks, SmpsRAM.MusicTrks, sizeof(TRK_RAM) * MUS_TRKCNT);
	
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &MusState->MusicTrks[CurTrk];
		if (TempTrk->SmpsSet != NULL)
			TempTrk->SmpsSet->UsageCounter ++;
	}
	MusState->InUse = 0x01;
	
	return;
}

void RestoreMusic(MUS_STATE* MusState)
{
	UINT8 CurTrk;
	TRK_RAM* TempTrk;
	
	if (! MusState->InUse)
		return;
	
	Extra_SongStop(1);
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		TempTrk->PlaybkFlags &= ~PBKFLG_HOLD;
		DoNoteOff(TempTrk);
		DisableSSGEG(TempTrk);
		if (TempTrk->SmpsSet != NULL)
			TempTrk->SmpsSet->UsageCounter --;
	}
	FreeSMPSFileRef_Zero(&SmpsRAM.MusSet);
	
	SmpsRAM.MusSet = MusState->MusSet;
	memcpy(SmpsRAM.DacChVol, MusState->DacChVol, sizeof(UINT8) * 2);
	SmpsRAM.TimerAVal = MusState->TimerAVal;
	SmpsRAM.TimerBVal = MusState->TimerBVal;
	SmpsRAM.TimingMode = MusState->TimingMode;
	SmpsRAM.LockTimingMode = MusState->LockTimingMode;
	SmpsRAM.MusicPaused = MusState->MusicPaused;
	SmpsRAM.SpcFM3Mode = MusState->SpcFM3Mode;
	SmpsRAM.NoiseDrmVol = MusState->NoiseDrmVol;
	SmpsRAM.TempoCntr = MusState->TempoCntr;
	SmpsRAM.TempoInit = MusState->TempoInit;
	memcpy(SmpsRAM.FM3Freqs_Mus, MusState->FM3Freqs_Mus, sizeof(UINT16) * 4);
	memcpy(SmpsRAM.MusicTrks, MusState->MusicTrks, sizeof(TRK_RAM) * MUS_TRKCNT);
	
#ifdef FREE_SAVE_ON_RESTORE
	FreeSMPSFile(MusState->MusSet);
	FreeSMPSFileRef_Zero(&MusState->MusSet);
	MusState->InUse = 0x00;
#endif
	
	Extra_SongStart(1);
	
	if (SmpsRAM.TimingMode == 0x00)
		ym2612_timer_mask(0x00);	// no YM2612 Timer
	else if (SmpsRAM.TimingMode == 0x20)
		ym2612_timer_mask(0x01);	// YM2612 Timer A
	else if (SmpsRAM.TimingMode == 0x40)
		ym2612_timer_mask(0x02);	// YM2612 Timer B
	else //if (SmpsRAM.TimingMode == 0x80)
		ym2612_timer_mask(0x03);	// YM2612 Timer A and B
	ResetYMTimerA();
	ResetYMTimerB();
	if (SmpsRAM.MusSet != NULL)
	{
		SetDACDriver((DAC_CFG*)&SmpsRAM.MusSet->Cfg->DACDrv);
#ifndef DISABLE_NECPCM
		SetNecPCMDriver((DAC_CFG*)&SmpsRAM.MusSet->Cfg->DACDrv);
#endif
		DAC_ResetOverride();
	}
	
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
	{
		TempTrk = &SmpsRAM.MusicTrks[CurTrk];
		if (TempTrk->SmpsSet != NULL)
			TempTrk->SmpsSet->UsageCounter ++;
		TempTrk->PlaybkFlags |= PBKFLG_ATREST;
		RestoreBGMChannel(TempTrk);
	}
	
	return;
}

void SetDACState(UINT8 DacOn)
{
	SmpsRAM.DacState = DacOn;
	WriteFMI(0x2B, DacOn);
	
	return;
}

UINT8 SmpsIsRunning(void)
{
	UINT8 CurTrk;
	UINT8 IsRunning;
	
	SmpsRAM.TrkMode = TRKMODE_MUSIC;
	IsRunning = 0x00;
	for (CurTrk = 0; CurTrk < MUS_TRKCNT; CurTrk ++)
		IsRunning |= SmpsRAM.MusicTrks[CurTrk].PlaybkFlags;
	
	return IsRunning & PBKFLG_ACTIVE;
}

UINT8* SmpsGetVariable(UINT8 Type)
{
	switch(Type)
	{
	case SMPSVAR_COMMUNICATION:
		return &SmpsRAM.CommData;
	case SMPSVAR_CONDIT_JUMP:
		return &SmpsRAM.CondJmpVal;
	case SMPSVAR_RESTORE_REQ:
		return &SmpsRAM.LoadSaveRequest;
	case SMPSVAR_MUSSTATE_USE:
		return &MusicSaveState.InUse;
	}
	return NULL;
}
