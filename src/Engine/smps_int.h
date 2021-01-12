#ifndef __SMPS_INT_H__
#define __SMPS_INT_H__

// --- Internal SMPS functions ---
// Note: I know that "static" functions are out of place here,
//       but this way I have a nice list of all functions.

#include <common_def.h>
#include "smps_structs.h"
#include "smps_structs_int.h"

void WriteFMMain(const TRK_RAM* Trk, UINT8 Reg, UINT8 Data);

//void InitDriver(void);
//void DeinitDriver(void);
void FreeSMPSFile(SMPS_SET* SmpsSet);	// from loader_smps.c, but required by SMPS routines to do memory management
static void FreeSMPSFileRef_Zero(SMPS_SET** SmpsSetRef);
static UINT8 CleanSmpsTrack(TRK_RAM* Trk);
static void CleanSmpsFiles(void);

static void ResetYMTimerA(void);
static void ResetYMTimerB(void);

//void UpdateAll(void);
void UpdateMusic(void);
void UpdateSFX(void);

INLINE void UpdateTrack(TRK_RAM* Trk);
INLINE UINT8 ReprocTrackCheck(TRK_RAM* Trk, UINT8 ExecMode);
static void UpdateFMTrack(TRK_RAM* Trk);
static void UpdatePSGTrack(TRK_RAM* Trk);
static void UpdatePSGVolume(TRK_RAM* Trk, UINT8 WasNewNote);
static void UpdateDrumTrack(TRK_RAM* Trk);
static void Update2OpDrumTrack(DRUM_TRK_RAM* Trk);
static void UpdatePWMTrack(TRK_RAM* Trk);
static void UpdatePSGNoiseTrack(TRK_RAM* Trk);

static void SendDACFrequency(TRK_RAM* Trk, UINT16 Freq);
static void SendFMFrequency(TRK_RAM* Trk, UINT16 Freq);
INLINE UINT16* GetFM3FreqPtr(void);
static void SendPSGFrequency(TRK_RAM* Trk, UINT16 Freq);

static void TrkUpdate_Proc(TRK_RAM* Trk);
static void FinishTrkUpdate(TRK_RAM* Trk, UINT8 ReadDuration);
static UINT8 DoNoteStop(TRK_RAM* Trk);
UINT16 GetNote(TRK_RAM* Trk, UINT8 NoteCmd);

static void DoPanAnimation(TRK_RAM* Trk, UINT8 Continue);
static void InitLFOModulation(TRK_RAM* Trk);
static void ExecLFOModulation(TRK_RAM* Trk);
static void DoFMVolEnv(TRK_RAM* Trk);
static void PrepareModulation(TRK_RAM* Trk);
void LoadModulation(TRK_RAM* Trk);
static UINT8 DoModulation(TRK_RAM* Trk, UINT16* Freq);
static INT16 DoCustomModulation(TRK_RAM* Trk);
static INT16 DoModulatEnvelope(TRK_RAM* Trk, UINT8 EnvID);
static UINT8 DoVolumeEnvelope(TRK_RAM* Trk, UINT8 EnvID);
static UINT8 DoEnvelope(const ENV_DATA* EnvData, const UINT8* EnvCmds, UINT8* EnvIdx, UINT8* EnvMult);
static void PrepareADSR(TRK_RAM* Trk);
static UINT8 DoADSR(TRK_RAM* Trk);

void DoNoteOn(TRK_RAM* Trk);
void DoNoteOff(TRK_RAM* Trk);
static void DoPSGNoteOff(TRK_RAM* Trk, UINT8 OffByTimeout);
void Do2OpNote(void);
static void DoCinossuPortamento(TRK_RAM* Trk);
static UINT16 DoPitchSlide(TRK_RAM* Trk);
const INS_LIB* GetSongInsLib(TRK_RAM* Trk, UINT8 SongID);
void SendFMIns(TRK_RAM* Trk, const UINT8* InsData, const INS_LIB* InsLib);
void RefreshVolume(TRK_RAM* Trk);
const UINT8* GetOperatorOrder(const SMPS_CFG* SmpsCfg);
static UINT8 ApplyOutOperatorVol(TRK_RAM* Trk, UINT8 AlgoMask, UINT8 Reg, UINT8 CurTL);
void RefreshFMVolume(TRK_RAM* Trk);
void SendSSGEG(TRK_RAM* Trk, const UINT8* Data, UINT8 ForceMaxAtk);

