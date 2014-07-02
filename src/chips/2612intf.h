#ifndef __2612INTF_H__
#define __2612INTF_H__

typedef void (*FM_CALLBACK)(void *param, int irq);

void ym2612_update_request(void *param);

void ym2612_stream_update(UINT8 ChipID, stream_sample_t **outputs, int samples);
int device_start_ym2612(UINT8 ChipID, int clock);
void device_stop_ym2612(UINT8 ChipID);
void device_reset_ym2612(UINT8 ChipID);
void ym2612_set_callback(UINT8 ChipID, FM_CALLBACK CallbackFunc);

UINT8 ym2612_r(UINT8 ChipID, offs_t offset);
void ym2612_w(UINT8 ChipID, offs_t offset, UINT8 data);

UINT8 ym2612_status_port_a_r(UINT8 ChipID, offs_t offset);
UINT8 ym2612_status_port_b_r(UINT8 ChipID, offs_t offset);
UINT8 ym2612_data_port_a_r(UINT8 ChipID, offs_t offset);
UINT8 ym2612_data_port_b_r(UINT8 ChipID, offs_t offset);

void ym2612_control_port_a_w(UINT8 ChipID, offs_t offset, UINT8 data);
void ym2612_control_port_b_w(UINT8 ChipID, offs_t offset, UINT8 data);
void ym2612_data_port_a_w(UINT8 ChipID, offs_t offset, UINT8 data);
void ym2612_data_port_b_w(UINT8 ChipID, offs_t offset, UINT8 data);

void ym2612_set_options(UINT8 Flags);
void ym2612_set_mute_mask(UINT8 ChipID, UINT32 MuteMask);

#endif /* __2612INTF_H__ */
