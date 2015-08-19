// Data File Loader
// ----------------
// Written by Valley Bell, 2014-2015
#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>	// for isspace()
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include <common_def.h>
#ifndef DISABLE_DLOAD_FILE
#include "ini_lib.h"
#endif
#include "Engine/smps_structs.h"
#include "Engine/dac.h"
#include "loader_data.h"
#include "loader_smps.h"	// for SmpsOffsetFromFilename()

#ifdef _MSC_VER

#ifndef strdup	// crtdbg.h redefines strdup, usually it throws a warning
#define strdup		_strdup
#endif
#define stricmp		_stricmp

#else
#define stricmp		strcasecmp
#endif


static const UINT8 DefDPCMData[0x10] =
{	0x00, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40,
	0x80, 0xFF, 0xFE, 0xFC, 0xF8, 0xF0, 0xE0, 0xC0};

static const char* SIG_ENV = "LST_ENV";
static const char* SIG_DRUM = "SDRM";
static const char* SIG_PANI = "SPAN";
static const char* SIG_INS = "SINS";

typedef struct _dac_new_smpl
{
	UINT8 Compr;
	UINT8 Pan;
	UINT8 Flags;
	UINT8 Algo;
	UINT32 Rate;
	UINT8* DPCMData;
} DAC_NSMPL;


// Function Prototypes
#ifndef DISABLE_DLOAD_FILE
//void LoadDACData(const char* FileName, DAC_CFG* DACDrv);
static void LoadDACSample(DAC_CFG* DACDrv, UINT16 DACSnd, const char* FileName, const DAC_NSMPL* SmplData);
static UINT8 GetDACAlgoID(const DAC_SETTINGS* DACCfg, const DAC_NSMPL* SmplData);
#endif
//void FreeDACData(DAC_CFG* DACDrv);

#ifndef DISABLE_DLOAD_FILE
static UINT8 LoadFileData(const char* FileName, UINT32* RetFileLen, UINT8** RetFileData,
						  UINT16 MinSize, UINT16 SigLen, const void* SigStr);
#endif
//UINT8 LoadEnvelopeData_File(const char* FileName, ENV_LIB* EnvLib);
//UINT8 LoadEnvelopeData_Mem(UINT32 FileLen, const UINT8* FileData, ENV_LIB* EnvLib);
//void FreeEnvelopeData(ENV_LIB* EnvLib);
//UINT8 LoadDrumTracks_File(const char* FileName, DRUM_TRK_LIB* DrumLib, UINT8 DrumMode);
//UINT8 LoadDrumTracks_Mem(UINT32 FileLen, const UINT8* FileData, DRUM_TRK_LIB* DrumLib, UINT8 DrumMode);
//UINT8 FreeDrumTracks(DRUM_TRK_LIB* DrumLib)
//UINT8 LoadPanAniData_File(const char* FileName, PAN_ANI_LIB* PAniLib);
//UINT8 LoadPanAniData_Mem(UINT32 FileLen, const UINT8* FileData, PAN_ANI_LIB* PAniLib);
//void FreePanAniData(PAN_ANI_LIB* PAniLib);
//UINT8 LoadGlobalInstrumentLib_File(const char* FileName, SMPS_CFG* SmpsCfg);
//UINT8 LoadGlobalInstrumentLib_Mem(UINT32 FileLen, UINT8* FileData, SMPS_CFG* SmpsCfg);
static UINT8 LoadSimpleInstrumentLib(UINT32 FileLen, UINT8* FileData, SMPS_CFG* SmpsCfg);
static UINT8 LoadAdvancedInstrumentLib(UINT32 FileLen, UINT8* FileData, SMPS_CFG* SmpsCfg);
//void FreeGlobalInstrumentLib(SMPS_CFG* SmpsCfg);
//void FreeFileData(FILE_DATA* File);
INLINE UINT16 ReadLE16(const UINT8* Data);
INLINE UINT16 ReadBE16(const UINT8* Data);
INLINE UINT16 ReadPtr16(const UINT8* Data, const UINT8 Flags);


