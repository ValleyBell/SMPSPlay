#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <memory.h>
#include <malloc.h>

#include "chips/mamedef.h"
#include "Sound.h"
#include "Stream.h"
#include "chips/2612intf.h"
#include "chips/sn764intf.h"
#include "chips/upd7759.h"
#include "Engine/smps.h"
#include "Engine/dac.h"
#include "Engine/necpcm.h"
#include "vgmwrite.h"


// from main.c
void FinishedSongSignal(void);


typedef void (*strm_func)(UINT8 ChipID, stream_sample_t **outputs, int samples);

typedef struct chip_audio_attributes CAUD_ATTR;
struct chip_audio_attributes
{
	UINT32 SmpRate;
	UINT16 Volume;
	UINT8 ChipType;
	UINT8 ChipID;		// 0 - 1st chip, 1 - 2nd chip, etc.
	// Resampler Type:
	//	00 - Old
	//	01 - Upsampling
	//	02 - Copy
	//	03 - Downsampling
	UINT8 Resampler;
	strm_func StreamUpdate;
	UINT32 SmpP;		// Current Sample (Playback Rate)
	UINT32 SmpLast;		// Sample Number Last
	UINT32 SmpNext;		// Sample Number Next
	WAVE_32BS LSmpl;	// Last Sample
	WAVE_32BS NSmpl;	// Next Sample
//	CAUD_ATTR* Paired;
};

typedef struct chip_audio_struct
{
	CAUD_ATTR SN76496;
	CAUD_ATTR YM2612;
	CAUD_ATTR uPD7759;
} CHIP_AUDIO;


//UINT8 StartAudioOutput(void);
//UINT8 StopAudioOutput(void);
static void SetupResampler(CAUD_ATTR* CAA);
INLINE INT16 Limit2Short(INT32 Value);
static void null_update(UINT8 ChipID, stream_sample_t **outputs, int samples);
static void ResampleChipStream(CAUD_ATTR* CAA, WAVE_32BS* RetSample, UINT32 Length);
//UINT32 FillBuffer(WAVE_16BS* Buffer, UINT32 BufferSize);
static void YM2612_Callback(void *param, int irq);

//void ym2612_fm_write(UINT8 ChipID, UINT8 Port, UINT8 Register, UINT8 Data);
//void sn76496_psg_write(UINT8 ChipID, UINT8 Data);


#define CLOCK_YM2612	7670454
#define CLOCK_SN76496	3579545

#define CHIP_COUNT		0x02

UINT32 SampleRate;	// Note: also used by some sound cores to determinate the chip sample rate

UINT8 ResampleMode;	// 00 - HQ both, 01 - LQ downsampling, 02 - LQ both
UINT8 CHIP_SAMPLING_MODE;
INT32 CHIP_SAMPLE_RATE;

static CHIP_AUDIO ChipAudio;

#define SMPL_BUFSIZE	0x100
static INT32* StreamBufs[0x02];
stream_sample_t* DUMMYBUF[0x02] = {NULL, NULL};

static UINT8 DeviceState = 0x00;	// 00 - not running, 01 - running
static UINT8 TimerExpired;
UINT16 FrameDivider = 60;
//static UINT32 SmplsPerFrame;
static UINT32 SmplsTilFrame;
static UINT8 TimerMask;

UINT32 PlayingTimer;
INT32 StoppedTimer;

