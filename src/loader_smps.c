// SMPS Loader and Preparser
// -------------------------
// Written by Valley Bell, 2014
#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>	// for NULL
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include <common_def.h>
#include "Engine/smps_structs.h"
#include "Engine/smps_structs_int.h"	// for PBKFLG_* defines
#include "Engine/smps_commands.h"
#include "Engine/dac.h"		// for DAC usage stuff
#include "loader_data.h"	// for FreeFileData()
#include "loader_smps.h"


#ifndef DISABLE_DEBUG_MSGS
void ClearLine(void);			// from main.c

extern UINT8 DebugMsgs;
#else
#define ClearLine()
#define DebugMsgs	0
#endif


// Function Prototypes
// -------------------
INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT16 ReadRawPtr(const UINT8* Data, const SMPS_CFG* SmpsCfg);
INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_SET* SmpsSet);
INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_SET* SmpsSet);

//UINT8 GuessSMPSOffset(SMPS_CFG* SmpsCfg);
//UINT8 SmpsOffsetFromFilename(const char* FileName, UINT16* RetOffset);
static void DuplicateInsTable(const INS_LIB* InsLibSrc, INS_LIB* InsLibDst);
static void CreateInstrumentTable(SMPS_SET* SmpsSet, UINT32 FileLen, const UINT8* FileData, UINT32 StartOfs);
//UINT8 PreparseSMPSFile(SMPS_CFG* SmpsCfg);
#ifdef ENABLE_VGM_LOGGING
static void MarkDrumNote(const SMPS_CFG* SmpsCfg, DAC_CFG* DACDrv, const DRUM_LIB* DrumLib, UINT8 Note);
static void MarkDrum_Sub(const SMPS_CFG* SmpsCfg, DAC_CFG* DACDrv, const DRUM_DATA* DrumData);
static void MarkDrum_DACNote(DAC_CFG* DACDrv, UINT8 Bank, UINT8 Note);
static void MarkDrumTrack(const SMPS_CFG* SmpsCfg, DAC_CFG* DACDrv, const DRUM_DATA* DrumData, UINT8 Mode);
#endif
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

INLINE UINT16 ReadPtr(const UINT8* Data, const SMPS_SET* SmpsSet)
{
	return ReadRawPtr(Data, SmpsSet->Cfg) - SmpsSet->SeqBase;
}

