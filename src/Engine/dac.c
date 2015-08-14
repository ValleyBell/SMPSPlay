// DAC Engine
// ----------
// Written by Valley Bell, 2014-2015

#include <stddef.h>	// for NULL
#include <stdtype.h>
#include "dac.h"
#include "smps.h"
#include "../chips/mamedef.h"
#include "../Sound.h"
#include "../chips/2612intf.h"
#ifdef ENABLE_VGM_LOGGING
#include "../vgmwrite.h"
#endif

#define CLOCK_Z80	3579545

typedef struct _dac_state
{
	const DAC_SAMPLE* DACSmplPtr;
	const DAC_TABLE* DACTblPtr;
	const UINT8* DPCMData;
	const UINT8* SmplData;
	const DAC_ALGO* DACAlgo;
	
	// semi-constant variables
	UINT32 FreqForce;	// FreqForce and RateForce force override the current playback speed.
	UINT32 RateForce;	// The priority order (highest to lowest) is: FreqForce, RateForce, OverriddenRate, Rate
	UINT16 BaseSmpl;	// for banked sounds
	UINT16 Volume;		// sample volume (0x100 = 100%)
	UINT8 PbBaseFlags;	// global Playback Flags
	UINT8 PbFlags;		// Playback Flags for current DAC sound
	
	// working variables
	UINT32 Pos;
	UINT32 PosFract;	// 16.16 fixed point (upper 16 bits are used during calculation)
	UINT32 DeltaFract;	// 16.16 fixed point Position Step
	UINT32 SmplLen;		// remaining samples
	INT16 SmplLast;
	INT16 SmplNext;
	
	INT16 OutSmpl;		// needs to be 16-bit to allow volumes > 100%
	UINT8 DPCMState;	// current DPCM sample value
	UINT8 DPCMNibble;	// 00 - high nibble, 01 - low nibble
} DAC_STATE;


// Function Prototypes
//void SetDACDriver(DAC_CFG* DACSet);
static UINT32 CalcDACFreq(const DAC_ALGO* DacAlgo, UINT32 Rate);
static UINT32 CalcDACDelta_Hz(UINT32 FreqHz);
static UINT32 CalcDACDelta_Rate(const DAC_ALGO* DacAlgo, UINT32 Rate);
static UINT8 GetNextSample(DAC_STATE* ChnState, INT16* RetSmpl);
static UINT8 HandleSampleEnd(DAC_STATE* ChnState);
static INT16 UpdateChannels(UINT16* ProcSmpls, UINT8* StopSignal);
//void UpdateDAC(UINT32 Samples);
//void DAC_Reset(void);
//void DAC_ResetOverride(void);
//void DAC_SetFeature(UINT8 Chn, UINT8 DacFlag, UINT8 Set);
//void DAC_SetRateOverride(UINT16 SmplID, UINT32 Rate);
//void DAC_SetVolume(UINT8 Chn, UINT16 Volume);
//void DAC_Stop(UINT8 Chn);
//void DAC_Play(UINT8 Chn, UINT16 SmplID);
//void DAC_SetRate(UINT8 Chn, UINT32 Rate, UINT8 MidNote);
//void DAC_SetFrequency(UINT8 Chn, UINT32 Freq, UINT8 MidNote);


static DAC_CFG* DACDrv = NULL;

extern UINT32 SampleRate;

#define MAX_DAC_CHNS	4
static DAC_STATE DACChnState[MAX_DAC_CHNS];

void SetDACDriver(DAC_CFG* DACSet)
{
	DACDrv = DACSet;
	if (DACDrv == NULL)
		return;
	
	if (! DACDrv->Cfg.Channels || DACDrv->Cfg.Channels > MAX_DAC_CHNS)
		DACDrv->Cfg.Channels = MAX_DAC_CHNS;
	if (! DACDrv->Cfg.VolDiv)
		DACDrv->Cfg.VolDiv = 1;
	
	return;
}

