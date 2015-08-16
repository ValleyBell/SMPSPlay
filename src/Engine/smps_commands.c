// SMPS Command Handler
// --------------------
// Written by Valley Bell, 2014-2015

#include <stdio.h>
#include <stdlib.h>	// for rand()
#include <stddef.h>

#include <common_def.h>
#include "smps_structs.h"
#include "smps_structs_int.h"
#include "smps_commands.h"
#include "smps.h"
#include "smps_int.h"
#include "../Sound.h"
#include "dac.h"

#define WriteFMI(Reg, Data)		ym2612_fm_write(0x00, 0x00, Reg, Data)
#define WriteFMII(Reg, Data)	ym2612_fm_write(0x00, 0x01, Reg, Data)
#define WritePSG(Data)			sn76496_psg_write(0x00, Data)


#ifndef DISABLE_DEBUG_MSGS
void ClearLine(void);				// from main.c
extern UINT8 DebugMsgs;
#else
#define ClearLine()
#define DebugMsgs	0
#endif


extern SND_RAM SmpsRAM;
extern SMPS_CB_SIGNAL CB_CommVar;

static const UINT8 AlgoOutMask[0x08] =
	{0x08, 0x08, 0x08, 0x08, 0x0C, 0x0E, 0x0E, 0x0F};
// Note: bit order is 40 44 48 4C (Bit 0 = 40, Bit 3 = 4C)

static const UINT8 ChouYMN_Vols[0x10] =
	{0x08, 0x0A, 0x0C, 0x10, 0x14, 0x19, 0x20, 0x28, 0x32, 0x40, 0x50, 0x65, 0x80, 0xA0, 0xCA, 0xFF};

static const UINT16 S2R_Vols[0x04] = {0x100, 0xD7, 0xB5, 0x98};	// 0 db, -1.5 db, -3.0 db, -4.5 db

static const UINT8 CoI_PSG_AtkRates[0x20] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10,
	0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18,
	0x19, 0x1C, 0x1F, 0x2F, 0x3F, 0x5F, 0x7F, 0xFF};
static const UINT8 CoI_PSG_DecRates[0x20] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x1C, 0x1F, 0x2F, 0x3F, 0x5F, 0x7F, 0xFF};
static const UINT8 CoI_PSG_SusLevels[0x10] = {
	0x00, 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70,
	0x80, 0x90, 0xA0, 0xB0, 0xC0, 0xD0, 0xE0, 0xFF};
static const UINT8 CoI_PSG_SusRates[0x20] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x1C, 0x1F, 0x2F, 0x3F, 0x5F, 0x7F, 0xFF};
static const UINT8 CoI_PSG_RelRates[0x10] = {
	0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
	0x09, 0x0B, 0x0D, 0x10, 0x1F, 0x3F, 0x7F, 0xFF};



// from smps_extra.c
void Extra_StopCheck(void);
#ifdef ENABLE_LOOP_DETECTION
void Extra_LoopEndCheck(TRK_RAM* Trk);
#else
#define Extra_LoopEndCheck(x)
#endif


INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE void WriteLE16(UINT8* Data, UINT16 Value);
INLINE UINT16 ReadRawPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_SET* SmpsSet);
INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_SET* SmpsSet);

//void cfHandler(TRK_RAM* Trk, UINT8 Command)
static void cfMetaHandler(TRK_RAM* Trk, UINT8 Command);
static void DoCoordinationFlag(TRK_RAM* Trk, const CMD_FLAGS* CFlag);
static UINT8 GetInsRegPtrs(TRK_RAM* Trk, const UINT8** RetRegPtr, const UINT8** RetInsPtr, UINT8 Register);
static void cfSetIns_PSG(TRK_RAM* Trk, UINT8 InsID);
static UINT8 cfSetInstrument(TRK_RAM* Trk, const CMD_FLAGS* CFlag, const UINT8* Params);
static UINT8 cfVolume(TRK_RAM* Trk, const CMD_FLAGS* CFlag, const UINT8* Params);
static UINT8 cfSpecialDAC(TRK_RAM* Trk, const CMD_FLAGS* CFlag);
INLINE UINT16* GetFM3FreqPtr(void);
static void print_msg(TRK_RAM* Trk, UINT8 CmdLen, const char* DescStr);


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



void cfHandler(TRK_RAM* Trk, UINT8 Command)
{
	const CMD_LIB* CmdLib = &Trk->SmpsSet->Cfg->CmdList;
	
	if (Command < CmdLib->FlagBase || Command - CmdLib->FlagBase >= CmdLib->FlagCount)
	{
		Trk->Pos ++;	// skip this command
		return;
	}
	
	DoCoordinationFlag(Trk, &CmdLib->CmdData[Command - CmdLib->FlagBase]);
	
	if (Trk->Pos >= Trk->SmpsSet->Seq.Len)
		Trk->PlaybkFlags &= ~PBKFLG_ACTIVE;
	
	return;
}

static void cfMetaHandler(TRK_RAM* Trk, UINT8 Command)
{
	const CMD_LIB* CmdLib = &Trk->SmpsSet->Cfg->CmdMetaList;
	
	if (Command < CmdLib->FlagBase || Command - CmdLib->FlagBase >= CmdLib->FlagCount)
	{
		Trk->Pos ++;	// skip this command
		return;
	}
	
	DoCoordinationFlag(Trk, &CmdLib->CmdData[Command - CmdLib->FlagBase]);
	
	return;
}