INLINE UINT16 ReadJumpPtr(const UINT8* Data, const UINT16 PtrPos, const SMPS_SET* SmpsSet)
{
	const SMPS_CFG* SmpsCfg = SmpsSet->Cfg;
	UINT16 PtrVal;
	UINT8 Offset;
	
	PtrVal = ReadRawPtr(Data, SmpsCfg);
	Offset = SmpsCfg->PtrFmt & PTRFMT_OFSMASK;
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

UINT8 GuessSMPSOffset(SMPS_SET* SmpsSet)
{
	const SMPS_CFG* SmpsCfg = SmpsSet->Cfg;
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
	
	FileLen = SmpsSet->Seq.Len;
	FileData = SmpsSet->Seq.Data;
	if (! FileLen || FileData == NULL)
		return 0xFF;
	
	CurPos = 0x00;
	InsPtr = ReadRawPtr(&FileData[CurPos + 0x00], SmpsCfg);
	FMTrkCnt = FileData[CurPos + 0x02];
	PSGTrkCnt = FileData[CurPos + 0x03];
	if (FMTrkCnt + PSGTrkCnt >= 0x10)
		return 0x80;
	
	CurPos += 0x06;
	TempOfs = CurPos + FMTrkCnt * 0x04 + PSGTrkCnt * 0x06 + SmpsCfg->AddChnCnt * 0x04;
	if (FileLen < TempOfs)
		return 0x81;
	
	TrkCount = 0x00;
	for (CurTrk = 0x00; CurTrk < FMTrkCnt; CurTrk ++, TrkCount ++, CurPos += 0x04)
		TrkOfs[TrkCount] = ReadRawPtr(&FileData[CurPos], SmpsCfg);
	for (CurTrk = 0x00; CurTrk < PSGTrkCnt; CurTrk ++, TrkCount ++, CurPos += 0x06)
		TrkOfs[TrkCount] = ReadRawPtr(&FileData[CurPos], SmpsCfg);
	for (CurTrk = 0x00; CurTrk < SmpsCfg->AddChnCnt; CurTrk ++, TrkCount ++, CurPos += 0x04)
		TrkOfs[TrkCount] = ReadRawPtr(&FileData[CurPos], SmpsCfg);
	
	SmpsSet->SeqBase = 0x0000;
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
	SmpsSet->SeqBase = TempOfs - CurPos;
	
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

static void DuplicateInsTable(const INS_LIB* InsLibSrc, INS_LIB* InsLibDst)
{
	free(InsLibDst->InsPtrs);
	
	InsLibDst->InsPtrs = (UINT8**)malloc(InsLibSrc->InsCount * sizeof(UINT8*));
	memcpy(InsLibDst->InsPtrs, InsLibSrc->InsPtrs, InsLibSrc->InsCount * sizeof(UINT8*));
	InsLibDst->InsCount = InsLibSrc->InsCount;
	
	return;
}

static void CreateInstrumentTable(SMPS_SET* SmpsSet, UINT32 FileLen, const UINT8* FileData, UINT32 StartOfs)
{
	const SMPS_CFG* SmpsCfg = SmpsSet->Cfg;
	INS_LIB* InsLib;
	UINT32 CurPos;
	UINT16 TempOfs;
	
	InsLib = &SmpsSet->InsLib;
	InsLib->InsCount = (FileLen - StartOfs) / SmpsCfg->InsRegCnt;
	if (InsLib->InsCount > 0x100)
		InsLib->InsCount = 0x100;
	InsLib->InsPtrs = (UINT8**)malloc(InsLib->InsCount * sizeof(UINT8*));
	
	CurPos = StartOfs;
	for (TempOfs = 0; TempOfs < InsLib->InsCount; TempOfs ++, CurPos += SmpsCfg->InsRegCnt)
		InsLib->InsPtrs[TempOfs] = (UINT8*)&FileData[CurPos];
	
	return;
}

UINT8 PreparseSMPSFile(SMPS_SET* SmpsSet)
{
	const SMPS_CFG* SmpsCfg = SmpsSet->Cfg;
	UINT32 FileLen;
	const UINT8* FileData;
	UINT8* FileMask;
	const CMD_LIB* CmdList;
	const CMD_LIB* CmdMetaList;
	const CMD_LIB* CmdLstCur;
#ifdef ENABLE_VGM_LOGGING
	DAC_CFG* DACDrv;	// can't be const, because I set the Usage counters
#endif
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
	UINT16 LoopExitPtrs[0x08];
	UINT16 StackPtrsE[0x08];	// GoSub Entry Pointer
	UINT8 StackPos;
	UINT8 LoopID;
	UINT8 UsageMask;
	UINT8 IsDacTrk;
	UINT8 DACBank;
	
	FileLen = SmpsSet->Seq.Len;
	FileData = SmpsSet->Seq.Data;
	if (! FileLen || FileData == NULL)
		return 0xFF;
	CmdList = &SmpsCfg->CmdList;
	CmdMetaList = &SmpsCfg->CmdMetaList;
#ifdef ENABLE_VGM_LOGGING
	DACDrv = (DAC_CFG*)&SmpsCfg->DACDrv;
#endif
	
	CurPos = 0x00;
	InsPtr = ReadPtr(&FileData[CurPos + 0x00], SmpsSet);
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
		TrkOfs[TrkCount] = ReadPtr(&FileData[CurPos], SmpsSet);
	for (CurTrk = 0x00; CurTrk < PSGTrkCnt; CurTrk ++, TrkCount ++, CurPos += 0x06)
		TrkOfs[TrkCount] = ReadPtr(&FileData[CurPos], SmpsSet);
	for (CurTrk = 0x00; CurTrk < SmpsCfg->AddChnCnt; CurTrk ++, TrkCount ++, CurPos += 0x04)
		TrkOfs[TrkCount] = ReadPtr(&FileData[CurPos], SmpsSet);
	
	if (SmpsCfg->InsMode & INSMODE_INT)
	{
		// interleaved instruments ALWAYS use external files
		// (requires instrument pointers due to variable instrument size)
		SmpsSet->InsLib.Type = INSTYPE_GBL;
		SmpsSet->InsBase = 0x0000;
		DuplicateInsTable(&SmpsCfg->GblInsLib, &SmpsSet->InsLib);
	}
	else if (InsPtr && (UINT32)InsPtr + SmpsCfg->InsRegCnt <= FileLen)
	{
		SmpsSet->InsLib.Type = INSTYPE_SEQ;
		SmpsSet->InsBase = InsPtr;
		CreateInstrumentTable(SmpsSet, FileLen, FileData, InsPtr);
	}
	else if (SmpsCfg->GblInsLib.InsCount)
	{
		InsPtr += SmpsSet->SeqBase;
		if (SmpsCfg->GblInsBase == 0x0000)	// an instrument set offset of 0 enforces "use always the full table"
			InsPtr = 0x0000;
		
		SmpsSet->InsLib.Type = INSTYPE_GBL;
		if (InsPtr > SmpsCfg->GblInsBase && InsPtr < SmpsCfg->GblInsBase + SmpsCfg->GblIns.Len)
		{
			// read from the middle of the Instrument Table, if the song's Table Offset is
			// TblStart < SongTblOfs < TblEnd
			// (TblStart == SongTblOfs is done in the else block)
			SmpsSet->InsBase = InsPtr - SmpsCfg->GblInsBase;
			CreateInstrumentTable(SmpsSet, SmpsCfg->GblIns.Len, SmpsCfg->GblIns.Data, SmpsSet->InsBase);
		}
		else
		{
			SmpsSet->InsBase = 0x0000;
			DuplicateInsTable(&SmpsCfg->GblInsLib, &SmpsSet->InsLib);
		}
	}
	else
	{
		SmpsSet->InsLib.Type = INSTYPE_NONE;
		SmpsSet->InsLib.InsCount = 0x00;
		SmpsSet->InsLib.InsPtrs = NULL;
	}
	SmpsSet->UsageCounter = 0x01;
#ifdef ENABLE_LOOP_DETECTION
	SmpsSet->LoopPtrs = (SMPS_LOOPPTR*)malloc(TrkCount * sizeof(SMPS_LOOPPTR));
#endif
	
#ifdef ENABLE_VGM_LOGGING
	// reset DAC usage
	for (TempOfs = 0x00; TempOfs < DACDrv->SmplCount; TempOfs ++)
		DACDrv->Smpls[TempOfs].UsageID = 0xFF;
#endif
	
	// Note: Preparsing the SMPS file is required, because it doesn't only detect
	//       loops and enumerate DAC sounds - it also sets the correct SeqFlags.
	//       (required for Sonic 1/2/3K Fade-In commands and Sonic 3K Continuous SFX)
	SmpsSet->SeqFlags = 0x00;
	
	FileMask = (UINT8*)malloc(FileLen);
	for (CurTrk = 0x00; CurTrk < TrkCount; CurTrk ++)
	{
		CurPos = TrkOfs[CurTrk];
#ifdef ENABLE_LOOP_DETECTION
		SmpsSet->LoopPtrs[CurTrk].Ptr = 0x0000;
#endif
		if (CurPos >= FileLen)
		{
			if (DebugMsgs & 0x04)
			{
				ClearLine();
				printf("Invalid Start Offset %04X for Track %u\n", CurPos, CurTrk);
			}
			continue;
		}
		
		if (CurTrk < FMTrkCnt)
			CurCmd = SmpsCfg->FMChnList[CurTrk];
		else if (CurTrk < FMTrkCnt + PSGTrkCnt)
			CurCmd = SmpsCfg->PSGChnList[CurTrk - FMTrkCnt];
		else
			CurCmd = 0x00;
		IsDacTrk = ((CurCmd & 0xF0) == 0x10) ? 0x01 : 0x00;
		DACBank = 0xFF;
		
		memset(FileMask, 0x00, FileLen);
		memset(LoopExitPtrs, 0x00, sizeof(UINT16) * 0x08);
		memset(StackPtrs, 0x00, sizeof(UINT16) * 0x08);
		StackPos = 0x08;
		TrkMode = PBKFLG_ACTIVE;
		while(CurPos < FileLen && (TrkMode & PBKFLG_ACTIVE))
		{
			FileMask[CurPos] |= 1 << (8 - StackPos);
			if (FileData[CurPos] < CmdList->FlagBase)
			{
				if (TrkMode & PBKFLG_RAWFREQ)
				{
					CurPos += 0x02 + 0x01;	// frequency + delay
				}
				else if (FileData[CurPos] < SmpsCfg->NoteBase)
				{
					CurPos ++;	// delay
				}
				else
				{
#ifdef ENABLE_VGM_LOGGING
					if (IsDacTrk)
					{
						if (! (TrkMode & PBKFLG_SPCMODE))	// if not Phantasy Star IV
							MarkDrumNote(SmpsCfg, DACDrv, &SmpsCfg->DrumLib, FileData[CurPos]);
						else if (TrkMode & PBKFLG_RAWFREQ)	// handle PS4 special mode
							MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos]);
					}
					else if (SmpsCfg->DrumChnMode == DCHNMODE_SMGP2 && CurTrk == 1)
					{
						if (FileData[CurPos] != SmpsCfg->NoteBase)
						{
							//if (FileData[CurPos] + Trk->Transp < 0xB0)
							//	MarkDrum_DACNote(DACDrv, DACBank, 0x86);
							//else
							//	MarkDrum_DACNote(DACDrv, DACBank, 0x87);
							MarkDrum_DACNote(DACDrv, DACBank, 0x86);
							MarkDrum_DACNote(DACDrv, DACBank, 0x87);
						}
					}
#endif
					
					CurPos ++;	// note
					if (TrkMode & PBKFLG_PITCHSLIDE)
						CurPos += 0x01 + 0x01;	// slide speed + delay
					else if (! (FileData[CurPos] & 0x80))
						CurPos ++;	// delay
				}
				continue;
			}
			
			CmdLstCur = CmdList;
			while(FileData[CurPos] >= CmdLstCur->FlagBase)
			{
				CurCmd = FileData[CurPos] - CmdLstCur->FlagBase;
				if (CurCmd >= CmdLstCur->FlagCount)
					break;
				if (CmdLstCur->CmdData[CurCmd].Type != CF_META_CF)
					break;
				CurPos += CmdLstCur->CmdData[CurCmd].Len;
				if (CurPos >= FileLen)
				{
					CurPos --;
					break;
				}
				CmdLstCur = CmdMetaList;
				FileMask[CurPos] |= 0x01;
			}
			CurCmd = FileData[CurPos] - CmdLstCur->FlagBase;
			if (CurCmd >= CmdLstCur->FlagCount)
			{
				if (DebugMsgs & 0x04)
					printf("Unknown Coordination Flag 0x%02X in Track %u at 0x%04X\n",
							CmdLstCur->FlagBase + CurCmd, CurTrk, CurPos);
				CurPos ++;
				continue;
			}
			
			CmdLen = CmdLstCur->CmdData[CurCmd].Len;
			if (CmdLen & 0x80)
			{
				CmdLen &= 0x7F;
				switch(CmdLstCur->CmdData[CurCmd].Type)
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
			switch(CmdLstCur->CmdData[CurCmd].Type)
			{
			case CF_TRK_END:
				TrkMode = 0x00;
				break;
			case CF_COND_JUMP:
				if (! (CmdLstCur->CmdData[CurCmd].SubType & CFS_CJMP_2PTRS))
					break;
				// fall through
			case CF_GOTO:
				OldPos = CurPos;
				TempOfs = CurPos + CmdLstCur->CmdData[CurCmd].JumpOfs;
				CurPos = ReadJumpPtr(&FileData[TempOfs], TempOfs, SmpsSet);
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
#ifdef ENABLE_LOOP_DETECTION
					SmpsSet->LoopPtrs[CurTrk].Ptr = CurPos;
					SmpsSet->LoopPtrs[CurTrk].SrcOfs = OldPos;
#endif
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
				if (LoopExitPtrs[LoopID])
				{
					// a Loop Exit command was found, so process its data now
					OldPos = CurPos;
					CurPos = LoopExitPtrs[LoopID];
					LoopExitPtrs[LoopID] = 0x0000;
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
#ifdef ENABLE_LOOP_DETECTION
						SmpsSet->LoopPtrs[CurTrk].Ptr = CurPos;
						SmpsSet->LoopPtrs[CurTrk].SrcOfs = OldPos;
#endif
						TrkMode = 0x00;
					}
				}
				break;
			case CF_LOOP_EXIT:
				LoopID = FileData[CurPos + 0x01] & 0x07;
				TempOfs = CurPos + CmdLstCur->CmdData[CurCmd].JumpOfs;
				LoopExitPtrs[LoopID] = ReadJumpPtr(&FileData[TempOfs], TempOfs, SmpsSet);
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
				TempOfs = CurPos + CmdLstCur->CmdData[CurCmd].JumpOfs;
				CurPos = ReadJumpPtr(&FileData[TempOfs], TempOfs, SmpsSet);
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
				switch(CmdLstCur->CmdData[CurCmd].SubType)
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
#ifdef ENABLE_VGM_LOGGING
					MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos + 0x01]);
#endif
					break;
				}
				break;
			case CF_CONT_SFX:
				SmpsSet->SeqFlags |= SEQFLG_CONT_SFX;
				break;
			case CF_DAC_BANK:
				DACBank = FileData[CurPos + 0x01];
				break;
			case CF_PLAY_DAC:
#ifdef ENABLE_VGM_LOGGING
				switch(CmdLen)
				{
				case 0x02:
					MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos + 0x01] & 0x7F);
					break;
				case 0x03:	// Zaxxon Motherbase 2000 32X
					DACBank = FileData[CurPos + 0x01];
					MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos + 0x02] & 0x7F);
					break;
				case 0x04:	// Mercs
					MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos + 0x01] & 0x7F);
					break;
				default:
					if (DebugMsgs & 0x04)
						printf("Unknown DAC command (Pos 0x%04X)\n", CurPos);
					break;
				}
