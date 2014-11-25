#ifndef __LOADER_H__
#define __LOADER_H__

#include "Engine/smps_structs.h"
#include "Engine/dac.h"

typedef struct _file_list
{
	UINT32 FileCount;
	UINT32 FileAlloc;
	char** Files;
} FILE_LIST;

typedef struct _smps_extention_definition SMPS_EXT_DEF;
struct _smps_extention_definition
{
	char* Extention;
	SMPS_EXT_DEF* EqualExt;
	
	char* DriverFile;
	char* CmdFile;
	char* DrumDefFile;
	char* PSGDrumDefFile;
	char* ModEnvFile;
	char* VolEnvFile;
	char* PanAniFile;
	char* DACFile;
	char* FMDrmFile;
	char* PSGDrmFile;
	char* GlbInsLibFile;
	
	SMPS_CFG SmpsCfg;
};
typedef struct _extention_list
{
	UINT32 ExtCount;
	UINT32 ExtAlloc;
	SMPS_EXT_DEF* ExtData;
} EXT_LIST;

typedef struct _config_data
{
	char* MusPath;
	
	FILE_LIST CfgFiles;	// loaded configuration files (prevent endless recursion)
	
	UINT8 ExtFilter;
	UINT8 CompressVGMs;
	UINT8 FM6DACOff;
	UINT8 ResmplForce;
	UINT8 DebugMsgs;
	UINT8 LogWave;
	UINT32 AudioBufs;
	
	EXT_LIST ExtList;
} CONFIG_DATA;


// loader_def.c
void LoadDriverDefinition(const char* FileName, SMPS_CFG* SmpsCfg);
void FreeDriverDefinition(SMPS_CFG* SmpsCfg);

void LoadCommandDefinition(const char* FileName, SMPS_CFG* SmpsCfg);
void FreeCommandDefinition(SMPS_CFG* SmpsCfg);

void LoadDrumDefinition(const char* FileName, DRUM_LIB* DrumDef);
void FreeDrumDefinition(DRUM_LIB* DrumDef);

void LoadPSGDrumDefinition(const char* FileName, PSG_DRUM_LIB* DrumDef);
void FreePSGDrumDefinition(PSG_DRUM_LIB* DrumDef);

UINT8 LoadDrumTracks(const char* FileName, DRUM_TRK_LIB* DrumLib, UINT8 DrumMode);
void FreeDrumTracks(DRUM_TRK_LIB* DrumLib);

UINT8 LoadPanAniData(const char* FileName, PAN_ANI_LIB* PAniLib);
void FreePanAniData(PAN_ANI_LIB* PAniLib);

UINT8 LoadGlobalInstrumentLib(const char* FileName, SMPS_CFG* SmpsCfg);
void FreeGlobalInstrumentLib(SMPS_CFG* SmpsCfg);

void FreeFileData(FILE_DATA* File);

// loader_data.c
void LoadDACData(const char* FileName, DAC_CFG* DACDrv);
void FreeDACData(DAC_CFG* DACDrv);

UINT8 LoadEnvelopeData(const char* FileName, ENV_LIB* EnvLib);
void FreeEnvelopeData(ENV_LIB* EnvLib);

// loader_smps.c
UINT8 GuessSMPSOffset(SMPS_SET* SmpsSet);
UINT8 SmpsOffsetFromFilename(const char* FileName, UINT16* RetOffset);
UINT8 PreparseSMPSFile(SMPS_SET* SmpsSet);
void FreeSMPSFile(SMPS_SET* SmpsSet);

// loader_ini.c
SMPS_EXT_DEF* GetExtentionData(EXT_LIST* ExtList, const char* ExtStr);
UINT8 LoadConfigurationFiles(CONFIG_DATA* CfgData, const char* FileName);
void FreeConfigurationFiles(CONFIG_DATA* CfgData);

void LoadExtentionData(EXT_LIST* ExtList);
void FreeSMPSConfiguration(SMPS_CFG* SmpsCfg);
void FreeExtentionData(EXT_LIST* ExtList);

#endif	// __LOADER_H__