static void DoCoordinationFlag(TRK_RAM* Trk, const CMD_FLAGS* CFlag)
{
	const SMPS_CFG* SmpsCfg = Trk->SmpsSet->Cfg;
	const UINT8* Data = &Trk->SmpsSet->Seq.Data[Trk->Pos + 0x01];
	UINT8 CmdLen;
	UINT8 TempByt;
	
	CmdLen = CFlag->Len;
	switch(CFlag->Type)
	{
	// General Flags
	// -------------
	case CF_PANAFMS:
		if (! (CFlag->SubType & 0xF0))
		{
			switch(CFlag->SubType)
			{
			case CFS_PAFMS_PAN_PAOFF:	// E0 Disable Pan Anim + Set Pan
				Trk->PanAni.Type = 0x00;
				// fall through
			case CFS_PAFMS_PAN:	// E0 Set Pan
				TempByt = 0x3F;
				break;
			case CFS_PAFMS_AMS:	// EA Set AMS
				TempByt = 0xC7;
				break;
			case CFS_PAFMS_FMS:	// EB Set FMS
				TempByt = 0xF0;
				break;
			}
			Trk->PanAFMS &= TempByt;
			Trk->PanAFMS |= Data[0x00];
		}
		else
		{
			// preSMPS 68k - set Pan C/R/L
			switch(CFlag->SubType)
			{
			case CFS_PAFMS_PAN_C:	// preSMPS FD
				TempByt = 0xC0;
				break;
			case CFS_PAFMS_PAN_L:	// preSMPS FF
				TempByt = 0x80;
				break;
			case CFS_PAFMS_PAN_R:	// preSMPS FE
				TempByt = 0x40;
				break;
			}
			Trk->PanAFMS = TempByt;
		}
		WriteFMMain(Trk, 0xB4, Trk->PanAFMS);
		break;
	case CF_DETUNE:		// E1 Detune
		if (CmdLen & 0x80)
		{
			CmdLen &= 0x7F;
			// Castle Of Illusion:
			//	PSG channel: eat 2 parameters
			//	FM channel: do as intended
			if (Trk->ChannelMask & 0x80)
			{
				CmdLen ++;
				break;
			}
		}
		
		if (CFlag->SubType == CFS_DET_HOLD)
			Trk->PlaybkFlags |= PBKFLG_HOLD;
		if (CmdLen <= 2)
			Trk->Detune = (INT8)Data[0x00];
		else
			Trk->Detune = ReadBE16(&Data[0x00]);
		break;
	case CF_SET_COMM:	// E2 Set Communication Byte
		SmpsRAM.CommData = Data[0x00];
		if (CB_CommVar != NULL)
			CB_CommVar();
		break;
	case CF_VOL_QUICK:
		Data --;
		switch(CFlag->SubType)
		{
		case CFS_VQ_SET_3B:
			Trk->Volume = *Data & 0x07;
			if (! (Trk->ChannelMask & 0xF8))
				RefreshFMVolume(Trk);
			break;
		case CFS_VQ_SET_4B:
			Trk->Volume = *Data & 0x0F;
			if (! (Trk->ChannelMask & 0xF8))
				RefreshFMVolume(Trk);
			break;
		case CFS_VQ_SET_4B_WOI:
			Trk->Volume = *Data & 0x0F;
			if (! (Trk->ChannelMask & 0xF8))
			{
				Trk->Volume = 0x10 + (Trk->Volume * 0x04);
				RefreshFMVolume(Trk);
			}
			break;
		case CFS_VQ_SET_4B_WOI2:
			TempByt = *Data & 0x0F;
			Trk->Volume = Trk->CoI_VolBase + TempByt;
			if (! (Trk->ChannelMask & 0xF8))
			{
				TempByt *= 0x04;
				if (! SmpsRAM.TrkMode)
					Trk->Volume = 0x10 + Trk->CoI_VolBase + TempByt;	// Music Mode
				else
					Trk->Volume = TempByt;	// SFX/Special SFX Mode
				RefreshFMVolume(Trk);
			}
			break;
		case CFS_VQ_SET_4B_QS:
			TempByt = *Data & 0x0F;
			Trk->Volume = Trk->CoI_VolBase + TempByt;
			if (! (Trk->ChannelMask & 0xF8))
			{
				TempByt *= 0x04;
				Trk->Volume = Trk->CoI_VolBase + TempByt;
				RefreshFMVolume(Trk);
			}
			break;
		}
		break;
	case CF_VOLUME:		// E5/E6/EC Change Volume
		CmdLen = cfVolume(Trk, CFlag, &Data[0x00]);
		break;
	case CF_HOLD:		// E7 Hold Note
		switch(CFlag->SubType)
		{
		case CFS_HOLD_ON:
			Trk->PlaybkFlags |= PBKFLG_HOLD;
			break;
		case CFS_HOLD_OFF:
			Trk->PlaybkFlags &= ~PBKFLG_HOLD;
			break;
		case CFS_HOLD_LOCK:
			if (Data[0x00] == 0x01)
				Trk->PlaybkFlags |= (PBKFLG_HOLD_LOCK | PBKFLG_HOLD);
			else
				Trk->PlaybkFlags &= ~(PBKFLG_HOLD_LOCK | PBKFLG_HOLD);
			break;
		case CFS_HOLD_LOCK_NEXT:
			if (Data[0x00] == 0x01)
				Trk->PlaybkFlags |= PBKFLG_HOLD_LOCK;	// This doesn't affect the next note.
			else
				Trk->PlaybkFlags &= ~(PBKFLG_HOLD_LOCK | PBKFLG_HOLD);	// DoNoteOff() is done when processing the next note
			break;
		}
		break;
	case CF_NOTE_STOP:	// E8 Note Stop
		TempByt = Data[0x00];
		if (CFlag->SubType == CFS_NSTOP_MULT)
			TempByt *= Trk->TickMult;
		
		Trk->NStopTout = TempByt;
		Trk->NStopInit = TempByt;
		if (! (Trk->NStopRevMode & 0x70))
			Trk->NStopRevMode = 0x00;	// disable Reversed Note Stop
		break;
	case CF_TRANSPOSE:	// FB Transpose
		switch(CFlag->SubType)
		{
		case CFS_TRNSP_ADD:
			Trk->Transpose += Data[0x00];
			break;
		case CFS_TRNSP_SET:
			Trk->Transpose = (INT8)Data[0x00];
			break;
		case CFS_TRNSP_SET_S3K:
			Trk->Transpose = Data[0x00] - 0x40;
			break;
		case CFS_TRNSP_ADD_ALL:
			// The drum channel is not included.
			for (TempByt = TRACK_MUS_FM1; TempByt < MUS_TRKCNT; TempByt ++)
				SmpsRAM.MusicTrks[TempByt].Transpose += Data[0x00];
			break;
		case CFS_TRNSP_RAND:
			Trk->Transpose = rand() & 0xFF;
			break;
		case CFS_TRNSP_GADD:
			//Trk->Transpose += SmpsRAM._1C1E;
			break;
		case CFS_TRNSP_GSET:
			//Trk->Transpose += SmpsRAM._1C21;
			break;
		}
		break;
	case CF_INSTRUMENT:	// EF/F5 Set Instrument
		CmdLen = cfSetInstrument(Trk, CFlag, &Data[0x00]);
		break;
	case CF_PSG_NOISE:
		if (CFlag->SubType == CFS_PNOIS_SRES)
		{
			if (Trk->ChannelMask & PBKFLG_OVERRIDDEN)	// not exactly what they wanted, but well...
				break;
			WritePSG(0xDF);
			
			Trk->NoiseMode = Data[0x00];
			if (! Trk->NoiseMode)
			{
				Trk->PlaybkFlags &= ~PBKFLG_SPCMODE;
				WritePSG(0xFF);
			}
			else
			{
				Trk->PlaybkFlags |= PBKFLG_SPCMODE;
				WritePSG(Trk->NoiseMode);
			}
		}
		else if (CFlag->SubType == CFS_PNOIS_SET)
		{
#if 0	// Moving the track from PSG1/2 to PSG3 breaks the loop detection
			if (Trk->ChannelMask & 0x80)
			{
				if (Trk->ChannelMask < 0xC0)
				{
					if (! (SmpsRAM.MusicTrks[TRACK_MUS_PSG3].PlaybkFlags & PBKFLG_ACTIVE))
					{
						TRK_RAM* DstTrk;
						UINT8 PbkFlags;
						
						DstTrk = &SmpsRAM.MusicTrks[TRACK_MUS_PSG3];
						PbkFlags = Trk->PlaybkFlags & ~PBKFLG_OVERRIDDEN;
						PbkFlags |= DstTrk->PlaybkFlags & PBKFLG_OVERRIDDEN;
						*DstTrk = *Trk;
						DstTrk->PlaybkFlags = PbkFlags;
						DstTrk->ChannelMask = 0xC0;
						DstTrk->Timeout = 0x01;
						Trk->PlaybkFlags = 0x00;
						return;
					}
				}
			}
#endif
			// SMPS 68k always turns the channel into PSG3
			// Note: This is used by a few offical games.
			Trk->ChannelMask = 0xC0;
			Trk->PlaybkFlags |= PBKFLG_SPCMODE;
			Trk->NoiseMode = Data[0x00];
			if (! (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN))
				WritePSG(Trk->NoiseMode);
		}
		else if (CFlag->SubType == CFS_PNOIS_SET2)
		{
			Trk->PlaybkFlags |= PBKFLG_SPCMODE;
			Trk->NoiseMode = 0xE0 | Data[0x00];
			if (! (Trk->PlaybkFlags & PBKFLG_OVERRIDDEN))
				WritePSG(Trk->NoiseMode);
		}
		break;
	case CF_FM_COMMAND:
		switch(CFlag->SubType)
		{
		case CFS_FMW_CHN:
			WriteFMMain(Trk, Data[0x00], Data[0x01]);
			break;
		case CFS_FMW_FM1:
			WriteFMI(Data[0x00], Data[0x01]);
			break;
		}
		break;
	case CF_SND_CMD:
		//SmpsRAM._1C09 = Data[0x00];
		if (DebugMsgs & 0x01)
			printf("Queued Sound ID %02X on Channel %02X at %04X\n",
					Data[0x00], Trk->ChannelMask, Trk->Pos);
		break;
	case CF_MUS_PAUSE:
		// Verified with SMPS Z80 and SMPS 68k.
		if (CFlag->SubType & 0x10)
		{
			switch(CFlag->SubType)
			{
			case CFS_MUSP_GBL_ON:
				SmpsRAM.PauseMode = 0x01;
				break;
			case CFS_MUSP_GBL_OFF:
				SmpsRAM.PauseMode = 0x80;
				break;
			}
			break;
		}
		
		// CmdLen is checked for safety.
		TempByt = (CmdLen > 0x01) ? Data[0x00] : 0x01;
		if (CmdLen & 0x80)
		{
			CmdLen &= 0x7F;
			// do Golden Axe III
			if (TempByt & 0x80)
			{
				TempByt &= 0x7F;
				CmdLen ++;
				
				if (! TempByt)
				{
					// set Fading parameters
					/*// F004 = Fade Mode??
					// F006 = Fade Increment (step is processed on counter F007 overflow)
					// F005 = Remaining Fade Steps/Fading Volume (the driver is doing weird things with it)
					SmpsRAM.F004 = 0x41;
					SmpsRAM.F006 = Data[0x01];
					SmpsRAM.F005 = 0x00;*/
					break;	// return instantly
				}
				else
				{
					// set Fading parameters?
					/*SmpsRAM.F004 = 0xC0;
					SmpsRAM.F006 = Data[0x01];
					SmpsRAM.F005 = 0x30;*/
					// continue with "Pause Music"
					TempByt = 0x00;
				}
			}
		}
		if (CFlag->SubType == CFS_MUSP_COI)
		{
			//if (! SmpsRAM.1C13)
			//	TempByt = 0x00;	// enforce music resume
			if (TempByt == 0x04)
				TempByt = 0x01;	// pause music
			else if (TempByt == 0x05)
				TempByt = 0x00;	// unpause music
			else
			{
				CMD_FLAGS EndFlag;
				
				//SmpsRAM.1C13 = 0x01;
				EndFlag.Type = CF_TRK_END;
				EndFlag.SubType = CFS_TEND_STD;
				EndFlag.Len = 0x00;
				DoCoordinationFlag(Trk, &EndFlag);
				break;
			}
		}
		
		SmpsRAM.MusicPaused = TempByt;
		// Note: The usage of PBKFLG_PAUSED is actually SMPS 68k only.
		//       SMPS Z80 stops and resumes ALL tracks. But this can cause bugs if not all channels were active when pausing.
		if (TempByt)
		{
			TRK_RAM* TempTrk;
			
			// Pause Music
			for (TempByt = 0x00; TempByt < MUS_TRKCNT; TempByt ++)
			{
				TempTrk = &SmpsRAM.MusicTrks[TempByt];
				if (TempTrk->PlaybkFlags & PBKFLG_ACTIVE)
				{
					TempTrk->PlaybkFlags &= ~PBKFLG_ACTIVE;
					TempTrk->PlaybkFlags |= PBKFLG_PAUSED;
					
					if (CFlag->SubType == CFS_MUSP_68K && ! (TempTrk->ChannelMask & 0x80))
						WriteFMMain(TempTrk, 0xB4, 0x00);	// silence channel instantly
					DoNoteOff(TempTrk);
				}
			}
		}
		else
		{
			TRK_RAM* TempTrk;
			
			// Resume Music
			for (TempByt = 0x00; TempByt < MUS_TRKCNT; TempByt ++)
			{
				TempTrk = &SmpsRAM.MusicTrks[TempByt];
				if (TempTrk->PlaybkFlags & PBKFLG_PAUSED)
				{
					TempTrk->PlaybkFlags &= ~PBKFLG_PAUSED;
					TempTrk->PlaybkFlags |= PBKFLG_ACTIVE;
					
					if (CFlag->SubType == CFS_MUSP_68K && ! (TempTrk->ChannelMask & 0x80))
						WriteFMMain(TempTrk, 0xB4, TempTrk->PanAFMS);	// re-enable channel
				}
			}
		}
		break;
	case CF_COPY_MEM:
		{
			UINT8* SeqData;
			UINT16 SrcAddr;
			UINT8 ByteCnt;
			
			SrcAddr = ReadLE16(&Data[0x00]);
			ByteCnt = Data[0x02];
			Trk->Pos += CFlag->Len;
			SeqData = Trk->SmpsSet->Seq.Data;
			
			// Of course I can't emulate this, since I'm not simulating a full Z80 incl. RAM and ROM.
			// But even if you ignore this, copying data into the sequence makes absolutely no sense.
			//
			// It probably was intended to copy from Sequence to Z80 RAM, not the other way round,
			// but no game ever used that feature anyway.
#if 0
			while(ByteCnt)
			{
				SeqData[Trk->Pos] = *SrcAddr;
				SrcAddr ++;	Trk->Pos ++;
				ByteCnt --;
			}
#else
			Trk->Pos += ByteCnt;
#endif
			CmdLen = 0x00;
		}
		break;
	case CF_FADE_SPC:
		if (CFlag->SubType < 0x80)
		{
			FADE_SPC_INF* Fade = &SmpsRAM.FadeSpc;
			if (CFlag->SubType < 0x10)
			{
				if (Fade->Mode == 0x02)
				{
					if (CmdLen & 0x80)
						CmdLen = 0x01;	// no more parameters
					break;
				}
				CmdLen &= 0x7F;
			}
			switch(CFlag->SubType)
			{
			case CFS_FDSPC_FMPSG:
				Fade->Mode = 0x01;
				Fade->AddDAC = 0x00;
				Fade->AddFM = Data[0x00];
				Fade->AddPSG = Data[0x01];
				Fade->AddPWM = 0x00;
				break;
			case CFS_FDSPC_DFP:
				Fade->Mode = 0x01;
				Fade->AddDAC = Data[0x00];
				Fade->AddFM = Data[0x01];
				Fade->AddPSG = Data[0x02];
				Fade->AddPWM = 0x00;
				break;
			case CFS_FDSPC_DFPPWM:
				Fade->Mode = 0x01;
				Fade->AddDAC = Data[0x00];
				Fade->AddFM = Data[0x01];
				Fade->AddPSG = Data[0x02];
				Fade->AddPWM = Data[0x03];
				break;
			case CFS_FDSPC_PSG:
				Fade->Mode = 0x01;
				Fade->AddDAC = 0x00;
				Fade->AddFM = 0x00;
				Fade->AddPSG = Data[0x00];
				Fade->AddPWM = 0x00;
				break;
			case CFS_FDSPC_FP_TRS:
				if (! Fade->Mode)
				{
					Fade->Mode = 0x01;
					TempByt = Fade->AddFM | Fade->AddPSG;
					if (TempByt)
					{
						if (CmdLen & 0x80)
							CmdLen = 0x01;
						break;	// exit early
					}
					Fade->AddDAC = 0x00;
					Fade->AddFM = Data[0x00];
					Fade->AddPSG = Data[0x01];
					Fade->AddPWM = 0x00;
				}
				CmdLen &= 0x7F;
				break;
			}
		}
		else
		{
			switch(CFlag->SubType)
			{
			case CFS_FDSPC_STOP:
				SmpsRAM.FadeSpc.Mode = 0x80;
				break;
			case CFS_FDSPC_STOP_TRS:
				if (SmpsRAM.FadeSpc.Mode == 0x02)
					SmpsRAM.FadeSpc.Mode = 0x80;
				break;
			}
		}
		break;
	case CF_DAC_BANK:
		DAC_SetBank(CFlag->SubType & 0x0F, Data[0x00]);
		break;
	case CF_PLAY_DAC:
		{
			const DAC_CFG* DacDrv = &SmpsCfg->DACDrv;
			UINT8 DacChn;
			UINT8 DacSnd;
			
			DacChn = CFlag->SubType & 0x0F;
			switch(CmdLen)
			{
			case 0x02:
				DacSnd = Data[0x00] & 0x7F;
				break;
			case 0x03:	// Zaxxon Motherbase 2000 32X
				// EA bb dd - DAC Sound dd, Bank bb
				DAC_SetBank(DacChn, Data[0x00] & 0x7F);
				DacSnd = Data[0x01] & 0x7F;
				break;
			case 0x04:	// Mercs
				DacSnd = Data[0x00] & 0x7F;
				DAC_SetRate(DacChn, Data[0x01], 0x00);
				WriteFMII(0xB6, Data[0x02]);
				break;
			default:
				DacChn = 0xFF;
				break;
			}
			if (DacChn == 0xFF)
				break;	// handle invalid command sizes
			
			//SmpsRAM._1C3C = DacSnd;
			DAC_Play(DacChn, DacSnd - 0x01);
			if (SmpsCfg->DrumChnMode == DCHNMODE_CYMN && DacChn == 0x00)
			{
				if (DacSnd)
					SmpsRAM.MusicTrks[TRACK_MUS_DRUM].PlaybkFlags |= PBKFLG_OVERRIDDEN;
				else
					SmpsRAM.MusicTrks[TRACK_MUS_DRUM].PlaybkFlags &= ~PBKFLG_OVERRIDDEN;
			}
		}
		break;
	case CF_PLAY_PWM:
		//SmpsRAM._1C1F = Data[0x00];
		//SmpsRAM._1C20 = Data[0x01];
		{
			UINT8 VolValue;
			
			VolValue  = 1 + ((Data[0x00] & 0x0F) >> 0);
			VolValue += 1 + ((Data[0x00] & 0xF0) >> 4);
			DAC_SetVolume(0x01, VolValue * 0x10);
			DAC_Play(0x01, Data[0x01]);
		}
		break;
	// Modulation Flags
	// ----------------
	case CF_MOD_SETUP:	// F0 Modulation Setup
		Trk->CstMod.DataPtr = Trk->Pos + 0x01;
		if ((SmpsCfg->ModAlgo & 0xF0) == MODALGO_68K)
		{
			// unconditional inlined PrepareModulat()
			Trk->ModEnv |= 0x80;
			Trk->CstMod.Delay = Data[0x00];
			Trk->CstMod.Rate = Data[0x01];
			Trk->CstMod.Delta = Data[0x02];
			Trk->CstMod.RemSteps = Data[0x03] / 2;
			Trk->CstMod.Freq = 0;
		}
		else
		{
			Trk->ModEnv = 0x80;
		}
		break;
	case CF_MOD_SET:
		switch(CFlag->SubType)
		{
		case CFS_MODS_ON:	// F1/FC Modulation On
			Trk->ModEnv |= 0x80;
			break;
		case CFS_MODS_OFF:	// F4/FD Modulation Off
			Trk->ModEnv &= ~0x80;
			break;
		}
		break;
	case CF_MOD_ENV:
		switch(CFlag->SubType)
		{
		case CFS_MENV_GEN:	// F4 Set Modulation Envelope
			Trk->ModEnv = Data[0x00];
			break;
		case CFS_MENV_FMP:	// F1 Set Modulation Envelope (seperate values for PSG/FM)
			if (Trk->ChannelMask & 0x80)
				Trk->ModEnv = Data[0x00];	// PSG channel
			else
				Trk->ModEnv = Data[0x01];	// FM channel
			break;
		case CFS_MENV_GEN2:	// F1 Set Modulation Envelope (broken, Tempo 32x)
			Trk->ModEnv = Data[0x01];
			break;
		}
		break;
	case CF_FM_VOLENV:
		//if (DebugMsgs & 0x02)
		//	print_msg(Trk, CmdLen, "FM Volume Envelope");
		Trk->FMVolEnv.VolEnv = Data[0x00];
		Trk->FMVolEnv.OpMask = Data[0x01];
		break;
	case CF_LFO_MOD:
		CmdLen &= 0x7F;
		Trk->LFOMod.MaxFMS = Data[0x00];
		if (Trk->LFOMod.MaxFMS)
		{
			Trk->LFOMod.Delay = Data[0x01];
			Trk->LFOMod.DelayInit = Data[0x01];
			Trk->LFOMod.Timeout = Data[0x02];
			Trk->LFOMod.ToutInit = Data[0x02];
			CmdLen += 0x02;
		}
		break;
	case CF_ADSR:
		switch(CFlag->SubType)
		{
		case CFS_ADSR_SETUP:
			Trk->Instrument = 0x80;	// enable ADSR
			Trk->ADSR.AtkRate = Data[0x00];
			Trk->ADSR.DecRate = Data[0x01];
			Trk->ADSR.DecLvl  = Data[0x02];
			Trk->ADSR.SusRate = Data[0x03];
			Trk->ADSR.RelRate = Data[0x04];
			break;
		case CFS_ADSR_MODE:
			Trk->ADSR.Mode = Data[0x00] ? ADSRM_REPT_AD : 0x00;
			break;
		}
		break;
	// Effect Flags
	// ------------
	case CF_PAN_ANIM:	// E4 Pan Animation
		// SMPS 68k: 1 or 5 parameters
		// SMPS Z80: always 5 parameters
		if (CmdLen & 0x80)
		{
			CmdLen &= 0x7F;
			Trk->PanAni.Type = Data[0x00];
			if (Trk->PanAni.Type)
			{
				//if (DebugMsgs & 0x02)
				//	printf("Chn %02X %s Pan Ani: %02X %02X %02X %02X %02X\n",
				//			Trk->ChannelMask, "68k", Data[0x00], Data[0x01], Data[0x02], Data[0x03], Data[0x04]);
				Trk->PanAni.Anim = Data[0x01] - 1;	// Z80 is 0-based, 68k is 1-based
				Trk->PanAni.AniIdx = Data[0x02];
				Trk->PanAni.AniLen = Data[0x03] + 1;	// SMPS 68k plays [0..n] instead of [0..n-1]
				Trk->PanAni.ToutInit = Data[0x04];
				// SMPS 68k actually sets PanAni.Timeout to Data[0x04], but 1 has the
				// correct effect for the SMPS Z80-based PanAnimation routine.
				Trk->PanAni.Timeout = 0x01;
				CmdLen += 0x04;
			}
		}
		else
		{
			Trk->PanAni.Type = Data[0x00];
			//if (Trk->PanAni.Type && (DebugMsgs & 0x02))
			//	printf("Chn %02X %s Pan Ani: %02X %02X %02X %02X %02X\n",
			//			Trk->ChannelMask, "Z80", Data[0x00], Data[0x01], Data[0x02], Data[0x03], Data[0x04]);
			Trk->PanAni.Anim = Data[0x01];
			Trk->PanAni.AniIdx = Data[0x02];
			Trk->PanAni.AniLen = Data[0x03];
			Trk->PanAni.ToutInit = Data[0x04];
			Trk->PanAni.Timeout = 0x01;
		}
		break;
	case CF_SET_LFO:	// E9 Set LFO Data
		if (CFlag->SubType == CFS_LFO_AMSEN)
		{
			const UINT8* OpPtr;
			const UINT8* InsPtr;
			UINT8 CurOp;
			UINT8 OpMask;
			
			CurOp = GetInsRegPtrs(Trk, &OpPtr, &InsPtr, 0x60);	// get data for register 60/8/4/C
			if (! CurOp)
			{
				OpMask = Data[0x00];
				for (CurOp = 0x00; CurOp < 0x04; CurOp ++, OpMask <<= 1)
				{
					if (OpMask & 0x80)
						WriteFMMain(Trk, OpPtr[CurOp], InsPtr[CurOp] | 0x80);
				}
			}
		}
		
		WriteFMI(0x22, Data[0x00]);
		
		Trk->PanAFMS &= 0xC0;
		Trk->PanAFMS |= Data[0x01];
		WriteFMMain(Trk, 0xB4, Trk->PanAFMS);
		break;
	case CF_SET_LFO_SPD:	// E9 Set LFO Speed
		WriteFMI(0x22, Data[0x00]);
		break;
	case CF_PITCH_SLIDE:	// FC Pitch Slide
		if (Data[0x00] == 0x01)
		{
			Trk->PlaybkFlags |= PBKFLG_PITCHSLIDE;
		}
		else
		{
			Trk->PlaybkFlags &= ~(PBKFLG_PITCHSLIDE | PBKFLG_HOLD);
			Trk->Detune = 0x00;
		}
		break;
	case CF_RAW_FREQ:		// FD Raw Frequency Mode
		if (Data[0x00] == 0x01)
		{
			if (DebugMsgs & 0x02)
				print_msg(Trk, CmdLen, "Raw Frequency Mode");
			Trk->PlaybkFlags |= PBKFLG_RAWFREQ;
		}
		else
			Trk->PlaybkFlags &= ~PBKFLG_RAWFREQ;
		break;
	case CF_SPC_FM3:		// FE FM3 Special Mode
		if (Trk->SmpsSet->Seq.Data != SmpsCfg->FMDrums.File.Data && (DebugMsgs & 0x02))
			print_msg(Trk, CmdLen, "Special FM3 mode");
		if (Trk->ChannelMask != 0x02)
			break;
		
		if (CmdLen == 0x05)
		{
			UINT16* FM3FrqRAM;
			UINT8 CurOp;
			
			// TODO: This is probably different on other drivers.
			Trk->PlaybkFlags |= PBKFLG_SPCMODE;
			FM3FrqRAM = GetFM3FreqPtr();
			
			for (CurOp = 0x00; CurOp < 0x04; CurOp ++)
			{
				if (Data[CurOp] < SmpsCfg->FM3FreqCnt)
					FM3FrqRAM[CurOp] = SmpsCfg->FM3Freqs[Data[CurOp]];
				else
					FM3FrqRAM[CurOp] = 0x0000;
			}
			SmpsRAM.SpcFM3Mode = 0x4F;
			WriteFMI(0x27, SmpsRAM.SpcFM3Mode);
		}
		break;
	case CF_SSG_EG:			// FF 06 SSG-EG
		if (Trk->SmpsSet->Seq.Data != SmpsCfg->FMDrums.File.Data && (DebugMsgs & 0x02))
			print_msg(Trk, CmdLen, "SSG-EG Enable");
		
		if (CFlag->SubType == CFS_SEG_FULLATK)
			Trk->SSGEG.Type = 0x81;
		else
			Trk->SSGEG.Type = 0x80;
		Trk->SSGEG.DataPtr = Trk->Pos + 0x01;
		SendSSGEG(Trk, Data, Trk->SSGEG.Type & 0x01);
		break;
	// Tempo Flags
	// -----------
	case CF_TEMPO:			// EA/FF 00 Set Tempo
		switch(CFlag->SubType)
		{
		case CFS_TEMPO_SET:
			SmpsRAM.TempoInit = Data[0x00];
			SmpsRAM.TempoCntr = Data[0x00];
			break;
		case CFS_TEMPO_ADD:
			SmpsRAM.TempoInit += Data[0x00];
			//SmpsRAM.TempoCntr = SmpsRAM.TempoInit;
			SmpsRAM.TempoCntr += Data[0x00];
			break;
		}
		break;
	case CF_TICK_MULT:		// FA Set Tick Multiplier
		switch(CFlag->SubType)
		{
		case CFS_TMULT_CUR:
			Trk->TickMult = Data[0x00];
			break;
		case CFS_TMULT_ALL:
			for (TempByt = 0x00; TempByt < MUS_TRKCNT; TempByt ++)
				SmpsRAM.MusicTrks[TempByt].TickMult = Data[0x00];
			break;
		}
		break;
	case CF_TIMING:			// EA/EB Set YM2612 Timer
		TempByt = 0x00;
		switch(CFlag->SubType)
		{
		case CFS_TIME_SET:
		case CFS_TIME_SET_BE:
			if (CmdLen == 0x02)
			{
				SmpsRAM.TimerBVal = Data[0x00];
				TempByt = 0x02;
			}
			else if (CmdLen == 0x03)
			{
				if (CFlag->SubType == CFS_TIME_SET_BE)
					SmpsRAM.TimerAVal = ReadBE16(&Data[0x00]);
				else
					SmpsRAM.TimerAVal = ReadLE16(&Data[0x00]);
				TempByt = 0x01;
			}
			else if (CmdLen == 0x04)
			{
				if (CFlag->SubType == CFS_TIME_SET_BE)
					SmpsRAM.TimerAVal = ReadBE16(&Data[0x00]);
				else
					SmpsRAM.TimerAVal = ReadLE16(&Data[0x00]);
				SmpsRAM.TimerBVal = Data[0x02];
				TempByt = 0x03;
			}
			break;
		case CFS_TIME_ADD:
		case CFS_TIME_ADD_BE:
			if (CmdLen == 0x02)
			{
				SmpsRAM.TimerBVal += Data[0x00];
				TempByt = 0x02;
			}
			else if (CmdLen == 0x03)
			{
				if (CFlag->SubType == CFS_TIME_SET_BE)
					SmpsRAM.TimerAVal += ReadBE16(&Data[0x00]);
				else
					SmpsRAM.TimerAVal += ReadLE16(&Data[0x00]);
				TempByt = 0x01;
			}
			else if (CmdLen == 0x04)
			{
				if (CFlag->SubType == CFS_TIME_SET_BE)
					SmpsRAM.TimerAVal += ReadBE16(&Data[0x00]);
				else
					SmpsRAM.TimerAVal += ReadLE16(&Data[0x00]);
				SmpsRAM.TimerBVal += Data[0x02];
				TempByt = 0x03;
			}
			break;
		case CFS_TIME_ADD_0A:
			SmpsRAM.TimerAVal += Data[0x00];
			TempByt = 0x01;
			break;
		case CFS_TIME_SPC:
			if (CmdLen == 0x02)
			{
				UINT8 Mode;
				UINT8 Tempo;
				
				Mode = Data[0x00] & 0xC0;
				Tempo = Data[0x00] & 0x3F;
				if (Mode == 0x40)
					SmpsRAM.TimerBVal -= Tempo;
				else if (Mode == 0x80)
					SmpsRAM.TimerBVal += Tempo;
				else if (Mode == 0xC0)
					SmpsRAM.TimerBVal = Tempo;
				// Mode 00 does nothing
				TempByt = 0x02;
			}
			else //if (CmdLen == 0x83)
			{
				UINT8 Mode;
				UINT16 Tempo;
				
				CmdLen &= 0x7F;
				Tempo = ReadLE16(&Data[0x00]) & 0x0FFF;
				Mode = Data[0x01] & 0xF0;
				TempByt = 0x01;
				if (Mode & 0x30)	// Bits 4/5 set - set Tempo + Timing Mode
				{
					Mode &= 0x30;
					if (Mode == 0x10)
					{
						SmpsRAM.TimingMode = 0x00;
						SmpsRAM.TimerAVal = Tempo;
					}
					else if (Mode == 0x20)
					{
						SmpsRAM.TimingMode = 0x40;
						SmpsRAM.TimerBVal = Data[0x00];
						TempByt = 0x02;
					}
					else if (Mode == 0x30)
					{
						SmpsRAM.TimingMode = 0x80;
						SmpsRAM.TimerAVal = Tempo;
						SmpsRAM.TimerBVal = Data[0x02];
						CmdLen ++;
						TempByt = 0x03;
					}
				}	// else - set or change Music Tempo
				else if (Mode == 0x00)
					SmpsRAM.TimerAVal = Tempo;
				else if (Mode & 0x80)
					SmpsRAM.TimerAVal += Tempo;
				else if (Mode & 0x40)
					SmpsRAM.TimerAVal -= Tempo;
			}
			break;
		}
		
		if (1)
		{
			// [not in the driver] disable and reenable timers
			// This improves the timing on tempo changes.
			if (TempByt & 0x01)
			{
				WriteFMI(0x25, SmpsRAM.TimerAVal & 0x03);
				WriteFMI(0x24, SmpsRAM.TimerAVal >> 2);
			}
			if (TempByt & 0x02)
				WriteFMI(0x26, SmpsRAM.TimerBVal);
			WriteFMI(0x27, SmpsRAM.SpcFM3Mode & ~TempByt);
			WriteFMI(0x27, SmpsRAM.SpcFM3Mode);
		}
		break;
	case CF_TIMING_MODE:	// FF 00 Set Timing Mode
		if (SmpsRAM.LockTimingMode)
			break;
		SmpsRAM.TimingMode = Data[0x00];
		if (SmpsRAM.TimingMode == 0x00)
			ym2612_timer_mask(0x00);	// no YM2612 Timer
		else if (SmpsRAM.TimingMode == 0x20)
			ym2612_timer_mask(0x01);	// YM2612 Timer A
		else if (SmpsRAM.TimingMode == 0x40)
			ym2612_timer_mask(0x02);	// YM2612 Timer B
		else //if (SmpsRAM.TimingMode == 0x80)
			ym2612_timer_mask(0x03);	// YM2612 Timer A and B
		break;
	// Jump and Control Flags
	// ----------------------
	case CF_COND_JUMP:		// FF 07 Conditional Jump
		if (CFlag->SubType == CFS_CJMP_RESET_VAR)
		{
			SmpsRAM.CondJmpVal = 0x00;
			break;
		}
		
		TempByt = 0;
		switch(CFlag->SubType & 0x0F)
		{
		case CFS_CJMP_NZ:
			TempByt = (SmpsRAM.CondJmpVal != 0x00);
			break;
		case CFS_CJMP_Z:
			TempByt = (SmpsRAM.CondJmpVal == 0x00);
			break;
		case CFS_CJMP_EQ:
			//TempByt = (SmpsRAM.CondJmpVal == Data[0x00]);
			// Right now CondJmpVal can only be 00 or 01, so I limit the parameter to these values.
			if (Data[0x00])
				TempByt = (SmpsRAM.CondJmpVal != 0x00);
			else
				TempByt = (SmpsRAM.CondJmpVal == 0x00);
			break;
		}
		if (CFlag->SubType & CFS_CJMP_RESET)
			SmpsRAM.CondJmpVal = 0x00;
		
		if (! (CFlag->SubType & CFS_CJMP_2PTRS))
		{
			if (DebugMsgs & 0x01)
				printf("Conditional Jump on Channel %02X at %04X: %s\n", Trk->ChannelMask, Trk->Pos,
						TempByt ? "taken" : "not taken");
			if (! TempByt)	// if condition NOT true, continue normally
				break;
		}
		else
		{
			if (DebugMsgs & 0x01)
				printf("Conditional Jump on Channel %02X at %04X: %s pointer taken\n", Trk->ChannelMask, Trk->Pos,
						TempByt ? "2nd" : "1st");
			if (TempByt)	// if the condition is true, use 2nd pointer
				Trk->Pos += 0x02;	// This works unless I am able to read 2 jump parameters.
			else			// if the condition is false, use 1st pointer
				Trk->Pos += 0x00;
		}
		// fall through
	case CF_GOTO:			// F6 GoTo
		Trk->LastJmpPos = Trk->Pos;
		Trk->Pos += CFlag->JumpOfs;
		Trk->Pos = ReadJumpPtr(&Trk->SmpsSet->Seq.Data[Trk->Pos], Trk->Pos, Trk->SmpsSet);
		Extra_LoopEndCheck(Trk);
		CmdLen = 0x00;
		break;
	case CF_LOOP:			// F7 Loop
		TempByt = Data[0x00];
		if (! Trk->LoopStack[TempByt])
			Trk->LoopStack[TempByt] = Data[0x01];
		
		Trk->LoopStack[TempByt] --;
		if (Trk->LoopStack[TempByt])
		{
			// jump back
			Trk->LastJmpPos = Trk->Pos;
			Trk->Pos += CFlag->JumpOfs;
			Trk->Pos = ReadJumpPtr(&Trk->SmpsSet->Seq.Data[Trk->Pos], Trk->Pos, Trk->SmpsSet);
			CmdLen = 0x00;
		}
		break;
	case CF_GOSUB:			// F8 GoSub
		if (! Trk->StackPtr)
			break;	// stack full - skip GoSub
		Trk->StackPtr -= 0x02;	
		WriteLE16(&Trk->LoopStack[Trk->StackPtr], Trk->Pos + CFlag->Len);
		
		// jump back
		Trk->LastJmpPos = Trk->Pos;
		Trk->Pos += CFlag->JumpOfs;
		Trk->Pos = ReadJumpPtr(&Trk->SmpsSet->Seq.Data[Trk->Pos], Trk->Pos, Trk->SmpsSet);
		CmdLen = 0x00;
		break;
	case CF_RETURN:			// F9 Return from GoSub
		Trk->Pos = ReadLE16(&Trk->LoopStack[Trk->StackPtr]);
		Trk->StackPtr += 0x02;
		CmdLen = 0x00;
		break;
	case CF_LOOP_EXIT:		// EB Exit from Loop
		TempByt = Data[0x00];
		
		if (Trk->LoopStack[TempByt] == 0x01)
		{
			// jump
			Trk->LoopStack[TempByt] --;
			Trk->LastJmpPos = Trk->Pos;
			Trk->Pos += CFlag->JumpOfs;
			Trk->Pos = ReadJumpPtr(&Trk->SmpsSet->Seq.Data[Trk->Pos], Trk->Pos, Trk->SmpsSet);
			Extra_LoopEndCheck(Trk);
			CmdLen = 0x00;
		}
		break;
	case CF_META_CF:		// FF xx Meta Command
		Trk->Pos += CFlag->Len;
		CmdLen = 0x00;
		cfMetaHandler(Trk, Data[0x00]);
		break;
	case CF_TRK_END:		// E3/F2 Track End
		if (CFlag->SubType == CFS_TEND_MUTE)
			SilenceFMChn(Trk);
		
		// Note: SMPS Z80 keeps the PBKFLG_HOLD. This can cause notes to hang. (Example: 9A 04 E7 F2)
		Trk->PlaybkFlags &= ~(PBKFLG_ACTIVE | PBKFLG_HOLD);
		if (Trk->ChannelMask & 0x10)
			Trk->ChannelMask &= ~0x10;	// make drum channels stop FM notes (fixed DJ Boy: Game Over)
		DoNoteOff(Trk);
		Extra_StopCheck();
		
		if (SmpsRAM.TrkMode != TRKMODE_MUSIC)
		{
			SmpsRAM.CurSFXPrio = 0x00;
			RestoreBGMChannel(Trk);
		}
		break;
	// Special Game-Specific Flags
	// ---------------------------
	case CF_FADE_IN_SONG:
		if (CFlag->Len == 0x01)
		{
			// Sonic 1/2: Stops all music processing and instantly restores the previous BGM.
			// (I won't do this here, so I just stop the current track.)
			CMD_FLAGS EndFlag;
			
			EndFlag.Type = CF_TRK_END;
			EndFlag.SubType = CFS_TEND_STD;
			EndFlag.Len = 0x00;
			DoCoordinationFlag(Trk, &EndFlag);
			
			SmpsRAM.LoadSaveRequest = 0x01;
			SmpsRAM.FadeIn.Steps = SmpsCfg->FadeIn.Steps | 0x80;
			SmpsRAM.FadeIn.DlyInit = SmpsCfg->FadeIn.Delay;
		}
		else
		{
			SmpsRAM.CommData = Data[0x00];	// do the actual behaviour first
			
			// Sonic 3K: sets the Communication Byte to FF to tell the driver that the previous
			// song has to be restored.
			if (SmpsRAM.CommData == 0xFF)
			{
				SmpsRAM.LoadSaveRequest = 0x01;
				SmpsRAM.CommData = 0x00;
				SmpsRAM.FadeIn.Steps = SmpsCfg->FadeIn.Steps | 0x80;
				SmpsRAM.FadeIn.DlyInit = SmpsCfg->FadeIn.Delay;
			}
			if (CB_CommVar != NULL)
				CB_CommVar();
		}
		break;
	case CF_SND_OFF:
		WriteFMMain(Trk, 0x88, 0x0F);
		WriteFMMain(Trk, 0x8C, 0x0F);
		break;
	case CF_NOTE_STOP_REV:
		// Ristar:
		//	1. set Note Stop Reverse to Data[0x00]
		//	2. Reset normal Note Stop values to 0
		//	Update: if Note Stop Reverse != 0
		//		-> Note Fill Timeout = NoteLen - NoteStopRev [no limit]
		// Chou Yakyuu Miracle Nine:
		//	1. usual Note Stop with Tick Multiplier (Data[0x00])
		//	2. Enable Note Stop Reverse mode
		//	Update: if Note Stop Reverse != 0
		//		-> Note Fill Timeout = NoteLen - NoteStop [lower limit is 1]
		TempByt = Data[0x00];
		switch(CFlag->SubType)
		{
		case CFS_NSREV_CYMN:
			// Chou Yakyuu Miracle Nine
			TempByt *= Trk->TickMult;
			Trk->NStopTout = TempByt;
			Trk->NStopInit = TempByt;
			Trk->NStopRevMode = 0x01;	// enable Reversed Note Stop
			break;
		case CFS_NSREV_RST:
			// Ristar
			Trk->NStopTout = 0x00;
			Trk->NStopInit = TempByt;
			if (TempByt)
				Trk->NStopRevMode = 0x02 | 0x80;	// enable Reversed Note Stop (with "always execute" mode)
			else
				Trk->NStopRevMode = 0x00;	// parameter byte 00 disables it
			break;
		}
		break;
	case CF_NOTE_STOP_MODE:
		Trk->NStopRevMode = Data[0x00] ? (0x10 | Data[0x00]) : 0x00;
		break;
	case CF_SPINDASH_REV:
		switch(CFlag->SubType)
		{
		case CFS_SDREV_INC:
			Trk->Transpose += SmpsRAM.SpinDashRev;
			if (Trk->Transpose != 0x10)
				SmpsRAM.SpinDashRev ++;
			break;
		case CFS_SDREV_RESET:
			SmpsRAM.SpinDashRev = 0;
			break;
		}
		break;
	case CF_CONT_SFX:
		if (SmpsRAM.ContSfxFlag != 0x80)
		{
			SmpsRAM.ContSfxID = 0x00;
			SmpsRAM.ContSfxFlag = 0x00;
		}
		else
		{
			SmpsRAM.ContSfxLoop --;
			if (! SmpsRAM.ContSfxLoop)
				SmpsRAM.ContSfxFlag = 0x00;
			
			Trk->LastJmpPos = Trk->Pos;
			Trk->Pos += CFlag->JumpOfs;
			Trk->Pos = ReadJumpPtr(&Trk->SmpsSet->Seq.Data[Trk->Pos], Trk->Pos, Trk->SmpsSet);
			Extra_LoopEndCheck(Trk);
			CmdLen = 0x00;
		}
		break;
	case CF_DAC_PS4:
	case CF_DAC_CYMN:
	case CF_DAC_GAXE3:
		CmdLen = cfSpecialDAC(Trk, CFlag);
		break;
	case CF_DAC_PLAY_MODE:
		Trk->DAC.Unused = Data[0x00];
		break;
	}
	
	CmdLen &= 0x7F;	// strip the 0x80 bit off, in case the routine above didn't.
	Trk->Pos += CmdLen;
	
	return;
}