#endif
				break;
			case CF_PLAY_PWM:
			case CF_DAC_CYMN:
				if (DebugMsgs & 0x04)
					printf("Special DAC command (Pos 0x%04X)\n", CurPos);
				break;
			case CF_IGNORE:
				if (CmdLstCur->CmdData[CurCmd].JumpOfs && (DebugMsgs & 0x04))
					printf("Unknown Conditional Jump (Pos 0x%04X)\n", CurPos);
				break;
			case CF_INVALID:
				if (DebugMsgs & 0x04)
					printf("Unknown Coordination Flag 0x%02X (Pos 0x%04X)\n",
							CmdLstCur->FlagBase + CurCmd, CurPos);
				break;
			case CF_FADE_IN_SONG:
				if (CmdLstCur->CmdData[CurCmd].Len == 0x01)
				{
					TrkMode = 0x00;	// Sonic 1's Fade In command also terminates the song.
					SmpsSet->SeqFlags |= SEQFLG_NEED_SAVE;
				}
				else if (FileData[CurPos + 0x01] == 0xFF)
				{
					// if not FF, the flag acts like CF_SET_COMM
					SmpsSet->SeqFlags |= SEQFLG_NEED_SAVE;
				}
				break;
			}
			
			CurPos += CmdLen;
		}
	}
	free(FileMask);
	
	return 0x00;
}