static UINT32 CalcDACFreq(const DAC_ALGO* DacAlgo, UINT32 Rate)
{
	UINT32 Numerator;
	UINT32 Divisor;
	
	switch(DacAlgo->RateMode)
	{
	case DACRM_DELAY:
		if (DacAlgo->BaseCycles)
		{
			Numerator = CLOCK_Z80 * DacAlgo->LoopSamples;
			Divisor = DacAlgo->BaseCycles + DacAlgo->LoopCycles * (Rate - 1);
		}
		else
		{
			Numerator = DacAlgo->BaseRate * 100;
			Divisor = DacAlgo->Divider + Rate * 100;
		}
		break;
	case DACRM_NOVERFLOW:
		if (Rate < DacAlgo->RateOverflow)
			Rate = DacAlgo->RateOverflow - Rate;
		else
			Rate = 0;
		// fall through
	case DACRM_OVERFLOW:
		if (DacAlgo->BaseCycles)
		{
			Numerator = CLOCK_Z80 * DacAlgo->LoopSamples * Rate;
			Divisor = DacAlgo->BaseCycles * DacAlgo->RateOverflow;
		}
		else
		{
			Numerator = DacAlgo->BaseRate * Rate;
			Divisor = DacAlgo->RateOverflow;
		}
		break;
	default:
		Numerator = 0;
		Divisor = 1;
		break;
	}
	
	return (Numerator + Divisor / 2) / Divisor;
}

static UINT32 CalcDACDelta_Hz(UINT32 FreqHz)	// returns 16.16 fixed point delta
{
	UINT32 Numerator;
	
	Numerator = FreqHz << 16;
	return (Numerator + SampleRate / 2) / SampleRate;
}

static UINT32 CalcDACDelta_Rate(const DAC_ALGO* DacAlgo, UINT32 Rate)	// returns 16.16 fixed point delta
{
	UINT64 Numerator;
	UINT64 Divisor;
	
	switch(DacAlgo->RateMode)
	{
	case DACRM_DELAY:
		if (DacAlgo->BaseCycles)
		{
			Numerator = CLOCK_Z80 * DacAlgo->LoopSamples;
			Divisor = DacAlgo->BaseCycles + DacAlgo->LoopCycles * (Rate - 1);
		}
		else
		{
			Numerator = DacAlgo->BaseRate * 100;
			Divisor = DacAlgo->Divider + Rate * 100;
		}
		break;
	case DACRM_NOVERFLOW:
		if (Rate < DacAlgo->RateOverflow)
			Rate = DacAlgo->RateOverflow - Rate;
		else
			Rate = 0;
		// fall through
	case DACRM_OVERFLOW:
		if (DacAlgo->BaseCycles)
		{
			Numerator = CLOCK_Z80 * DacAlgo->LoopSamples * Rate;
			Divisor = DacAlgo->BaseCycles * DacAlgo->RateOverflow;
		}
		else
		{
			Numerator = DacAlgo->BaseRate * Rate;
			Divisor = DacAlgo->RateOverflow;
		}
		break;
	default:
		Numerator = 0;
		Divisor = 1;
		break;
	}
	
	Numerator <<= 16;
	Divisor *= SampleRate;
	return (UINT32)((Numerator + Divisor / 2) / Divisor);
}

static UINT8 GetNextSample(DAC_STATE* ChnState, INT16* RetSmpl)
{
	if (ChnState->DACSmplPtr->Compr == COMPR_PCM)
	{
		*RetSmpl = ChnState->SmplData[ChnState->Pos] - 0x80;
		if (ChnState->PbFlags & DACFLAG_REVERSE)
			ChnState->Pos --;
		else
			ChnState->Pos ++;
		ChnState->SmplLen --;
	}
	else if (ChnState->DACSmplPtr->Compr == COMPR_DPCM)
	{
		UINT8 NibbleData;
		
		NibbleData = ChnState->SmplData[ChnState->Pos];
		if (! (ChnState->PbFlags & DACFLAG_REVERSE))
		{
			// forward
			if (! ChnState->DPCMNibble)
			{
				NibbleData >>= 4;
			}
			else
			{
				//NibbleData >>= 0;
				ChnState->Pos ++;
				ChnState->SmplLen --;
			}
			NibbleData &= 0x0F;
			ChnState->DPCMState += ChnState->DPCMData[NibbleData];
		}
		else
		{
			// backward
			if (ChnState->DPCMNibble)
			{
				NibbleData >>= 4;
				ChnState->Pos --;
				ChnState->SmplLen --;
			}
			NibbleData &= 0x0F;
			ChnState->DPCMState -= ChnState->DPCMData[NibbleData];
		}
		ChnState->DPCMNibble ^= 0x01;
		*RetSmpl = ChnState->DPCMState - 0x80;
	}
	
	if (! ChnState->SmplLen)
		return HandleSampleEnd(ChnState);
	return 0x00;
}

