#ifndef __SOUND_H__
#define __SOUND_H__

#include "stdtype.h"

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

typedef UINT8* WAVE_BINARY;

typedef enum chip_type
{
	CHIP_YM2612,
	CHIP_SN76496,
} CHIP;

UINT8 StartAudioOutput(void);
UINT8 StopAudioOutput(void);
UINT8 ToggleMuteAudioChannel(CHIP chip, UINT8 nChannel);
UINT32 FillBuffer(WAVE_BINARY Buffer, UINT32 BufferSize);

void ym2612_timer_mask(UINT8 Mask);
void ym2612_fm_write(UINT8 ChipID, UINT8 Port, UINT8 Register, UINT8 Data);
void sn76496_psg_write(UINT8 ChipID, UINT8 Data);

#endif	// __SOUND_H__
