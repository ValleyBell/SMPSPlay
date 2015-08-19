#ifndef __LOADER_SMPS_H__
#define __LOADER_SMPS_H__

#include <stdtype.h>
#include "Engine/smps_structs.h"

UINT8 GuessSMPSOffset(SMPS_SET* SmpsSet);
UINT8 SmpsOffsetFromFilename(const char* FileName, UINT16* RetOffset);
UINT8 PreparseSMPSFile(SMPS_SET* SmpsSet);
void FreeSMPSFile(SMPS_SET* SmpsSet);

#endif	// __LOADER_SMPS_H__