#ifdef ENABLE_VGM_LOGGING
static void MarkDrumNote(const SMPS_CFG* SmpsCfg, DAC_CFG* DACDrv, const DRUM_LIB* DrumLib, UINT8 Note)
{
	if (Note < 0x80)
		return;
	
	Note &= 0x7F;
	if (DrumLib->Mode == DRMMODE_NORMAL)
	{
		if (Note < DrumLib->DrumCount)
			MarkDrum_Sub(SmpsCfg, DACDrv, &DrumLib->DrumData[Note]);
	}
	else
	{
		UINT8 TempNote;
		
		TempNote = (Note & DrumLib->Mask1);// >> DrumLib->Shift1;
		if (TempNote && TempNote < DrumLib->DrumCount)
			MarkDrum_Sub(SmpsCfg, DACDrv, &DrumLib->DrumData[TempNote]);
		
		TempNote = (Note & DrumLib->Mask2);// >> DrumLib->Shift2;
		if (TempNote && TempNote < DrumLib->DrumCount)
			MarkDrum_Sub(SmpsCfg, DACDrv, &DrumLib->DrumData[TempNote]);
	}
	
	return;
}

static void MarkDrum_Sub(const SMPS_CFG* SmpsCfg, DAC_CFG* DACDrv, const DRUM_DATA* DrumData)
{
	const DRUM_TRK_LIB* DTrkLib;
	UINT16 DrumID;
	UINT16 SmplID;
	UINT16 DrumOfs;
	
	switch(DrumData->Type)
	{
	case DRMTYPE_DAC:
		if (DrumData->DrumID >= DACDrv->TblCount)
			return;
		DrumID = DrumData->DrumID;
		break;
	case DRMTYPE_FMDAC:
		DTrkLib = &SmpsCfg->FMDrums;
		if (DrumData->DrumID >= DTrkLib->DrumCount)
			return;
		
		DrumOfs = DTrkLib->DrumList[DrumData->DrumID] - DTrkLib->DrumBase;
		DrumID = DTrkLib->File.Data[DrumOfs + 0x05] - 0x01;
		break;
	case DRMTYPE_FM:
		// handle drums played on this track
		MarkDrumTrack(SmpsCfg, DACDrv, DrumData, 0x00);
		return;
	default:
		return;
	}
	if (DrumID >= DACDrv->TblCount)
		return;
	SmplID = DACDrv->SmplTbl[DrumID].Sample;
	if (SmplID >= DACDrv->SmplCount)
		return;
	
	DACDrv->Smpls[SmplID].UsageID = 0xFE;
	
	return;
}