static UINT8 GetInsRegPtrs(TRK_RAM* Trk, const UINT8** RetRegPtr, const UINT8** RetInsPtr, UINT8 Register)
{
	const SMPS_SET* SmpsSet = Trk->SmpsSet;
	const UINT8* RegList;
	const UINT8* InsData;
	UINT8 CurReg;
	
	RegList = SmpsSet->Cfg->InsRegs;
	if (RegList == NULL)
		return 0x01;
	if (Trk->Instrument >= SmpsSet->InsLib.InsCount)
		return 0x02;
	InsData = SmpsSet->InsLib.InsPtrs[Trk->Instrument];
	
	// Since we're using a user-defined Register order for the instruments,
	// we need to search for the respective register in the instrument register list.
	for (CurReg = 0x00; CurReg < SmpsSet->Cfg->InsRegCnt; CurReg ++)
	{
		if (RegList[CurReg] == Register)
		{
			*RetRegPtr = &RegList[CurReg];
			*RetInsPtr = &InsData[CurReg];
			return 0x00;
		}
	}
	
	return 0xFF;
}

static void cfSetIns_PSG(TRK_RAM* Trk, UINT8 InsID)
{
	const ENV_LIB* VolEnvLib = &Trk->SmpsSet->Cfg->VolEnvs;
	
	Trk->Instrument = InsID;
	if (Trk->Instrument > VolEnvLib->EnvCount)	// 1-based, so not >=
	{
		if (DebugMsgs & 0x01)
			printf("Error: Invalid PSG instrument %02X at %04X!\n", Trk->Instrument, Trk->Pos);
		Trk->Instrument = 0x00;
	}
	
	return;
}

