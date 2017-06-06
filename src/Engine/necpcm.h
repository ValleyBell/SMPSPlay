#ifndef __NECPCM_H__
#define __NECPCM_H__

#include "dac.h"

void SetNecPCMDriver(DAC_CFG* DACSet);
void NECPCM_Reset(void);
void NECPCM_Stop(void);
UINT8 NECPCM_Play(UINT16 SmplID);

void UpdateNECPCM(void);

#endif	// __NECPCM_H__
