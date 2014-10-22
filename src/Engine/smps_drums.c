// SMPS Drum Handler
// -----------------
// Written by Valley Bell, 2014

#include <memory.h>
#include <stdio.h>

#include "stdtype.h"
#include "../chips/mamedef.h"	// for INLINE
#include "smps_structs.h"
#include "smps_structs_int.h"
#include "smps.h"
#include "smps_int.h"
#include "../Sound.h"
#include "dac.h"

#define WriteFMI(Reg, Data)		ym2612_fm_write(0x00, 0x00, Reg, Data)
#define WriteFMII(Reg, Data)	ym2612_fm_write(0x00, 0x01, Reg, Data)
#define WritePSG(Data)			sn76496_psg_write(0x00, Data)


extern SND_RAM SmpsRAM;
extern UINT8 DebugMsgs;


INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteLE16(UINT8* Data, UINT16 Value);
INLINE UINT16 ReadRawPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_SET* SmpsSet);
INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_SET* SmpsSet);

//void PlayDrumNote(TRK_RAM* Trk, UINT8 Note);
static void DoDrum(TRK_RAM* Trk, const DRUM_DATA* DrumData);
//void PlayPS4DrumNote(TRK_RAM* Trk, UINT8 Note)


INLINE UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

INLINE UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

