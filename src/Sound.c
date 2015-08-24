#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

// mutex functions/types
#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#include <stdtype.h>
#include "Sound.h"
#include <audio/AudioStream.h>
#include <audio/AudioStream_SpcDrvFuns.h>

#include "chips/mamedef.h"
#include "chips/2612intf.h"
#include "chips/sn764intf.h"
#ifndef DISABLE_NECPCM
#include "chips/upd7759.h"
#endif
#include "Engine/smps.h"
#include "Engine/dac.h"
#ifndef DISABLE_NECPCM
#include "Engine/necpcm.h"
#endif
#ifdef ENABLE_VGM_LOGGING
#include "vgmwrite.h"
#endif


#ifdef _MSC_VER
#define stricmp		_stricmp
#else
#define stricmp		strcasecmp
#endif


typedef void (*strm_func)(UINT8 ChipID, stream_sample_t **outputs, int samples);

typedef struct waveform_16bit_stereo
{
	INT16 Left;
	INT16 Right;
} WAVE_16BS;
typedef struct waveform_32bit_stereo
{
	INT32 Left;
	INT32 Right;
} WAVE_32BS;

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
#ifndef DISABLE_NECPCM
	CAUD_ATTR uPD7759;
#endif
} CHIP_AUDIO;


//void InitAudioOutput(void);
//void DeinitAudioOutput(void);
static UINT32 GetAudioDriver(UINT8 Type, const char* PreferredDrv);
static void InitalizeChips(void);
static void DeinitChips(void);
//UINT8 QueryDeviceParams(const char* audAPIName, AUDIO_CFG* retAudioCfg);
//void SetAudioHWnd(void* hWnd);
//UINT8 StartAudioOutput(void);
//UINT8 StopAudioOutput(void);
//void PauseStream(UINT8 PauseOn);
//UINT8 ToggleMuteAudioChannel(CHIP chip, UINT8 nChannel);
static void SetupResampler(CAUD_ATTR* CAA);
INLINE UINT8 Limit8Bit(INT32 Value);
INLINE INT16 Limit16Bit(INT32 Value);
INLINE INT32 Limit24Bit(INT32 Value);
INLINE INT32 Limit32Bit(INT32 Value);
static void null_update(UINT8 ChipID, stream_sample_t **outputs, int samples);
static void ResampleChipStream(CAUD_ATTR* CAA, WAVE_32BS* RetSample, UINT32 Length);
static UINT32 FillBuffer(void* Params, UINT32 bufSize, void* data);
static void YM2612_Callback(void *param, int irq);

//void ym2612_fm_write(UINT8 ChipID, UINT8 Port, UINT8 Register, UINT8 Data);
//void sn76496_psg_write(UINT8 ChipID, UINT8 Data);


#define CLOCK_YM2612	7670454
#define CLOCK_SN76496	3579545

#ifdef DISABLE_NECPCM
#define CHIP_COUNT		0x02
#else
#define CHIP_COUNT		0x03
#endif

//#define VOL_SHIFT		7	// shift X bits to the right after mixing everything together
#define VOL_SHIFT		10	// 7 [main shift] + (8-5) [OutputVolume post-shift]

AUDIO_CFG AudioCfg;
static AUDIO_OPTS* audOpts;
UINT32 SampleRate;	// Note: also used by some sound cores to determinate the chip sample rate
INT32 OutputVolume = 0x100;

UINT8 ResampleMode;	// 00 - HQ both, 01 - LQ downsampling, 02 - LQ both
UINT8 CHIP_SAMPLING_MODE;
INT32 CHIP_SAMPLE_RATE;

static CHIP_AUDIO ChipAudio;

#define SMPL_BUFSIZE	0x100
static INT32* StreamBufs[0x02];
stream_sample_t* DUMMYBUF[0x02] = {NULL, NULL};

static UINT8 DeviceState = 0xFF;	// FF - not initialized, 00 - not running, 01 - running
static void* audDrv;
static void* audDrvLog;

static UINT8 TimerExpired;
UINT16 FrameDivider = 60;
//static UINT32 SmplsPerFrame;
static UINT32 SmplsTilFrame;
static UINT8 TimerMask;
static UINT32 MuteChannelMaskYm2612 = 0;
static UINT32 MuteChannelMaskSn76496 = 0;

volatile UINT32 SMPS_PlayingTimer;
volatile INT32 SMPS_StoppedTimer;
volatile INT32 SMPS_CountdownTimer;
extern SMPS_CB_SIGNAL CB_Signal;

