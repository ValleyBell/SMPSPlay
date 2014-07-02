// SMPS Drum Handler
// -----------------
// Written by Valley Bell, 2014

#include <memory.h>

#include "stdtype.h"
#include "../chips/mamedef.h"	// for INLINE
#include "smps_structs.h"
#include "smps_structs_int.h"
#include "smps.h"
#include "smps_int.h"
//#include "../Sound.h"
#include "dac.h"

//#define WriteFMI(Reg, Data)		ym2612_fm_write(0x00, 0x00, Reg, Data)


extern SND_RAM SmpsRAM;


INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteLE16(UINT8* Data, UINT16 Value);
INLINE UINT16 ReadRawPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_CFG* SmpsCfg);

//void PlayDrumNote(TRK_RAM* Trk, UINT8 Note);
static void DoDrum(TRK_RAM* Trk, DRUM_DATA* DrumData);
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

INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg)
{
	return ReadRawPtr(Data, SmpsCfg) - SmpsCfg->SeqBase;
}


void PlayDrumNote(TRK_RAM* Trk, UINT8 Note)
{
	DRUM_LIB* DrumLib = &Trk->SmpsCfg->DrumLib;
	
	if (Note < 0x80)
		return;
	
	Note &= 0x7F;
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

static void DoDrum(TRK_RAM* Trk, DRUM_DATA* DrumData)
{
	TRK_RAM* DrumTrk;
	const DRUM_TRK_LIB* DTrkLib;
	SMPS_CFG* DTrkCfg;
	const UINT8* DTrkData;
	UINT16 DrumOfs;
	
	switch(DrumData->Type)
	{
	case DRMTYPE_DAC:
		if (DrumData->PitchOvr)
			DAC_SetRateOverride(DrumData->DrumID, DrumData->PitchOvr);
		if (! DrumData->ChnMask)
		{
			DAC_Play(Trk->ChannelMask & 0x01, DrumData->DrumID);
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
		DrumTrk = &SmpsRAM.MusicTrks[TRACK_MUS_FM6];
		if (DrumTrk->PlaybkFlags & PBKFLG_OVERRIDDEN)
			return;
		// I need to use DrumTrk here, because NoteOff ignores the Drum channel
		DrumTrk->ChannelMask = Trk->ChannelMask & 0x0F;
		DoNoteOff(DrumTrk);
		
		DTrkLib = &Trk->SmpsCfg->FMDrums;
		if (DrumData->DrumID >= DTrkLib->DrumCount)
			return;
		
		// Initialize configuration structures
		DTrkCfg = &SmpsRAM.DrumCfg[0x00];	// 0x00 - FM drums
		*DTrkCfg = *Trk->SmpsCfg;
		DrumOfs = DTrkLib->DrumList[DrumData->DrumID] - DTrkLib->DrumBase;
		DTrkData = &DTrkLib->Data[DrumOfs];
		DTrkCfg->SeqBase = DTrkLib->DrumBase;
		DTrkCfg->SeqLength = DTrkLib->DataLen;
		DTrkCfg->SeqData = DTrkLib->Data;
		
		memset(DrumTrk, 0x00, sizeof(TRK_RAM));
		DrumTrk->SmpsCfg = DTrkCfg;
		DrumTrk->PlaybkFlags = PBKFLG_ACTIVE;
		DrumTrk->ChannelMask = Trk->ChannelMask & 0x0F;	// make it FM6 or FM3
		DrumTrk->TickMult = 1;
		DrumTrk->Pos = ReadPtr(&DTrkData[0x00], DTrkCfg);
		DrumTrk->Transpose = DTrkData[0x02] + Trk->Transpose;
		DrumTrk->Volume = DTrkData[0x03] + Trk->Volume;
		DrumTrk->ModEnv = DTrkData[0x04];
		DrumTrk->Instrument = DTrkData[0x05];
		//FinishTrkInit:
		DrumTrk->StackPtr = TRK_STACK_SIZE;
		DrumTrk->PanAFMS = 0xC0;
		DrumTrk->Timeout = 0x01;
		
		if (DrumTrk->Instrument < DTrkLib->InsLib.InsCount)
			SendFMIns(DrumTrk, DTrkLib->InsLib.InsPtrs[DrumTrk->Instrument]);
		
		// Note: This is always called, even on SMPS drivers that play drums on another channel than FM3.
		// Also, MegaMan Wily Wars, song 07 (MM1 Dr Wily Stage 1) relies on that behaviour.
		ResetSpcFM3Mode();
		
		if (DrumTrk->Pos >= DTrkCfg->SeqLength)
			DrumTrk->PlaybkFlags &= ~PBKFLG_ACTIVE;
		break;
	case DRMTYPE_PSG:
		DrumTrk = &SmpsRAM.MusicTrks[TRACK_MUS_PSG3];
		if (DrumTrk->PlaybkFlags & PBKFLG_OVERRIDDEN)
			return;
		DoNoteOff(Trk);
		
		DTrkLib = &Trk->SmpsCfg->PSGDrums;
		if (DrumData->DrumID >= DTrkLib->DrumCount)
			return;
		
		// Initialize configuration structures
		DTrkCfg = &SmpsRAM.DrumCfg[0x01];	// 0x01 - PSG drums
		*DTrkCfg = *Trk->SmpsCfg;
		DrumOfs = DTrkLib->DrumList[DrumData->DrumID] - DTrkLib->DrumBase;
		DTrkData = &DTrkLib->Data[DrumOfs];
		DTrkCfg->SeqBase = DTrkLib->DrumBase;
		DTrkCfg->SeqLength = DTrkLib->DataLen;
		DTrkCfg->SeqData = DTrkLib->Data;
		
		memset(DrumTrk, 0x00, sizeof(TRK_RAM));
		DrumTrk->SmpsCfg = DTrkCfg;
		DrumTrk->PlaybkFlags = PBKFLG_ACTIVE;
		DrumTrk->ChannelMask = 0xC0;
		DrumTrk->TickMult = 1;
		DrumTrk->Pos = ReadPtr(&DTrkData[0x00], DTrkCfg);;
		DrumTrk->Transpose = DTrkData[0x02];
		DrumTrk->Volume = DTrkData[0x03];
		DrumTrk->ModEnv = DTrkData[0x04];
		DrumTrk->Instrument = DTrkData[0x05];
		//FinishTrkInit:
		DrumTrk->StackPtr = TRK_STACK_SIZE;
		DrumTrk->PanAFMS = 0xC0;
		DrumTrk->Timeout = 0x01;
		
		if (DrumTrk->Pos >= DTrkCfg->SeqLength)
			DrumTrk->PlaybkFlags &= ~PBKFLG_ACTIVE;
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
	Note &= 0x7F;
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