static UINT8 cfSetInstrument(TRK_RAM* Trk, const CMD_FLAGS* CFlag, const UINT8* Params)
{
	const ENV_LIB* VolEnvLib = &Trk->SmpsSet->Cfg->VolEnvs;
	const INS_LIB* InsLib = &Trk->SmpsSet->InsLib;
	UINT8 CFInsType;
	UINT8 CmdLen;
	UINT8 InsID;
	const UINT8* InsData;
	
	CmdLen = CFlag->Len;
	if (CmdLen & 0x80)
	{
		CmdLen &= 0x7F;	// SMPS Z80: if the Instrument ID is negative, it takes an additional Song ID parameter byte
		if (Params[0x00] & 0x80)
			CmdLen ++;
	}
	
	CFInsType = CFlag->SubType & CFS_INS_IMASK;
	// At first, catch all special cases.
	switch(CFInsType)
	{
	case CFS_INS_COI:
		if (! (Trk->ChannelMask & 0x80))
		{
			Trk->PanAFMS |= 0xC0;	// enforce L+R
			CFInsType = CFS_INS_FM;
			break;
		}
		CFInsType = 0xFF;	// prevent it from doing anything else
		
		// [Castle of Illusion] set ADSR values based on FM instrument data
		Trk->Instrument = 0x80;	// set to Volume Envelope: ADSR
		InsID = Params[0x00];
		if (InsLib == NULL)
			break;
		if (InsID >= InsLib->InsCount)
		{
			if (DebugMsgs & 0x01)
				printf("Error: Invalid PSG instrument %02X at %04X!\n", InsID, Trk->Pos);
			break;
		}
		InsData = InsLib->InsPtrs[InsID];
		Trk->ADSR.AtkRate = CoI_PSG_AtkRates[InsData[0x05] & 0x1F];
		Trk->ADSR.DecRate = CoI_PSG_DecRates[InsData[0x09] & 0x1F];
		Trk->ADSR.SusRate = CoI_PSG_SusRates[InsData[0x0D] & 0x1F];
		Trk->ADSR.DecLvl = CoI_PSG_SusLevels[(InsData[0x11] & 0xF0) >> 4];
		Trk->ADSR.RelRate = CoI_PSG_RelRates[(InsData[0x11] & 0x0F) >> 0];
		Trk->ADSR.Level = 0xFF;
		break;
	}
	
	switch(CFInsType)
	{
	case CFS_INS_FMP:
		if (Trk->ChannelMask & 0x80)
		{
			// [Sonic 3K] The PSG instrument is set for the EF flag, too.
			cfSetIns_PSG(Trk, Params[0x00]);
			break;
		}
		// fall through for FM instruments
	case CFS_INS_FM:	// EF Set FM Instrument
		if ((CFlag->SubType & CFS_INS_CMASK) == CFS_INS_C)
		{
			if (Trk->ChannelMask & 0x80)
				break;	// PSG channel - return [SMPS Z80]
		}
		
		Trk->FMInsSong = 0x00;	// [not in the driver] reset Instrument Song ID (checked when restoring BGM track)
		if (CFlag->Len & 0x80)
		{
			// EF ii [ss] - Set FM Instrument ii (can use instrument library of song ss)
			UINT8 CurOp;
			
			//SetMaxRelRate:
			for (CurOp = 0x00; CurOp < 0x10; CurOp += 0x04)
				WriteFMMain(Trk, 0x80 | CurOp, 0xFF);
			
			InsID = Params[0x00];
			Trk->Instrument = InsID;
			if (InsID & 0x80)
			{
				Trk->FMInsSong = Params[0x01];
				InsID &= 0x7F;
				InsLib = GetSongInsLib(Trk, Trk->FMInsSong);	// get new Instrument Library
				if (InsLib->InsCount == 0 || Trk->FMInsSong > 0x81)
				{
					printf("Error: FM instrument cross-reference (ins %02X, song %02X) at %04X!\n",
							Trk->Instrument, Trk->FMInsSong, Trk->Pos);
					InsLib = NULL;
				}
			}
		}
		else
		{
			// EF ii - Set FM Instrument
			InsID = Params[0x00];
			Trk->Instrument = InsID;
		}
		
		if (InsLib == NULL)
			break;
		if (InsID >= InsLib->InsCount)
		{
			if (DebugMsgs & 0x01)
				printf("Error: Invalid FM instrument %02X at %04X!\n", Trk->Instrument, Trk->Pos);
			break;
		}
		SendFMIns(Trk, InsLib->InsPtrs[InsID]);
		break;
	case CFS_INS_PSG:	// F5 Set PSG Instrument (Volume Envelope)
		if ((CFlag->SubType & CFS_INS_CMASK) == CFS_INS_C)
		{
			if (! (Trk->ChannelMask & 0x80))
				break;	// FM channel - return [SMPS Z80]
		}
		
		cfSetIns_PSG(Trk, Params[0x00]);
		break;
	}
	
	return CmdLen;
}

