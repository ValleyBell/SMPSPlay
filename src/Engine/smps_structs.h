#ifndef __SMPS_STRUCTS_H__
#define __SMPS_STRUCTS_H__

#include <stdtype.h>
#include "dac.h"	// for DAC_CFG

typedef struct _file_data
{
	UINT8 alloc;	// 0 - don't touch Data pointer, 1 - free automatically
	UINT16 Len;
	UINT8* Data;
} FILE_DATA;

// SMPS Settings
// -------------
#define PTRFMT_BE		0x00	// Big Endian
#define PTRFMT_LE		0x10	// Little Endian
#define PTRFMT_EMASK	0x10	// Endianess mask

//#define PTRFMT_		(PTRFMT_BE | 0x01)	// 68k, relative to (ptr+0)
#define PTRFMT_68K		(PTRFMT_BE | 0x02)	// 68k, relative to (ptr+1)
#define PTRFMT_RST		(PTRFMT_BE | 0x03)	// 68k, relative to (ptr+2)
#define PTRFMT_Z80		(PTRFMT_LE | 0x00)	// Z80, absolute
#define PTRFMT_OFSMASK	0x0F	// relative pointer Offset Mask

#define INSMODE_DEF		0x00	// default: x0 x8 x4 xC
#define INSMODE_HW		0x01	// hardware order: x0 x4 x8 xC
#define INSMODE_CST		0x10	// custom format (use InsRegs pointer)
#define INSMODE_INT		0x80	// interleaved register/data

#define TEMPO_TIMEOUT	0x00	// preSMPS, most SMPS 68k, SMPS Z80 Type 1
#define TEMPO_OVERFLOW	0x01	// SMPS Z80 Type 2
#define TEMPO_OVERFLOW2	0x02	// Sonic 2/OutRunners
#define TEMPO_TOUT_OFLW	0x03	// Golden Axe III
#define TEMPO_OFLW_MULT	0x04	// Shadow Squadron 32x
#define TEMPO_TOUT_REV	0x05	// Castle of Illusion
#define TEMPO_NONE		0xFF

#define T1TICK_NOTEMPO	0x00	// DoTempo before PlayMusic (not executed during first tick after PlayMusic) (most SMPS 68k/Z80)
#define T1TICK_DOTEMPO	0x01	// PlayMusic before DoTempo

#define FMBASEN_B		+1		// table starts with B (note 81 is still a C)
#define FMBASEN_C		 0		// table starts with C

#define PSGBASEN_C		 0		// table starts with C
#define PSGBASEN_B		+1		// table starts with B (note 81 is still a C)

// behaviour of rests (Note-Rest-Delay-Delay behaviour)
#define DLYFREQ_RESET	0x00	// reset frequency (replay note)
#define DLYFREQ_KEEP	0x01	// keep frequency (play rest)

// behaviour of "E7 Note"
#define NONPREV_HOLD	0x00	// prevent NoteOn when "Hold" bit is set (SMPS Z80)
#define NONPREV_REST	0x01	// prevent NoteOn when "at Rest" bit is set (SMPS 68k)

#define MODALGO_68K		0x00	// 68k Modulation algorithm
#define MODALGO_Z80		0x10	// Z80 Modulation algorithm
#define MODULAT_68K		(MODALGO_68K | 0x00)	// SMPS 68k
#define MODULAT_68K_1	(MODALGO_68K | 0x01)	// Sonic 2 (SMPS 68k + DoModulation on DoNote)
#define MODULAT_Z80		(MODALGO_Z80 | 0x01)	// SMPS Z80 Type 2 DAC (fixed)
#define MODULAT_Z80_B	(MODALGO_Z80 | 0x01 | 0x02)	// SMPS Z80 (broken)

#define ENVMULT_PRE		0x00	// unsigned multiplier, prevents multiplication by 0
#define ENVMULT_68K		0x01	// signed multiplier
#define ENVMULT_Z80		0x02	// unsigned multiplier, Multiplier+1

#define VOLMODE_ALGO	0x00	// the Algorithm setting sets the Output TL operators
#define VOLMODE_BIT7	0x01	// TL operators with Bit 7 set are Output TL operators
#define VOLMODE_SETVOL	0x10	// set TL to Volume instead of adding them

