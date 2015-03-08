// Configuration Loader
// --------------------
// Written by Valley Bell, 2014
#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <string.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "stdtype.h"
#include "ini_lib.h"
#include "loader.h"
#include "Engine/smps_structs.h"


static UINT8 AddIniFileToList(FILE_LIST* FLst, const char* FileName)
{
	UINT32 CurFile;
	
	for (CurFile = 0; CurFile < FLst->FileCount; CurFile ++)
	{
		if (! _stricmp(FLst->Files[CurFile], FileName))
			return 1;	// already in the list
	}
	
	if (FLst->FileCount >= FLst->FileAlloc)
	{
		FLst->FileAlloc += 0x10;
		FLst->Files = (char**)realloc(FLst->Files, FLst->FileAlloc * sizeof(char*));
	}
	FLst->Files[FLst->FileCount] = _strdup(FileName);
	FLst->FileCount ++;
	
	return 0;
}

SMPS_EXT_DEF* GetExtentionData(EXT_LIST* ExtList, const char* ExtStr)
{
	UINT32 CurExt;
	
	for (CurExt = 0; CurExt < ExtList->ExtCount; CurExt ++)
	{
		if (! _stricmp(ExtList->ExtData[CurExt].Extention, ExtStr))
			return &ExtList->ExtData[CurExt];
	}
	
	return NULL;
}

static SMPS_EXT_DEF* GetNewExtentionData(EXT_LIST* ExtList, const char* ExtStr)
{
	SMPS_EXT_DEF* TempExt;
	
	TempExt = GetExtentionData(ExtList, ExtStr);
	if (TempExt != NULL)
		return TempExt;
	
	if (ExtList->ExtCount >= ExtList->ExtAlloc)
	{
		ExtList->ExtAlloc += 0x10;
		ExtList->ExtData = (SMPS_EXT_DEF*)realloc(ExtList->ExtData, ExtList->ExtAlloc * sizeof(SMPS_EXT_DEF));
	}
	
	TempExt = &ExtList->ExtData[ExtList->ExtCount];
	TempExt->Extention = _strdup(ExtStr);
	TempExt->EqualExt = NULL;
	TempExt->DriverFile = NULL;
	TempExt->CmdFile = NULL;
	TempExt->DrumDefFile = NULL;
	TempExt->PSGDrumDefFile = NULL;
	TempExt->ModEnvFile = NULL;
	TempExt->VolEnvFile = NULL;
	TempExt->PanAniFile = NULL;
	TempExt->DACFile = NULL;
	TempExt->PWMFile = NULL;
	TempExt->FMDrmFile = NULL;
	TempExt->PSGDrmFile = NULL;
	TempExt->GlbInsLibFile = NULL;
	memset(&TempExt->SmpsCfg, 0x00, sizeof(SMPS_EXT_DEF));
	ExtList->ExtCount ++;
	
	return TempExt;
}

INLINE void strdup_free(char** DestStr, const char* SrcStr)
{
	// free DestStr before using strdup()
	if (*DestStr != NULL)
		free(*DestStr);
	*DestStr = _strdup(SrcStr);
	
	return;
}