static UINT8 cfVolume(TRK_RAM* Trk, const CMD_FLAGS* CFlag, const UINT8* Params)
{
	UINT8 CmdLen;
	UINT8 TempByt;
	UINT16 TempVol;
	
	CmdLen = CFlag->Len;
//	switch(CFlag->SubType & 0xF0)
//	{
//	case 0x00:	// SMPS 68k
		switch(CFlag->SubType)
		{
		case CFS_VOL_NN_FMP:	// E5
			if (Trk->ChannelMask & 0x80)
			{
				Trk->Volume += Params[0x00];
				break;
			}
			else
			{
				Params ++;
				// fall through
			}
		case CFS_VOL_NN_FM:	// E6
			Trk->Volume += Params[0x00];
			if (Trk->SpcDacMode == DCHNMODE_VRDLX)
			{
				RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, Trk->Volume);
				break;
			}
			RefreshFMVolume(Trk);
			break;
		case CFS_VOL_NN_FMP1:	// E6 (early SMPS Z80)
			Trk->Volume += Params[0x00];
			if (Trk->ChannelMask & 0xF8)
				break;
			RefreshFMVolume(Trk);
			break;
		case CFS_VOL_NN_PSG:	// EC
			Trk->Volume += Params[0x00];
			break;
//		}
//		break;
//	case 0x10:	// SMPS Z80 Type 1/2 (volume is not clipped)
//		switch(CFlag->SubType)
//		{
		case CFS_VOL_CN_FMP:	// E5
			Trk->Volume += Params[0x00];
			Params ++;
			// fall through
		case CFS_VOL_CN_FM:	// E6
			if (Trk->ChannelMask & 0x80)
				break;	// PSG channel - return
			Trk->Volume += Params[0x00];
			if (Trk->SpcDacMode == DCHNMODE_VRDLX)
			{
				RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, Trk->Volume);
				break;
			}
			if (Trk->ChannelMask & 0x78)
				break;	// Drum/PWM channel - return
			RefreshFMVolume(Trk);
			break;
		case CFS_VOL_CN_PSG:	// EC
			if (! (Trk->ChannelMask & 0x80))
				break;	// FM channel - return
			Trk->PlaybkFlags &= ~PBKFLG_ATREST;
			if (Trk->VolEnvIdx)	// [not in driver] prevent changing VolEnvIdx from 00 to FF
				Trk->VolEnvIdx --;
			Trk->Volume += Params[0x00];
			break;
