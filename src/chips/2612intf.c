/***************************************************************************

  2612intf.c

  The YM2612 emulator supports up to 2 chips.
  Each chip has the following connections:
  - Status Read / Control Write A
  - Port Read / Data Write A
  - Control Write B
  - Data Write B

***************************************************************************/

#include <stdlib.h>
#include <stddef.h>	// for NULL
#include "mamedef.h"
#include "fm.h"
#include "2612intf.h"


typedef struct _ym2612_state ym2612_state;
struct _ym2612_state
{
	void *			chip;
};


extern UINT8 CHIP_SAMPLING_MODE;
extern INT32 CHIP_SAMPLE_RATE;

#define MAX_CHIPS	0x02
static ym2612_state YM2612Data[MAX_CHIPS];
static UINT8 ChipFlags = 0x00;

/* update request from fm.c */
void ym2612_update_request(void *param)
{
	ym2612_state *info = (ym2612_state *)param;
	//stream_update(info->stream);
	
	ym2612_update_one(info->chip, DUMMYBUF, 0);
}

/***********************************************************/
/*    YM2612                                               */
/***********************************************************/

//static STREAM_UPDATE( ym2612_stream_update )
void ym2612_stream_update(UINT8 ChipID, stream_sample_t **outputs, int samples)
{
	//ym2612_state *info = (ym2612_state *)param;
	ym2612_state *info = &YM2612Data[ChipID];
	
	ym2612_update_one(info->chip, outputs, samples);
}


//static DEVICE_START( ym2612 )
int device_start_ym2612(UINT8 ChipID, int clock)
{
	//ym2612_state *info = get_safe_token(device);
	ym2612_state *info;
	int rate;

	if (ChipID >= MAX_CHIPS)
		return 0;
	
	info = &YM2612Data[ChipID];
	rate = clock/72;
	if (! (ChipFlags & 0x02))
		rate /= 2;
	if ((CHIP_SAMPLING_MODE == 0x01 && rate < CHIP_SAMPLE_RATE) ||
		CHIP_SAMPLING_MODE == 0x02)
		rate = CHIP_SAMPLE_RATE;

	/**** initialize YM2612 ****/
	//info->chip = ym2612_init(info,clock,rate,timer_handler,IRQHandler);
	info->chip = ym2612_init(info, clock, rate, NULL, NULL);
	//assert_always(info->chip != NULL, "Error creating YM2612 chip");

	return rate;
}


//static DEVICE_STOP( ym2612 )
void device_stop_ym2612(UINT8 ChipID)
{
	//ym2612_state *info = get_safe_token(device);
	ym2612_state *info = &YM2612Data[ChipID];
	ym2612_shutdown(info->chip);
}

//static DEVICE_RESET( ym2612 )
void device_reset_ym2612(UINT8 ChipID)
{
	//ym2612_state *info = get_safe_token(device);
	ym2612_state *info = &YM2612Data[ChipID];
	ym2612_reset_chip(info->chip);
}

void ym2612_set_callback(UINT8 ChipID, FM_CALLBACK CallbackFunc)
{
	ym2612_state *info = &YM2612Data[ChipID];
	
	ym2612_set_irqhandler(info->chip, CallbackFunc);
}


//READ8_DEVICE_HANDLER( ym2612_r )
UINT8 ym2612_r(UINT8 ChipID, offs_t offset)
{
	//ym2612_state *info = get_safe_token(device);
	ym2612_state *info = &YM2612Data[ChipID];
	return ym2612_read(info->chip, offset & 3);
}

//WRITE8_DEVICE_HANDLER( ym2612_w )
void ym2612_w(UINT8 ChipID, offs_t offset, UINT8 data)
{
	//ym2612_state *info = get_safe_token(device);
	ym2612_state *info = &YM2612Data[ChipID];
	ym2612_write(info->chip, offset & 3, data);
}


UINT8 ym2612_status_port_a_r(UINT8 ChipID, offs_t offset)
{
	return ym2612_r(ChipID, 0);
}
UINT8 ym2612_status_port_b_r(UINT8 ChipID, offs_t offset)
{
	return ym2612_r(ChipID, 2);
}
UINT8 ym2612_data_port_a_r(UINT8 ChipID, offs_t offset)
{
	return ym2612_r(ChipID, 1);
}
UINT8 ym2612_data_port_b_r(UINT8 ChipID, offs_t offset)
{
	return ym2612_r(ChipID, 3);
}

void ym2612_control_port_a_w(UINT8 ChipID, offs_t offset, UINT8 data)
{
	ym2612_w(ChipID, 0, data);
}
void ym2612_control_port_b_w(UINT8 ChipID, offs_t offset, UINT8 data)
{
	ym2612_w(ChipID, 2, data);
}
void ym2612_data_port_a_w(UINT8 ChipID, offs_t offset, UINT8 data)
{
	ym2612_w(ChipID, 1, data);
}
void ym2612_data_port_b_w(UINT8 ChipID, offs_t offset, UINT8 data)
{
	ym2612_w(ChipID, 3, data);
}


void ym2612_set_options(UINT8 Flags)
{
	ChipFlags = Flags;
	ym2612_setoptions(Flags);
	
	return;
}

void ym2612_set_mute_mask(UINT8 ChipID, UINT32 MuteMask)
{
	ym2612_state *info = &YM2612Data[ChipID];
	ym2612_set_mutemask(info->chip, MuteMask);
	
	return;
}



/**************************************************************************
 * Generic get_info
 **************************************************************************/

/*DEVICE_GET_INFO( ym2612 )
{
	switch (state)
	{
		// --- the following bits of info are returned as 64-bit signed integers ---
		case DEVINFO_INT_TOKEN_BYTES:					info->i = sizeof(ym2612_state);				break;

		// --- the following bits of info are returned as pointers to data or functions ---
		case DEVINFO_FCT_START:							info->start = DEVICE_START_NAME( ym2612 );	break;
		case DEVINFO_FCT_STOP:							info->stop = DEVICE_STOP_NAME( ym2612 );	break;
		case DEVINFO_FCT_RESET:							info->reset = DEVICE_RESET_NAME( ym2612 );	break;

		// --- the following bits of info are returned as NULL-terminated strings ---
		case DEVINFO_STR_NAME:							strcpy(info->s, "YM2612");					break;
		case DEVINFO_STR_FAMILY:					strcpy(info->s, "Yamaha FM");				break;
		case DEVINFO_STR_VERSION:					strcpy(info->s, "1.0");						break;
		case DEVINFO_STR_SOURCE_FILE:						strcpy(info->s, __FILE__);					break;
		case DEVINFO_STR_CREDITS:					strcpy(info->s, "Copyright Nicola Salmoria and the MAME Team"); break;
	}
}*/