UINT8 StartAudioOutput(void)
{
	UINT8 CurChip;
	UINT8 RetVal;
	CAUD_ATTR* CAA;
	
	if (DeviceState)
		return 0x80;	// already running
	
	SampleRate = 44100;
	ResampleMode = 0x00;
	CHIP_SAMPLING_MODE = 0x00;
	CHIP_SAMPLE_RATE = 0x00000000;
	
	for (CurChip = 0x00; CurChip < CHIP_COUNT; CurChip ++)
	{
		CAA = (CAUD_ATTR*)&ChipAudio + CurChip;
		CAA->SmpRate = 0x00;
		CAA->Volume = 0x00;
		CAA->ChipType = 0xFF;
		CAA->ChipID = 0x00;
		CAA->Resampler = 0x00;
		CAA->StreamUpdate = &null_update;
	}
	
	StreamBufs[0x00] = (INT32*)malloc(SMPL_BUFSIZE * sizeof(INT32));
	StreamBufs[0x01] = (INT32*)malloc(SMPL_BUFSIZE * sizeof(INT32));
	
	CAA = &ChipAudio.YM2612;
	CAA->SmpRate = device_start_ym2612(0x00, CLOCK_YM2612);
	CAA->StreamUpdate = &ym2612_stream_update;
	CAA->Volume = 0x100;
	device_reset_ym2612(0x00);
	ym2612_set_callback(0x00, &YM2612_Callback);
	SetupResampler(CAA);
	
	CAA = &ChipAudio.SN76496;
	CAA->SmpRate = device_start_sn764xx(0x00, CLOCK_SN76496, 0x10, 0x09, 1, 1, 0, 0);
	CAA->StreamUpdate = &sn764xx_stream_update;
	CAA->Volume = 0x80;
	device_reset_sn764xx(0x00);
	SetupResampler(CAA);
	
	CAA = &ChipAudio.uPD7759;
	CAA->SmpRate = device_start_upd7759(0x00, 0x80000000 | (UPD7759_STANDARD_CLOCK * 2));
	CAA->StreamUpdate = &upd7759_update;
	CAA->Volume = 0x2B;	// ~0.33 * PSG according to Kega Fusion 3.64
	device_reset_upd7759(0x00);
	SetupResampler(CAA);
	
	DeviceState = 0x01;
	TimerExpired = 0xFF;
	TimerMask = 0x03;
	PlayingTimer = 0;
	StoppedTimer = -1;
	
	//SmplsPerFrame = SampleRate / 60;
	SmplsTilFrame = 0;
	
	//SoundLogging(false);
	RetVal = StartStream(0x00);
	if (RetVal)
	{
		//printf("Error openning Sound Device!\n");
		StopAudioOutput();
		return 0xC0;
	}
	
	return 0x00;
}

UINT8 StopAudioOutput(void)
{
	UINT8 RetVal;
	
	if (! DeviceState)
		return 0x00;	// not running
	
	RetVal = StopStream();
	
	free(StreamBufs[0x00]);	StreamBufs[0x00] = NULL;
	free(StreamBufs[0x01]);	StreamBufs[0x01] = NULL;
	
	device_stop_ym2612(0x00);
	device_stop_sn764xx(0x00);
	device_stop_upd7759(0x00);
	DeviceState = 0x00;
	
	return 0x00;
}


static void SetupResampler(CAUD_ATTR* CAA)
{
	if (! CAA->SmpRate)
	{
		CAA->Resampler = 0xFF;
		return;
	}
	
	if (CAA->SmpRate < SampleRate)
		CAA->Resampler = 0x01;
	else if (CAA->SmpRate == SampleRate)
		CAA->Resampler = 0x02;
	else if (CAA->SmpRate > SampleRate)
		CAA->Resampler = 0x03;
	if (CAA->Resampler == 0x01 || CAA->Resampler == 0x03)
	{
		if (ResampleMode == 0x02 || (ResampleMode == 0x01 && CAA->Resampler == 0x03))
			CAA->Resampler = 0x00;
	}
	
	CAA->SmpP = 0x00;
	CAA->SmpLast = 0x00;
	CAA->SmpNext = 0x00;
	CAA->LSmpl.Left = 0x00;
	CAA->LSmpl.Right = 0x00;
	if (CAA->Resampler == 0x01)
	{
		// Pregenerate first Sample (the upsampler is always one too late)
		CAA->StreamUpdate(CAA->ChipID, StreamBufs, 1);
		CAA->NSmpl.Left = StreamBufs[0x00][0x00];
		CAA->NSmpl.Right = StreamBufs[0x01][0x00];
	}
	else
	{
		CAA->NSmpl.Left = 0x00;
		CAA->NSmpl.Right = 0x00;
	}
	
	return;
}

INLINE INT16 Limit2Short(INT32 Value)
{
	INT32 NewValue;
	
	NewValue = Value;
	if (NewValue < -0x8000)
		NewValue = -0x8000;
	if (NewValue > 0x7FFF)
		NewValue = 0x7FFF;
	
	return (INT16)NewValue;
}

static void null_update(UINT8 ChipID, stream_sample_t **outputs, int samples)
{
	memset(outputs[0x00], 0x00, sizeof(stream_sample_t) * samples);
	memset(outputs[0x01], 0x00, sizeof(stream_sample_t) * samples);
	
	return;
}

// I recommend 11 bits as it's fast and accurate
#define FIXPNT_BITS		11
#define FIXPNT_FACT		(1 << FIXPNT_BITS)
#if (FIXPNT_BITS <= 11)
	typedef UINT32	SLINT;	// 32-bit is a lot faster
#else
	typedef UINT64	SLINT;