static UINT8 HandleSampleEnd(DAC_STATE* ChnState)
{
	UINT8 RestartSmpl;
#ifdef ENABLE_VGM_LOGGING
	UINT8 VgmSmplID;
#endif
	
	RestartSmpl = (ChnState->PbFlags & DACFLAG_LOOP);
	if (ChnState->PbFlags & DACFLAG_FLIP_FLOP)
	{
		if (! (ChnState->PbFlags & DACFLAG_FF_STATE))
			RestartSmpl |= 0x02;
		else if (ChnState->PbFlags & DACFLAG_LOOP)
			RestartSmpl |= 0x02;
		ChnState->PbFlags ^= (DACFLAG_REVERSE | DACFLAG_FF_STATE);
	}
	if (! RestartSmpl)
	{
		ChnState->DACSmplPtr = NULL;
		ChnState->SmplLast = ChnState->SmplNext = 0x0000;
		return 0x01;
	}
	
	ChnState->SmplLen = ChnState->DACSmplPtr->Size;
	if (ChnState->PbFlags & DACFLAG_REVERSE)
		ChnState->Pos = ChnState->SmplLen - 1;
	else
		ChnState->Pos = 0x00;
	ChnState->DPCMState = 0x80;
	
#ifdef ENABLE_VGM_LOGGING
	VgmSmplID = ChnState->DACSmplPtr->UsageID;
	if (VgmSmplID < 0xFE)
	{
		if (ChnState->PbFlags & DACFLAG_REVERSE)
			vgm_write_stream_data_command(0x00, 0x05, VgmSmplID | 0x100000);
		else
			vgm_write_stream_data_command(0x00, 0x05, VgmSmplID);
	}
#endif
	
	return 0x00;
}

static INT16 UpdateChannels(UINT16* ProcSmpls, UINT8* StopSignal)
{
	DAC_STATE* ChnState;
	UINT8 CurChn;
	INT16 OutSmpl;
	UINT16 ProcessedSmpls;	// if 0, nothing is sent to the YM2612
	UINT8 DacStopSignal;
	UINT8 ChnStop;
	
	ProcessedSmpls = 0;
	DacStopSignal = 0;
	OutSmpl = 0x0000;
	for (CurChn = 0; CurChn < DACDrv->Cfg.Channels; CurChn ++)
	{
		ChnState = &DACChnState[CurChn];
		if (ChnState->DACSmplPtr == NULL)
			continue;
		
		ChnState->PosFract += ChnState->DeltaFract;
		ChnStop = 0;
		if (DACDrv->Cfg.SmplMode == DACSM_NORMAL)
		{
			while(ChnState->PosFract >= 0x10000 && ! ChnStop)
			{
				ChnState->PosFract -= 0x10000;
				ChnStop = GetNextSample(ChnState, &ChnState->OutSmpl);
				ProcessedSmpls ++;
			}
		}
		else //if (DACDrv->Cfg.SmplMode == DACSM_INTERPLT)
		{
			while(ChnState->PosFract >= 0x10000 && ! ChnStop)
			{
				ChnState->PosFract -= 0x10000;
				ChnState->SmplLast = ChnState->SmplNext;
				ChnStop = GetNextSample(ChnState, &ChnState->SmplNext);
			}
			ChnState->PosFract &= 0xFFFF;	// in case ChnStop is true
			ChnState->OutSmpl = (ChnState->SmplLast * (0x10000 - ChnState->PosFract) +
								 ChnState->SmplNext * ChnState->PosFract) >> 16;
			ProcessedSmpls ++;
		}
		
		DacStopSignal |= ChnStop;
		if (ChnState->Volume == 0x100)
			OutSmpl += ChnState->OutSmpl;
		else
			OutSmpl += (ChnState->OutSmpl * ChnState->Volume) >> 8;
	}	// end for (CurChn)
	
	if (ProcSmpls != NULL)
		*ProcSmpls = ProcessedSmpls;
	if (StopSignal != NULL)
		*StopSignal |= DacStopSignal;	// keep old bits set
	return OutSmpl;
}