// ---- DAC Files ---
#ifndef DISABLE_DLOAD_FILE
void LoadDACData(const char* FileName, DAC_CFG* DACDrv)
{
	FILE* hFile;
	char BasePath[0x100];
	char LineStr[0x100];
	char* LToken;
	char* RToken1;
	char* RToken2;
	UINT8 IniSection;
	UINT8 RetVal;
	UINT32 ArrSize;
	UINT8* ArrPtr;
	
	char DACFile[0x100];
	UINT8 CurAlgoID;
	UINT16 DrumIDBase;
	UINT16 CurDrumID;
	DAC_NSMPL DSmpl;
	DAC_ALGO* DAlgo;
	
	DACDrv->SmplAlloc = 0x100;
	DACDrv->SmplCount = 0x00;
	DACDrv->Smpls = (DAC_SAMPLE*)malloc(DACDrv->SmplAlloc * sizeof(DAC_SAMPLE));
	
	DACDrv->TblAlloc = 0x100;
	DACDrv->TblCount = 0x00;
	DACDrv->SmplTbl = (DAC_TABLE*)malloc(DACDrv->TblAlloc * sizeof(DAC_TABLE));
	memset(DACDrv->SmplTbl, 0x00, DACDrv->TblAlloc * sizeof(DAC_TABLE));
	
	DACDrv->BankAlloc = 0x00;
	DACDrv->BankCount = 0x00;
	DACDrv->BankTbl = NULL;
	
	DACDrv->Cfg.AlgoAlloc = 0x10;
	DACDrv->Cfg.AlgoCount = 0x01;
	DACDrv->Cfg.Algos = (DAC_ALGO*)malloc(DACDrv->Cfg.AlgoAlloc * sizeof(DAC_ALGO));
	memset(DACDrv->Cfg.Algos, 0x00, DACDrv->Cfg.AlgoAlloc * sizeof(DAC_ALGO));
	
	
	DAlgo = &DACDrv->Cfg.Algos[0x00];
	DAlgo->BaseRate = 272624;
	DAlgo->Divider = 796;
	DAlgo->RateMode = DACRM_DELAY;
	DAlgo->DefCompr = 0xFF;
	DACDrv->Cfg.SmplMode = DACSM_NORMAL;
	DACDrv->Cfg.Channels = 1;
	DACDrv->Cfg.VolDiv = 1;
	DSmpl.DPCMData = NULL;
	
	strcpy(BasePath, FileName);
	StandardizePath(BasePath);
	RToken1 = GetFileTitle(BasePath);
	*RToken1 = '\0';	// cut file title
	
	hFile = fopen(FileName, "rt");
	if (hFile == NULL)
	{
		printf("Error opening %s\n", FileName);
		return;
	}
	
	IniSection = 0x01;
	CurAlgoID = 0x00;
	DAlgo = &DACDrv->Cfg.Algos[CurAlgoID];
	DAlgo->DefCompr = 0xFF;	// by default, use this algorithm for all compression types
	DSmpl.Algo = 0xFF;
	DrumIDBase = 0x81;
	
	CurDrumID = 0x00;
	while(! feof(hFile))
	{
		RetVal = GetTextLine(0x100, LineStr, hFile);
		if (RetVal)
			break;
		RetVal = GetTokenPtrs(LineStr, &LToken, &RToken1);
		if (RetVal)
			continue;
		
		if (*LToken == '[')
		{
			if (IniSection & 0x80)
				LoadDACSample(DACDrv, CurDrumID, DACFile, &DSmpl);
			
			if (RToken1[0] == '$')
			{
				CurAlgoID = (UINT8)strtoul(RToken1 + 1, &RToken2, 0x10);
				if (CurAlgoID == DACDrv->Cfg.AlgoCount)
				{
					DACDrv->Cfg.AlgoCount = CurAlgoID + 1;
				}
				else if (CurAlgoID > DACDrv->Cfg.AlgoCount)
				{
					printf("Warning: Ignoring DAC algorithm %s! (must be defined consecutively)\n");
					RToken2 = RToken1;
				}
				
				if (RToken2 == RToken1 || CurAlgoID >= DACDrv->Cfg.AlgoAlloc)
				{
					IniSection = 0x00;	// invalid algorithm ID - ignore section
				}
				else
				{
					DAlgo = &DACDrv->Cfg.Algos[CurAlgoID];
					IniSection = 0x01;
				}
			}
			else if (! stricmp(RToken1, "Banks"))
			{
				IniSection = 0x02;
			}
			else
			{
				CurDrumID = (UINT8)strtoul(RToken1, &RToken2, 0x10);
				if (CurDrumID == 0 && ! (IniSection & 0x80) && DrumIDBase == 0x81)
					DrumIDBase = CurDrumID;	// make NECPCM.ini work without setting DrumIDBase
				if (RToken2 == RToken1 || CurDrumID < DrumIDBase)
				{
					IniSection = 0x00;
				}
				else
				{
					CurDrumID -= DrumIDBase;
					IniSection = 0x80;
					
					strcpy(DACFile, "");
					DSmpl.Compr = 0x00;
					DSmpl.Rate = 0x00;
					DSmpl.Pan = 0x00;
					DSmpl.Flags = 0x00;
				}
			}
			continue;
		}
		
		if (! IniSection)	// ignore all invalid sections
			continue;
		
		// "DPCMData" is valid in all sections
		if (! stricmp(LToken, "DPCMData"))
		{
			//RevertTokenTrim(RToken1, RToken2);
			ArrSize = ReadHexData(RToken1, &ArrPtr);
			// DPCM compression is nibble-based, so 10h values are necessary
			if (ArrSize >= 0x10)
			{
				if (DSmpl.DPCMData != NULL)
					free(DSmpl.DPCMData);
				DSmpl.DPCMData = ArrPtr;
			}
			else
			{
				free(ArrPtr);
			}
			continue;
		}
		
		RToken2 = TrimToken(RToken1);
		if (IniSection == 0x01)
		{
			if (! stricmp(LToken, "BaseRate"))
				DAlgo->BaseRate = (UINT32)strtoul(RToken1, NULL, 0);
			else if (! stricmp(LToken, "RateDiv"))
				DAlgo->Divider = (UINT32)(strtod(RToken1, NULL) * 100 + 0.5);
			else if (! stricmp(LToken, "BaseCycles"))
				DAlgo->BaseCycles = (UINT32)strtoul(RToken1, NULL, 0);
			else if (! stricmp(LToken, "LoopCycles"))
				DAlgo->LoopCycles = (UINT32)strtoul(RToken1, NULL, 0);
			else if (! stricmp(LToken, "LoopSamples"))
				DAlgo->LoopSamples = (UINT32)strtoul(RToken1, NULL, 0);
			else if (! stricmp(LToken, "RateMode"))
				DAlgo->RateMode = (UINT8)strtoul(RToken1, NULL, 0);
			else if (! stricmp(LToken, "RateOverflow"))
				DAlgo->RateOverflow = (UINT32)(strtol(RToken1, NULL, 0));
			else if (! stricmp(LToken, "DefCompr"))
			{
				if (! stricmp(RToken1, "All"))
					DAlgo->DefCompr = 0xFF;
				else if (! stricmp(RToken1, "PCM"))
					DAlgo->DefCompr = COMPR_PCM;
				else if (! stricmp(RToken1, "DPCM"))
					DAlgo->DefCompr = COMPR_DPCM;
				else
					DAlgo->DefCompr = (UINT8)strtoul(RToken1, NULL, 0);
			}
			else if (! stricmp(LToken, "ResampleMode"))
				DACDrv->Cfg.SmplMode = (UINT8)strtoul(RToken1, NULL, 0);
			else if (! stricmp(LToken, "DrumIDBase"))
				DrumIDBase = (UINT16)strtoul(RToken1, NULL, 0x10);
		}
		else if (IniSection == 0x02)
		{
			CurDrumID = (UINT8)strtoul(LToken, &RToken2, 0x10);
			if (DACDrv->BankAlloc <= CurDrumID)
			{
				UINT16 BaseIdx;
				
				BaseIdx = DACDrv->BankAlloc;
				DACDrv->BankAlloc = CurDrumID + 1;
				DACDrv->BankAlloc = (DACDrv->BankAlloc + 0x0F) & ~0x0F;	// round up to 10h
				DACDrv->BankTbl = (UINT16*)realloc(DACDrv->BankTbl, sizeof(UINT16) * DACDrv->BankAlloc);
				memset(DACDrv->BankTbl + BaseIdx, 0x00, sizeof(UINT16) * (DACDrv->BankAlloc - BaseIdx));
			}
			if (CurDrumID < DACDrv->BankAlloc)
			{
				if (DACDrv->BankCount <= CurDrumID)
					DACDrv->BankCount = CurDrumID + 1;
				DACDrv->BankTbl[CurDrumID] = (UINT8)strtoul(RToken1, &RToken2, 0x10) - DrumIDBase;
			}
		}
		else
		{
			if (! stricmp(LToken, "File"))
			{
				RevertTokenTrim(RToken1, RToken2);
				strcpy(DACFile, BasePath);
				strcat(DACFile, RToken1);
				StandardizePath(DACFile);
			}
			else if (! stricmp(LToken, "Compr"))
			{
				// "False" and "True" are for backwards compatibility with
				// older DAC.ini files.
				if (! stricmp(RToken1, "PCM") || ! stricmp(RToken1, "False"))
					DSmpl.Compr = COMPR_PCM;
				else if (! stricmp(RToken1, "DPCM") || ! stricmp(RToken1, "True"))
					DSmpl.Compr = COMPR_DPCM;
				else
					DSmpl.Compr = strtol(RToken1, NULL, 0) ? 0x01 : 0x00;
			}
			else if (! stricmp(LToken, "Rate"))
				DSmpl.Rate = (UINT32)strtol(RToken1, NULL, 0);
			else if (! stricmp(LToken, "Pan"))
				DSmpl.Pan = (UINT8)strtol(RToken1, NULL, 0);
			else if (! stricmp(LToken, "Looping"))
			{
				DSmpl.Flags &= ~DACFLAG_LOOP;
				DSmpl.Flags |= GetBoolValue(RToken1, "True", "False") << 0;
			}
			else if (! stricmp(LToken, "Reverse"))
			{
				DSmpl.Flags &= ~DACFLAG_REVERSE;
				DSmpl.Flags |= GetBoolValue(RToken1, "True", "False") << 1;
			}
		}
	}
	if (IniSection & 0x80)
		LoadDACSample(DACDrv, CurDrumID, DACFile, &DSmpl);
	for (CurAlgoID = 0x00; CurAlgoID < DACDrv->Cfg.AlgoCount; CurAlgoID ++)
	{
		DAlgo = &DACDrv->Cfg.Algos[CurAlgoID];
		if (DAlgo->RateOverflow == 0x00 && DAlgo->RateMode != DACRM_DELAY)
			DAlgo->RateOverflow = 0x100;
	}
	
	if (DSmpl.DPCMData != NULL)
		free(DSmpl.DPCMData);
	
	fclose(hFile);
	
	return;
}

