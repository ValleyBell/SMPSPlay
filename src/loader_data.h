#ifndef __LOADER_DATA_H__
#define __LOADER_DATA_H__

#include <stdtype.h>
#include "Engine/smps_structs.h"
#include "Engine/dac.h"

#ifndef DISABLE_DLOAD_FILE	// disable loading data from files
void LoadDACData(const char* FileName, DAC_CFG* DACDrv);
UINT8 LoadEnvelopeData_File(const char* FileName, ENV_LIB* EnvLib);
UINT8 LoadDrumTracks_File(const char* FileName, DRUM_TRK_LIB* DrumLib, UINT8 DrumMode);
UINT8 LoadPanAniData_File(const char* FileName, PAN_ANI_LIB* PAniLib);
UINT8 LoadGlobalInstrumentLib_File(const char* FileName, SMPS_CFG* SmpsCfg);
#endif

void FreeDACData(DAC_CFG* DACDrv);

UINT8 LoadEnvelopeData_Mem(UINT32 FileLen, const UINT8* FileData, ENV_LIB* EnvLib);
void FreeEnvelopeData(ENV_LIB* EnvLib);

UINT8 LoadDrumTracks_Mem(UINT32 FileLen, const UINT8* FileData, DRUM_TRK_LIB* DrumLib, UINT8 DrumMode);
void FreeDrumTracks(DRUM_TRK_LIB* DrumLib);

UINT8 LoadPanAniData_Mem(UINT32 FileLen, const UINT8* FileData, PAN_ANI_LIB* PAniLib);
void FreePanAniData(PAN_ANI_LIB* PAniLib);

UINT8 LoadGlobalInstrumentLib_Mem(UINT32 FileLen, UINT8* FileData, SMPS_CFG* SmpsCfg);
void FreeGlobalInstrumentLib(SMPS_CFG* SmpsCfg);

void FreeFileData(FILE_DATA* File);

#endif	// __LOADER_DATA_H__