UINT8 LoadConfigurationFiles(CONFIG_DATA* CfgData, const char* FileName)
{
	FILE* hFile;
	UINT32 BasePathAlloc;
	UINT32 DataPathAlloc;
	UINT32 PathBufAlloc;
	char* IniPath;
	char* BasePath;	// Additional Base Path
	char* DataPath;
	char* PathBuf;
	char LineStr[0x100];
	char* LToken;
	char* RToken1;
	char* RToken2;
	UINT8 Group;
	UINT8 RetVal;
	SMPS_EXT_DEF* ExtData;
	
	RetVal = AddIniFileToList(&CfgData->CfgFiles, FileName);
	if (RetVal)
		return 0x00;	// Prevent endless recursion
	
	hFile = fopen(FileName, "rt");
	if (hFile == NULL)
	{
		printf("Error opening %s\n", FileName);
		return 0xFF;
	}
	
	IniPath = _strdup(FileName);
	LToken = GetFileTitle(IniPath);	// can't return NULL
	*LToken = '\0';
	
	BasePath = _strdup(IniPath);
	DataPath = _strdup(IniPath);
	BasePathAlloc = (UINT32)strlen(BasePath) + 1;
	DataPathAlloc = BasePathAlloc;
	
	PathBufAlloc = (UINT32)strlen(FileName) + 0x100;
	PathBuf = (char*)malloc(PathBufAlloc * sizeof(char));
	
	Group = 0xFF;
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
			// [Section]
			if (! _stricmp(RToken1, "Main"))
				Group = 0x00;
			else if (RToken1[0] == '.')
			{
				Group = 0x10;
				ExtData = GetNewExtentionData(&CfgData->ExtList, RToken1 + 1);
			}
			else
				Group = 0xFF;
			continue;
		}
		
		RToken2 = TrimToken(RToken1);
		if (Group == 0x00)	// [Main] group
		{
			if (! _stricmp(LToken, "BasePath"))
			{
				RevertTokenTrim(RToken1, RToken2);
				CreatePath(&PathBufAlloc, &PathBuf, RToken1);
				ConcatPath(&BasePathAlloc, &BasePath, IniPath, PathBuf);
				ConcatPath(&DataPathAlloc, &DataPath, BasePath, "");
			}
			else if (! _stricmp(LToken, "MusicDir"))
			{
				RevertTokenTrim(RToken1, RToken2);
				CreatePath(&PathBufAlloc, &PathBuf, RToken1);
				
				if (CfgData->MusPath != NULL)
				{
					free(CfgData->MusPath);
					CfgData->MusPath = NULL;
				}
				ConcatPath(NULL, &CfgData->MusPath, BasePath, PathBuf);
			}
			else if (! _stricmp(LToken, "DataDir"))
			{
				RevertTokenTrim(RToken1, RToken2);
				CreatePath(&PathBufAlloc, &PathBuf, RToken1);
				ConcatPath(&DataPathAlloc, &DataPath, BasePath, PathBuf);
			}
			else if (! _stricmp(LToken, "LoadConfig"))
			{
				RevertTokenTrim(RToken1, RToken2);
				StandardizePath(RToken1);
				ConcatPath(&PathBufAlloc, &PathBuf, DataPath, RToken1);
				LoadConfigurationFiles(CfgData, PathBuf);
			}
			else if (! _stricmp(LToken, "ExtFilter"))
				CfgData->ExtFilter = GetBoolValue(RToken1, "True", "False");
			else if (! _stricmp(LToken, "CompressVGM"))
				CfgData->CompressVGMs = (UINT8)strtoul(RToken1, NULL, 0);
			else if (! _stricmp(LToken, "FM6DACOff"))
				CfgData->FM6DACOff = GetBoolValue(RToken1, "True", "False");
			else if (! _stricmp(LToken, "ResmplForce"))
				CfgData->ResmplForce = (UINT8)strtoul(RToken1, NULL, 0);
			else if (! _stricmp(LToken, "DebugMsgs"))
				CfgData->DebugMsgs = (UINT8)strtoul(RToken1, NULL, 0);
			else if (! _stricmp(LToken, "SamplesPerSec"))
				CfgData->SamplePerSec = (UINT32)strtoul(RToken1, NULL, 0);
			else if (! _stricmp(LToken, "BitsPerSample"))
				CfgData->BitsPerSample = (UINT8)strtoul(RToken1, NULL, 0);
			else if (! _stricmp(LToken, "AudioBuffers"))
				CfgData->AudioBufs = (UINT8)strtoul(RToken1, NULL, 0);
			else if (! _stricmp(LToken, "LogWave"))
				CfgData->LogWave = GetBoolValue(RToken1, "True", "False");
		}
		else if (Group == 0x10)	// [.ext] group
		{
			if (! _stricmp(LToken, "Equals"))
			{
				ExtData->EqualExt = GetExtentionData(&CfgData->ExtList, RToken1);
				if (ExtData->EqualExt == NULL)
					printf("Warning: Unable to find extention '%s'!\n", RToken1);
			}
			else
			{
				RevertTokenTrim(RToken1, RToken2);
				StandardizePath(RToken1);
				ConcatPath(&PathBufAlloc, &PathBuf, DataPath, RToken1);
				
				if (! _stricmp(LToken, "Driver"))
					strdup_free(&ExtData->DriverFile, PathBuf);
				else if (! _stricmp(LToken, "Commands"))
					strdup_free(&ExtData->CmdFile, PathBuf);
				else if (! _stricmp(LToken, "Drums"))
					strdup_free(&ExtData->DrumDefFile, PathBuf);
				else if (! _stricmp(LToken, "PSGDrumDef"))
					strdup_free(&ExtData->PSGDrumDefFile, PathBuf);
				else if (! _stricmp(LToken, "ModEnv"))
					strdup_free(&ExtData->ModEnvFile, PathBuf);
				else if (! _stricmp(LToken, "VolEnv"))
					strdup_free(&ExtData->VolEnvFile, PathBuf);
				else if (! _stricmp(LToken, "PanAni"))
					strdup_free(&ExtData->PanAniFile, PathBuf);
				else if (! _stricmp(LToken, "DAC"))
					strdup_free(&ExtData->DACFile, PathBuf);
				else if (! _stricmp(LToken, "PWM"))
					strdup_free(&ExtData->PWMFile, PathBuf);
				else if (! _stricmp(LToken, "FMDrums"))
					strdup_free(&ExtData->FMDrmFile, PathBuf);
				else if (! _stricmp(LToken, "PSGDrums"))
					strdup_free(&ExtData->PSGDrmFile, PathBuf);
				else if (! _stricmp(LToken, "GlobalInsLib"))
					strdup_free(&ExtData->GlbInsLibFile, PathBuf);
			}
		}
	}
	
	fclose(hFile);
	free(IniPath);
	free(BasePath);
	free(DataPath);
	free(PathBuf);
	
	return 0x00;
}