static void LoadDACSample(DAC_CFG* DACDrv, UINT16 DACSnd, const char* FileName, const DAC_NSMPL* SmplData)
{
	UINT16 CurSmpl;
	DAC_SAMPLE* TempSmpl;
	DAC_TABLE* TempTbl;
	size_t TempInt;
	FILE* hFile;
	
	if (*FileName == '\0')
		return;
	if (DACSnd >= DACDrv->TblAlloc)
		return;
	
	while(DACDrv->TblCount <= DACSnd)
	{
		DACDrv->SmplTbl[DACDrv->TblCount].Sample = 0xFFFF;
		DACDrv->TblCount ++;
	}
	
	TempTbl = &DACDrv->SmplTbl[DACSnd];
	TempTbl->Rate = SmplData->Rate;
	TempTbl->Pan = SmplData->Pan;
	TempTbl->Flags = SmplData->Flags;
	TempTbl->Algo = GetDACAlgoID(&DACDrv->Cfg, SmplData);
	
	if (TempTbl->Sample != 0xFFFF)
	{
		TempSmpl = &DACDrv->Smpls[TempTbl->Sample];
	}
	else
	{
		for (CurSmpl = 0x00; CurSmpl < DACDrv->SmplCount; CurSmpl ++)
		{
			if (! stricmp(FileName, DACDrv->Smpls[CurSmpl].File))
				break;
		}
		if (CurSmpl >= DACDrv->SmplAlloc)
		{
			TempTbl->Sample = 0xFFFF;
			return;
		}
		TempSmpl = &DACDrv->Smpls[CurSmpl];
		if (CurSmpl >= DACDrv->SmplCount)
		{
			DACDrv->SmplCount ++;
			TempSmpl->File = NULL;
		}
		
		TempTbl->Sample = CurSmpl;
	}
	
	if (TempSmpl->File != NULL && ! stricmp(FileName, TempSmpl->File))
		return;	// already loaded
	
	if (TempSmpl->File != NULL)
		free(TempSmpl->File);
	TempInt = strlen(FileName) + 1;
	TempSmpl->File = strdup(FileName);
	TempSmpl->Compr = SmplData->Compr;
	if (SmplData->DPCMData == NULL)
	{
		TempSmpl->DPCMArr = (UINT8*)DefDPCMData;
	}
	else
	{
		TempSmpl->DPCMArr = (UINT8*)malloc(0x10);	// make a copy so that free() works properly later
		memcpy(TempSmpl->DPCMArr, SmplData->DPCMData, 0x10);
	}
	
	hFile = fopen(TempSmpl->File, "rb");
	if (hFile == NULL)
	{
		printf("Error opening %s\n", TempSmpl->File);
		
		free(TempSmpl->File);	TempSmpl->File = NULL;
		TempTbl->Sample = 0xFFFF;
		
		if (CurSmpl == (DACDrv->SmplCount - 1))
			DACDrv->SmplCount --;
		return;
	}
	
	fseek(hFile, 0x00, SEEK_END);
	TempInt = ftell(hFile);
	if (TempInt > 0x100000)	// 1 MB samples should be enough
		TempInt = 0x100000;
	TempSmpl->Size = (UINT32)TempInt;
	
	fseek(hFile, 0x00, SEEK_SET);
	TempSmpl->Data = (UINT8*)malloc(TempSmpl->Size);
	TempSmpl->Size = (UINT32)fread(TempSmpl->Data, 0x01, TempSmpl->Size, hFile);
	
	fclose(hFile);
	
	return;
}