void UpdateDAC(UINT32 Samples)
{
	UINT8 CurChn;
	UINT8 FnlSmpl;
	INT16 OutSmpl;
	UINT16 ProcessedSmpls;	// if 0, nothing is sent to the YM2612
	UINT8 DacStopSignal;
	UINT8 RunningChns;
	
	if (DACDrv == NULL)
		return;
	
	DacStopSignal = 0x00;
	ym2612_w(0x00, 0x00, 0x2A);	// YM2612: Register 2A: DAC Data
	while(Samples)
	{
		OutSmpl = UpdateChannels(&ProcessedSmpls, &DacStopSignal);
		
		if (ProcessedSmpls)
		{
			if (DACDrv->Cfg.VolDiv > 0)
				OutSmpl /= DACDrv->Cfg.VolDiv;
			else
				OutSmpl *= -(INT16)DACDrv->Cfg.VolDiv;
			if (OutSmpl < -0x80)
				OutSmpl = -0x80;
			else if (OutSmpl > 0x7F)
				OutSmpl = 0x7F;
			FnlSmpl = (UINT8)(0x80 + OutSmpl);
			ym2612_w(0x00, 0x01, FnlSmpl);	// write data directly to chip, skipping VGM logging
		}
		Samples --;
	}
	
	if (DacStopSignal)
	{
		// Loop over all channels and check, if one of them is still running.
		// If not, the DAC can be turned off. (As most DAC drivers do it.)
		RunningChns = 0;
		for (CurChn = 0; CurChn < DACDrv->Cfg.Channels; CurChn ++)
		{
			if (DACChnState[CurChn].DACSmplPtr != NULL)
				RunningChns ++;
		}
		if (! RunningChns)
			SetDACState(0x00);	// also does WriteFMI(0x2B, 0x00);
	}
	
	return;
}

#if 0
// Reference code ported from the PWM driver used by Knuckles Chaotix.
// I assume that it's the default PWM driver from the 32x development kit.
static void ProcessPWMSample(void)
{
	ChnState->PosFract += ChnState->DeltaFract;
	if (ChnState->PosFract < 0x10000)
	{
		Out1_L += ChnState->SmplDataL;
		Out2_L += ChnState->SmplDataL;
		Out1_R += ChnState->SmplDataR;
		Out2_R += ChnState->SmplDataR;
		return;
	}
	ChnState->PosFract -= 0x10000;
	
	ChnState->SmplLen --;
	if (! ChnState->SmplLen)
	{
		ChnState->Pos = ChnState->DACSmplPtr->LoopOfs;
		if (! ChnState->Pos)
			return;
		ChnState->SmplLen = ChnState->DACSmplPtr->Size - ChnState->DACSmplPtr->LoopOfs;
	}
	SmplData = ChnState->SmplData[ChnState->Pos] - 0x80;
	ChnState->Pos ++;
	
	NewSmpData = SmplData * VolL >> 4;
	OldSmpData = ChnState->SmplDataL;
	ChnState->SmplDataL = NewSmpData;
	Out2_L += NewSmpData;
	Out1_L += (OldSmpData + NewSmpData) / 2;
	
	NewSmpData = SmplData * VolR >> 4;
	OldSmpData = ChnState->SmplDataR;
	ChnState->SmplDataR = NewSmpData;
	Out2_R += NewSmpData;
	Out1_R += (OldSmpData + NewSmpData) / 2;
	
	return;
}
#endif


void DAC_Reset(void)
{
	UINT8 CurChn;
	DAC_STATE* DACChn;
	
	for (CurChn = 0; CurChn < MAX_DAC_CHNS; CurChn ++)
	{
		DACChn = &DACChnState[CurChn];
		DACChn->DACSmplPtr = NULL;
		//DACChn->PbBaseFlags = 0x00;
		//DACChn->Volume = 0x100;
		DACChn->SmplLast = DACChn->SmplNext = 0x0000;
		DACChn->DACAlgo = NULL;
	}
	
	DAC_ResetOverride();
	
	return;
}

void DAC_ResetOverride(void)
{
	UINT16 CurSmpl;
	UINT8 CurChn;
	DAC_STATE* DACChn;
	
	if (DACDrv != NULL)
	{
		for (CurSmpl = 0; CurSmpl < DACDrv->SmplCount; CurSmpl ++)
			DACDrv->SmplTbl[CurSmpl].OverriddenRate = 0x00;
	}
	
	for (CurChn = 0; CurChn < MAX_DAC_CHNS; CurChn ++)
	{
		DACChn = &DACChnState[CurChn];
		DACChn->FreqForce = 0;
		DACChn->RateForce = 0;
		DACChn->PbBaseFlags = 0x00;
		//DACChn->PbFlags = 0x00;
		DACChn->Volume = 0x100;
		DACChn->BaseSmpl = 0x00;
	}
	
	return;
}