#endif
#define FIXPNT_MASK		(FIXPNT_FACT - 1)

#define getfriction(x)	((x) & FIXPNT_MASK)
#define getnfriction(x)	((FIXPNT_FACT - (x)) & FIXPNT_MASK)
#define fpi_floor(x)	((x) & ~FIXPNT_MASK)
#define fpi_ceil(x)		((x + FIXPNT_MASK) & ~FIXPNT_MASK)
#define fp2i_floor(x)	((x) / FIXPNT_FACT)
#define fp2i_ceil(x)	((x + FIXPNT_MASK) / FIXPNT_FACT)

static void ResampleChipStream(CAUD_ATTR* CAA, WAVE_32BS* RetSample, UINT32 Length)
{
	INT32* CurBufL;
	INT32* CurBufR;
	INT32* StreamPnt[0x02];
	UINT32 InBase;
	UINT32 InPos;
	UINT32 InPosNext;
	UINT32 OutPos;
	UINT32 SmpFrc;	// Sample Friction
	UINT32 InPre;
	UINT32 InNow;
	SLINT InPosL;
	INT64 TempSmpL;
	INT64 TempSmpR;
	INT32 TempS32L;
	INT32 TempS32R;
	INT32 SmpCnt;	// must be signed, else I'm getting calculation errors
	INT32 CurSmpl;
	UINT64 ChipSmpRate;
	
	CurBufL = StreamBufs[0x00];
	CurBufR = StreamBufs[0x01];
	
	// This Do-While-Loop gets and resamples the chip output of one or more chips.
	// It's a loop to support the AY8910 paired with the YM2203/YM2608/YM2610.
	//do
	//{
		switch(CAA->Resampler)
		{
		case 0x00:	// old, but very fast resampler
			CAA->SmpLast = CAA->SmpNext;
			CAA->SmpP += Length;
			CAA->SmpNext = (UINT32)((UINT64)CAA->SmpP * CAA->SmpRate / SampleRate);
			if (CAA->SmpLast >= CAA->SmpNext)
			{
				RetSample->Left += CAA->LSmpl.Left * CAA->Volume;
				RetSample->Right += CAA->LSmpl.Right * CAA->Volume;
			}
			else
			{
				SmpCnt = CAA->SmpNext - CAA->SmpLast;
				
				CAA->StreamUpdate(CAA->ChipID, StreamBufs, SmpCnt);
				
				if (SmpCnt == 1)
				{
					RetSample->Left += CurBufL[0x00] * CAA->Volume;
					RetSample->Right += CurBufR[0x00] * CAA->Volume;
					CAA->LSmpl.Left = CurBufL[0x00];
					CAA->LSmpl.Right = CurBufR[0x00];
				}
				else if (SmpCnt == 2)
				{
					RetSample->Left += (CurBufL[0x00] + CurBufL[0x01]) * CAA->Volume >> 1;
					RetSample->Right += (CurBufR[0x00] + CurBufR[0x01]) * CAA->Volume >> 1;
					CAA->LSmpl.Left = CurBufL[0x01];
					CAA->LSmpl.Right = CurBufR[0x01];
				}
				else
				{
					TempS32L = CurBufL[0x00];
					TempS32R = CurBufR[0x00];
					for (CurSmpl = 0x01; CurSmpl < SmpCnt; CurSmpl ++)
					{
						TempS32L += CurBufL[CurSmpl];
						TempS32R += CurBufR[CurSmpl];
					}
					RetSample->Left += TempS32L * CAA->Volume / SmpCnt;
					RetSample->Right += TempS32R * CAA->Volume / SmpCnt;
					CAA->LSmpl.Left = CurBufL[SmpCnt - 1];
					CAA->LSmpl.Right = CurBufR[SmpCnt - 1];
				}
			}
			break;
		case 0x01:	// Upsampling
			ChipSmpRate = CAA->SmpRate;
			InPosL = (SLINT)(FIXPNT_FACT * CAA->SmpP * ChipSmpRate / SampleRate);
			InPre = (UINT32)fp2i_floor(InPosL);
			InNow = (UINT32)fp2i_ceil(InPosL);
			
			CurBufL[0x00] = CAA->LSmpl.Left;
			CurBufR[0x00] = CAA->LSmpl.Right;
			CurBufL[0x01] = CAA->NSmpl.Left;
			CurBufR[0x01] = CAA->NSmpl.Right;
			StreamPnt[0x00] = &CurBufL[0x02];
			StreamPnt[0x01] = &CurBufR[0x02];
			CAA->StreamUpdate(CAA->ChipID, StreamPnt, InNow - CAA->SmpNext);
			
			InBase = FIXPNT_FACT + (UINT32)(InPosL - (SLINT)CAA->SmpNext * FIXPNT_FACT);
			SmpCnt = FIXPNT_FACT;
			CAA->SmpLast = InPre;
			CAA->SmpNext = InNow;
			for (OutPos = 0x00; OutPos < Length; OutPos ++)
			{
				InPos = InBase + (UINT32)(FIXPNT_FACT * OutPos * ChipSmpRate / SampleRate);
				
				InPre = fp2i_floor(InPos);
				InNow = fp2i_ceil(InPos);
				SmpFrc = getfriction(InPos);
				
				// Linear interpolation
				TempSmpL = ((INT64)CurBufL[InPre] * (FIXPNT_FACT - SmpFrc)) +
							((INT64)CurBufL[InNow] * SmpFrc);
				TempSmpR = ((INT64)CurBufR[InPre] * (FIXPNT_FACT - SmpFrc)) +
							((INT64)CurBufR[InNow] * SmpFrc);
				RetSample[OutPos].Left += (INT32)(TempSmpL * CAA->Volume / SmpCnt);
				RetSample[OutPos].Right += (INT32)(TempSmpR * CAA->Volume / SmpCnt);
			}
			CAA->LSmpl.Left = CurBufL[InPre];
			CAA->LSmpl.Right = CurBufR[InPre];
			CAA->NSmpl.Left = CurBufL[InNow];
			CAA->NSmpl.Right = CurBufR[InNow];
			CAA->SmpP += Length;
			break;
		case 0x02:	// Copying
			CAA->SmpNext = CAA->SmpP * CAA->SmpRate / SampleRate;
			CAA->StreamUpdate(CAA->ChipID, StreamBufs, Length);
			
			for (OutPos = 0x00; OutPos < Length; OutPos ++)
			{
				RetSample[OutPos].Left += CurBufL[OutPos] * CAA->Volume;
				RetSample[OutPos].Right += CurBufR[OutPos] * CAA->Volume;
			}
			CAA->SmpP += Length;
			CAA->SmpLast = CAA->SmpNext;
			break;
		case 0x03:	// Downsampling
			ChipSmpRate = CAA->SmpRate;
			InPosL = (SLINT)(FIXPNT_FACT * (CAA->SmpP + Length) * ChipSmpRate / SampleRate);
			CAA->SmpNext = (UINT32)fp2i_ceil(InPosL);
			
			CurBufL[0x00] = CAA->LSmpl.Left;
			CurBufR[0x00] = CAA->LSmpl.Right;
			StreamPnt[0x00] = &CurBufL[0x01];
			StreamPnt[0x01] = &CurBufR[0x01];
			CAA->StreamUpdate(CAA->ChipID, StreamPnt, CAA->SmpNext - CAA->SmpLast);
			
			InPosL = (SLINT)(FIXPNT_FACT * CAA->SmpP * ChipSmpRate / SampleRate);
			// I'm adding 1.0 to avoid negative indexes
			InBase = FIXPNT_FACT + (UINT32)(InPosL - (SLINT)CAA->SmpLast * FIXPNT_FACT);
			InPosNext = InBase;
			for (OutPos = 0x00; OutPos < Length; OutPos ++)
			{
				//InPos = InBase + (UINT32)(FIXPNT_FACT * OutPos * ChipSmpRate / SampleRate);
				InPos = InPosNext;
				InPosNext = InBase + (UINT32)(FIXPNT_FACT * (OutPos+1) * ChipSmpRate / SampleRate);
				
				// first frictional Sample
				SmpFrc = getnfriction(InPos);
				if (SmpFrc)
				{
					InPre = fp2i_floor(InPos);
					TempSmpL = (INT64)CurBufL[InPre] * SmpFrc;
					TempSmpR = (INT64)CurBufR[InPre] * SmpFrc;
				}
				else
				{
					TempSmpL = TempSmpR = 0x00;
				}
				SmpCnt = SmpFrc;
				
				// last frictional Sample
				SmpFrc = getfriction(InPosNext);
				InPre = fp2i_floor(InPosNext);
				if (SmpFrc)
				{
					TempSmpL += (INT64)CurBufL[InPre] * SmpFrc;
					TempSmpR += (INT64)CurBufR[InPre] * SmpFrc;
					SmpCnt += SmpFrc;
				}
				
				// whole Samples in between
				//InPre = fp2i_floor(InPosNext);
				InNow = fp2i_ceil(InPos);
				SmpCnt += (InPre - InNow) * FIXPNT_FACT;	// this is faster
				while(InNow < InPre)
				{
					TempSmpL += (INT64)CurBufL[InNow] * FIXPNT_FACT;
					TempSmpR += (INT64)CurBufR[InNow] * FIXPNT_FACT;
					//SmpCnt ++;
					InNow ++;
				}
				
				RetSample[OutPos].Left += (INT32)(TempSmpL * CAA->Volume / SmpCnt);
				RetSample[OutPos].Right += (INT32)(TempSmpR * CAA->Volume / SmpCnt);
			}
			
			CAA->LSmpl.Left = CurBufL[InPre];
			CAA->LSmpl.Right = CurBufR[InPre];
			CAA->SmpP += Length;
			CAA->SmpLast = CAA->SmpNext;
			break;
		default:
			CAA->SmpP += SampleRate;
			break;	// do absolutely nothing
		}
		
		if (CAA->SmpLast >= CAA->SmpRate)
		{
			CAA->SmpLast -= CAA->SmpRate;
			CAA->SmpNext -= CAA->SmpRate;
			CAA->SmpP -= SampleRate;
		}
		
	//	CAA = CAA->Paired;
	//} while(CAA != NULL);
	
	return;
}

