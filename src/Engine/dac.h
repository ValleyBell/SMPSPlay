#ifndef __DAC_H__
#define __DAC_H__

#define COMPR_PCM	0x00
#define COMPR_DPCM	0x01

typedef struct _dac_sample
{
	char* File;
	UINT8* Data;
	UINT8* DPCMArr;
	UINT32 Size;
	UINT8 Compr;
#ifdef ENABLE_VGM_LOGGING
	UINT8 UsageID;
#endif
} DAC_SAMPLE;
typedef struct _dac_table
{
	UINT16 Sample;
	UINT8 Pan;
	UINT8 Flags;
	UINT8 Algo;
	UINT32 Rate;
	UINT32 OverriddenRate;
} DAC_TABLE;

#define DACRM_DELAY		0x00
#define DACRM_OVERFLOW	0x01
#define DACRM_NOVERFLOW	0x02
#define DACSM_NORMAL	0x00
#define DACSM_INTERPLT	0x01	// do linear interpolation between samples
typedef struct _dac_algorithm
{
	UINT8 DefCompr;		// default compression mode
	UINT8 RateMode;
	UINT32 BaseCycles;
	UINT32 LoopCycles;
	UINT32 LoopSamples;
	
	UINT32 BaseRate;
	union
	{
		// RM_DELAY: use Divider, RM_[N]OVERFLOW: use RateOverflow
		UINT32 Divider;	// Note: .2 (decimal) Fixed Point, i.e. 123 means 1.23
		UINT32 RateOverflow;
	};
} DAC_ALGO;
typedef struct _dac_settings
{
	UINT8 AlgoAlloc;
	UINT8 AlgoCount;
	DAC_ALGO* Algos;
	
	UINT8 SmplMode;	// Resample Mode
	UINT8 Channels;
	INT8 VolDiv;
} DAC_SETTINGS;

typedef struct _dac_config
{
	DAC_SETTINGS Cfg;
	
	UINT16 SmplCount;
	UINT16 SmplAlloc;
	DAC_SAMPLE* Smpls;
	
	UINT16 TblCount;
	UINT16 TblAlloc;
	DAC_TABLE* SmplTbl;
	
	UINT8 BankCount;
	UINT8 BankAlloc;
	UINT16* BankTbl;	// stores the Base Sample for each bank
} DAC_CFG;


#define DACFLAG_LOOP		0x01
#define DACFLAG_REVERSE		0x02
#define DACFLAG_FLIP_FLOP	0x04
#define DACFLAG_FF_STATE	0x08


void SetDACDriver(DAC_CFG* DACSet);

void DAC_Reset(void);
void DAC_ResetOverride(void);

void DAC_SetFeature(UINT8 Chn, UINT8 DacFlag, UINT8 Set);
void DAC_SetRateOverride(UINT16 SmplID, UINT32 Rate);
void DAC_SetVolume(UINT8 Chn, UINT16 Volume);

void DAC_Stop(UINT8 Chn);
UINT8 DAC_Play(UINT8 Chn, UINT16 SmplID);
void DAC_SetBank(UINT8 Chn, UINT8 BankID);
void DAC_SetRate(UINT8 Chn, UINT32 Rate, UINT8 MidNote);
void DAC_SetFrequency(UINT8 Chn, UINT32 Freq, UINT8 MidNote);

void UpdateDAC(UINT32 Samples);

#endif	// __DAC_H__
