#ifndef __NECPCM_H__
#define __NECPCM_H__

#include "dac.h"

void SetNecPCMDriver(DAC_CFG* DACSet);
void NECPCM_Reset(void);
void NECPCM_Stop(void);
UINT8 NECPCM_Play(UINT16 SmplID);

void UpdateNECPCM(void);

void NECPCM_SetReset(UINT8 State);	// reset bit (0 - reset, 1 - default)
void NECPCM_SetStart(UINT8 State);	// start line (0 - normal, 1 - start)
void NECPCM_WriteData(UINT8 Data);

#endif	// __NECPCM_H__
