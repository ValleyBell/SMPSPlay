// SMPS Loader and Preparser
// -------------------------
// Written by Valley Bell, 2014
#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>	// for strtoul()
#include <malloc.h>
#include <memory.h>
#include <string.h>	// for strrchr()
#include <stddef.h>	// for NULL
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "stdtype.h"
#include "loader.h"
#include "Engine/smps_structs.h"
#include "Engine/smps_structs_int.h"	// for PBKFLG_* defines
#include "Engine/smps_commands.h"
#include "Engine/dac.h"	// for DAC usage stuff


void ClearLine(void);			// from main.c

extern UINT8 DebugMsgs;


// Function Prototypes
// -------------------
INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT16 ReadRawPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_CFG* SmpsCfg);

//UINT8 GuessSMPSOffset(SMPS_CFG* SmpsCfg);
//UINT8 SmpsOffsetFromFilename(const char* FileName, UINT16* RetOffset);
//UINT8 PreparseSMPSFile(SMPS_CFG* SmpsCfg);
static void MarkDrumNote(DAC_CFG* DACDrv, const DRUM_LIB* DrumLib, UINT8 Note);
static void MarkDrum_Sub(DAC_CFG* DACDrv, const DRUM_DATA* DrumData);
static void MarkDrum_DACNote(DAC_CFG* DACDrv, UINT8 Note);
//void FreeSMPSFile(SMPS_CFG* SmpsCfg);


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

INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg)
{
	return ReadRawPtr(Data, SmpsCfg) - SmpsCfg->SeqBase;
}

INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_CFG* SmpsCfg)
{
	UINT16 PtrVal;
	UINT8 Offset;
	
	PtrVal = ReadRawPtr(Data, SmpsCfg);
	Offset = SmpsCfg->PtrFmt & PTRFMT_OFSMASK;
	if (! Offset)
	{
		// absolute
		return PtrVal - SmpsCfg->SeqBase;
	}
	else
	{
		// relative
		Offset --;
		return PtrPos + Offset + (INT16)PtrVal;
	}
}

UINT8 GuessSMPSOffset(SMPS_CFG* SmpsCfg)
{
	UINT32 FileLen;
	UINT8* FileData;
	UINT16 InsPtr;
	UINT8 FMTrkCnt;
	UINT8 PSGTrkCnt;
	UINT8 TrkCount;
	UINT8 CurTrk;
	UINT16 CurPos;
	UINT16 TrkOfs[0x10];
	UINT16 TempOfs;
	
	if ((SmpsCfg->PtrFmt & PTRFMT_OFSMASK) != 0x00)
		return 0x00;
	
	FileLen = SmpsCfg->SeqLength;
	FileData = SmpsCfg->SeqData;
	if (! FileLen || FileData == NULL)
		return 0xFF;
	
	CurPos = 0x00;
	InsPtr = ReadRawPtr(&FileData[CurPos + 0x00], SmpsCfg);
	FMTrkCnt = FileData[CurPos + 0x02];
	PSGTrkCnt = FileData[CurPos + 0x03];
	if (FMTrkCnt + PSGTrkCnt >= 0x10)
		return 0x80;
	
	CurPos += 0x06;
	TempOfs = CurPos + FMTrkCnt * 0x04 + PSGTrkCnt * 0x06;
	if (FileLen < TempOfs)
		return 0x81;
	
	TrkCount = 0x00;
	for (CurTrk = 0x00; CurTrk < FMTrkCnt; CurTrk ++, TrkCount ++, CurPos += 0x04)
		TrkOfs[TrkCount] = ReadRawPtr(&FileData[CurPos], SmpsCfg);
	for (CurTrk = 0x00; CurTrk < PSGTrkCnt; CurTrk ++, TrkCount ++, CurPos += 0x06)
		TrkOfs[TrkCount] = ReadRawPtr(&FileData[CurPos], SmpsCfg);
	for (CurTrk = 0x00; CurTrk < SmpsCfg->AddChnCnt; CurTrk ++, TrkCount ++, CurPos += 0x04)
		TrkOfs[TrkCount] = ReadRawPtr(&FileData[CurPos], SmpsCfg);
	
	SmpsCfg->SeqBase = 0x0000;
	// Search for first track
	TempOfs = 0xFFFF;
	for (CurTrk = 0x00; CurTrk < TrkCount; CurTrk ++)
	{
		if (TrkOfs[CurTrk] < TempOfs)
			TempOfs = TrkOfs[CurTrk];
	}
	if (InsPtr < TempOfs && InsPtr >= TempOfs - 0x180)	// if the distance is extreme, it probably uses a global instrument library
		TempOfs = InsPtr;
	// Calculate the sequence's ROM offset based on the assumption that
	// it starts immediately after the header.
	SmpsCfg->SeqBase = TempOfs - CurPos;
	
	return 0x00;
}

