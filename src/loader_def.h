#ifndef __LOADER_DEF_H__
#define __LOADER_DEF_H__

#include "Engine/smps_structs.h"

void LoadDriverDefinition(const char* FileName, SMPS_CFG* SmpsCfg);
void FreeDriverDefinition(SMPS_CFG* SmpsCfg);

void LoadCommandDefinition(const char* FileName, SMPS_CFG* SmpsCfg);
void FreeCommandDefinition(SMPS_CFG* SmpsCfg);

void LoadDrumDefinition(const char* FileName, DRUM_LIB* DrumDef);
void FreeDrumDefinition(DRUM_LIB* DrumDef);

void LoadPSGDrumDefinition(const char* FileName, PSG_DRUM_LIB* DrumDef);
void FreePSGDrumDefinition(PSG_DRUM_LIB* DrumDef);

#endif	// __LOADER_DEF_H__