static UINT8 GetDACAlgoID(const DAC_SETTINGS* DACCfg, const DAC_NSMPL* SmplData)
{
	UINT8 CurAlgo;
	
	if (SmplData->Algo < 0xFF)
		return SmplData->Algo;
	
	for (CurAlgo = 0x00; CurAlgo < DACCfg->AlgoCount; CurAlgo ++)
	{
		if (DACCfg->Algos[CurAlgo].DefCompr == SmplData->Compr)
			return CurAlgo;
		else if (DACCfg->Algos[CurAlgo].DefCompr == 0xFF)	// "all" compression types
			return CurAlgo;
	}
	return 0x00;
}
#endif	// DISABLE_DLOAD_FILE

void FreeDACData(DAC_CFG* DACDrv)
{
	UINT8 CurSmpl;
	DAC_SAMPLE* TempSmpl;
	
	DAC_Reset();
	SetDACDriver(NULL);
	
	for (CurSmpl = 0x00; CurSmpl < DACDrv->SmplCount; CurSmpl ++)
	{
		TempSmpl = &DACDrv->Smpls[CurSmpl];
		free(TempSmpl->File);	TempSmpl->File = NULL;
		TempSmpl->Size = 0;
		free(TempSmpl->Data);	TempSmpl->Data = NULL;
		
		if (TempSmpl->DPCMArr != DefDPCMData)
			free(TempSmpl->DPCMArr);
		TempSmpl->DPCMArr = NULL;
	}
	DACDrv->SmplCount = 0x00;
	DACDrv->TblCount = 0x00;
	DACDrv->BankCount = 0x00;
	DACDrv->Cfg.AlgoCount = 0x00;
	
	DACDrv->SmplAlloc = 0x00;
	free(DACDrv->Smpls);		DACDrv->Smpls = NULL;
	DACDrv->TblAlloc = 0x00;
	free(DACDrv->SmplTbl);		DACDrv->SmplTbl = NULL;
	DACDrv->BankAlloc = 0x00;
	free(DACDrv->BankTbl);		DACDrv->BankTbl = NULL;
	DACDrv->Cfg.AlgoAlloc = 0x00;
	free(DACDrv->Cfg.Algos);	DACDrv->Cfg.Algos = NULL;
	
	return;
}