void DAC_SetFeature(UINT8 Chn, UINT8 DacFlag, UINT8 Set)
{
	if (Chn >= MAX_DAC_CHNS)
		return;
	if (DACDrv != NULL && Chn >= DACDrv->Cfg.Channels)
		return;
	
	if (Set)
		DACChnState[Chn].PbBaseFlags |= DacFlag;
	else
		DACChnState[Chn].PbBaseFlags &= ~DacFlag;
	
	return;
}

void DAC_SetRateOverride(UINT16 SmplID, UINT32 Rate)
{
	if (DACDrv == NULL)
		return;
	
	if (SmplID >= DACDrv->TblCount)
		return;
	
	DACDrv->SmplTbl[SmplID].OverriddenRate = Rate;
	
	return;
}

void DAC_SetVolume(UINT8 Chn, UINT16 Volume)
{
	if (Chn >= MAX_DAC_CHNS)
		return;
	if (DACDrv != NULL && Chn >= DACDrv->Cfg.Channels)
		return;
	
	DACChnState[Chn].Volume = Volume;
	
	return;
}

void DAC_Stop(UINT8 Chn)
{
	DAC_STATE* DACChn;
	UINT8 CurChn;
	
	if (Chn >= MAX_DAC_CHNS)
		return;
	
	DACChn = &DACChnState[Chn];
	if (DACChn->DACSmplPtr == NULL)
		return;
	
	DACChn->DACSmplPtr = NULL;
	DACChn->SmplLast = DACChn->SmplNext = 0x0000;
#ifdef ENABLE_VGM_LOGGING
	vgm_write_stream_data_command(0x00, 0x04, 0x00);
#endif
	
	for (CurChn = 0; CurChn < MAX_DAC_CHNS; CurChn ++)
	{
		if (DACChnState[CurChn].DACSmplPtr != NULL)
			return;
	}
	SetDACState(0x00);	// also does WriteFMI(0x2B, 0x00);
	
	return;
}

UINT8 DAC_Play(UINT8 Chn, UINT16 SmplID)
{
	UINT32 Rate;
	DAC_TABLE* TempEntry;
	DAC_SAMPLE* TempSmpl;
	DAC_STATE* DACChn;
	UINT32 FreqHz;
	
	if (Chn >= MAX_DAC_CHNS || DACDrv == NULL)
		return 0xFF;
	if (Chn >= DACDrv->Cfg.Channels)
		return 0xC0;
	
	if (SmplID == 0xFFFF)
	{
		DAC_Stop(Chn);
		return 0x00;
	}
	
	DACChn = &DACChnState[Chn];
	
	SmplID += DACChn->BaseSmpl;	// do banked sounds
	if (SmplID >= DACDrv->TblCount)
		return 0x10;
	TempEntry = &DACDrv->SmplTbl[SmplID];
	if (TempEntry->Sample == 0xFFFF || TempEntry->Sample >= DACDrv->SmplCount)
		return 0x11;
	
	TempSmpl = &DACDrv->Smpls[TempEntry->Sample];
	if (! TempSmpl->Size)
	{
		DAC_Stop(Chn);
		return 0x00;
	}
	
	DACChn->DACSmplPtr = TempSmpl;
	DACChn->DACTblPtr = TempEntry;
	DACChn->DPCMData = TempSmpl->DPCMArr;
	DACChn->SmplData = TempSmpl->Data;
	DACChn->DACAlgo = &DACDrv->Cfg.Algos[TempEntry->Algo];
	DACChn->DPCMState = 0x80;
	DACChn->DPCMNibble = 0x00;
	DACChn->OutSmpl = 0x0000;
	DACChn->SmplLast = DACChn->SmplNext;
	DACChn->SmplNext = 0x0000;
	
	DACChn->PbFlags = TempEntry->Flags;
	DACChn->PbFlags |= (DACChn->PbBaseFlags & ~DACFLAG_REVERSE);
	DACChn->PbFlags ^= (DACChn->PbBaseFlags & DACFLAG_REVERSE);	// 'reverse' needs XOr
	if (DACChn->PbFlags & DACFLAG_FLIP_FLOP)
	{
		if (DACChn->PbFlags & DACFLAG_FF_STATE)
			DACChn->PbFlags ^= DACFLAG_REVERSE;
	}
	
	DACChn->SmplLen = TempSmpl->Size;
	if (DACChn->PbFlags & DACFLAG_REVERSE)
		DACChn->Pos = DACChn->SmplLen - 1;
	else
		DACChn->Pos = 0x00;
	if (DACDrv->Cfg.SmplMode == DACSM_NORMAL)
		DACChn->PosFract = 0x0000;
	else //if (DACDrv->Cfg.SmplMode == DACSM_INTERPLT)
		DACChn->PosFract = 0x10000;	// make it read the next sample immediately
	
	if (DACChn->FreqForce)
	{
		FreqHz = DACChn->FreqForce;
		DACChn->DeltaFract = CalcDACDelta_Hz(FreqHz);
	}
	else
	{
		if (DACChn->RateForce)
			Rate = DACChn->RateForce;
		else if (TempEntry->OverriddenRate)
			Rate = TempEntry->OverriddenRate;
		else
			Rate = TempEntry->Rate;
		FreqHz = CalcDACFreq(DACChn->DACAlgo, Rate);	// for VGM logging
		DACChn->DeltaFract = CalcDACDelta_Rate(DACChn->DACAlgo, Rate);
	}
	
	SetDACState(0x80);	// also does WriteFMI(0x2B, 0x00);
	if (TempEntry->Pan)
		ym2612_fm_write(0x00, 0x01, 0xB6, TempEntry->Pan);
#ifdef ENABLE_VGM_LOGGING
	if (TempSmpl->UsageID < 0xFE)
	{
		vgm_write_stream_data_command(0x00, 0x02, FreqHz);
		if (DACChn->PbFlags & DACFLAG_REVERSE)
			vgm_write_stream_data_command(0x00, 0x05, TempSmpl->UsageID | 0x100000);
		else
			vgm_write_stream_data_command(0x00, 0x05, TempSmpl->UsageID);
	}
#endif
	
	return 0x00;
}

