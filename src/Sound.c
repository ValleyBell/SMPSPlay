#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#ifdef _WIN32
#include <windows.h>	// for HWND
#endif

#include <stdtype.h>
#include <common_def.h>	// for INLINE
#include "Sound.h"
#include <audio/AudioStream.h>
#include <audio/AudioStream_SpcDrvFuns.h>
#include <utils/OSMutex.h>

#include <emu/EmuStructs.h>
#include <emu/SoundEmu.h>
#include <emu/Resampler.h>
#include <emu/SoundDevs.h>
//#include <emu/EmuCores.h>
#include <emu/cores/sn764intf.h>	// for SN76496_CFG

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


typedef struct chip_audio_attributes CAUD_ATTR;
struct chip_audio_attributes
{
	DEV_INFO defInf;
	RESMPL_STATE resmpl;
	DEVFUNC_WRITE_A8D8 write8;		// write 8-bit data to 8-bit register/offset
	DEVFUNC_READ_A8D8 read8;		// read 8-bit data to 8-bit register/offset
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
static UINT32 FillBuffer(void* Params, void* userParam, UINT32 bufSize, void* data);
static void YM2612_Callback(void *param, int irq);

//void ym2612_timer_mask(UINT8 Mask);
//UINT8 ym2612_fm_read(UINT8 ChipID);
//void ym2612_fm_write(UINT8 ChipID, UINT8 Port, UINT8 Register, UINT8 Data);
//void sn76496_psg_write(UINT8 ChipID, UINT8 Data);
//UINT8 upd7759_ready(void);
//UINT8 upd7759_get_fifo_space(void);
//void upd7759_write(UINT8 Func, UINT8 Data);


#define CLOCK_YM2612	7670454
#define CLOCK_SN76496	3579545
#define CLOCK_PICO_PCM	1280000

//#define VOL_SHIFT		7	// shift X bits to the right after mixing everything together
#define VOL_SHIFT		10	// 7 [main shift] + (8-5) [OutputVolume post-shift]

AUDIO_CFG AudioCfg;
static AUDIO_OPTS* audOpts;
UINT32 SampleRate;	// Note: also used by some sound cores to determinate the chip sample rate
INT32 OutputVolume = 0x100;

static CHIP_AUDIO ChipAudio;

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
static HWND hWndSnd = NULL;
#endif
static OS_MUTEX* hMutex = NULL;
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

static void InitOneChip(CAUD_ATTR* CAA, const DEV_GEN_CFG* devCfg, UINT8 chipID, UINT16 volume)
{
	UINT8 retVal;
	
	retVal = SndEmu_Start(chipID, devCfg, &CAA->defInf);
	if (retVal)
	{
		CAA->defInf.dataPtr = NULL;
		CAA->defInf.devDef = NULL;
		return;
	}
	SndEmu_FreeDevLinkData(&CAA->defInf);	// just in case
	
	SndEmu_GetDeviceFunc(CAA->defInf.devDef, RWF_REGISTER | RWF_WRITE, DEVRW_A8D8, 0, (void**)&CAA->write8);
	SndEmu_GetDeviceFunc(CAA->defInf.devDef, RWF_REGISTER | RWF_READ, DEVRW_A8D8, 0, (void**)&CAA->read8);
	Resmpl_SetVals(&CAA->resmpl, RSMODE_LINEAR, volume, audOpts->sampleRate);
	Resmpl_DevConnect(&CAA->resmpl, &CAA->defInf);
	Resmpl_Init(&CAA->resmpl);
	
	return;
}

static void InitalizeChips(void)
{
	DEV_GEN_CFG devCfg;
	SN76496_CFG snCfg;
	CAUD_ATTR* CAA;
	
	if (DeviceState)
		return;
	
	memset(&ChipAudio, 0x00, sizeof(CHIP_AUDIO));
	
	SampleRate = audOpts->sampleRate;	// used by dac.c, vgmwrite.c and main.c
	devCfg.emuCore = 0x00;	// default
	devCfg.srMode = DEVRI_SRMODE_NATIVE;
	devCfg.smplRate = audOpts->sampleRate;
	
	CAA = &ChipAudio.YM2612;
	devCfg.clock = CLOCK_YM2612;
	devCfg.flags = 0x00;
	//devCfg.emuCore = FCC_GPGX;
	InitOneChip(CAA, &devCfg, DEVID_YM2612, 0x100);
	//ym2612_set_callback(ChipAudio.YM2612.defInf.dataPtr, &YM2612_Callback);
	
	CAA = &ChipAudio.SN76496;
	devCfg.clock = CLOCK_SN76496;
	devCfg.flags = 0x00;
	//devCfg.emuCore = FCC_MAME;
	snCfg._genCfg = devCfg;
	snCfg.noiseTaps = 0x09;	snCfg.shiftRegWidth = 16;
	snCfg.negate = 1;		snCfg.clkDiv = 8;
	snCfg.segaPSG = 1;		snCfg.stereo = 1;
	snCfg.t6w28_tone = NULL;
	InitOneChip(CAA, (DEV_GEN_CFG*)&snCfg, DEVID_SN76496, 0x80);
	
#ifndef DISABLE_NECPCM
	CAA = &ChipAudio.uPD7759;
	devCfg.clock = CLOCK_PICO_PCM;
	devCfg.flags = 0x01;	// slave mode
	//devCfg.emuCore = FCC_MAME;
	InitOneChip(CAA, &devCfg, DEVID_uPD7759, 0x28);	// ~0.33 * PSG according to Kega Fusion 3.64
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
	
	if (ChipAudio.YM2612.defInf.dataPtr != NULL)
	{
		Resmpl_Deinit(&ChipAudio.YM2612.resmpl);
		SndEmu_Stop(&ChipAudio.YM2612.defInf);
	}
	if (ChipAudio.SN76496.defInf.dataPtr != NULL)
	{
		Resmpl_Deinit(&ChipAudio.SN76496.resmpl);
		SndEmu_Stop(&ChipAudio.SN76496.defInf);
	}
#ifndef DISABLE_NECPCM
	if (ChipAudio.uPD7759.defInf.dataPtr != NULL)
	{
		Resmpl_Deinit(&ChipAudio.uPD7759.resmpl);
		SndEmu_Stop(&ChipAudio.uPD7759.defInf);
	}
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
		printf("Error loading Audio Driver! (Error Code %02X)\n", RetVal);
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
	
	AudioDrv_SetCallback(audDrv, FillBuffer, NULL);
	if (audDrvLog != NULL)
	{
		AudioDrv_DataForward_Add(audDrv, audDrvLog);
		RetVal = AudioDrv_Start(audDrvLog, 0);
		if (RetVal)
			AudioDrv_Deinit(&audDrvLog);
	}
	OSMutex_Init(&hMutex, 0);
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
	
	OSMutex_Deinit(hMutex);	hMutex = NULL;
	
	if (audDrv != NULL)
	{
		RetVal = AudioDrv_Stop(audDrv);
		RetVal = AudioDrv_Deinit(&audDrv);
		audDrv = NULL;
	}
	if (audDrvLog != NULL)
	{
		RetVal = AudioDrv_Stop(audDrvLog);
		RetVal = AudioDrv_Deinit(&audDrvLog);
		audDrvLog = NULL;
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
	
	if (PauseAndWait)
	{
		UINT8 RetVal = OSMutex_Lock(hMutex);
		if (! RetVal)
			lastMutexLockMode = 1;
	}
	else
	{
		UINT8 RetVal = OSMutex_Unlock(hMutex);
		if (! RetVal)
			lastMutexLockMode = 0;
	}
	
	return;
}



UINT8 ToggleMuteAudioChannel(CHIP chip, UINT8 nChannel)
{
	UINT8 result;
	UINT32 mask = 1 << nChannel;
	UINT32* CurrentMuteMask;
	DEV_INFO* devInf;
	
	switch (chip)
	{
	case CHIP_YM2612:
		CurrentMuteMask = &MuteChannelMaskYm2612;
		devInf = &ChipAudio.YM2612.defInf;
		break;
	case CHIP_SN76496:
		CurrentMuteMask = &MuteChannelMaskSn76496;
		devInf = &ChipAudio.SN76496.defInf;
		break;
	}
	result = *CurrentMuteMask & mask;
	if (result != 0)
		*CurrentMuteMask &= ~mask;
	else
		*CurrentMuteMask |= mask;
	devInf->devDef->SetMuteMask(devInf->dataPtr, *CurrentMuteMask);
	
	return result;
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

static UINT32 FillBuffer(void* Params, void* userParam, UINT32 bufSize, void* data)
{
	UINT32 BufferSmpls;
	UINT8* Buffer;
	UINT32 CurSmpl;
	WAVE_32BS TempBuf;
	INT32 tempSmpl;
	
	if (data == NULL)
		return 0x00;
	
	OSMutex_Lock(hMutex);
	Buffer = (UINT8*)data;
	BufferSmpls = bufSize * 8 / audOpts->numBitsPerSmpl / 2;
	for (CurSmpl = 0; CurSmpl < BufferSmpls; CurSmpl ++)
	{
		if (! SmplsTilFrame)
		{
			UpdateAll(UPDATEEVT_VINT);
			UpdateAll(UPDATEEVT_TIMER);	// check for Timer-based update (in case we changed timing)
			//SmplsTilFrame = SmplsPerFrame;
			SmplsTilFrame = SampleRate / FrameDivider;
		}
		SmplsTilFrame --;
		YM2612_Callback(NULL, 0);	// need to call this here until I libvgm supports callbacks
		if (TimerExpired)
		{
			UpdateAll(UPDATEEVT_TIMER);
			TimerExpired = ym2612_fm_read() & TimerMask;
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
		
		TempBuf.L = 0;
		TempBuf.R = 0;
		
		Resmpl_Execute(&ChipAudio.YM2612.resmpl, 1, &TempBuf);
		Resmpl_Execute(&ChipAudio.SN76496.resmpl, 1, &TempBuf);
#ifndef DISABLE_NECPCM
		if (ChipAudio.uPD7759.defInf.dataPtr != NULL && ! upd7759_ready())
			Resmpl_Execute(&ChipAudio.uPD7759.resmpl, 1, &TempBuf);
#endif
		
		TempBuf.L = (TempBuf.L >> 5) * OutputVolume;
		TempBuf.R = (TempBuf.R >> 5) * OutputVolume;
		// now done by the LimitXBit routines
		//TempBuf.L = TempBuf.L >> VOL_SHIFT;
		//TempBuf.R = TempBuf.R >> VOL_SHIFT;
		switch(audOpts->numBitsPerSmpl)
		{
		case 8:	// 8-bit is unsigned
			*Buffer++ = Limit8Bit(TempBuf.L);
			*Buffer++ = Limit8Bit(TempBuf.R);
			break;
		case 16:
			((INT16*)Buffer)[0] = Limit16Bit(TempBuf.L);
			((INT16*)Buffer)[1] = Limit16Bit(TempBuf.R);
			Buffer += sizeof(INT16) * 2;
			break;
		case 24:
			tempSmpl = Limit24Bit(TempBuf.L);
			*Buffer++ = (tempSmpl >>  0) & 0xFF;
			*Buffer++ = (tempSmpl >>  8) & 0xFF;
			*Buffer++ = (tempSmpl >> 16) & 0xFF;
			tempSmpl = Limit24Bit(TempBuf.R);
			*Buffer++ = (tempSmpl >>  0) & 0xFF;
			*Buffer++ = (tempSmpl >>  8) & 0xFF;
			*Buffer++ = (tempSmpl >> 16) & 0xFF;
			break;
		case 32:
			((INT32*)Buffer)[0] = Limit32Bit(TempBuf.L);
			((INT32*)Buffer)[1] = Limit32Bit(TempBuf.R);
			Buffer += sizeof(INT32) * 2;
			break;
		}
	}
	OSMutex_Unlock(hMutex);
	
	return CurSmpl * audOpts->numBitsPerSmpl * 2 / 8;
}

static void YM2612_Callback(void* param, int irq)
{
	TimerExpired = ym2612_fm_read() & TimerMask;
	
	return;
}




void ym2612_timer_mask(UINT8 Mask)
{
	TimerMask = Mask;
	TimerExpired = ym2612_fm_read() & TimerMask;
	
	return;
}

UINT8 ym2612_fm_read(void)
{
	CAUD_ATTR* ym = &ChipAudio.YM2612;
	
	if (ym->defInf.dataPtr == NULL)
		return 0x00;
	return ym->read8(ym->defInf.dataPtr, 0x00);
}

void ym2612_direct_write(UINT8 Offset, UINT8 Data)
{
	CAUD_ATTR* ym = &ChipAudio.YM2612;
	
	if (ym->defInf.dataPtr == NULL)
		return;
	ym->write8(ym->defInf.dataPtr, Offset, Data);
	
	return;
}

void ym2612_fm_write(UINT8 Port, UINT8 Register, UINT8 Data)
{
	// Note: Don't do DAC writes with this function to prevent spamming logged VGMs.
	CAUD_ATTR* ym = &ChipAudio.YM2612;
	
	if (ym->defInf.dataPtr == NULL)
		return;
	ym->write8(ym->defInf.dataPtr, 0x00 | (Port << 1), Register);
	ym->write8(ym->defInf.dataPtr, 0x01 | (Port << 1), Data);
	
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_YM2612, Port, Register, Data);
#endif
	
	return;
}

void sn76496_psg_write(UINT8 Data)
{
	CAUD_ATTR* sn = &ChipAudio.SN76496;
	
	if (sn->defInf.dataPtr == NULL)
		return;
	sn->write8(sn->defInf.dataPtr, 0x00, Data);
	
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_SN76496, 0, Data, 0);
#endif
	
	return;
}

#ifndef DISABLE_NECPCM
UINT8 upd7759_ready(void)
{
	CAUD_ATTR* upd = &ChipAudio.uPD7759;
	
	if (upd->defInf.dataPtr == NULL)
		return 0x00;
	return upd->read8(upd->defInf.dataPtr, 0x00);
}

UINT8 upd7759_get_fifo_space(void)
{
	CAUD_ATTR* upd = &ChipAudio.uPD7759;
	
	if (upd->defInf.dataPtr == NULL)
		return 0x00;
	return upd->read8(upd->defInf.dataPtr, 'F');
}

void upd7759_write(UINT8 Func, UINT8 Data)
{
	CAUD_ATTR* upd = &ChipAudio.uPD7759;
	
	if (upd->defInf.dataPtr == NULL)
		return;
	upd->write8(upd->defInf.dataPtr, Func, Data);
	
#ifdef ENABLE_VGM_LOGGING
	vgm_write(VGMC_UPD7759, 0, Func, Data);
#endif
	
	return;
}
#endif