#define DCHNMODE_NORMAL	0x00
#define DCHNMODE_PS4	0x01	// Phantasy Star IV
#define DCHNMODE_GAXE3	0x02	// Golden Axe III
#define DCHNMODE_CYMN	0x03	// Chou Yakyuu Miracle Nine
#define DCHNMODE_VRDLX	0x04	// Virtua Racing Deluxe 32x
#define DCHNMODE_S2R	0x05	// Sonic 2 Recreation
#define DCHNMODE_SMGP2	0x06	// Ayrton Senna's Super Monaco GP II

#define FADEMODE_Z80	0x00	// delay by (n-1) ticks
#define FADEMODE_68K	0x01	// delay by n ticks

#define ENVCMD_DATA		0x00	// not a command, but usual envelope data
#define ENVCMD_RESET	0x01	// reset Envelope (set env. index to 0)
#define ENVCMD_HOLD		0x02	// hold Envelope at current level
#define ENVCMD_LOOP		0x03	// jump back to env. index xx
#define ENVCMD_STOP		0x04	// stop the Envelope and Note
#define ENVCMD_CHGMULT	0x05	// change Envelope Multiplier
#define ENVCMD_VST_MHLD	0x06	// VolEnv: STOP, ModEnv: HOLD

#define DRMMODE_NORMAL	0x00	// one note = one drum
#define DRMMODE_DUAL	0x01	// one note = 2 drums (FM/PSG)

#define DRMTYPE_FM		0x01
#define DRMTYPE_PSG		0x02
#define DRMTYPE_DAC		0x03
#define DRMTYPE_FM2OP	0x04	// preSMPS 2op drums (using separate tracks)
#define DRMTYPE_FMDAC	0x05	// load mini-track, similar to FM drums
#define DRMTYPE_NECPCM	0x06	// Sega Pico NEC ADPCM drums
#define DRMTYPE_NONE	0x00

// Music:
#define SEQFLG_NEED_SAVE	0x01
// SFX:
#define SEQFLG_CONT_SFX		0x10

#define INSTYPE_NONE	0xFF
#define INSTYPE_SEQ		0x00	// data is part of the sequence
#define INSTYPE_GBL		0x01	// data is copy from global instrument data

typedef struct _command_flags
{
	UINT8 Type;
	UINT8 SubType;
	UINT8 Len;
	UINT8 JumpOfs;
} CMD_FLAGS;
typedef struct _command_flag_library
{
	UINT16 FlagCount;
	UINT8 FlagBase;
	CMD_FLAGS* CmdData;
} CMD_LIB;
typedef struct _instrument_library
{
	UINT8 Type;
	UINT16 InsCount;
	UINT8** InsPtrs;
} INS_LIB;
typedef struct _fade_configuration
{
	UINT8 Steps;
	UINT8 Delay;
	UINT8 AddFM;
	UINT8 AddPSG;
} FADE_CFG;