INLINE void WriteLE16(UINT8* Data, UINT16 Value)
{
	Data[0x00] = (Value & 0x00FF) >> 0;
	Data[0x01] = (Value & 0xFF00) >> 8;
	return;
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


void PlayDrumNote(TRK_RAM* Trk, UINT8 Note)
{
	const DRUM_LIB* DrumLib = &Trk->SmpsSet->Cfg->DrumLib;
	
	if (Note < Trk->SmpsSet->Cfg->NoteBase)
		return;
	
	Note -= Trk->SmpsSet->Cfg->NoteBase;
	if (DrumLib->Mode == DRMMODE_NORMAL)
	{
		if (Note < DrumLib->DrumCount)
			DoDrum(Trk, &DrumLib->DrumData[Note]);
	}
	else
	{
		UINT8 TempNote;
		
		TempNote = (Note & DrumLib->Mask1);// >> DrumLib->Shift1;
		if (TempNote && TempNote < DrumLib->DrumCount)
			DoDrum(Trk, &DrumLib->DrumData[TempNote]);
		
		TempNote = (Note & DrumLib->Mask2);// >> DrumLib->Shift2;
		if (TempNote && TempNote < DrumLib->DrumCount)
			DoDrum(Trk, &DrumLib->DrumData[TempNote]);
	}
	
	return;
}

static void DoDrum(TRK_RAM* Trk, const DRUM_DATA* DrumData)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	TRK_RAM* DrumTrk;
	DRUM_TRK_RAM* DrumTrk2Op;
	const DRUM_TRK_LIB* DTrkLib;
	SMPS_SET* DTrkSet;
	const UINT8* DTrkData;
	UINT16 DrumOfs;
	
	switch(DrumData->Type)
	{
	case DRMTYPE_DAC:
		if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
			return;
		
		if (DrumData->PitchOvr)
			DAC_SetRateOverride(DrumData->DrumID, DrumData->PitchOvr);
		if (! DrumData->ChnMask)
		{
			UINT8 RetVal;
			
			RetVal = DAC_Play(Trk->ChannelMask & 0x01, DrumData->DrumID);
			if ((RetVal & 0xF0) == 0x10 && (DebugMsgs & 0x01))
				printf("Warning: Unmapped DAC drum %02X at %04X!\n", DrumData->DrumID, Trk->Pos);
		}
		else
		{
			UINT8 CurChn;
			
			for (CurChn = 0; CurChn < 8; CurChn ++)
			{
				if (DrumData->ChnMask & (1 << CurChn))
 					DAC_Play(CurChn, DrumData->DrumID);
			}
		}
		break;
	case DRMTYPE_FM:
	case DRMTYPE_FMDAC:
		DrumTrk = &SmpsRAM.MusicTrks[TRACK_MUS_FM6];
		if (DrumTrk->PlaybkFlags & PBKFLG_OVERRIDDEN)
			return;
		// I need to use DrumTrk here, because NoteOff ignores the Drum channel
		DrumTrk->ChannelMask = Trk->ChannelMask & 0x0F;
		DoNoteOff(DrumTrk);
		FreeSMPSFile(DrumTrk->SmpsSet);
		
		DTrkLib = &SmpsCfg->FMDrums;
		if (DrumData->DrumID >= DTrkLib->DrumCount)
			return;
		
		// Initialize configuration structures
		DTrkSet = &SmpsRAM.DrumSet[0x00];	// 0x00 - FM drums
		*DTrkSet = *Trk->SmpsSet;
		DrumOfs = DTrkLib->DrumList[DrumData->DrumID] - DTrkLib->DrumBase;
		DTrkData = &DTrkLib->File.Data[DrumOfs];
		DTrkSet->SeqBase = DTrkLib->DrumBase;
		DTrkSet->Seq = DTrkLib->File;
		DTrkSet->UsageCounter = 0xFF;	// must never reach 0, since we're just reusing data.
		
		memset(DrumTrk, 0x00, sizeof(TRK_RAM));
		DrumTrk->SmpsSet = DTrkSet;
		DrumTrk->PlaybkFlags = PBKFLG_ACTIVE;
		DrumTrk->ChannelMask = Trk->ChannelMask & 0x0F;	// make it FM6 or FM3
		DrumTrk->TickMult = 1;
		DrumTrk->Pos = ReadPtr(&DTrkData[0x00], DTrkSet);
		DrumTrk->Transpose = DTrkData[0x02] + Trk->Transpose;
		DrumTrk->Volume = DTrkData[0x03] + Trk->Volume;
		DrumTrk->ModEnv = DTrkData[0x04];
		DrumTrk->Instrument = DTrkData[0x05];
		//FinishTrkInit:
		DrumTrk->StackPtr = TRK_STACK_SIZE;
		DrumTrk->PanAFMS = 0xC0;
		DrumTrk->RemTicks = 0x01;
		
		if (DrumData->Type == DRMTYPE_FM)
		{
			if (DrumTrk->Instrument < DTrkLib->InsLib.InsCount)
				SendFMIns(DrumTrk, DTrkLib->InsLib.InsPtrs[DrumTrk->Instrument]);
			
			// Note: This is always called, even on SMPS drivers that play drums on another channel than FM3.
			// Also, MegaMan Wily Wars, song 07 (MM1 Dr Wily Stage 1) relies on that behaviour.
			ResetSpcFM3Mode();
		}
		else if (DrumData->Type == DRMTYPE_FMDAC)
		{
			DAC_SetRate(Trk->ChannelMask & 0x01, DTrkData[0x06], 0x00);
			WriteFMII(0xB6, DTrkData[0x07]);
			DAC_Play(Trk->ChannelMask & 0x01, DrumTrk->Instrument - 0x01);
		}
		
		if (DrumTrk->Pos >= DTrkSet->Seq.Len)
			DrumTrk->PlaybkFlags &= ~PBKFLG_ACTIVE;
		break;
	case DRMTYPE_PSG:
		DrumTrk = &SmpsRAM.MusicTrks[TRACK_MUS_PSG3];
		if (DrumTrk->PlaybkFlags & PBKFLG_OVERRIDDEN)
			return;
		if (DrumTrk->ChannelMask)
			DoNoteOff(DrumTrk);
		FreeSMPSFile(DrumTrk->SmpsSet);
		
		DTrkLib = &SmpsCfg->PSGDrums;
		if (DrumData->DrumID >= DTrkLib->DrumCount)
			return;
		
		// Initialize configuration structures
		DTrkSet = &SmpsRAM.DrumSet[0x01];	// 0x01 - PSG drums
		*DTrkSet = *Trk->SmpsSet;
		DrumOfs = DTrkLib->DrumList[DrumData->DrumID] - DTrkLib->DrumBase;
		DTrkData = &DTrkLib->File.Data[DrumOfs];
		DTrkSet->SeqBase = DTrkLib->DrumBase;
		DTrkSet->Seq = DTrkLib->File;
		DTrkSet->UsageCounter = 0xFF;	// must never reach 0, since we're just reusing data.
		
		memset(DrumTrk, 0x00, sizeof(TRK_RAM));
		DrumTrk->SmpsSet = DTrkSet;
		DrumTrk->PlaybkFlags = PBKFLG_ACTIVE;
		DrumTrk->ChannelMask = 0xC0;
		DrumTrk->TickMult = 1;
		DrumTrk->Pos = ReadPtr(&DTrkData[0x00], DTrkSet);;
		DrumTrk->Transpose = DTrkData[0x02];
		DrumTrk->Volume = DTrkData[0x03];
		DrumTrk->ModEnv = DTrkData[0x04];
		DrumTrk->Instrument = DTrkData[0x05];
		//FinishTrkInit:
		DrumTrk->StackPtr = TRK_STACK_SIZE;
		DrumTrk->PanAFMS = 0xC0;
		DrumTrk->RemTicks = 0x01;
		
		if (DrumTrk->Pos >= DTrkSet->Seq.Len)
			DrumTrk->PlaybkFlags &= ~PBKFLG_ACTIVE;
		break;
	case DRMTYPE_FM2OP:
		if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
			return;
		
		if (DrumData->ChnMask >= 0x02)
			return;
		DrumTrk2Op = &SmpsRAM.MusDrmTrks[DrumData->ChnMask];
		DrumTrk2Op->PlaybkFlags &= 0x01;	// mask all bits but the Channel Select out
		Do2OpNote();						// refresh Note State
		
		DTrkLib = &SmpsCfg->FMDrums;
		if (DrumData->DrumID >= DTrkLib->DrumCount)
			return;
		
		if (! (SmpsRAM.SpcFM3Mode & 0x40))
		{
			SmpsRAM.SpcFM3Mode |= 0x40;
			WriteFMI(0x27, SmpsRAM.SpcFM3Mode);
			Trk->Volume = 0x00;
		}
		
		// Initialize configuration structures
		DrumOfs = DTrkLib->DrumList[DrumData->DrumID] - DTrkLib->DrumBase;
		DTrkData = &DTrkLib->File.Data[DrumOfs];
		
		memset(DrumTrk2Op, 0x00, sizeof(DRUM_TRK_RAM));
		DrumTrk2Op->Trk = Trk;
		DrumTrk2Op->PlaybkFlags = DTrkData[0x00];
		DrumTrk2Op->Freq1MSB = DTrkData[0x03];
		DrumTrk2Op->Freq1LSB = DTrkData[0x04];
		DrumTrk2Op->Freq2MSB = DTrkData[0x05];
		DrumTrk2Op->Freq2LSB = DTrkData[0x06];
		DrumTrk2Op->Freq1Inc = DTrkData[0x07];
		DrumTrk2Op->Freq2Inc = DTrkData[0x08];
		DrumTrk2Op->RemTicks = DTrkData[0x09];
		
		DrumOfs = ReadLE16(&DTrkData[0x01]) - DTrkLib->DrumBase;
		if (DrumOfs < DTrkLib->File.Len)
			SendFMIns(Trk, &DTrkLib->File.Data[DrumOfs]);
		Do2OpNote();
		break;
	}
}