UINT8 SmpsOffsetFromFilename(const char* FileName, UINT16* RetOffset)
{
	const char* ExtentionDot;
	const char* CurDot;
	const char* LastDot;
	UINT16 BaseOfs;
	
	ExtentionDot = strrchr(FileName, '.');
	CurDot = FileName;
	LastDot = NULL;
	while(CurDot != NULL)
	{
		CurDot = strchr(CurDot + 1, '.');
		if (CurDot == ExtentionDot)
			break;
		LastDot = CurDot + 1;
	}
	if (CurDot == NULL || LastDot == NULL)
		return 0xFF;
	
	if (LastDot != CurDot - 4)	// There must be exactly 4 characters between the 2 dots.
		return 0xFF;
	
	BaseOfs = (UINT16)strtoul(LastDot, (char**)&CurDot, 0x10);
	if (CurDot != ExtentionDot)
		return 0xFF;	// was not a full 2-byte hexadecimal number
	
	*RetOffset = BaseOfs;
	return 0x00;
}

static void CreateInstrumentTable(SMPS_CFG* SmpsCfg, UINT32 FileLen, UINT8* FileData, UINT32 StartOfs)
{
	INS_LIB* InsLib;
	UINT32 CurPos;
	UINT16 TempOfs;
	
	InsLib = (INS_LIB*)malloc(sizeof(INS_LIB));
	InsLib->InsCount = (FileLen - StartOfs) / SmpsCfg->InsRegCnt;
	if (InsLib->InsCount > 0x100)
		InsLib->InsCount = 0x100;
	InsLib->InsPtrs = (UINT8**)malloc(InsLib->InsCount * sizeof(UINT8*));
	
	CurPos = StartOfs;
	for (TempOfs = 0; TempOfs < InsLib->InsCount; TempOfs ++, CurPos += SmpsCfg->InsRegCnt)
		InsLib->InsPtrs[TempOfs] = &FileData[CurPos];
	
	SmpsCfg->InsLib = InsLib;
	return;
}

