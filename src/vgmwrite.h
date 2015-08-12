#ifndef __VGMWRITE_H__
#define __VGMWRITE_H__

#include <stdtype.h>

void vgm_init(void);
void vgm_deinit(void);
void vgm_set_chip_enable(UINT8 Mask);
void MakeVgmFileName(const char* FileName);
int vgm_dump_start(void);
int vgm_dump_stop(void);
void vgm_update(UINT32 PbkSamples);
void vgm_write(UINT8 chip_type, UINT8 port, UINT16 r, UINT8 v);
void vgm_write_large_data(UINT8 chip_type, UINT8 type, UINT32 datasize, UINT32 value1, UINT32 value2, const void* data);
void vgm_write_stream_data_command(UINT8 stream, UINT8 type, UINT32 data);
void vgm_set_loop(UINT8 SetLoop);

// VGM Chip Constants
#define VGMC_SN76496	0x00
#define VGMC_YM2612		0x02
#define VGMC_RF5C164	0x11
#define VGMC_PWM		0x12
#define VGMC_UPD7759	0x16

#define VGM_CEN_SCD_PCM	0x01
#define VGM_CEN_32X_PWM	0x02
#define VGM_CEN_PICOPCM	0x04

extern UINT8 Enable_VGMDumping;
//extern UINT8 VGM_IgnoreWrt; only used by smps.c

#endif /* __VGMWRITE_H__ */