/*typedef struct _envelope_data
{
	UINT8 Len;
	UINT8* Data;
} ENV_DATA;*/
typedef FILE_DATA ENV_DATA;
typedef struct _envelope_library
{
	UINT8 EnvCount;
	ENV_DATA* EnvData;
} ENV_LIB;
typedef struct _drum_data
{
	UINT8 Type;
	UINT8 ChnMask;
	UINT16 DrumID;
	UINT32 PitchOvr;	// DAC Pitch Override
} DRUM_DATA;
typedef struct _drum_library
{
	UINT8 Mode;
	UINT8 Mask1;
	UINT8 Shift1;
	UINT8 Mask2;
	UINT8 Shift2;
	UINT8 DrumCount;
	DRUM_DATA* DrumData;
} DRUM_LIB;
typedef struct _psg_drum_data
{
	UINT8 VolEnv;	// PSG instrument
	UINT8 Volume;
	UINT8 NoiseMode;
	UINT8 Ch3Vol;
	UINT8 Ch3Slide;	// PSG 3 pitch slide
	UINT16 Ch3Freq;
} PSG_DRUM_DATA;
typedef struct _psg_drum_library
{
	UINT8 DrumCount;
	PSG_DRUM_DATA* DrumData;
} PSG_DRUM_LIB;
typedef struct _smps_initial_configuration
{
	UINT8 Timing_DefMode;
	UINT8 Timing_Lock;
	UINT16 Timing_TimerA;
	UINT8 Timing_TimerB;
} SMPS_CFG_INIT;
typedef struct _drum_track_library
{
	//UINT16 DataLen;
	//UINT8* Data;
	FILE_DATA File;
	UINT8 SmpsPtrFmt;
	UINT8 TickMult;
	UINT8 DrumCount;
	UINT16 DrumBase;
	UINT16* DrumList;
	INS_LIB InsLib;
} DRUM_TRK_LIB;
typedef struct _pan_animation_library
{
	UINT16 DataLen;
	UINT8* Data;
	UINT8 AniCount;
	UINT16 AniBase;
	UINT16* AniList;
} PAN_ANI_LIB;
typedef struct _smps_configuration	// global SMPS driver configuration
{
	SMPS_CFG_INIT InitCfg;
	
	UINT8 PtrFmt;
	UINT8 InsMode;
	UINT8 TempoMode;
	UINT8 Tempo1Tick;	// order of PlayMusic and DoTempo in the first tick
	INT8 FMBaseNote;
	UINT8 FMBaseOct;
	UINT8 FMOctWrap;
	INT8 PSGBaseNote;
	UINT8 DelayFreq;
	UINT8 NoteOnPrevent;
	UINT8 FM6DACOff;	// turn DAC off when FM6 note is played
	UINT8 ModAlgo;
	UINT8 EnvMult;
	UINT8 VolMode;
	UINT8 DrumChnMode;
	
	DRUM_LIB DrumLib;
	PSG_DRUM_LIB PSGDrumLib;
	UINT16 GblInsBase;
	FILE_DATA GblIns;
	INS_LIB GblInsLib;
	
	UINT8 FMChnCnt;
	UINT8 FMChnList[0x10];
	UINT8 PSGChnCnt;
	UINT8 PSGChnList[0x04];
	UINT8 AddChnCnt;
	UINT8 AddChnList[0x10];
	UINT8 InsRegCnt;
	UINT8* InsRegs;
	UINT8* InsReg_TL;
	
	UINT8 FMFreqCnt;
	UINT16* FMFreqs;
	UINT8 PSGFreqCnt;
	UINT16* PSGFreqs;
	UINT8 FM3FreqCnt;
	UINT16* FM3Freqs;
	
	UINT8 FadeMode;
	FADE_CFG FadeOut;
	FADE_CFG FadeIn;
	
	UINT8 EnvCmds[0x80];
	ENV_LIB ModEnvs;
	ENV_LIB VolEnvs;
	PAN_ANI_LIB PanAnims;
	
	DAC_CFG DACDrv;
	DRUM_TRK_LIB FMDrums;
	DRUM_TRK_LIB PSGDrums;
	
	UINT8 NoteBase;
	CMD_LIB CmdList;
	CMD_LIB CmdMetaList;
} SMPS_CFG;

#ifdef ENABLE_LOOP_DETECTION
typedef struct _smps_loop_pointer
{
	UINT16 Ptr;
	UINT16 SrcOfs;
} SMPS_LOOPPTR;
#endif

typedef struct _smps_settings	// sequence-specific settings
{
	const SMPS_CFG* Cfg;
	char CfgExtFCC[4];	// Four-Char-Code: extention of files that use this configuration
						// Using this, I can reload the SMPS configuration and re-link everything later.
	
	UINT8 UsageCounter;
	UINT8 SeqFlags;
	UINT16 SeqBase;		// Z80 only: Sequence Base Offset
	FILE_DATA Seq;
	
	UINT16 InsBase;
	INS_LIB InsLib;
	
#ifdef ENABLE_LOOP_DETECTION
	SMPS_LOOPPTR* LoopPtrs;
#endif
} SMPS_SET;


#endif // __SMPS_STRUCTS_H__