#ifdef _WIN32
HANDLE hMutex = NULL;
HWND hWndSnd = NULL;
#else
pthread_mutex_t hMutex = 0;
#endif
static UINT8 lastMutexLockMode;

void InitAudioOutput(void)
{
	memset(&AudioCfg, 0x00, sizeof(AUDIO_CFG));
	
	Audio_Init();
	
	DeviceState = 0x00;
	return;
}

void DeinitAudioOutput(void)
{
	Audio_Deinit();
	
	return;
}

static UINT32 GetAudioDriver(UINT8 Type, const char* PreferredDrv)
{
	UINT32 idDrv;
	AUDDRV_INFO* drvInfo;
	UINT32 drvCount;
	UINT32 curDrv;
	
	drvCount = Audio_GetDriverCount();
	idDrv = (UINT32)-1;
	for (curDrv = 0; curDrv < drvCount; curDrv ++)
	{
		Audio_GetDriverInfo(curDrv, &drvInfo);
		if (drvInfo->drvType != Type)
			continue;
		
		if (PreferredDrv != NULL)
		{
			if (! stricmp(drvInfo->drvName, PreferredDrv))
				return curDrv;
		}
		else
		{
			if (drvInfo->drvSig == ADRVSIG_WASAPI)
				continue;	// WASAPI is crap due to limited sample rates
			
			// uncomment to use the first possible device
			//if (idDrv == (UINT32)-1)
			// We use the last device ID here, because the more "advanced" devices
			// have later IDs.
			idDrv = curDrv;
		}
	}
	return idDrv;
}

static void InitalizeChips(void)
{
	UINT8 CurChip;
	CAUD_ATTR* CAA;
	
	if (DeviceState)
		return;
	
	ResampleMode = 0x00;
	CHIP_SAMPLING_MODE = 0x00;
	CHIP_SAMPLE_RATE = 0x00000000;
	SampleRate = audOpts->sampleRate;	// used by some chips as output sample rate
	
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
	
#ifndef DISABLE_NECPCM
	CAA = &ChipAudio.uPD7759;
	CAA->SmpRate = device_start_upd7759(0x00, 0x80000000 | (UPD7759_STANDARD_CLOCK * 2));
	CAA->StreamUpdate = &upd7759_update;
	CAA->Volume = 0x2B;	// ~0.33 * PSG according to Kega Fusion 3.64
	device_reset_upd7759(0x00);
	SetupResampler(CAA);
#endif
	
	TimerExpired = 0xFF;
	TimerMask = 0x03;
	SMPS_PlayingTimer = 0;
	SMPS_StoppedTimer = -1;
	SMPS_CountdownTimer = 0;
	
	//SmplsPerFrame = SampleRate / 60;
	SmplsTilFrame = 0;
	
	DeviceState = 0x01;
	return;
}

static void DeinitChips(void)
{
	if (DeviceState != 0x01)
		return;
	
	free(StreamBufs[0x00]);	StreamBufs[0x00] = NULL;
	free(StreamBufs[0x01]);	StreamBufs[0x01] = NULL;
	
	device_stop_ym2612(0x00);
	device_stop_sn764xx(0x00);
#ifndef DISABLE_NECPCM
	device_stop_upd7759(0x00);
#endif
	DeviceState = 0x00;
	
	return;
}


UINT8 QueryDeviceParams(const char* audAPIName, AUDIO_CFG* retAudioCfg)
{
	UINT8 RetVal;
	UINT32 idWaveOut;
	void* tempAudDrv;
	AUDDRV_INFO* drvInfo;
	AUDIO_OPTS* tempAudOpts;
	
	idWaveOut = GetAudioDriver(ADRVTYPE_OUT, audAPIName);
	if (idWaveOut == (UINT32)-1)
		return 0xFF;
	
	RetVal = AudioDrv_Init(idWaveOut, &tempAudDrv);
	if (RetVal)
		return 0xC0;
	Audio_GetDriverInfo(idWaveOut, &drvInfo);
	
	tempAudOpts = AudioDrv_GetOptions(tempAudDrv);
	memset(retAudioCfg, 0x00, sizeof(AUDIO_CFG));
	retAudioCfg->AudAPIName = (char*)audAPIName;
	retAudioCfg->AudioBufs = tempAudOpts->numBuffers;
	retAudioCfg->AudioBufSize = (tempAudOpts->usecPerBuf + 500) / 1000;
	retAudioCfg->SamplePerSec = tempAudOpts->sampleRate;
	retAudioCfg->BitsPerSample = tempAudOpts->numBitsPerSmpl;
	
	AudioDrv_Deinit(&tempAudDrv);
	
	return 0x00;
}