static void MarkDrum_DACNote(DAC_CFG* DACDrv, UINT8 Bank, UINT8 Note)
{
	UINT8 TblID;
	UINT16 SmplID;
	
	Note &= 0x7F;
	if (! Note)
		return;
	
	TblID = Note - 0x01;
	if (Bank < DACDrv->BankCount)
		TblID += DACDrv->BankTbl[Bank];	// do banked sounds
	if (TblID >= DACDrv->TblCount)
		return;
	SmplID = DACDrv->SmplTbl[TblID].Sample;
	if (SmplID >= DACDrv->SmplCount)
		return;
	
	DACDrv->Smpls[SmplID].UsageID = 0xFE;
	
	return;
}

static void MarkDrumTrack(const SMPS_CFG* SmpsCfg, DAC_CFG* DACDrv, const DRUM_DATA* DrumData, UINT8 Mode)
{
	const UINT8* FileData;
	const DRUM_TRK_LIB* DTrkLib;
	const UINT8* DTrkData;
	SMPS_SET DTrkSet;
	UINT16 DrumOfs;
	UINT16 CurPos;
	UINT16 TempOfs;
	const CMD_LIB* CmdList;
	const CMD_LIB* CmdMetaList;
	const CMD_LIB* CmdLstCur;
	UINT8 TrkMode;
	UINT8 CurCmd;
	UINT8 CmdLen;
	UINT8 DACBank;
	
	DTrkLib = Mode ? &SmpsCfg->PSGDrums : &SmpsCfg->FMDrums;
	if (DrumData->DrumID >= DTrkLib->DrumCount)
		return;
	
	CmdList = &SmpsCfg->CmdList;
	CmdMetaList = &SmpsCfg->CmdMetaList;
	DrumOfs = DTrkLib->DrumList[DrumData->DrumID] - DTrkLib->DrumBase;
	DTrkData = &DTrkLib->File.Data[DrumOfs];
	DTrkSet.Cfg = SmpsCfg;
	DTrkSet.SeqBase = DTrkLib->DrumBase;
	DTrkSet.Seq = DTrkLib->File;
	FileData = DTrkLib->File.Data;
	CurPos = ReadPtr(&DTrkData[0x00], &DTrkSet);
	
	DACBank = 0xFF;
	TrkMode = PBKFLG_ACTIVE;
	while(CurPos < DTrkLib->File.Len && (TrkMode & PBKFLG_ACTIVE))
	{
		if (FileData[CurPos] < CmdList->FlagBase)
		{
			if (TrkMode & PBKFLG_RAWFREQ)
			{
				CurPos += 0x02 + 0x01;	// frequency + delay
			}
			else if (FileData[CurPos] & 0x80)
			{
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
		
		CmdLstCur = CmdList;
		while(FileData[CurPos] >= CmdLstCur->FlagBase)
		{
			CurCmd = FileData[CurPos] - CmdLstCur->FlagBase;
			if (CurCmd >= CmdLstCur->FlagCount)
				break;
			if (CmdLstCur->CmdData[CurCmd].Type != CF_META_CF)
				break;
			CurPos += CmdLstCur->CmdData[CurCmd].Len;
			if (CurPos >= DTrkLib->File.Len)
			{
				CurPos --;
				break;
			}
			CmdLstCur = CmdMetaList;
		}
		CurCmd = FileData[CurPos] - CmdLstCur->FlagBase;
		if (CurCmd >= CmdLstCur->FlagCount)
		{
			CurPos ++;
			continue;
		}
		
		CmdLen = CmdLstCur->CmdData[CurCmd].Len;
		if (CmdLen & 0x80)
		{
			CmdLen &= 0x7F;
			switch(CmdLstCur->CmdData[CurCmd].Type)
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
		switch(CmdLstCur->CmdData[CurCmd].Type)
		{
		case CF_TRK_END:
		case CF_RETURN:
			TrkMode = 0x00;
			break;
		case CF_COND_JUMP:
			if (! (CmdLstCur->CmdData[CurCmd].SubType & CFS_CJMP_2PTRS))
				break;
			// fall through
		case CF_GOTO:
			TempOfs = CurPos + CmdLstCur->CmdData[CurCmd].JumpOfs;
			CurPos = ReadJumpPtr(&FileData[TempOfs], TempOfs, &DTrkSet);
			if (CurPos >= DTrkLib->File.Len)
				return;
			CmdLen = 0x00;
			break;
		case CF_LOOP:
			break;
		case CF_LOOP_EXIT:
			//TempOfs = CurPos + CmdLstCur->CmdData[CurCmd].JumpOfs;
			//CurPos = ReadJumpPtr(&FileData[TempOfs], TempOfs, &DTrkSet);
			//CmdLen = 0x00;
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
		case CF_DAC_BANK:
			DACBank = FileData[CurPos + 0x01];
			break;
		case CF_PLAY_DAC:
			switch(CmdLen)
			{
			case 0x02:
				MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos + 0x01] & 0x7F);
				break;
			case 0x03:	// Zaxxon Motherbase 2000 32X
				DACBank = FileData[CurPos + 0x01];
				MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos + 0x02] & 0x7F);
				break;
			case 0x04:	// Mercs
				MarkDrum_DACNote(DACDrv, DACBank, FileData[CurPos + 0x01] & 0x7F);
				break;
			default:
				if (DebugMsgs & 0x04)
					printf("Unknown DAC command (Pos 0x%04X)\n", CurPos);
				break;
			}
			break;
		case CF_PLAY_PWM:
		case CF_DAC_CYMN:
			if (DebugMsgs & 0x04)
				printf("Special DAC command (Pos 0x%04X)\n", CurPos);
			break;
		}
		
		CurPos += CmdLen;
	}
	
	return;
}
#endif	// ENABLE_VGM_LOGGING

void FreeSMPSFile(SMPS_SET* SmpsSet)
{
	if (SmpsSet == NULL)
		return;
	
	if (SmpsSet->UsageCounter)
	{
		SmpsSet->UsageCounter --;
		if (SmpsSet->UsageCounter)
			return;
	}
	
	FreeFileData(&SmpsSet->Seq);
	
	SmpsSet->InsLib.InsCount = 0x00;
	free(SmpsSet->InsLib.InsPtrs);
	SmpsSet->InsLib.InsPtrs = NULL;
	
#ifdef ENABLE_LOOP_DETECTION
	if (SmpsSet->LoopPtrs != NULL)
	{
		free(SmpsSet->LoopPtrs);
		SmpsSet->LoopPtrs = NULL;
	}
#endif
	
	return;
}