static const UINT32 PS4_Rates[0x1D] =
{	0x16A, 0x152, 0x13B, 0x125, 0x110, 0x0FC, 0x0EA, 0x0D8,
	0x0C6, 0x0B5, 0x0A5, 0x096, 0x088, 0x07B, 0x06F, 0x064,
	0x059, 0x04F, 0x046, 0x03D, 0x035, 0x02D, 0x025, 0x01F,
	0x018, 0x012, 0x00D, 0x007, 0x002};

void PlayPS4DrumNote(TRK_RAM* Trk, UINT8 Note)
{
	if (! Trk->PS4_DacMode)	// check DAC mode
		return;
	
	// special Phantasy Star IV mode
	Note -= Trk->SmpsSet->Cfg->NoteBase;
	if (Trk->PS4_AltTrkMode != 0x01)
	{
		if (! Note)	// DAC note here
		{
			DAC_Stop(0x00);
			Trk->PlaybkFlags |= PBKFLG_ATREST;
		}
		else
		{
			if (! Trk->PS4_DacMode)
				return;	// DAC disabled - return
			
			if (Trk->PS4_DacMode > 0 && Note > 0x1D)
			{
				// simulate the behaviour of the DAC driver
				DAC_Stop(0x00);
				return;
			}
			
			//DAC_SetFrequency(0x00, PS4_Freqs[Note - 0x01], 0x00);
			if (Trk->PS4_DacMode > 0)
				DAC_SetRate(0x00, PS4_Rates[Note - 0x01], 0x00);
			else if (Trk->PS4_DacMode < 0)
				DAC_SetRate(0x00, 0x20, 0x00);	// if Volume Control is enabled, the playback rate is constant
			
			if (! (Trk->PlaybkFlags & PBKFLG_HOLD))	// not in the driver, but seems to be intended
			{
				if (Trk->DAC.Unused)
					DAC_Play(0x00, Trk->DAC.Unused - 0x01);
				else
					DAC_Stop(0x00);
			}
		}
	}
	else
	{
		if (! Note)
		{
			Trk->PlaybkFlags |= PBKFLG_ATREST;
		}
		else
		{
			if (! Trk->PS4_DacMode)
				return;	// DAC disabled - return
			DAC_Play(0x00, Note - 0x01);
		}
	}
	
	return;
}

