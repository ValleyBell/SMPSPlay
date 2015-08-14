// NEC PCM Streamer (for Sega Pico SMPS)
// ----------------
// Written by Valley Bell, 2015

#include <stddef.h>	// for NULL
#include <stdtype.h>
#include "necpcm.h"
#include "dac.h"	// for DAC_SAMPLE
#include "../chips/mamedef.h"
#include "../chips/upd7759.h"
#ifdef ENABLE_VGM_LOGGING
#include "../vgmwrite.h"
#endif

typedef struct _nec_state
{
	const DAC_SAMPLE* DACSmplPtr;
	const UINT8* SmplData;
	
	// working variables
	UINT32 Pos;
	UINT32 SmplLen;		// remaining samples
	
	UINT16 ChipCtrlReg;
} NEC_STATE;


static DAC_CFG* DACDrv = NULL;

static NEC_STATE NecState;
static UINT16 SmplModeVals[0x06] =
{	0x40, 0x40, 0x80, 0x80, 0xC0, 0x4EB9};	// actually only 5 words long, but the 6th one is commonly read

void SetNecPCMDriver(DAC_CFG* DACSet)
{
	DACDrv = DACSet;
	return;
}

static void WritePicoCtrlReg(UINT16 Data)
{
	NECPCM_SetReset((Data >> 8) & 0x08);
	NECPCM_SetStart((Data >> 8) & 0x40);
	if (Data & 0x4000)
	{
		NECPCM_WriteData(0xFF);	// "Last Sample" value (must be >= 0x10)
		NECPCM_WriteData(0x00);	// Dummy 1
		NECPCM_WriteData(0x00);	// Addr MSB
		NECPCM_WriteData(0x00);	// Addr LSB
	}
	
	return;
}

void NECPCM_Reset(void)
{
	NECPCM_Stop();
	NecState.DACSmplPtr = NULL;
	NecState.SmplData = NULL;
	
	return;
}

void NECPCM_Stop(void)
{
	WritePicoCtrlReg(0x8000);	// reset PCM chip + FIFO
	WritePicoCtrlReg(0x0880);	// return to normal operation
	NecState.ChipCtrlReg = 0x0880;
	
	return;
}

UINT8 NECPCM_Play(UINT16 SmplID)
{
	DAC_TABLE* TempEntry;
	DAC_SAMPLE* TempSmpl;
	UINT16 SmplMode1;
	UINT16 SmplMode2;
	UINT16 TempSht;
	
	if (DACDrv == NULL)
		return 0xFF;
	
	if (SmplID == 0xFFFF)
	{
		NECPCM_Stop();
		return 0x00;
	}
	
	if (SmplID >= DACDrv->TblCount)
		return 0x10;
	TempEntry = &DACDrv->SmplTbl[SmplID];
	if (TempEntry->Sample == 0xFFFF || TempEntry->Sample >= DACDrv->SmplCount)
		return 0x11;
	
	TempSmpl = &DACDrv->Smpls[TempEntry->Sample];
	if (! TempSmpl->Size)
	{
		NECPCM_Stop();
		return 0x00;
	}
	
	NecState.DACSmplPtr = TempSmpl;
	NecState.SmplData = TempSmpl->Data;
	NecState.SmplLen = TempSmpl->Size;
	NecState.Pos = 0x00;
	SmplMode1 = TempEntry->Rate;
	SmplMode2 = 0xFFFF;
	
	if (SmplMode1 < 0x06)
		TempSht = SmplModeVals[SmplMode1];
	else
		TempSht = 0x0000;
	NecState.ChipCtrlReg &= ~0x00C0;
	NecState.ChipCtrlReg |= (TempSht & 0x00C0);
	WritePicoCtrlReg(NecState.ChipCtrlReg);
	
	if (SmplMode2 != 0xFFFF)
	{
		NecState.ChipCtrlReg &= ~0x0007;
		NecState.ChipCtrlReg |= (SmplMode2 & 0x0007);
		WritePicoCtrlReg(NecState.ChipCtrlReg);
	}
	
	// reset ADPCM engine first
	WritePicoCtrlReg(0x8000);
	WritePicoCtrlReg(0x0880);
	// then start and play
	WritePicoCtrlReg(NecState.ChipCtrlReg | 0x4000);
	
	return 0x00;
}

void UpdateNECPCM(void)
{
	UINT32 RemFIFOBytes;
	
	if (NecState.SmplData == NULL)
		return;
	
	RemFIFOBytes = upd7759_get_fifo_space(0x00);
	if (RemFIFOBytes < 0x14)
		return;
	RemFIFOBytes -= 0x04;	// don't fill FIFO completely (safe for VGM logging)
	if (RemFIFOBytes > NecState.SmplLen)
		RemFIFOBytes = NecState.SmplLen;
	NecState.SmplLen -= RemFIFOBytes;
	
	while(RemFIFOBytes)
	{
		NECPCM_WriteData(NecState.SmplData[NecState.Pos]);
		NecState.Pos ++;
		RemFIFOBytes --;
	}
	if (! NecState.SmplLen)
	{
		WritePicoCtrlReg(NecState.ChipCtrlReg);
		NecState.SmplData = NULL;
	}
	
	return;
}



void NECPCM_SetReset(UINT8 State)
{
	upd7759_reset_w(0x00, State);
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_UPD7759, 0, 0x00, State);
#endif
	return;
}

void NECPCM_SetStart(UINT8 State)
{
	upd7759_start_w(0x00, State);
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_UPD7759, 0, 0x01, State);
#endif
	return;
}

void NECPCM_WriteData(UINT8 Data)
{
	upd7759_port_w(0x00, 0x00, Data);
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_UPD7759, 0, 0x02, Data);
#endif
	return;
}