#ifndef DISABLE_DLOAD_FILE
static UINT8 LoadFileData(const char* FileName, UINT32* RetFileLen, UINT8** RetFileData,
						  UINT16 MinSize, UINT16 SigLen, const void* SigStr)
{
	FILE* hFile;
	size_t FileLen;
	UINT8* FileData;
	UINT8 FileHdr[0x10];
	size_t ReadBytes;
	
	if (MinSize > 0x10 || SigLen > MinSize)
		return 0xCC;	// invalid usage
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
		return 0xFF;
	
	fseek(hFile, 0x00, SEEK_END);
	FileLen = ftell(hFile);
	
	fseek(hFile, 0x00, SEEK_SET);
	ReadBytes = fread(FileHdr, 0x01, MinSize, hFile);
	if (SigStr != NULL)
	{
		if (ReadBytes < (size_t)SigLen || memcmp(FileHdr, SigStr, SigLen))
		{
			fclose(hFile);
			return 0x80;	// invalid file signature
		}
	}
	if (ReadBytes < (size_t)MinSize)
	{
		fclose(hFile);
		return 0x81;	// file too small
	}
	
	FileData = (UINT8*)malloc(FileLen);
	memcpy(&FileData[0x00], FileHdr, ReadBytes);
	FileLen = ReadBytes + fread(&FileData[ReadBytes], 0x01, FileLen - ReadBytes, hFile);
	
	fclose(hFile);
	
	*RetFileLen = (UINT32)FileLen;
	*RetFileData = FileData;
	return 0x00;
}

// ---- Envelope Files ----
UINT8 LoadEnvelopeData_File(const char* FileName, ENV_LIB* EnvLib)
{
	UINT32 FileLen;
	UINT8* FileData;
	UINT8 RetVal;
	
	RetVal = LoadFileData(FileName, &FileLen, &FileData, 0x08, 0x07, SIG_ENV);
	if (RetVal)
		return RetVal;
	
	RetVal = LoadEnvelopeData_Mem(FileLen, FileData, EnvLib);
	free(FileData);
	
	return RetVal;
}
#endif	// DISABLE_DLOAD_FILE

UINT8 LoadEnvelopeData_Mem(UINT32 FileLen, const UINT8* FileData, ENV_LIB* EnvLib)
{
	UINT8 CurEnv;
	UINT32 NameLen;
	UINT32 CurPos;
	ENV_DATA* TempEnv;
	
	if (memcmp(&FileData[0x00], SIG_ENV, 0x07))
		return 0x80;	// invalid file
	
	if (FileLen < 0x08)
		return 0x80;
	EnvLib->EnvCount = FileData[0x07];
	EnvLib->EnvData = (ENV_DATA*)malloc(EnvLib->EnvCount * sizeof(ENV_DATA));
	CurPos = 0x08;
	for (CurEnv = 0x00; CurEnv < EnvLib->EnvCount; CurEnv ++)
	{
		TempEnv = &EnvLib->EnvData[CurEnv];
		if (CurPos < FileLen)
			NameLen = FileData[CurPos];
		else
			NameLen = 0;
		CurPos ++;
		//TempEnv->Name = (char*)malloc(NameLen + 1);
		//fread(TempEnv->Name, 0x01, NameLen, hFile);
		//TempEnv->Name[NameLen] = '\0';
		CurPos += NameLen;
		
		if (CurPos >= FileLen)
		{
			EnvLib->EnvCount = CurEnv;
			return 0x01;
		}
		TempEnv->alloc = 0x00;
		TempEnv->Len = FileData[CurPos];
		CurPos ++;
		TempEnv->Data = (UINT8*)malloc(TempEnv->Len);
		memcpy(TempEnv->Data, &FileData[CurPos], TempEnv->Len);
		CurPos += TempEnv->Len;
	}
	
	return 0x00;
}

void FreeEnvelopeData(ENV_LIB* EnvLib)
{
	UINT8 CurEnv;
	ENV_DATA* TempEnv;
	
	if (EnvLib->EnvData == NULL)
		return;
	
	for (CurEnv = 0x00; CurEnv < EnvLib->EnvCount; CurEnv ++)
	{
		TempEnv = &EnvLib->EnvData[CurEnv];
		//if (TempEnv->Name !=NULL)
		//	free(TempEnv->Name);
		if (TempEnv->Data != NULL);
			free(TempEnv->Data);
	}
	EnvLib->EnvCount = 0x00;
	free(EnvLib->EnvData);	EnvLib->EnvData = NULL;
	
	return;
}


