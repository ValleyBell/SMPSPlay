/****************************************************************

    MAME / MESS functions

****************************************************************/

#include "mamedef.h"
#include "sn76496.h"
#include "sn764intf.h"


/* for stream system */
typedef struct _sn764xx_state sn764xx_state;
struct _sn764xx_state
{
	void *chip;
};

extern UINT32 SampleRate;
#define MAX_CHIPS	0x02
static sn764xx_state SN764xxData[MAX_CHIPS];

void sn764xx_stream_update(UINT8 ChipID, stream_sample_t **outputs, int samples)
{
	sn764xx_state *info = &SN764xxData[ChipID];
	SN76496Update(info->chip, outputs, samples);
}

int device_start_sn764xx(UINT8 ChipID, int clock, int shiftregwidth, int noisetaps,
						 int negate, int stereo, int clockdivider, int freq0)
{
	sn764xx_state *info;
	int rate;
	
	if (ChipID >= MAX_CHIPS)
		return 0;
	
	info = &SN764xxData[ChipID];
	/* emulator create */
	rate = sn76496_start(&info->chip, clock, shiftregwidth, noisetaps,
						negate, stereo, clockdivider, freq0);
	sn76496_freq_limiter(clock & 0x3FFFFFFF, clockdivider, SampleRate);
 
	return rate;
}

void device_stop_sn764xx(UINT8 ChipID)
{
	sn764xx_state *info = &SN764xxData[ChipID];
	sn76496_shutdown(info->chip);
}

void device_reset_sn764xx(UINT8 ChipID)
{
	sn764xx_state *info = &SN764xxData[ChipID];
	sn76496_reset(info->chip);
}


void sn764xx_w(UINT8 ChipID, offs_t offset, UINT8 data)
{
	sn764xx_state *info = &SN764xxData[ChipID];
	switch(offset)
	{
	case 0x00:
		sn76496_write_reg(info->chip, offset & 1, data);
		break;
	case 0x01:
		sn76496_stereo_w(info->chip, offset, data);
		break;
	}
}

void sn764xx_set_mute_mask(UINT8 ChipID, UINT32 MuteMask)
{
	sn764xx_state *info = &SN764xxData[ChipID];
	sn76496_set_mutemask(info->chip, MuteMask);
	
	return;
}