#ifdef _WIN32
void SetAudioHWnd(void* hWnd)
{
	hWndSnd = (HWND)hWnd;
	
	return;
}
#endif

UINT8 StartAudioOutput(void)
{
	UINT8 RetVal;
	UINT32 idWaveOut;
	UINT32 idWaveWrt;
	AUDDRV_INFO* drvInfo;
	AUDIO_OPTS* optsLog;
	void* aDrv;
	
	if (DeviceState)
		return 0x80;	// already running
	
	audDrv = audDrvLog = NULL;
	idWaveOut = GetAudioDriver(ADRVTYPE_OUT, AudioCfg.AudAPIName);
	idWaveWrt = GetAudioDriver(ADRVTYPE_DISK, "WaveWrite");
	
	RetVal = AudioDrv_Init(idWaveOut, &audDrv);
	if (RetVal)
	{
		audDrv = NULL;
		printf("Error loading Audio Driver!\n");
		StopAudioOutput();
		return 0xC0;
	}
	Audio_GetDriverInfo(idWaveOut, &drvInfo);
#if defined(_WIN32) && defined(AUDDRV_DSOUND)
	if (drvInfo->drvSig == ADRVSIG_DSOUND)
	{
		aDrv = AudioDrv_GetDrvData(audDrv);
		DSound_SetHWnd(aDrv, hWndSnd);
	}
#endif
	if (AudioCfg.LogWave && idWaveWrt != (UINT32)-1 && AudioCfg.WaveLogPath != NULL)
	{
		RetVal = AudioDrv_Init(idWaveWrt, &audDrvLog);
		if (! RetVal)
		{
			Audio_GetDriverInfo(idWaveWrt, &drvInfo);
			if (drvInfo->drvSig == ADRVSIG_WAVEWRT)
			{
				aDrv = AudioDrv_GetDrvData(audDrvLog);
				WavWrt_SetFileName(aDrv, AudioCfg.WaveLogPath);
			}
		}
	}
	
	audOpts = AudioDrv_GetOptions(audDrv);
	if (AudioCfg.SamplePerSec)
		audOpts->sampleRate = AudioCfg.SamplePerSec;
	audOpts->numChannels = 2;
	if (AudioCfg.BitsPerSample)
		audOpts->numBitsPerSmpl = AudioCfg.BitsPerSample;
	if (AudioCfg.AudioBufs)
		audOpts->numBuffers = AudioCfg.AudioBufs;
	if (AudioCfg.AudioBufSize)
		audOpts->usecPerBuf = AudioCfg.AudioBufSize * 1000;
	if (AudioCfg.Volume > 0.0f)
		OutputVolume = (INT32)(AudioCfg.Volume * 0x100 + 0.5f);
	if (audDrvLog != NULL)
	{
		optsLog = AudioDrv_GetOptions(audDrvLog);
		*optsLog = *audOpts;
	}
	
	AudioDrv_SetCallback(audDrv, FillBuffer);
	if (audDrvLog != NULL)
	{
		AudioDrv_DataForward_Add(audDrv, audDrvLog);
		RetVal = AudioDrv_Start(audDrvLog, 0);
		if (RetVal)
			AudioDrv_Deinit(&audDrvLog);
	}
#ifdef _WIN32
	hMutex = CreateMutex(NULL, FALSE, NULL);
#else
	pthread_mutex_init(&hMutex, NULL);
#endif
	lastMutexLockMode = 0;
	InitalizeChips();
	RetVal = AudioDrv_Start(audDrv, AudioCfg.AudAPIDev);
	if (RetVal)
	{
		printf("Error openning Sound Device! (Error Code %02X)\n", RetVal);
		StopAudioOutput();
		return 0xC0;
	}
	
	return 0x00;
}

UINT8 StopAudioOutput(void)
{
	UINT8 RetVal;
	
#ifdef _WIN32
	if (hMutex != NULL)
	{
		ReleaseMutex(hMutex);
		CloseHandle(hMutex);
		hMutex = NULL;
	}
#else
	if (hMutex)
	{
		pthread_mutex_unlock(&hMutex);
		pthread_mutex_destroy(&hMutex);
		hMutex = 0;
	}
#endif
	
	if (audDrv != NULL)
	{
		RetVal = AudioDrv_Stop(audDrv);
		RetVal = AudioDrv_Deinit(&audDrv);
	}
	if (audDrvLog != NULL)
	{
		RetVal = AudioDrv_Stop(audDrvLog);
		RetVal = AudioDrv_Deinit(&audDrvLog);
	}
	
	DeinitChips();
	
	return 0x00;
}