//		}
//		break;
//	case 0x20:	// SMPS Z80 Type 2/S3K (volume gets clipped)
//		switch(CFlag->SubType)
//		{
		case CFS_VOL_CC_FMP:	// E5
			Trk->Volume += Params[0x00];
			// fall through
		case CFS_VOL_CC_FMP2:	// S3K broken E5
			Params ++;
			// fall through
		case CFS_VOL_CC_FM:	// E6 [Note: Only S3K is known to clip the FM volume]
			if (Trk->ChannelMask & 0x80)
				break;	// PSG channel - return
			TempVol = Trk->Volume + Params[0x00];
			Trk->Volume = TempVol & 0xFF;
			if (Trk->Volume & 0x80)
			{
				if (TempVol & 0x100)
					Trk->Volume = 0x00;
				else
					Trk->Volume = 0x7F;
			}
			if (Trk->SpcDacMode == DCHNMODE_VRDLX)
			{
				RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, Trk->Volume);
				break;
			}
			if (Trk->ChannelMask & 0x78)
				break;	// Drum/PWM channel - return
			RefreshFMVolume(Trk);
			break;
		case CFS_VOL_CC_PSG:	// EC
			if (! (Trk->ChannelMask & 0x80))
				break;	// FM channel - return
			Trk->PlaybkFlags &= ~PBKFLG_ATREST;
			Trk->VolEnvIdx --;
			Trk->Volume += Params[0x00];
			if (Trk->Volume > 0x0F)
				Trk->Volume = 0x0F;
			break;