static void InitMusicPlay(const SMPS_CFG* SmpsFileConfig);
static void InitTempo(UINT8 TempoVal);
static UINT8 GetTrackIDFromChnBits(SMPS_SET* SmpsSet, UINT8 ChnBits);
static void SetupMusicChnMasks(const SMPS_CFG* SmpsCfg);
static void PlayPreSMPS(SMPS_SET* SmpsSet);
void LoadPreSMPSTrack(TRK_RAM* Trk, const UINT8* Data);
static UINT8 CheckTrkRange(UINT8 TrkID, UINT8 BestTrkID, UINT8 FirstTrk, UINT8 TrkEnd);
static UINT8 CheckTrkID(UINT8 TrkID, UINT8 ChnBits);
static void LoadChannelSet(UINT8 TrkIDStart, UINT8 ChnCount, UINT16* FilePos, UINT8 Mode,
						   UINT8 ChnListSize, const UINT8* ChnList, UINT8 TickMult, UINT8 TrkBase);
//void PlayMusic(SMPS_SET* SmpsFileSet);
//void PlaySFX(SMPS_SET* SmpsFileSet, UINT8 SpecialSFX);
void GetSFXChnPtrs(UINT8 ChannelMask, UINT8* MusicTrk, UINT8* SFXTrk, UINT8* SpcSFXTrk);
UINT8 GetChannelTrack(UINT8 ChannelMask, UINT8 TrkCount, const TRK_RAM* Tracks);

static void DoPause(void);
static void DoTempo(void);
//void FadeOutMusic(void);
//void FadeOutMusic_Custom(UINT8 StepCnt, UINT8 DelayFrames);
static void DoFade(UINT8 FadeMode);
static void DoSpecialFade(void);

//void StopAllSound(void);
void ResetSpcFM3Mode(void);
void DisableSSGEG(TRK_RAM* Trk);
void SilenceFMChn(TRK_RAM* Trk);
static void SilenceAll(void);
static void SilencePSG(void);
void RestoreBGMChannel(TRK_RAM* Trk);

void BackupMusic(MUS_STATE* MusState);
void RestoreMusic(MUS_STATE* MusState);

//void SetDACState(UINT8 DacOn);


// smps_commands.c
void cfHandler(TRK_RAM* Trk, UINT8 Command);
static void cfMetaHandler(TRK_RAM* Trk, UINT8 Command);
UINT8 IsHoldCFNext(TRK_RAM* Trk);
static void DoCoordinationFlag(TRK_RAM* Trk, const CMD_FLAGS* CFlag);
static UINT8 GetInsRegPtrs(TRK_RAM* Trk, const UINT8** RetRegPtr, const UINT8** RetInsPtr, UINT8 Register);
static void cfSetIns_FM(TRK_RAM* Trk, UINT8 InsID, const INS_LIB* InsLib);
static void cfSetIns_PSG(TRK_RAM* Trk, UINT8 InsID);
static UINT8 cfSetInstrument(TRK_RAM* Trk, const CMD_FLAGS* CFlag, const UINT8* Params);
static UINT8 cfVolume(TRK_RAM* Trk, const CMD_FLAGS* CFlag, const UINT8* Params);
static UINT8 cfSpecialDAC(TRK_RAM* Trk, const CMD_FLAGS* CFlag);
void RefreshDACVolume(TRK_RAM* Trk, UINT8 DacMode, UINT8 DacChn, UINT8 Volume);
INLINE UINT16* GetFM3FreqPtr(void);
static void print_msg(TRK_RAM* Trk, UINT8 CmdLen, const char* DescStr);


// smps_drum.c
void PlayDrumNote(TRK_RAM* Trk, UINT8 Note);
static void DoDrum(TRK_RAM* Trk, const DRUM_DATA* DrumData);
static UINT8 LoadDrumMD(TRK_RAM* BaseTrk, TRK_RAM* DrumTrk, const DRUM_DATA* DrumData, UINT8 Mode,
						const UINT8** RetDTrkData);
static UINT8 LoadDrumMDPre(TRK_RAM* BaseTrk, TRK_RAM* DrumTrk, const DRUM_DATA* DrumData, UINT8 Mode,
							const UINT8** RetDTrkData);
static UINT8 LoadDrumMD2Op(TRK_RAM* Trk, DRUM_TRK_RAM* DrumTrk2Op, const DRUM_DATA* DrumData);
void PlayPS4DrumNote(TRK_RAM* Trk, UINT8 Note);
void PlaySMGP2DACNote(TRK_RAM* Trk, UINT8 Note);
void PlayPSGDrumNote(TRK_RAM* Trk, UINT8 Note);


#endif	// __SMPS_INT_H__