void PlayPSGDrumNote(TRK_RAM* Trk, UINT8 Note)
{
	const PSG_DRUM_LIB* DrumLib = &Trk->SmpsSet->Cfg->PSGDrumLib;
	const PSG_DRUM_DATA* TempDrum;
	UINT8 TempVol;
	
	if (Note < Trk->SmpsSet->Cfg->NoteBase)
		return;
	if (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN)
		return;
	
	Note -= Trk->SmpsSet->Cfg->NoteBase;
	if (Note >= DrumLib->DrumCount)
	{
		Trk->PlaybkFlags |= PBKFLG_ATREST;
		return;
	}
	TempDrum = &DrumLib->DrumData[Note];
	if (! TempDrum->NoiseMode)
	{
		if (TempDrum->NoiseMode && (DebugMsgs & 0x01))
			printf("Warning: Unmapped PSG drum %02X at %04X!\n", 0x80 | Note, Trk->Pos);
		Trk->PlaybkFlags |= PBKFLG_ATREST;
		DoNoteOff(Trk);
		Trk->PlaybkFlags &= ~PBKFLG_SPCMODE;
		return;
	}
	
	Trk->PlaybkFlags &= ~PBKFLG_SPCMODE;
	// Note: Implementation based on Sonic 2 Master System
	Trk->Instrument = TempDrum->VolEnv;
	Trk->Volume = SmpsRAM.NoiseDrmVol + TempDrum->Volume;
	WritePSG(TempDrum->NoiseMode);
	
	if (TempDrum->Ch3Freq != (UINT16)-1)
	{
		Trk->PlaybkFlags |= PBKFLG_SPCMODE;
		// based on Space Harrier II (MegaDrive)
		Trk->Frequency = TempDrum->Ch3Freq;
		Trk->Detune = TempDrum->Ch3Slide;
		
		TempVol = SmpsRAM.NoiseDrmVol + TempDrum->Ch3Vol;
		if (TempVol > 0x0F)
			TempVol = 0x0F;
		WritePSG(0xD0 | TempVol);
	}
	
	return;
}
