#ifndef __SMPS_INT_H__
#define __SMPS_INT_H__

// --- Internal SMPS functions ---

#include "stdtype.h"
#include "smps_structs.h"
#include "smps_structs_int.h"

void WriteFMMain(const TRK_RAM* Trk, UINT8 Reg, UINT8 Data);

//void InitDriver(void);
static void ResetYMTimerA(void);
static void ResetYMTimerB(void);

//void UpdateAll(void);
void UpdateMusic(void);
void UpdateSFX(void);

INLINE void UpdateTrack(TRK_RAM* Trk);
static void UpdateFMTrack(TRK_RAM* Trk);
static void UpdatePSGTrack(TRK_RAM* Trk);
static void UpdateDrumTrack(TRK_RAM* Trk);
static void UpdatePWMTrack(TRK_RAM* Trk);

static void SendFMFrequency(TRK_RAM* Trk, UINT16 Freq);
INLINE UINT16* GetFM3FreqPtr(void);
static void SendPSGFrequency(TRK_RAM* Trk, UINT16 Freq);

static void TrkUpdate_Proc(TRK_RAM* Trk);
static UINT16 GetNote(TRK_RAM* Trk, UINT8 NoteCmd);

static void DoPanAnimation(TRK_RAM* Trk, UINT8 Continue);
static void InitLFOModulation(TRK_RAM* Trk);
static void ExecLFOModulation(TRK_RAM* Trk);
static void DoFMVolEnv(TRK_RAM* Trk);
static void PrepareModulat(TRK_RAM* Trk);
static UINT8 DoModulation(TRK_RAM* Trk, UINT16* Freq);
static INT16 DoCustomModulation(TRK_RAM* Trk);
static INT16 DoModulatEnvelope(TRK_RAM* Trk, UINT8 EnvID);
static UINT8 DoVolumeEnvelope(TRK_RAM* Trk, UINT8 EnvID);
static UINT8 DoEnvelope(const ENV_DATA* EnvData, const UINT8* EnvCmds, UINT8* EnvIdx, UINT8* EnvMult);

void DoNoteOn(TRK_RAM* Trk);
void DoNoteOff(TRK_RAM* Trk);
static UINT16 DoPitchSlide(TRK_RAM* Trk);
void SendFMIns(TRK_RAM* Trk, const UINT8* InsData);
void RefreshVolume(TRK_RAM* Trk);
void SendSSGEG(TRK_RAM* Trk, const UINT8* Data, UINT8 ForceMaxAtk);

//void PlayMusic(SMPS_CFG* SmpsFileConfig);
//void PlaySFX(SMPS_CFG* SmpsFileConfig, UINT8 SpecialSFX);
void GetSFXChnPtrs(UINT8 ChannelMask, UINT8* MusicTrk, UINT8* SFXTrk, UINT8* SpcSFXTrk);
UINT8 GetChannelTrack(UINT8 ChannelMask, UINT8 TrkCount, const TRK_RAM* Tracks);

static void DoPause(void);
static void DoTempo(void);
//void FadeOutMusic(void);
static void DoFadeOut(void);

//void StopAllSound(void);
void ResetSpcFM3Mode(void);
void DisableSSGEG(TRK_RAM* Trk);
void SilenceFMChn(TRK_RAM* Trk);
static void SilenceAll(void);
static void SilencePSG(void);

//void SetDACState(UINT8 DacOn);


// smps_commands.c
void cfHandler(TRK_RAM* Trk, UINT8 Command);
//static void cfMetaHandler(TRK_RAM* Trk, UINT8 Command);
//static void DoCoordinationFlag(TRK_RAM* Trk, const CMD_FLAGS* CFlag);
//static UINT8 cfVolume(TRK_RAM* Trk, const CMD_FLAGS* CFlag, const UINT8* Params);
void RefreshDACVolume(TRK_RAM* Trk, UINT8 DacMode, UINT8 DacChn, UINT8 Volume);


// smps_drum.c
void PlayDrumNote(TRK_RAM* Trk, UINT8 Note);
void PlayPS4DrumNote(TRK_RAM* Trk, UINT8 Note);


#endif	// __SMPS_INT_H__
