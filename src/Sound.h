#ifndef __SOUND_H__
#define __SOUND_H__

#include <stdtype.h>

typedef struct _audio_config
{
	char* AudAPIName;
	char* WaveLogPath;
	
	UINT8 LogWave;
	UINT8 BitsPerSample;
	UINT32 SamplePerSec;
	float Volume;
	UINT32 AudioBufs;
	UINT32 AudioBufSize;
	UINT32 AudAPIDev;
} AUDIO_CFG;

typedef enum chip_type
{
	CHIP_YM2612,
	CHIP_SN76496,
} CHIP;

void InitAudioOutput(void);
void DeinitAudioOutput(void);
#ifdef _WIN32
void SetAudioHWnd(void* hWnd);
#endif
UINT8 QueryDeviceParams(const char* audAPIName, AUDIO_CFG* retAudioCfg);
UINT8 StartAudioOutput(void);
UINT8 StopAudioOutput(void);
void PauseStream(UINT8 PauseOn);
void ThreadSync(UINT8 PauseAndWait);
UINT8 ToggleMuteAudioChannel(CHIP chip, UINT8 nChannel);

void ym2612_timer_mask(UINT8 Mask);
UINT8 ym2612_fm_read(void);
void ym2612_direct_write(UINT8 Offset, UINT8 Data);
void ym2612_fm_write(UINT8 Port, UINT8 Register, UINT8 Data);
void sn76496_psg_write(UINT8 Data);

#ifndef DISABLE_NECPCM
UINT8 upd7759_ready(void);
UINT8 upd7759_get_fifo_space(void);
void upd7759_write(UINT8 Func, UINT8 Data);
#endif

#endif	// __SOUND_H__