void DAC_SetBank(UINT8 Chn, UINT8 BankID)
{
	DAC_STATE* DACChn;
	
	if (Chn >= MAX_DAC_CHNS || DACDrv == NULL)
		return;
	
	DACChn = &DACChnState[Chn];
	
	if (BankID < DACDrv->BankCount)
		DACChn->BaseSmpl = DACDrv->BankTbl[BankID];
	else
		DACChn->BaseSmpl = 0x00;
	
	return;
}

void DAC_SetRate(UINT8 Chn, UINT32 Rate, UINT8 MidNote)
{
	// Note: MidNote is here for optimization.
	//       Setting it to 0 skips the rate recalculation, since that's done when playing the next sound anyway.
	DAC_STATE* DACChn;
	UINT32 FreqHz;
	
	if (Chn >= MAX_DAC_CHNS || DACDrv == NULL)
		return;
	
	DACChn = &DACChnState[Chn];
	
	DACChn->RateForce = Rate;
	if (DACChn->FreqForce || DACChn->DACSmplPtr == NULL || ! MidNote)
		return;
	if (DACChn->DACAlgo == NULL)
		return;
	
	if (! Rate && DACChn->DACTblPtr != NULL)
	{
		if (DACChn->DACTblPtr->OverriddenRate)
			Rate = DACChn->DACTblPtr->OverriddenRate;
		else
			Rate = DACChn->DACTblPtr->Rate;
	}
	FreqHz = CalcDACFreq(DACChn->DACAlgo, Rate);
#ifdef ENABLE_VGM_LOGGING
	if (DACChn->DACSmplPtr->UsageID < 0xFE)
		vgm_write_stream_data_command(0x00, 0x02, FreqHz);
#endif
	DACChn->DeltaFract = CalcDACDelta_Rate(DACChn->DACAlgo, Rate);
	
	return;
}

void DAC_SetFrequency(UINT8 Chn, UINT32 Freq, UINT8 MidNote)
{
	// Note: MidNote is here for optimization. (see DAC_SetRate)
	DAC_STATE* DACChn;
	
	if (Chn >= MAX_DAC_CHNS || DACDrv == NULL)
		return;
	
	DACChn = &DACChnState[Chn];
	
	DACChn->FreqForce = Freq;
	if (DACChn->DACSmplPtr != NULL || ! MidNote)
		return;
	
	if (! Freq)
	{
		DAC_SetRate(Chn, DACChn->RateForce, MidNote);
	}
	else
	{
#ifdef ENABLE_VGM_LOGGING
		if (DACChn->DACSmplPtr->UsageID < 0xFE)
			vgm_write_stream_data_command(0x00, 0x02, Freq);
#endif
		DACChn->DeltaFract = CalcDACDelta_Hz(Freq);
	}
	
	return;
}