//		}
//		break;
//	default:
//		switch(CFlag->SubType)
//		{
		case CFS_VOL_ABS:
			Trk->Volume = Params[0x00];
			if (Trk->SpcDacMode == DCHNMODE_VRDLX)
			{
				RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, Trk->Volume);
				break;
			}
			if (Trk->ChannelMask & 0xF8)
				break;	// Drum/PWM/PSG channel - return
			RefreshFMVolume(Trk);
			break;
		case CFS_VOL_ABS_S3K:
			// scale 00 (min) .. 7F (max) to
			//	FM:  7F (min) .. 00 (max)
			//	PSG: 0F (min) .. 00 (max)
			if (Trk->ChannelMask & 0x80)
			{
				TempByt = Params[0x00] >> 3;
				TempByt &= 0x0F;
				TempByt ^= 0x0F;
				Trk->Volume = TempByt;
			}
			else
			{
				TempByt = Params[0x00] >> 0;
				TempByt &= 0x7F;
				TempByt ^= 0x7F;
				Trk->Volume = TempByt;
				RefreshFMVolume(Trk);
			}
			break;
		case CFS_VOL_ABS_HF2:	// The Hybrid Front: E5
			Trk->Volume += Params[0x00];	// Note: has no effect (classical pasting error in the driver)
			Params ++;
			// fall through
		case CFS_VOL_ABS_HF:	// The Hybrid Front: FF 07
			//Trk->Volume = Params[0x00] + SmpsRAM.1C06;	// 1C06 - some global volume setting
			Trk->Volume = Params[0x00];
			if (Trk->Volume >= 0x80)
				Trk->Volume = 0x7F;
			RefreshFMVolume(Trk);
			break;
		case CFS_VOL_ABS_TMP:
			Trk->Volume = Params[0x00];
			RefreshFMVolume(Trk);
			break;
		case CFS_VOL_SPC_TMP:
			if (Trk->ChannelMask & 0x80)
			{
				Trk->Volume = Params[0x00];	// PSG channel - set volume
			}
			else
			{
				Trk->Volume += Params[0x00];	// FM channel - change volume
				RefreshFMVolume(Trk);
			}
			break;
		case CFS_VOL_ABS_COI:
			if (Trk->ChannelMask & 0x80)
			{
				Trk->Volume = Params[0x00];
			}
			else
			{
				Trk->Volume = Params[0x00] >> 2;
				Trk->Volume += Trk->CoI_VolBase;
				RefreshFMVolume(Trk);
			}
			break;
		case CFS_VOL_SET_BASE:
			Trk->CoI_VolBase = Params[0x00];
			break;
		case CFS_VOL_ABS_PDRM:
			SmpsRAM.NoiseDrmVol = Params[0x00];
			break;
		case CFS_VOL_CHG_PDRM:
			SmpsRAM.NoiseDrmVol += Params[0x00];
			//SmpsRAM.NoiseDrmVol &= 0x0F;	// done by the actual driver, but causes bugs with Fading
			break;
		}