void PauseStream(UINT8 PauseOn)
{
	if (audDrv == NULL)
		return;
	
	if (PauseOn)
		AudioDrv_Pause(audDrv);
	else
		AudioDrv_Resume(audDrv);
	
	return;
}

void ThreadSync(UINT8 PauseAndWait)
{
	if (PauseAndWait == lastMutexLockMode)
		return;
	
#ifdef _WIN32
	if (PauseAndWait)
	{
		DWORD RetVal;
		
		RetVal = WaitForSingleObject(hMutex, INFINITE);
		if (RetVal == WAIT_OBJECT_0)	// success
			lastMutexLockMode = 1;
	}
	else
	{
		BOOL RetVal;
		
		RetVal = ReleaseMutex(hMutex);
		if (RetVal)
			lastMutexLockMode = 0;
	}
#else
	int RetVal;
	
	if (PauseAndWait)
	{
		RetVal = pthread_mutex_lock(&hMutex);
		if (! RetVal)
			lastMutexLockMode = 1;
	}
	else
	{
		RetVal = pthread_mutex_unlock(&hMutex);
		if (! RetVal)
			lastMutexLockMode = 0;
	}
#endif
	
	return;
}



UINT8 ToggleMuteAudioChannel(CHIP chip, UINT8 nChannel)
{
	UINT8 result;
	UINT32 mask = 1 << nChannel;
	UINT32* CurrentMuteMask;
	void(*fMuteMask)(UINT8 ChipID, UINT32 MuteMask);
	
	switch (chip)
	{
	case CHIP_YM2612:
		CurrentMuteMask = &MuteChannelMaskYm2612;
		fMuteMask = ym2612_set_mute_mask;
		break;
	case CHIP_SN76496:
		CurrentMuteMask = &MuteChannelMaskSn76496;
		fMuteMask = sn764xx_set_mute_mask;
		break;
	}
	result = *CurrentMuteMask & mask;
	if (result != 0)
		*CurrentMuteMask &= ~mask;
	else
		*CurrentMuteMask |= mask;
	fMuteMask(0, *CurrentMuteMask);
	
	return result;
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

INLINE UINT8 Limit8Bit(INT32 Value)
{
	INT32 NewValue;
	
	// divide by (8 + VOL_SHIFT) with proper rounding
	Value += (0x80 << VOL_SHIFT);	// add rounding term (1 << (8 + VOL_SHIFT)) / 2
	NewValue = Value >> (8 + VOL_SHIFT);
	if (NewValue < -0x80)
		NewValue = -0x80;
	else if (NewValue > +0x7F)
		NewValue = +0x7F;
	
	return (UINT8)(0x80 + NewValue);	// return unsigned 8-bit
}

INLINE INT16 Limit16Bit(INT32 Value)
{
	INT32 NewValue;
	
	NewValue = Value >> VOL_SHIFT;
	if (NewValue < -0x8000)
		NewValue = -0x8000;
	else if (NewValue > +0x7FFF)
		NewValue = +0x7FFF;

	return (INT16)NewValue;
}

INLINE INT32 Limit24Bit(INT32 Value)
{
	INT32 NewValue;
	
#if (VOL_SHIFT < 8)
	NewValue = (Value << 8 >> VOL_SHIFT) + (Value >> (8 + VOL_SHIFT));
#else
	NewValue = (Value >> (VOL_SHIFT - 8)) + (Value >> (8 + VOL_SHIFT));
#endif
	if (NewValue < -0x800000)
		NewValue = -0x800000;
	else if (NewValue > +0x7FFFFF)
		NewValue = +0x7FFFFF;
	return NewValue;
}

INLINE INT32 Limit32Bit(INT32 Value)
{
	INT64 NewValue;
	
	NewValue = ((INT64)Value << 16 >> VOL_SHIFT) + (Value >> VOL_SHIFT);
#ifndef _MSC_VER
	if (NewValue < -0x80000000LL)
		NewValue = -0x80000000LL;
	else if (NewValue > +0x7FFFFFFFLL)
		NewValue = +0x7FFFFFFFLL;
#else	// fallback for MS VC++ 6.0
	if (NewValue < -0x80000000i64)
		NewValue = -0x80000000i64;
	else if (NewValue > +0x7FFFFFFFi64)
		NewValue = +0x7FFFFFFFi64;
#endif
	return (INT32)NewValue;
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

static UINT32 FillBuffer(void* Params, UINT32 bufSize, void* data)
{
	UINT32 BufferSmpls;
	UINT8* Buffer;
	UINT32 CurSmpl;
	WAVE_32BS TempBuf;
	//UINT8 CurChip;
	INT32 tempSmpl;
	
	if (data == NULL)
		return 0x00;
	
#ifdef _WIN32
	WaitForSingleObject(hMutex, INFINITE);
#else
	pthread_mutex_lock(&hMutex);
#endif
	Buffer = (UINT8*)data;
	BufferSmpls = bufSize * 8 / audOpts->numBitsPerSmpl / 2;
	for (CurSmpl = 0x00; CurSmpl < BufferSmpls; CurSmpl ++)
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
#ifndef DISABLE_NECPCM
		UpdateNECPCM();
#endif
		
		// count time and do VGM timing
		if (SMPS_CountdownTimer)
		{
			SMPS_CountdownTimer --;
			if (! SMPS_CountdownTimer && CB_Signal != NULL)
				CB_Signal();
		}
		if (SMPS_StoppedTimer != -1)
			SMPS_StoppedTimer ++;
		else if (SMPS_PlayingTimer != -1)
			SMPS_PlayingTimer ++;
		
#ifdef ENABLE_VGM_LOGGING
		if (SMPS_StoppedTimer != -1 || SMPS_PlayingTimer != -1)
			vgm_update(1);
#endif
		
		TempBuf.Left = 0x00;
		TempBuf.Right = 0x00;
		
		//for (CurChip = 0x00; CurChip < MAX_CHIPS; CurChip ++)
		//	ResampleChipStream(CurChip, &TempBuf, 1);
		ResampleChipStream(&ChipAudio.YM2612, &TempBuf, 1);
		ResampleChipStream(&ChipAudio.SN76496, &TempBuf, 1);
#ifndef DISABLE_NECPCM
		if (! upd7759_busy_r(0x00))
			ResampleChipStream(&ChipAudio.uPD7759, &TempBuf, 1);
#endif
		
		TempBuf.Left = (TempBuf.Left >> 5) * OutputVolume;
		TempBuf.Right = (TempBuf.Right >> 5) * OutputVolume;
		// now done by the LimitXBit routines
		//TempBuf.Left = TempBuf.Left >> VOL_SHIFT;
		//TempBuf.Right = TempBuf.Right >> VOL_SHIFT;
		switch(audOpts->numBitsPerSmpl)
		{
		case 8:	// 8-bit is unsigned
			*Buffer++ = Limit8Bit(TempBuf.Left);
			*Buffer++ = Limit8Bit(TempBuf.Right);
			break;
		case 16:
			((INT16*)Buffer)[0] = Limit16Bit(TempBuf.Left);
			((INT16*)Buffer)[1] = Limit16Bit(TempBuf.Right);
			Buffer += sizeof(INT16) * 2;
			break;
		case 24:
			tempSmpl = Limit24Bit(TempBuf.Left);
			*Buffer++ = (tempSmpl >>  0) & 0xFF;
			*Buffer++ = (tempSmpl >>  8) & 0xFF;
			*Buffer++ = (tempSmpl >> 16) & 0xFF;
			tempSmpl = Limit24Bit(TempBuf.Right);
			*Buffer++ = (tempSmpl >>  0) & 0xFF;
			*Buffer++ = (tempSmpl >>  8) & 0xFF;
			*Buffer++ = (tempSmpl >> 16) & 0xFF;
			break;
		case 32:
			((INT32*)Buffer)[0] = Limit32Bit(TempBuf.Left);
			((INT32*)Buffer)[1] = Limit32Bit(TempBuf.Right);
			Buffer += sizeof(INT32) * 2;
			break;
		}
	}
#ifdef _WIN32
	ReleaseMutex(hMutex);
#else
	pthread_mutex_unlock(&hMutex);
#endif
	
	return CurSmpl * audOpts->numBitsPerSmpl * 2 / 8;
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
	
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_YM2612, Port, Register, Data);
#endif
	
	return;
}

void sn76496_psg_write(UINT8 ChipID, UINT8 Data)
{
	sn764xx_w(ChipID, 0x00, Data);
	
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_SN76496, 0, Data, 0);
#endif
	
	return;
}