void FreeConfigurationFiles(CONFIG_DATA* CfgData)
{
	// Note: Make sure to call FreeExtentionData() first!
	FILE_LIST* FLst;
	EXT_LIST* ExtList;
	SMPS_EXT_DEF* ExtDef;
	UINT32 CurItem;
	
	free(CfgData->MusPath);
	CfgData->MusPath = NULL;
	
	FLst = &CfgData->CfgFiles;
	for (CurItem = 0; CurItem < FLst->FileCount; CurItem ++)
		free(FLst->Files[CurItem]);
	
	FLst->FileCount = 0x00;
	FLst->FileAlloc = 0x00;
	free(FLst->Files);	FLst->Files = NULL;
	
	ExtList = &CfgData->ExtList;
	for (CurItem = 0; CurItem < ExtList->ExtCount; CurItem ++)
	{
		ExtDef = &ExtList->ExtData[CurItem];
		
		free(ExtDef->Extention);
		ExtDef->EqualExt = NULL;
		if (ExtDef->DriverFile != NULL)
			free(ExtDef->DriverFile);
		if (ExtDef->CmdFile != NULL)
			free(ExtDef->CmdFile);
		if (ExtDef->DrumDefFile != NULL)
			free(ExtDef->DrumDefFile);
		if (ExtDef->PSGDrumDefFile != NULL)
			free(ExtDef->PSGDrumDefFile);
		if (ExtDef->ModEnvFile != NULL)
			free(ExtDef->ModEnvFile);
		if (ExtDef->VolEnvFile != NULL)
			free(ExtDef->VolEnvFile);
		if (ExtDef->PanAniFile != NULL)
			free(ExtDef->PanAniFile);
		if (ExtDef->DACFile != NULL)
			free(ExtDef->DACFile);
		if (ExtDef->PWMFile != NULL)
			free(ExtDef->PWMFile);
		if (ExtDef->FMDrmFile != NULL)
			free(ExtDef->FMDrmFile);
		if (ExtDef->PSGDrmFile != NULL)
			free(ExtDef->PSGDrmFile);
		if (ExtDef->GlbInsLibFile != NULL)
			free(ExtDef->GlbInsLibFile);
	}
	
	ExtList->ExtCount = 0x00;
	ExtList->ExtAlloc = 0x00;
	free(ExtList->ExtData);	ExtList->ExtData = NULL;
	
	return;
}