// ---- FM/PSG SMPS Drum Tracks ----
#ifndef DISABLE_DLOAD_FILE
UINT8 LoadDrumTracks_File(const char* FileName, DRUM_TRK_LIB* DrumLib, UINT8 DrumMode)
{
	UINT32 FileLen;
	UINT8* FileData;
	UINT8 RetVal;
	
	RetVal = LoadFileData(FileName, &FileLen, &FileData, 0x05, 0x04, SIG_DRUM);
	if (RetVal)
		return RetVal;
	
	RetVal = LoadDrumTracks_Mem(FileLen, FileData, DrumLib, DrumMode);
	free(FileData);
	
	return RetVal;
}
#endif	// DISABLE_DLOAD_FILE

UINT8 LoadDrumTracks_Mem(UINT32 FileLen, const UINT8* FileData, DRUM_TRK_LIB* DrumLib, UINT8 DrumMode)
{
	const UINT8* FileHdr;
	UINT8 Flags;
	UINT8 InsCount;
	UINT16 DrumOfs;
	UINT16 InsOfs;
	UINT16 InsBaseOfs;
	UINT16 BaseOfs;
	UINT8 CurItm;
	UINT16 CurPos;
	UINT16 TempOfs;
	
	FileHdr = &FileData[0x00];
	if (FileLen < 0x05 || memcmp(FileHdr, SIG_DRUM, 0x04))
		return 0x80;	// invalid file
	
	Flags = FileHdr[0x04];
	if ((Flags & 0x0F) != DrumMode)
		return 0xC0;	// wrong drum mode
	switch(DrumMode)
	{
	case 0x00:	// PSG drums (without Instrument lib.)
		if (FileLen < 0x0A)
			return 0x80;	// invalid file
		DrumLib->DrumCount = FileHdr[0x05];
		DrumOfs = ReadPtr16(&FileHdr[0x06], Flags);
		DrumLib->DrumBase = ReadPtr16(&FileHdr[0x08], Flags);
		DrumLib->TickMult = (DrumOfs <= 0x0A) ? 0 : FileHdr[0x0A];
		InsCount = 0x00;
		InsOfs = 0x0000;
		break;
	case 0x01:	// FM drums (with Instrument lib.)
		if (FileLen < 0x10)
			return 0x80;	// invalid file
		DrumLib->DrumCount = FileHdr[0x05];
		DrumOfs = ReadPtr16(&FileHdr[0x06], Flags);
		DrumLib->DrumBase = ReadPtr16(&FileHdr[0x08], Flags);
		DrumLib->TickMult = FileHdr[0x0A];
		InsCount = FileHdr[0x0B];
		InsOfs = ReadPtr16(&FileHdr[0x0C], Flags);
		InsBaseOfs = ReadPtr16(&FileHdr[0x0E], Flags);
		break;
	}
	if (! DrumOfs)
		return 0xC1;	// invalid drum offset
	
	if (! (Flags & 0x10))
		DrumLib->SmpsPtrFmt = PTRFMT_Z80;
	else if (! (Flags & 0x20))
		DrumLib->SmpsPtrFmt = PTRFMT_68K;
	else
		DrumLib->SmpsPtrFmt = PTRFMT_RST;
	
	BaseOfs = DrumOfs;
	if (InsOfs && InsOfs < BaseOfs)
		BaseOfs = InsOfs;
	
	DrumLib->File.alloc = 0x01;
	DrumLib->File.Len = FileLen - BaseOfs;
	DrumLib->File.Data = (UINT8*)malloc(DrumLib->File.Len);
	memcpy(DrumLib->File.Data, &FileData[BaseOfs], DrumLib->File.Len);
	
	DrumLib->DrumList = (UINT16*)malloc(DrumLib->DrumCount * sizeof(UINT16));
	CurPos = DrumOfs - BaseOfs;
	DrumLib->DrumBase -= CurPos;
	if (! (Flags & 0x10))
	{
		for (CurItm = 0x00; CurItm < DrumLib->DrumCount; CurItm ++, CurPos += 0x02)
			DrumLib->DrumList[CurItm] = ReadLE16(&DrumLib->File.Data[CurPos]);
	}
	else
	{
		for (CurItm = 0x00; CurItm < DrumLib->DrumCount; CurItm ++, CurPos += 0x02)
			DrumLib->DrumList[CurItm] = ReadBE16(&DrumLib->File.Data[CurPos]);
	}
	
	DrumLib->InsLib.InsCount = InsCount;
	DrumLib->InsLib.InsPtrs = NULL;
	if (InsCount)
	{
		DrumLib->InsLib.InsPtrs = (UINT8**)malloc(InsCount * sizeof(UINT8*));
		CurPos = InsOfs - BaseOfs;
		InsBaseOfs -= CurPos;
		for (CurItm = 0x00; CurItm < InsCount; CurItm ++, CurPos += 0x02)
		{
			TempOfs = ReadPtr16(&DrumLib->File.Data[CurPos], Flags) - InsBaseOfs;
			if (TempOfs >= BaseOfs && TempOfs < DrumLib->File.Len - 0x10)
				DrumLib->InsLib.InsPtrs[CurItm] = &DrumLib->File.Data[TempOfs];
			else
				DrumLib->InsLib.InsPtrs[CurItm] = NULL;
		}
	}
	
	return 0x00;
}