UINT8 PreparseSMPSFile(SMPS_CFG* SmpsCfg)
{
	UINT32 FileLen;
	UINT8* FileData;
	UINT8* FileMask;
	CMD_LIB* CmdList;
	CMD_LIB* CmdMetaList;
	DAC_CFG* DACDrv;
	UINT16 InsPtr;
	UINT8 FMTrkCnt;
	UINT8 PSGTrkCnt;
	UINT8 TrkCount;
	UINT8 CurTrk;
	UINT16 CurPos;
	UINT16 TrkOfs[0x10];
	UINT16 TempOfs;
	UINT16 OldPos;
	UINT8 TrkMode;
	UINT8 CurCmd;
	UINT8 CmdLen;
	UINT16 StackPtrs[0x08];
	UINT16 LoopPtrs[0x08];
	UINT16 StackPtrsE[0x08];	// GoSub Entry Pointer
	UINT8 StackPos;
	UINT8 LoopID;
	UINT8 UsageMask;
	UINT8 IsDrmTrk;
	
	FileLen = SmpsCfg->SeqLength;
	FileData = SmpsCfg->SeqData;
	if (! FileLen || FileData == NULL)
		return 0xFF;
	CmdList = &SmpsCfg->CmdList;
	CmdMetaList = &SmpsCfg->CmdMetaList;
	DACDrv = &SmpsCfg->DACDrv;
	
	CurPos = 0x00;
	InsPtr = ReadPtr(&FileData[CurPos + 0x00], SmpsCfg);
	FMTrkCnt = FileData[CurPos + 0x02];
	PSGTrkCnt = FileData[CurPos + 0x03];
	if (FMTrkCnt > SmpsCfg->FMChnCnt || PSGTrkCnt > SmpsCfg->PSGChnCnt)
		return 0x80;
	
	CurPos += 0x06;
	TempOfs = CurPos + FMTrkCnt * 0x04 + PSGTrkCnt * 0x06 + SmpsCfg->AddChnCnt * 0x04;
	if (FileLen < TempOfs)
		return 0x81;
	
	TrkCount = 0x00;
	for (CurTrk = 0x00; CurTrk < FMTrkCnt; CurTrk ++, TrkCount ++, CurPos += 0x04)
		TrkOfs[TrkCount] = ReadPtr(&FileData[CurPos], SmpsCfg);
	for (CurTrk = 0x00; CurTrk < PSGTrkCnt; CurTrk ++, TrkCount ++, CurPos += 0x06)
		TrkOfs[TrkCount] = ReadPtr(&FileData[CurPos], SmpsCfg);
	for (CurTrk = 0x00; CurTrk < SmpsCfg->AddChnCnt; CurTrk ++, TrkCount ++, CurPos += 0x04)
		TrkOfs[TrkCount] = ReadPtr(&FileData[CurPos], SmpsCfg);
	
	if (SmpsCfg->InsMode & INSMODE_INT)
	{
		// interleaved instruments ALWAYS use external files
		// (requires instrument pointers due to variable instrument size)
		SmpsCfg->InsLib = &SmpsCfg->GlbInsLib;
	}
	else if (InsPtr && (UINT32)InsPtr + SmpsCfg->InsRegCnt <= FileLen)
	{
		CreateInstrumentTable(SmpsCfg, FileLen, FileData, InsPtr);
	}
	else if (SmpsCfg->GlbInsData != NULL)
	{
		InsPtr += SmpsCfg->SeqBase;
		if (SmpsCfg->GlbInsBase == 0x0000)	// an instrument set offset of 0 enforces "use always the full table"
			InsPtr = 0x0000;
		if (InsPtr > SmpsCfg->GlbInsBase && InsPtr < SmpsCfg->GlbInsBase + SmpsCfg->GlbInsLen)
		{
			UINT16 BaseOfs;
			
			// read from the middle of the Instrument Table, if the song's Table Offset is
			// TblStart < SongTblOfs < TblEnd
			// (TblStart == SongTblOfs is done in the else block)
			BaseOfs = InsPtr - SmpsCfg->GlbInsBase;
			CreateInstrumentTable(SmpsCfg, SmpsCfg->GlbInsLen, SmpsCfg->GlbInsData, BaseOfs);
		}
		else
		{
			SmpsCfg->InsLib = &SmpsCfg->GlbInsLib;
		}
	}
	else
	{
		SmpsCfg->InsLib = NULL;
	}
	SmpsCfg->LoopPtrs = (UINT16*)malloc(TrkCount * sizeof(UINT16));
	
	// reset DAC usage
	for (TempOfs = 0x00; TempOfs < DACDrv->SmplCount; TempOfs ++)
		DACDrv->Smpls[TempOfs].UsageID = 0xFF;
	
	SmpsCfg->SeqFlags = 0x00;
	
	FileMask = (UINT8*)malloc(FileLen);
	for (CurTrk = 0x00; CurTrk < TrkCount; CurTrk ++)
	{
		CurPos = TrkOfs[CurTrk];
		SmpsCfg->LoopPtrs[CurTrk] = 0x0000;
		if (CurPos >= FileLen)
		{
			if (DebugMsgs & 0x04)
			{
				ClearLine();
				printf("Invalid Start Offset %04X for Track %u\n", CurPos, CurTrk);
			}
			continue;
		}
		
		if (! CurTrk && FMTrkCnt)
			IsDrmTrk = 0x01;
		else
			IsDrmTrk = 0x00;
		
		memset(FileMask, 0x00, FileLen);
		memset(LoopPtrs, 0x00, sizeof(UINT16) * 0x08);
		memset(StackPtrs, 0x00, sizeof(UINT16) * 0x08);
		StackPos = 0x08;
		TrkMode = PBKFLG_ACTIVE;
		while(CurPos < FileLen && (TrkMode & PBKFLG_ACTIVE))
		{
			FileMask[CurPos] |= 1 << (8 - StackPos);
			while(FileData[CurPos] >= CmdList->FlagBase)
			{
				CurCmd = FileData[CurPos] - CmdList->FlagBase;
				if (CurCmd >= CmdList->FlagCount)
					break;
				if (CmdList->CmdData[CurCmd].Type != CF_META_CF)
					break;
				CurPos += CmdList->CmdData[CurCmd].Len;
				if (CurPos >= FileLen)
				{
					CurPos --;
					break;
				}
				CurCmd = FileData[CurPos];
				FileMask[CurPos] |= 0x01;
			}
			if (FileData[CurPos] < CmdList->FlagBase)
			{
				if (TrkMode & PBKFLG_RAWFREQ)
				{
					CurPos += 0x02 + 0x01;	// frequency + delay
				}
				else if (FileData[CurPos] & 0x80)
				{
					if (IsDrmTrk)
					{
						if (! (TrkMode & PBKFLG_SPCMODE))	// if not Phantasy Star IV
							MarkDrumNote(DACDrv, &SmpsCfg->DrumLib, FileData[CurPos]);
						else if (TrkMode & PBKFLG_RAWFREQ)	// handle PS4 special mode
							MarkDrum_DACNote(DACDrv, FileData[CurPos]);
					}
					
					CurPos ++;	// note
					if (TrkMode & PBKFLG_PITCHSLIDE)
						CurPos += 0x01 + 0x01;	// slide speed + delay
					else if (! (FileData[CurPos] & 0x80))
						CurPos ++;	// delay
				}
				else
				{
					CurPos ++;	// delay
				}
				continue;
			}
			CurCmd = FileData[CurPos] - CmdList->FlagBase;
			if (CurCmd >= CmdList->FlagCount)
			{
				if (DebugMsgs & 0x04)
					printf("Unknown Coordination Flag 0x%02X in Track %u at 0x%04X\n",
							CmdList->FlagBase + CurCmd, CurTrk, CurPos);
				CurPos ++;
				continue;
			}
			
			CmdLen = CmdList->CmdData[CurCmd].Len;
			if (CmdLen & 0x80)
			{
				CmdLen &= 0x7F;
				switch(CmdList->CmdData[CurCmd].Type)
				{
				case CF_INSTRUMENT:
					if (FileData[CurPos + 0x01] & 0x80)
						CmdLen ++;
					break;
				case CF_PAN_ANIM:
					if (FileData[CurPos + 0x01])
						CmdLen += 0x04;
					break;
				}
			}
			switch(CmdList->CmdData[CurCmd].Type)
			{
			case CF_TRK_END:
				TrkMode = 0x00;
				break;
			case CF_GOTO:
				OldPos = CurPos;
				TempOfs = CurPos + CmdList->CmdData[CurCmd].JumpOfs;
				CurPos = ReadJumpPtr(&FileData[TempOfs], TempOfs, SmpsCfg);
				if (CurPos >= FileLen)
				{
					if (DebugMsgs & 0x04)
					{
						ClearLine();
						printf("GoTo to Offset %04X in Track %u at 0x%04X\n", CurPos, CurTrk, OldPos);
					}
					TrkMode = 0x00;
					break;
				}
				CmdLen = 0x00;
				
				UsageMask = (1 << (9 - StackPos)) - 1;
				if (FileMask[CurPos] & UsageMask)
				{
					SmpsCfg->LoopPtrs[CurTrk] = CurPos;
					TrkMode = 0x00;
					if (StackPos < 0x08 && (DebugMsgs & 0x04))
					{
						ClearLine();
						printf("Loop in GoSub Layer %u in Track %u at 0x%04X\n", 8 - StackPos, CurTrk, CurPos);
					}
				}
				if (StackPos < 0x08 && CurPos < OldPos)	// jumping backwards?
					StackPtrsE[StackPos] = 0x0000;	// mark as non-linear subroutine
				break;
			case CF_LOOP:
				LoopID = FileData[CurPos + 0x01] & 0x07;
				if (LoopPtrs[LoopID])
				{
					// a Loop Exit command was found, so process its data now
					OldPos = CurPos;
					CurPos = LoopPtrs[LoopID];
					LoopPtrs[LoopID] = 0x0000;
					CmdLen = 0x00;
					if (CurPos >= FileLen)
					{
						if (DebugMsgs & 0x04)
						{
							ClearLine();
							printf("Loop Exit to Offset %04X in Track %u at 0x%04X\n", CurPos, CurTrk, OldPos);
						}
						TrkMode = 0x00;
						break;
					}
					
					if (StackPos < 0x08 && CurPos < OldPos)	// jumping backwards?
						StackPtrsE[StackPos] = 0x0000;	// mark as non-linear subroutine
					UsageMask = (1 << (9 - StackPos)) - 1;
					if (FileMask[CurPos] & UsageMask)
					{
						SmpsCfg->LoopPtrs[CurTrk] = CurPos;
						TrkMode = 0x00;
					}
				}
				break;
			case CF_LOOP_EXIT:
				LoopID = FileData[CurPos + 0x01] & 0x07;
				TempOfs = CurPos + CmdList->CmdData[CurCmd].JumpOfs;
				LoopPtrs[LoopID] = ReadJumpPtr(&FileData[TempOfs], TempOfs, SmpsCfg);
				break;
			case CF_GOSUB:
				if (! StackPos)
				{
					if (DebugMsgs & 0x04)
					{
						ClearLine();
						printf("Stack Overflow in Track %u at 0x%04X\n", CurTrk, CurPos);
					}
					TrkMode = 0x00;
					break;
				}
				
				StackPos --;
				StackPtrs[StackPos] = CurPos + CmdLen;
				
				OldPos = CurPos;
				TempOfs = CurPos + CmdList->CmdData[CurCmd].JumpOfs;
				CurPos = ReadJumpPtr(&FileData[TempOfs], TempOfs, SmpsCfg);
				StackPtrsE[StackPos] = CurPos;
				CmdLen = 0x00;
				if (CurPos >= FileLen)
				{
					if (DebugMsgs & 0x04)
					{
						ClearLine();
						printf("GoSub to Offset %04X in Track %u at 0x%04X\n", CurPos, CurTrk, OldPos);
					}
					TrkMode = 0x00;
					break;
				}
				break;
			case CF_RETURN:
				if (StackPos >= 0x08)
				{
					TrkMode = 0x00;
					if (DebugMsgs & 0x04)
					{
						ClearLine();
						printf("Return without GoSub in Track %u at 0x%04X\n", CurTrk, CurPos);
					}
					break;
				}
				
				// unmark all bytes processed in the subroutine
				UsageMask = (1 << (8 - StackPos)) - 1;	// mask out everything with equal or higher stack
				if (StackPtrsE[StackPos])
				{
					// linear subroutine
					for (TempOfs = StackPtrsE[StackPos]; TempOfs <= CurPos; TempOfs ++)
						FileMask[TempOfs] &= UsageMask;
				}
				else
				{
					// There was a backwards-jump in the subroutine - go over the whole file.
					for (TempOfs = 0x00; TempOfs < FileLen; TempOfs ++)
						FileMask[TempOfs] &= UsageMask;
				}
				
				CurPos = StackPtrs[StackPos];
				StackPos ++;
				CmdLen = 0x00;
				break;
			case CF_PITCH_SLIDE:
				if (FileData[CurPos + 0x01] == 0x01)
					TrkMode |= PBKFLG_PITCHSLIDE;
				else
					TrkMode &= ~PBKFLG_PITCHSLIDE;
				break;
			case CF_RAW_FREQ:		// FC Raw Frequency Mode
				if (FileData[CurPos + 0x01] == 0x01)
					TrkMode |= PBKFLG_RAWFREQ;
				else
					TrkMode &= ~PBKFLG_RAWFREQ;
				break;
			case CF_DAC_PS4:
				switch(CmdList->CmdData[CurCmd].SubType)
				{
				case CFS_PS4_VOLCTRL:
					TrkMode |= PBKFLG_SPCMODE;
					break;
				case CFS_PS4_TRKMODE:
					if (FileData[CurPos + 0x01] == 0x01)
						TrkMode |= PBKFLG_RAWFREQ;
					else
						TrkMode &= ~PBKFLG_RAWFREQ;
					break;
				case CFS_PS4_SET_SND:
					MarkDrum_DACNote(DACDrv, FileData[CurPos + 0x01]);
					break;
				}
				break;
			//case CF_PLAY_DAC:
			case CF_PLAY_PWM:
			case CF_DAC_CYMN:
				if (DebugMsgs & 0x04)
					printf("Special DAC command (Pos 0x%04X)\n", CurPos);
				break;
			case CF_IGNORE:
				if (CmdList->CmdData[CurCmd].JumpOfs && (DebugMsgs & 0x04))
					printf("Unknown Conditional Jump (Pos 0x%04X)\n", CurPos);
				break;
			case CF_INVALID:
				if (DebugMsgs & 0x04)
					printf("Unknown Coordination Flag 0x%02X (Pos 0x%04X)\n",
							CmdList->FlagBase + CurCmd, CurPos);
				break;
			case CF_FADE_IN_SONG:
				if (CmdList->CmdData[CurCmd].Len == 0x01)
					TrkMode = 0x00;	// Sonic 1's Fade In command also terminates the song.
				SmpsCfg->SeqFlags |= SEQFLG_NEED_SAVE;
				break;
			}
			
			CurPos += CmdLen;
		}
	}
	free(FileMask);
	
	return 0x00;
}

