#ifndef __LOADER_INI_H__
#define __LOADER_INI_H__

#include <stdtype.h>
#include "Sound.h"	// for AUDIO_CFG
#include "Engine/smps_structs.h"

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
	char* ModPrsFile;
	char* ModEnvFile;
	char* VolEnvFile;
	char* PanAniFile;
	char* DACFile;
	char* PWMFile;
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
	AUDIO_CFG* AudioCfg;	// Note: must always be a valid pointer
	char* MusPath;
	
	FILE_LIST CfgFiles;	// loaded configuration files (prevent endless recursion)
	
	UINT8 ExtFilter;
	UINT8 CompressVGMs;
	UINT8 DisableVGMLoop;
	UINT8 FM6DACOff;
	UINT8 ResmplForce;
	UINT8 DebugMsgs;
	
	EXT_LIST ExtList;
} CONFIG_DATA;


SMPS_EXT_DEF* GetExtentionData(EXT_LIST* ExtList, const char* ExtStr);
UINT8 LoadConfigurationFiles(CONFIG_DATA* CfgData, const char* FileName);
void FreeConfigurationFiles(CONFIG_DATA* CfgData);

void LoadExtentionData(EXT_LIST* ExtList);
void FreeSMPSConfiguration(SMPS_CFG* SmpsCfg);
void FreeExtentionData(EXT_LIST* ExtList);

#endif	// __LOADER_INI_H__