//		break;
//	}
	
	return CmdLen;
}

static UINT8 cfSpecialDAC(TRK_RAM* Trk, const CMD_FLAGS* CFlag)
{
	const UINT8* Data = &Trk->SmpsSet->Seq.Data[Trk->Pos + 0x01];
	UINT8 CmdLen;
	UINT8 TempByt;
	
	CmdLen = CFlag->Len;
	switch(CFlag->Type)
	{
	case CF_DAC_PS4:
		switch(CFlag->SubType)
		{
		case CFS_PS4_VOLCTRL:
			Trk->SpcDacMode = DCHNMODE_PS4;
			//Trk->PlaybkFlags |= PBKFLG_SPCMODE;
			Trk->PS4_DacMode = (INT8)Data[0x00];
			if (! Trk->PS4_DacMode)			// 00 - DAC disabled
				DAC_Stop(0x00);
			RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, Trk->Volume);
			break;
		case CFS_PS4_VOLUME:
			Trk->Volume = Data[0x00];
			RefreshDACVolume(Trk, Trk->SpcDacMode, 0x00, Trk->Volume);
			break;
		case CFS_PS4_SET_SND:
			Trk->DAC.Unused = Data[0x00];
			break;
		case CFS_PS4_LOOP:
			DAC_SetFeature(0x00, DACFLAG_LOOP, Data[0x00]);
			break;
		case CFS_PS4_REVERSE:
			DAC_SetFeature(0x00, DACFLAG_REVERSE, Data[0x00]);
			break;
		case CFS_PS4_TRKMODE:
			Trk->PS4_AltTrkMode = Data[0x00];
			if (Trk->PS4_AltTrkMode == 1 && (DebugMsgs & 0x02))
				print_msg(Trk, CmdLen, "PS4 Alternate DAC Track mode");
			break;
		}
		break;
	case CF_DAC_CYMN:
		TempByt = CFlag->SubType & 0x01;
		switch(CFlag->SubType & 0xF0)
		{
		case CFS_CYMN_CHG_CH:
			SmpsRAM.DacChVol[TempByt] += Data[0x00];
			if (SmpsRAM.DacChVol[TempByt] < 0x80)
				SmpsRAM.DacChVol[TempByt] = 0x80;
			else if (SmpsRAM.DacChVol[TempByt] > 0x8F)
				SmpsRAM.DacChVol[TempByt] = 0x8F;
			RefreshDACVolume(Trk, DCHNMODE_CYMN, TempByt, SmpsRAM.DacChVol[TempByt]);
			break;
		case CFS_CYMN_SET_CH:
			SmpsRAM.DacChVol[TempByt] = 0x80 + Data[0x00];
			RefreshDACVolume(Trk, DCHNMODE_CYMN, TempByt, SmpsRAM.DacChVol[TempByt]);
			break;
		}
		break;
	case CF_DAC_GAXE3:
		switch(CFlag->SubType)
		{
		case CFS_GA3_2NOTE_TEMP:
			Trk->GA3_DacMode |= 0x01;
			break;
		case CFS_GA3_2NOTE_PERM:
			Trk->GA3_DacMode |= 0x03;
			break;
		case CFS_GA3_2NOTE_OFF:
			Trk->GA3_DacMode &= ~0x03;
			break;
		}
		break;
	}
	
	return CmdLen;
}

void RefreshDACVolume(TRK_RAM* Trk, UINT8 DacMode, UINT8 DacChn, UINT8 Volume)
{
	UINT16 CurVolume;
	
	switch(DacMode)
	{
	case DCHNMODE_PS4:
		if (! Trk->PS4_DacMode)			// 00 - DAC disabled
			DAC_Stop(0x00);
		else if (Trk->PS4_DacMode > 0)	// 01 - Volume Control disabled
			DAC_SetVolume(0x00, 0x100);
		else //if (Trk->PS4_DacMode < 0)	// 01 - Volume Control enabled (slower)
			DAC_SetVolume(0x00, Volume * 0x10);
		break;
	case DCHNMODE_CYMN:
		// The volume table has 0x80 as 100%
		CurVolume = ChouYMN_Vols[Volume & 0x0F] * 2;
		DAC_SetVolume(DacChn, CurVolume);
		break;
	case DCHNMODE_VRDLX:
		CurVolume = (Volume & 0x0F) << 4;
		CurVolume = 0x100 - CurVolume;
		DAC_SetVolume(DacChn, CurVolume);
		break;
	case DCHNMODE_S2R:
		if (Volume < 0x10)
		{
			CurVolume = S2R_Vols[Volume & 0x03];
			CurVolume >>= (Volume >> 2);
			DAC_SetVolume(DacChn, CurVolume);
		}
		else
		{
			DAC_SetVolume(DacChn, 0x00);
		}
		break;
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

static void print_msg(TRK_RAM* Trk, UINT8 CmdLen, const char* DescStr)
{
#ifndef DISABLE_DEBUG_MSGS
	const UINT8* Data = Trk->SmpsSet->Seq.Data;
	char TempStr[0x20];
	char* StrPtr;
	UINT8 CurByt;
	UINT8 TrkID;
	char* TrkStr;
	
	StrPtr = TempStr;
	if (Data[Trk->Pos] & 0x80)
		*StrPtr = '\0';
	else
		StrPtr += sprintf(StrPtr, "%02X ", Data[Trk->Pos - 0x01]);
	for (CurByt = 0x00; CurByt < CmdLen; CurByt ++)
		StrPtr += sprintf(StrPtr, "%02X ", Data[Trk->Pos + CurByt]);
	
	if (Trk < SmpsRAM.SFXTrks)
	{
		TrkStr = "Music";
		TrkID = (UINT8)(Trk - SmpsRAM.MusicTrks);
		if (TrkID > 0)	TrkID --;
	}
	else if (Trk < SmpsRAM.SpcSFXTrks)
	{
		TrkStr = "SFX";
		TrkID = (UINT8)(Trk - SmpsRAM.SFXTrks);
	}
	else
	{
		TrkStr = "Spc. SFX";
		TrkID = (UINT8)(Trk - SmpsRAM.SpcSFXTrks);
	}
	
	ClearLine();
	if (DescStr == NULL)
		printf("%s Track %u/%02X, Pos 0x%04X: Command %s\n",
				TrkStr, TrkID, Trk->ChannelMask, Trk->Pos, TempStr);
	else
		printf("%s Track %u/%02X, Pos 0x%04X: Command %s- %s\n",
				TrkStr, TrkID, Trk->ChannelMask, Trk->Pos, TempStr, DescStr);
#endif
	
	return;
}