static void MarkDrumNote(DAC_CFG* DACDrv, const DRUM_LIB* DrumLib, UINT8 Note)
{
	if (Note < 0x80)
		return;
	
	Note &= 0x7F;
	if (DrumLib->Mode == DRMMODE_NORMAL)
	{
		if (Note < DrumLib->DrumCount)
			MarkDrum_Sub(DACDrv, &DrumLib->DrumData[Note]);
	}
	else
	{
		UINT8 TempNote;
		
		TempNote = (Note & DrumLib->Mask1);// >> DrumLib->Shift1;
		if (TempNote && TempNote < DrumLib->DrumCount)
			MarkDrum_Sub(DACDrv, &DrumLib->DrumData[TempNote]);
		
		TempNote = (Note & DrumLib->Mask2);// >> DrumLib->Shift2;
		if (TempNote && TempNote < DrumLib->DrumCount)
			MarkDrum_Sub(DACDrv, &DrumLib->DrumData[TempNote]);
	}
	
	return;
}

static void MarkDrum_Sub(DAC_CFG* DACDrv, const DRUM_DATA* DrumData)
{
	UINT16 SmplID;
	
	if (DrumData->Type != DRMTYPE_DAC)
		return;
	
	if (DrumData->DrumID >= DACDrv->TblCount)
		return;
	SmplID = DACDrv->SmplTbl[DrumData->DrumID].Sample;
	if (SmplID >= DACDrv->SmplCount)
		return;
	
	DACDrv->Smpls[SmplID].UsageID = 0xFE;
	
	return;
}