static void LoadExtData_Single(SMPS_EXT_DEF* ExtDef)
{
	UINT8 RetVal;
	SMPS_CFG* SmpsCfg;
	
	SmpsCfg = &ExtDef->SmpsCfg;
	if (ExtDef->DACFile != NULL)
	{
		// Note: Must be loaded at first, because the Driver Definition
		//       can overwrite the DAC channel count.
		LoadDACData(ExtDef->DACFile, &SmpsCfg->DACDrv);
	}
	if (ExtDef->PWMFile != NULL && ExtDef->DACFile == NULL)
	{
		// Note: Must be loaded at first, because the Driver Definition
		//       can overwrite the DAC channel count.
		LoadDACData(ExtDef->PWMFile, &SmpsCfg->DACDrv);
	}
	if (ExtDef->DriverFile != NULL)
	{
		LoadDriverDefinition(ExtDef->DriverFile, SmpsCfg);
	}
	if (ExtDef->CmdFile != NULL)
	{
		LoadCommandDefinition(ExtDef->CmdFile, SmpsCfg);
	}
	if (ExtDef->DrumDefFile != NULL)
	{
		LoadDrumDefinition(ExtDef->DrumDefFile, &SmpsCfg->DrumLib);
	}
	if (ExtDef->PSGDrumDefFile != NULL)
	{
		LoadPSGDrumDefinition(ExtDef->PSGDrumDefFile, &SmpsCfg->PSGDrumLib);
	}
	if (ExtDef->ModEnvFile != NULL)
	{
		RetVal = LoadEnvelopeData(ExtDef->ModEnvFile, &SmpsCfg->ModEnvs);
		if (RetVal)
			printf("Error loading %s %s: Code 0x%02X\n", "Modulation", "Envelopes", RetVal);
	}
	if (ExtDef->VolEnvFile != NULL)
	{
		RetVal = LoadEnvelopeData(ExtDef->VolEnvFile, &SmpsCfg->VolEnvs);
		if (RetVal)
			printf("Error loading %s %s: Code 0x%02X\n", "Volume", "Envelopes", RetVal);
	}
	if (ExtDef->PanAniFile != NULL)
	{
		RetVal = LoadPanAniData(ExtDef->PanAniFile, &SmpsCfg->PanAnims);
		if (RetVal)
			printf("Error loading %s: Code 0x%02X\n", "Pan Animation", RetVal);
	}
	if (ExtDef->FMDrmFile != NULL)
	{
		RetVal = LoadDrumTracks(ExtDef->FMDrmFile, &SmpsCfg->FMDrums, 0x01);
		if (RetVal)
			printf("Error loading %s %s: Code 0x%02X\n", "FM", "Drums", RetVal);
	}
	if (ExtDef->PSGDrmFile != NULL)
	{
		RetVal = LoadDrumTracks(ExtDef->PSGDrmFile, &SmpsCfg->PSGDrums, 0x00);
		if (RetVal)
			printf("Error loading %s %s: Code 0x%02X\n", "PSG", "Drums", RetVal);
	}
	if (ExtDef->GlbInsLibFile != NULL)
	{
		RetVal = LoadGlobalInstrumentLib(ExtDef->GlbInsLibFile, SmpsCfg);
		if (RetVal)
			printf("Error loading %s: Code 0x%02X\n", "Instrument Library", RetVal);
	}
	
	return;
}

void LoadExtentionData(EXT_LIST* ExtList)
{
	UINT32 CurExt;
	SMPS_EXT_DEF* TempEDef;
	
	for (CurExt = 0; CurExt < ExtList->ExtCount; CurExt ++)
	{
		TempEDef = &ExtList->ExtData[CurExt];
		if (TempEDef->EqualExt != NULL)
			TempEDef->SmpsCfg = TempEDef->EqualExt->SmpsCfg;
		else
			LoadExtData_Single(TempEDef);
	}
	
	return;
}

void FreeSMPSConfiguration(SMPS_CFG* SmpsCfg)
{
	FreeDriverDefinition(SmpsCfg);
	FreeCommandDefinition(SmpsCfg);
	FreeDrumDefinition(&SmpsCfg->DrumLib);
	FreePSGDrumDefinition(&SmpsCfg->PSGDrumLib);
	FreeEnvelopeData(&SmpsCfg->ModEnvs);
	FreeEnvelopeData(&SmpsCfg->VolEnvs);
	FreePanAniData(&SmpsCfg->PanAnims);
	FreeDACData(&SmpsCfg->DACDrv);
	FreeDrumTracks(&SmpsCfg->FMDrums);
	FreeDrumTracks(&SmpsCfg->PSGDrums);
	FreeGlobalInstrumentLib(SmpsCfg);
	
	return;
}

void FreeExtentionData(EXT_LIST* ExtList)
{
	UINT32 CurExt;
	SMPS_EXT_DEF* TempEDef;
	
	for (CurExt = 0; CurExt < ExtList->ExtCount; CurExt ++)
	{
		TempEDef = &ExtList->ExtData[CurExt];
		if (TempEDef->EqualExt == NULL)
			FreeSMPSConfiguration(&TempEDef->SmpsCfg);
	}
	
	return;
}