UINT32 FillBuffer(WAVE_16BS* Buffer, UINT32 BufferSize)
{
	UINT32 CurSmpl;
	WAVE_32BS TempBuf;
	//UINT8 CurChip;
	
	if (Buffer == NULL)
		return 0x00;
	
	for (CurSmpl = 0x00; CurSmpl < BufferSize; CurSmpl ++)
	{
		if (! SmplsTilFrame)
		{
			UpdateAll(UPDATEEVT_VINT);
			UpdateAll(UPDATEEVT_TIMER);	// check for Timer-based update (in case we changed timing)
			//SmplsTilFrame = SmplsPerFrame;
			SmplsTilFrame = SampleRate / FrameDivider;
		}
		SmplsTilFrame --;
		if (TimerExpired)
		{
			UpdateAll(UPDATEEVT_TIMER);
			TimerExpired = ym2612_r(0x00, 0x00) & TimerMask;
		}
		UpdateDAC(1);
		UpdateNECPCM();
		
		if (StoppedTimer != -1)
		{
			StoppedTimer ++;
			if (StoppedTimer == 2 * SampleRate)
				FinishedSongSignal();
			vgm_update(1);
		}
		else
		{
			if (PlayingTimer != -1)
			{
				PlayingTimer ++;
				vgm_update(1);
			}
		}
		
		TempBuf.Left = 0x00;
		TempBuf.Right = 0x00;
		
		//for (CurChip = 0x00; CurChip < MAX_CHIPS; CurChip ++)
		//	ResampleChipStream(CurChip, &TempBuf, 1);
		ResampleChipStream(&ChipAudio.YM2612, &TempBuf, 1);
		ResampleChipStream(&ChipAudio.SN76496, &TempBuf, 1);
		if (! upd7759_busy_r(0x00))
			ResampleChipStream(&ChipAudio.uPD7759, &TempBuf, 1);
		
		TempBuf.Left = TempBuf.Left >> 7;
		TempBuf.Right = TempBuf.Right >> 7;
		Buffer[CurSmpl].Left = Limit2Short(TempBuf.Left);
		Buffer[CurSmpl].Right = Limit2Short(TempBuf.Right);
	}
	
	return CurSmpl;
}

static void YM2612_Callback(void* param, int irq)
{
	TimerExpired = ym2612_r(0x00, 0x00) & TimerMask;
	
	return;
}




void ym2612_timer_mask(UINT8 Mask)
{
	TimerMask = Mask;
	TimerExpired = ym2612_r(0x00, 0x00) & TimerMask;
	
	return;
}

void ym2612_fm_write(UINT8 ChipID, UINT8 Port, UINT8 Register, UINT8 Data)
{
	// Note: Don't do DAC writes with this function to prevent spamming logged VGMs.
	
	ym2612_w(ChipID, 0x00 | (Port << 1), Register);
	ym2612_w(ChipID, 0x01 | (Port << 1), Data);
	
	vgm_write(VGMC_YM2612, Port, Register, Data);
	
	return;
}

void sn76496_psg_write(UINT8 ChipID, UINT8 Data)
{
	sn764xx_w(ChipID, 0x00, Data);
	
	vgm_write(VGMC_SN76496, 0, Data, 0);
	
	return;
}