void FreeDrumTracks(DRUM_TRK_LIB* DrumLib)
{
	if (DrumLib->DrumList == NULL)
		return;
	
	DrumLib->DrumCount = 0x00;
	free(DrumLib->DrumList);
	DrumLib->DrumList = NULL;
	
	if (DrumLib->InsLib.InsPtrs != NULL)
	{
		DrumLib->InsLib.InsCount = 0x00;
		free(DrumLib->InsLib.InsPtrs);
		DrumLib->InsLib.InsPtrs = NULL;
	}
	
	FreeFileData(&DrumLib->File);
	
	return;
}


// ---- Pan Animation Data ----
#ifndef DISABLE_DLOAD_FILE
UINT8 LoadPanAniData_File(const char* FileName, PAN_ANI_LIB* PAniLib)
{
	UINT32 FileLen;
	UINT8* FileData;
	UINT8 RetVal;
	
	RetVal = LoadFileData(FileName, &FileLen, &FileData, 0x05, 0x04, SIG_PANI);
	if (RetVal)
		return RetVal;
	
	RetVal = LoadPanAniData_Mem(FileLen, FileData, PAniLib);
	free(FileData);
	
	return RetVal;
}
#endif	// DISABLE_DLOAD_FILE

UINT8 LoadPanAniData_Mem(UINT32 FileLen, const UINT8* FileData, PAN_ANI_LIB* PAniLib)
{
	const UINT8* FileHdr;
	UINT16 AniOfs;
	UINT8 Flags;
	UINT8 CurItm;
	UINT16 CurPos;
	UINT16 PtrSize;
	
	FileHdr = &FileData[0x00];
	if (FileLen < 0x05 || memcmp(FileData, SIG_PANI, 0x04))
		return 0x80;	// invalid file
	
	Flags = FileHdr[0x04];
	if ((Flags & ~0x30) != 0x00)
		return 0xC0;	// wrong mode
	PtrSize = (Flags & 0x20) ? 0x04 : 0x02;
	PAniLib->AniCount = FileHdr[0x05];
	AniOfs = ReadLE16(&FileHdr[0x06]);
	PAniLib->AniBase = ReadPtr16(&FileHdr[0x08], Flags);
	if (! AniOfs)
		return 0xC1;	// invalid pan animation offset
	
	PAniLib->DataLen = FileLen - AniOfs;
	PAniLib->Data = (UINT8*)malloc(PAniLib->DataLen);
	memcpy(PAniLib->Data, &FileData[AniOfs], PAniLib->DataLen);
	
	PAniLib->AniList = (UINT16*)malloc(PAniLib->AniCount * sizeof(UINT16));
	if (! (Flags & 0x10))
	{
		CurPos = 0x00;
		for (CurItm = 0x00; CurItm < PAniLib->AniCount; CurItm ++, CurPos += PtrSize)
			PAniLib->AniList[CurItm] = ReadLE16(&PAniLib->Data[CurPos]);
	}
	else
	{
		CurPos = PtrSize - 0x02;	// read the last 2 bytes of the pointer
		for (CurItm = 0x00; CurItm < PAniLib->AniCount; CurItm ++, CurPos += PtrSize)
			PAniLib->AniList[CurItm] = ReadBE16(&PAniLib->Data[CurPos]);
	}
	
	return 0x00;
}

void FreePanAniData(PAN_ANI_LIB* PAniLib)
{
	if (PAniLib->AniList == NULL)
		return;
	
	PAniLib->AniCount = 0x00;
	free(PAniLib->AniList);
	PAniLib->AniList = NULL;
	
	PAniLib->DataLen = 0x00;
	free(PAniLib->Data);
	PAniLib->AniList = NULL;
	
	return;
}


#ifndef DISABLE_DLOAD_FILE
UINT8 LoadGlobalInstrumentLib_File(const char* FileName, SMPS_CFG* SmpsCfg)
{
	UINT32 FileLen;
	UINT8* FileData;
	UINT8 RetVal;
	
	RetVal = LoadFileData(FileName, &FileLen, &FileData, 0x10, 0, NULL);
	if (RetVal)
		return RetVal;
	
	if (FileLen > 0x4000)
	{
		FileLen = 0x4000;	// This is enough for 315 instruments with register-data interleaving.
		FileData = (UINT8*)realloc(FileData, FileLen);
	}
	
	if (! memcmp(&FileData[0x00], SIG_INS, 0x04))
	{
		RetVal = LoadAdvancedInstrumentLib(FileLen, FileData, SmpsCfg);
	}
	else
	{
		RetVal = LoadSimpleInstrumentLib(FileLen, FileData, SmpsCfg);
		SmpsOffsetFromFilename(FileName, &SmpsCfg->GblInsBase);
	}
	SmpsCfg->GblIns.alloc = 0x01;	// make it free the data when unloading
	
	return RetVal;
}
#endif	// DISABLE_DLOAD_FILE