static void MarkDrum_DACNote(DAC_CFG* DACDrv, UINT8 Note)
{
	UINT8 TblID;
	UINT16 SmplID;
	
	Note &= 0x7F;
	if (! Note)
		return;
	
	TblID = Note - 0x01;
	if (TblID >= DACDrv->TblCount)
		return;
	SmplID = DACDrv->SmplTbl[TblID].Sample;
	if (SmplID >= DACDrv->SmplCount)
		return;
	
	DACDrv->Smpls[SmplID].UsageID = 0xFE;
	
	return;
}

void FreeSMPSFile(SMPS_CFG* SmpsCfg)
{
	if (SmpsCfg->SeqData != NULL)
	{
		free(SmpsCfg->SeqData);
		SmpsCfg->SeqData = NULL;
	}
	if (SmpsCfg->InsLib != NULL)
	{
		if (SmpsCfg->InsLib != &SmpsCfg->GlbInsLib)
		{
			free(SmpsCfg->InsLib->InsPtrs);
			free(SmpsCfg->InsLib);
		}
		SmpsCfg->InsLib = NULL;
	}
	
	if (SmpsCfg->LoopPtrs != NULL)
	{
		free(SmpsCfg->LoopPtrs);
		SmpsCfg->LoopPtrs = NULL;
	}
	
	return;
}