UINT8 LoadGlobalInstrumentLib_Mem(UINT32 FileLen, UINT8* FileData, SMPS_CFG* SmpsCfg)
{
	UINT8 RetVal;
	
	if (FileLen < 0x10)
		return 0xFF;	// empty file == no file
	
	if (! memcmp(&FileData[0x00], SIG_INS, 0x04))
		RetVal = LoadAdvancedInstrumentLib(FileLen, FileData, SmpsCfg);
	else
		RetVal = LoadSimpleInstrumentLib(FileLen, FileData, SmpsCfg);
	
	return RetVal;
}

static UINT8 LoadSimpleInstrumentLib(UINT32 FileLen, UINT8* FileData, SMPS_CFG* SmpsCfg)
{
	UINT16 InsCount;
	UINT16 CurIns;
	UINT16 CurPos;
	
	if (FileLen > 0xFFFF)
		FileLen = 0xFFFF;
	SmpsCfg->GblIns.alloc = 0x00;
	SmpsCfg->GblIns.Len = (UINT16)FileLen;
	SmpsCfg->GblIns.Data = FileData;
	InsCount = FileLen / SmpsCfg->InsRegCnt;
	SmpsCfg->GblInsLib.InsCount = InsCount;
	SmpsCfg->GblInsLib.InsPtrs = NULL;
	if (InsCount)
	{
		SmpsCfg->GblInsLib.InsPtrs = (UINT8**)malloc(InsCount * sizeof(UINT8*));
		CurPos = 0x0000;
		for (CurIns = 0x00; CurIns < InsCount; CurIns ++, CurPos += SmpsCfg->InsRegCnt)
			SmpsCfg->GblInsLib.InsPtrs[CurIns] = &FileData[CurPos];
	}
	
	SmpsCfg->GblInsBase = 0x0000;
	return 0x00;
}

static UINT8 LoadAdvancedInstrumentLib(UINT32 FileLen, UINT8* FileData, SMPS_CFG* SmpsCfg)
{
	UINT8 Flags;
	UINT16 InsCount;
	UINT16 CurIns;
	UINT16 InsOfs;
	UINT16 InsBase;
	UINT16 CurPos;
	UINT16 InsPos;
	
	Flags = FileData[0x04];
	if ((Flags & ~0x11) != 0x00)
		return 0xC0;	// wrong mode
	
	InsCount = FileData[0x05];
	InsOfs = ReadLE16(&FileData[0x06]);
	InsBase = ReadPtr16(&FileData[0x08], Flags);
	// For now I'll ignore the Register array. It is specified in DefDrv.txt.
	if (! InsOfs)
		return 0xC1;	// invalid drum offset
	
	if (FileLen > 0xFFFF)
		FileLen = 0xFFFF;
	SmpsCfg->GblIns.alloc = 0x00;
	SmpsCfg->GblIns.Len = (UINT16)FileLen;
	SmpsCfg->GblIns.Data = FileData;
	SmpsCfg->GblInsLib.InsCount = InsCount;
	SmpsCfg->GblInsLib.InsPtrs = NULL;
	if (InsCount)
	{
		SmpsCfg->GblInsLib.InsPtrs = (UINT8**)malloc(InsCount * sizeof(UINT8*));
		CurPos = InsOfs;
		for (CurIns = 0x00; CurIns < InsCount; CurIns ++, CurPos += 0x02)
		{
			InsPos = ReadPtr16(&FileData[CurPos], Flags);
			InsPos = InsPos - InsBase + InsOfs;
			if (InsPos >= InsOfs && InsPos < FileLen)
				SmpsCfg->GblInsLib.InsPtrs[CurIns] = &FileData[InsPos];
			else
				SmpsCfg->GblInsLib.InsPtrs[CurIns] = NULL;
		}
	}
	
	return 0x00;
}

void FreeGlobalInstrumentLib(SMPS_CFG* SmpsCfg)
{
	if (SmpsCfg->GblIns.Data == NULL)
		return;
	
	if (SmpsCfg->GblInsLib.InsPtrs != NULL)
	{
		SmpsCfg->GblInsLib.InsCount = 0x00;
		free(SmpsCfg->GblInsLib.InsPtrs);
		SmpsCfg->GblInsLib.InsPtrs = NULL;
	}
	
	FreeFileData(&SmpsCfg->GblIns);
	
	return;
}


void FreeFileData(FILE_DATA* File)
{
	if (! File->alloc)
		return;
	
	File->Len = 0x00;
	if (File->Data != NULL)
	{
		free(File->Data);
		File->Data = NULL;
	}
	File->alloc = 0x00;
	
	return;
}



INLINE UINT16 ReadLE16(const UINT8* Data)
{
	return (Data[0x01] << 8) | (Data[0x00] << 0);
}

INLINE UINT16 ReadBE16(const UINT8* Data)
{
	return (Data[0x00] << 8) | (Data[0x01] << 0);
}

INLINE UINT16 ReadPtr16(const UINT8* Data, const UINT8 Flags)
{
	if (! (Flags & 0x10))
		return ReadLE16(Data);
	else
		return ReadBE16(Data);
}
