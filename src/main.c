// SMPS Player
// -----------
// Written by Valley Bell, 2014-2015

#define SMPSPLAY_VER	"2.11"
//#define BETA

#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif
#include <direct.h>		// for _mkdir()
#include <conio.h>
#include <windows.h>
#include <sys/types.h>
#include <sys/stat.h>	// for stat()
#include "dirent.h"

#include <common_def.h>

#include "loader_ini.h"
#include "loader_smps.h"
#include "ini_lib.h"
#include "Sound.h"
#include "Engine/smps_structs.h"
#include "Engine/smps.h"
#include "Engine/dac.h"
#ifndef DISABLE_NECPCM
#include "Engine/necpcm.h"
#endif
#ifdef ENABLE_VGM_LOGGING
#include "vgmwrite.h"
#endif


int main(int argc, char* argv[]);
static void InitSmpsFile(SMPS_SET* SmpsFile, UINT32 FileLen, UINT8* FileData, const SMPS_EXT_DEF* ExtDef);
static void GetFileList(const char* DirPath);
static void PrintFileList(void);
static UINT32 GetFileData(const char* FileName, UINT8** RetBuffer);

void ClearLine(void);
void DisplayFileID(int FileID);
void RedrawStatusLine(void);
static void ReDisplayFileID(int FileID);
static void WaitTimeForKey(unsigned int MSec);
static void WaitForKey(void);
static void FinishedSongSignal(void);
static void SmpsStopSignal(void);
static void SmpsLoopSignal(void);
static void SmpsCountdownSignal(void);
static void CommVarChangeCallback(void);


typedef struct _file_name
{
	char* Full;		// full path
	char* Title;	// file title only
	char* Ext;		// extention (incl. dot)
	UINT16 SeqBase;
} FILE_NAME;

static UINT32 FileCount;
static UINT32 FileAlloc;
static FILE_NAME* FileList;
static UINT32 cursor = 0;
static UINT32 smps_playing = -1;
static volatile UINT8 ExitWait;
static bool GoToNextSong;
static bool CondVarChg;

extern UINT32 SampleRate;	// from Sound.c
extern INT32 OutputVolume;
extern UINT16 FrameDivider;
extern volatile UINT32 SMPS_PlayingTimer;
extern volatile INT32 SMPS_LoopCntr;
extern volatile INT32 SMPS_StoppedTimer;
extern volatile INT32 SMPS_CountdownTimer;

bool PauseMode;
bool PALMode;
static bool AutoProgress;
UINT8 DebugMsgs;
#ifdef ENABLE_VGM_LOGGING
UINT8 VGM_DataBlkCompress = 1;
UINT8 VGM_NoLooping = 0;
#endif
static UINT8 LastLineState;

static UINT8* CondJumpVar;
static UINT8* CommunicationVar;

CONFIG_DATA Config;
extern AUDIO_CFG AudioCfg;

int main(int argc, char* argv[])
{
	UINT8 RetVal;
	int inkey;
	UINT32 NewSeqLen;
	UINT8* NewSeqData;
	SMPS_SET LastSmpsCfg;
	SMPS_EXT_DEF* NewSmpsEDef;
	UINT8 NChannel;
	UINT8 MuteToggleResult;
	char* ChipName;
	
	printf("SMPS Music Player v" SMPSPLAY_VER "\n");
	printf("-----------------\n");
#ifndef BETA
	printf("by Valley Bell\n");
#else
	printf("by Valley Bell, beta version\n");
#endif
	
	InitAudioOutput();
#ifdef ENABLE_VGM_LOGGING
	vgm_init();
#endif
	memset(&Config, 0x00, sizeof(CONFIG_DATA));
	Config.AudioCfg = &AudioCfg;
	Config.FM6DACOff = 0xFF;
	Config.ResmplForce = 0xFF;
	
	LoadConfigurationFiles(&Config, "config.ini");
	DebugMsgs = Config.DebugMsgs;
	LoadExtentionData(&Config.ExtList);
	FileList = NULL;
	
	if (! Config.ExtList.ExtCount)
	{
		printf("No extentions defined! Closing.\n");
		getchar();
		goto FinishProgram;
	}
	
#ifdef ENABLE_VGM_LOGGING
	VGM_NoLooping = Config.DisableVGMLoop;
#endif
	if (Config.FM6DACOff != 0xFF)
	{
		for (smps_playing = 0; smps_playing < Config.ExtList.ExtCount; smps_playing ++)
		{
			NewSmpsEDef = &Config.ExtList.ExtData[smps_playing];
			NewSmpsEDef->SmpsCfg.FM6DACOff = Config.FM6DACOff;
		}
	}
	if (Config.ResmplForce != 0xFF)
	{
		for (smps_playing = 0; smps_playing < Config.ExtList.ExtCount; smps_playing ++)
		{
			NewSmpsEDef = &Config.ExtList.ExtData[smps_playing];
			NewSmpsEDef->SmpsCfg.DACDrv.Cfg.SmplMode = Config.ResmplForce;
		}
	}
	
	FileCount = 0x00;
	FileAlloc = 0x00;
	GetFileList(Config.MusPath);
	PrintFileList();
	
	if (! FileCount)
	{
		printf("No SMPS files found! Closing.\n");
		getchar();
		goto FinishProgram;
	}
	
	DAC_Reset();
#ifndef DISABLE_NECPCM
	NECPCM_Reset();
#endif
	PALMode = false;
	FrameDivider = PALMode ? 50 : 60;
	
	_mkdir("dumps");
	AudioCfg.WaveLogPath = "dumps/out.wav";
	
#ifdef _WIN32
#if _WIN32_WINNT >= 0x500
	SetAudioHWnd(GetConsoleWindow());
#else
	SetAudioHWnd(GetDesktopWindow());	// not as nice, but works
#endif
#endif	// _WIN32
	
	RetVal = StartAudioOutput();
	if (RetVal)
		goto FinishProgram;
	
	InitDriver();
	//SMPSExtra_SetCallbacks(SMPSCB_START, NULL);
	SMPSExtra_SetCallbacks(SMPSCB_STOP, &SmpsStopSignal);
	SMPSExtra_SetCallbacks(SMPSCB_LOOP, &SmpsLoopSignal);
	SMPSExtra_SetCallbacks(SMPSCB_CNTDOWN, &SmpsCountdownSignal);
	SMPSExtra_SetCallbacks(SMPSCB_COMM_VAR, &CommVarChangeCallback);
	CommunicationVar = SmpsGetVariable(SMPSVAR_COMMUNICATION);
	CondJumpVar = SmpsGetVariable(SMPSVAR_CONDIT_JUMP);
	SMPS_PlayingTimer = 0;
	SMPS_LoopCntr = -1;
	SMPS_StoppedTimer = -1;
	SMPS_CountdownTimer = 0;
	CondVarChg = false;
	GoToNextSong = false;
	ExitWait = 0;
	
	smps_playing = -1;
	memset(&LastSmpsCfg, 0x00, sizeof(SMPS_SET));
	LastLineState = 0xFF;
	DisplayFileID(cursor);
	inkey = 0x00;
	while(inkey != 0x1B)
	{
		switch(inkey)
		{
		case '1':
		case '2':
		case '3':
		case '4':
		case '5':
		case '6':
		case '7':
		case '8':
		case '9':
		case '0':
		case '*':
			if (inkey == '*')
			{
				NChannel = 10;
			}
			else
			{
				NChannel = inkey - '0';
				if (NChannel > 0)
					NChannel --;
				else
					NChannel = 9;
			}
			if (NChannel < 6)
			{
				MuteToggleResult = ToggleMuteAudioChannel(CHIP_YM2612, NChannel);
				ChipName = "YM2612";
			}
			else if (NChannel < 10)
			{
				NChannel -= 6;
				MuteToggleResult = ToggleMuteAudioChannel(CHIP_SN76496, NChannel);
				ChipName = "SN76496";
			}
			else
			{
				NChannel = 0;
				MuteToggleResult = ToggleMuteAudioChannel(CHIP_YM2612, 6);
				ChipName = "DAC";
			}
			ClearLine();
			printf("Channel %d of %s %s\r", NChannel + 1, ChipName, MuteToggleResult ? "enabled" : "disabled");
			WaitTimeForKey(1000);
			DisplayFileID(cursor);
			break;
		case 0xE0:
			inkey = _getch();
			switch(inkey)
			{
			case 0x48:	// Cursor Up
				if (cursor > 0)
				{
					cursor--;
					DisplayFileID(cursor);
				}
				break;
			case 0x50:	// Cursor Down
				if (cursor < FileCount-1)
				{
					cursor ++;
					DisplayFileID(cursor);
				}
				break;
			}
			break;
		case 'N':
			if (smps_playing >= FileCount - 1)
				break;
			
			cursor = smps_playing + 1;
			// fall through
		case 0x0D:
			NewSmpsEDef = GetExtentionData(&Config.ExtList, FileList[cursor].Ext);
			if (NewSmpsEDef == NULL && ! Config.ExtFilter)
				NewSmpsEDef = &Config.ExtList.ExtData[0];
			if (NewSmpsEDef != NULL)
				NewSeqLen = GetFileData(FileList[cursor].Full, &NewSeqData);
			if (NewSmpsEDef != NULL && NewSeqLen)
			{
				ThreadSync(1);
				
#ifdef ENABLE_VGM_LOGGING
				vgm_set_loop(0x00);
				vgm_dump_stop();
#endif
				
				SMPS_PlayingTimer = 0;
				smps_playing = -1;
				
				InitSmpsFile(&LastSmpsCfg, NewSeqLen, NewSeqData, NewSmpsEDef);
				
				RetVal = GuessSMPSOffset(&LastSmpsCfg);
				if (! RetVal)
					FileList[cursor].SeqBase = LastSmpsCfg.SeqBase;
				if ((LastSmpsCfg.Cfg->PtrFmt & PTRFMT_OFSMASK) == 0x00)
					SmpsOffsetFromFilename(FileList[cursor].Title, &LastSmpsCfg.SeqBase);
				
				if (! RetVal)
					RetVal = PreparseSMPSFile(&LastSmpsCfg);
				if (! RetVal)
				{
					//FileList[cursor].SeqBase = LastSmpsCfg.SeqBase;
#ifdef ENABLE_VGM_LOGGING
					MakeVgmFileName(FileList[cursor].Title);
#endif
					PlayMusic(&LastSmpsCfg);
					smps_playing = cursor;
					SMPS_CountdownTimer = 0;
				}
				else
				{
					StopAllSound();
					FreeSMPSFile(&LastSmpsCfg);
				}
				NewSeqData = NULL;
			}
			
			PauseMode = false;
			ThreadSync(0);
			PauseStream(PauseMode);
			DisplayFileID(cursor);	// erase line and redraw text
			if (LastSmpsCfg.Seq.Data == NULL)
			{
				ClearLine();
				printf("Error opening %s.\r", FileList[cursor].Title);
				WaitForKey();
				DisplayFileID(cursor);
			}
			
			break;
		case 'S':
			ThreadSync(1);
			StopAllSound();
			ThreadSync(0);
			SMPS_CountdownTimer = 0;
			break;
#ifdef ENABLE_VGM_LOGGING
		case 'V':
			Enable_VGMDumping = ! Enable_VGMDumping;
			ClearLine();
			printf("VGM Logging %s.\r", Enable_VGMDumping ? "enabled" : "disabled");
			WaitTimeForKey(1000);
			DisplayFileID(cursor);
			break;
#endif
		case 'P':
			PALMode = ! PALMode;
			FrameDivider = PALMode ? 50 : 60;
			ClearLine();
			printf("%s Mode.\r", PALMode ? "PAL" : "NTSC");
			WaitTimeForKey(1000);
			DisplayFileID(cursor);
			break;
		case ' ':
			PauseMode = ! PauseMode;
			PauseStream(PauseMode);
			DisplayFileID(cursor);
			break;
		case 'F':
			FadeOutMusic();
			break;
		case 'J':
			(*CondJumpVar) ^= 0x01;
			ClearLine();
			printf("Conditional Jump Variable: 0x%02X\r", *CondJumpVar);
			WaitTimeForKey(1000);
			DisplayFileID(cursor);
			break;
		case 'A':
			AutoProgress = ! AutoProgress;
			ClearLine();
			printf("Automatic Progressing %s.\r", AutoProgress ? "enabled" : "disabled");
			WaitTimeForKey(1000);
			DisplayFileID(cursor);
			break;
		case 'R':
			/*ThreadSync(1);
			FreeAll();
			
			LoadINIFile("data\\config.ini");
			ThreadSync(0);
			
			ClearLine();
			printf("Data reloaded.\r");
			WaitTimeForKey(1000);
			DisplayFileID(cursor);*/
			break;
		case 'U':
			/*UnderwMode = ! UnderwMode;
			SetSMPSExtra(0x00, UnderwMode);
			ClearLine();
			printf("Underwate Mode %s.\r", UnderwMode ? "enabled" : "disabled");
			WaitTimeForKey(1000);
			DisplayFileID(cursor);*/
			break;
		}
		
		WaitForKey();
		inkey = 0x00;
		if (CondVarChg)
		{
			CondVarChg = false;
			ClearLine();
			printf("Communication Variable changed: 0x%02X\r", *CommunicationVar);
			WaitTimeForKey(1000);
			DisplayFileID(cursor);
		}
		else if (GoToNextSong)
		{
			GoToNextSong = false;
			if (smps_playing < FileCount - 1)
				inkey = 'N';
			else
				FadeOutMusic();
		}
		else if (_kbhit())
		{
			inkey = _getch();
			if (inkey == 0x00 || inkey == 0xE0)
				inkey = 0xE0;
			else
				inkey = toupper(inkey);
		}
	}
	
	ThreadSync(1);
#ifdef ENABLE_VGM_LOGGING
	vgm_set_loop(0x00);
	vgm_dump_stop();
#endif
	
	DeinitDriver();
	StopAudioOutput();
	
FinishProgram:
#ifdef ENABLE_VGM_LOGGING
	vgm_deinit();
#endif
	DeinitAudioOutput();
	if (FileList != NULL)
	{
		UINT32 CurFile;
		
		for (CurFile = 0; CurFile < FileCount; CurFile ++)
			free(FileList[CurFile].Full);	// all other pointer are just references
		free(FileList);	FileList = NULL;
	}
	FreeExtentionData(&Config.ExtList);
	FreeConfigurationFiles(&Config);
#if _DEBUG
	if (_CrtDumpMemoryLeaks())
		_getch();
#endif
	
	return 0;
}

static void InitSmpsFile(SMPS_SET* SmpsFile, UINT32 FileLen, UINT8* FileData, const SMPS_EXT_DEF* ExtDef)
{
	UINT8 CurChr;
	
	SmpsFile->Cfg = &ExtDef->SmpsCfg;
	for (CurChr = 0; CurChr < 4; CurChr ++)
	{
		if (ExtDef->Extention[CurChr] == '\0')
			break;
		SmpsFile->CfgExtFCC[CurChr] = ExtDef->Extention[CurChr];
	}
	for (; CurChr < 4; CurChr ++)
		SmpsFile->CfgExtFCC[CurChr] = '\0';
	
	SmpsFile->Seq.alloc = 0x01;
	SmpsFile->Seq.Len = (FileLen < 0x10000) ? FileLen : 0xFFFF;
	SmpsFile->Seq.Data = FileData;
	SmpsFile->SeqBase = 0x0000;
	SmpsFile->InsLib.InsCount = 0x00;
	SmpsFile->InsLib.InsPtrs = NULL;
#ifdef ENABLE_LOOP_DETECTION
	SmpsFile->LoopPtrs = NULL;
#endif
	
	return;
}


static void GetFileList(const char* DirPath)
{
	DIR* hDir;
	struct dirent* dEntry;
	struct stat statbuf;
	int RetVal;
	UINT32 FNAlloc;
	UINT32 FNBase;
	char* FileName;
	UINT32 FileNameLen;
	FILE_NAME* TempFLst;
	
	if (DirPath == NULL || DirPath[0] == '\0')
		DirPath = ".";
	FNAlloc = (UINT32)strlen(DirPath) + 0x100;
	FileName = (char*)malloc(FNAlloc);
	FNBase = CreatePath(NULL, &FileName, DirPath);
	
	hDir = opendir(DirPath);
	dEntry = readdir(hDir);
	while(dEntry != NULL)
	{
		FileNameLen = (UINT32)strlen(dEntry->d_name);
		if (FNBase + FileNameLen >= FNAlloc)
		{
			FNAlloc = FNBase + FileNameLen + 0x10;
			FileName = (char*)realloc(FileName, FNAlloc);
		}
		strcpy(&FileName[FNBase], dEntry->d_name);
		
		RetVal = stat(FileName, &statbuf);
		if (RetVal != -1)
		{
			if (! (statbuf.st_mode & _S_IFDIR) && Config.ExtFilter)
			{
				char* ExtPtr;
				SMPS_EXT_DEF* ExtDef;
				
				ExtPtr = GetFileExtention(FileName);
				ExtDef = GetExtentionData(&Config.ExtList, ExtPtr);	// search for known extentions
				if (ExtDef == NULL)
					statbuf.st_mode |= _S_IFDIR;	// ignore this file
			}
			
			if (! (statbuf.st_mode & _S_IFDIR))
			{
				if (FileCount >= FileAlloc)
				{
					FileAlloc += 0x100;
					FileList = (FILE_NAME*)realloc(FileList, FileAlloc * sizeof(FILE_NAME));
				}
				
				TempFLst = &FileList[FileCount];
				TempFLst->Full = _strdup(FileName);
				TempFLst->Title = TempFLst->Full + FNBase;
				TempFLst->Ext = GetFileExtention(TempFLst->Title);
				TempFLst->SeqBase = 0x0000;
				FileCount ++;
			}
		}
		
		dEntry = readdir(hDir);
	}
	
	closedir(hDir);
	free(FileName);
	
	return;
}

static void PrintFileList(void)
{
	UINT32 CurFile;
	
	for (CurFile = 0; CurFile < FileCount; CurFile ++)
		printf("%2u %.75s\n", 1 + CurFile, FileList[CurFile].Title);
	
	return;
}

static UINT32 GetFileData(const char* FileName, UINT8** RetBuffer)
{
	FILE* hFile;
	UINT32 FileLen;
	UINT8* FileData;
	
	hFile = fopen(FileName, "rb");
	if (hFile == NULL)
		return 0x00;
	
	fseek(hFile, 0x00, SEEK_END);
	FileLen = ftell(hFile);
	if (! FileLen)
	{
		fclose(hFile);	// we don't need empty files
		return 0x00;
	}
	
	fseek(hFile, 0x00, SEEK_SET);
	FileData = (UINT8*)malloc(FileLen);
	FileLen = (UINT32)fread(FileData, 0x01, FileLen, hFile);
	fclose(hFile);
	
	if (! FileLen)
	{
		free(FileData);	// we didn't read anything, we it effectively failed
		return 0x00;
	}
	
	*RetBuffer = FileData;
	return FileLen;
}


void ClearLine(void)
{
	printf("%79s", "\r");
	LastLineState = 0xFF;
	return;
}

void DisplayFileID(int FileID)
{
	ClearLine();
	
	ReDisplayFileID(FileID);
	
	return;
}

void RedrawStatusLine(void)
{
	DisplayFileID(cursor);
	
	return;
}

static void ReDisplayFileID(int FileID)
{
	char TempBuf[0x20];	// maximum is 0x18 chars (for 2 loop digits) + '\0' (without Z80 offset)
	char* TempPnt;
	UINT8 NewLineState;
	
	if (FileID >= (int)FileCount)
		return;
	
	TempBuf[0x00] = '\0';
	if (FileList[FileID].SeqBase)
		sprintf(TempBuf, " [%04X]", FileList[FileID].SeqBase);
	
	NewLineState = 0xFF;
	if (FileID == smps_playing)
	{
		UINT32 PbTime;
		UINT32 Rest;
		UINT16 Min;
		UINT8 Sec;
		UINT8 DSec;	// deciseconds
		
		TempPnt = TempBuf + strlen(TempBuf);
		strcpy(TempPnt, " (");	TempPnt += 0x02;
		
		PbTime = SMPS_PlayingTimer;
		if (PbTime == -1)
			PbTime = 0;
		Rest = PbTime % SampleRate;
		DSec = (UINT8)((Rest * 100 + SampleRate / 2) / SampleRate);
		Rest = PbTime / SampleRate;
		while(DSec >= 100)
		{
			DSec -= 100;
			Rest ++;
		}
		Sec = (UINT8)(Rest % 60);
		Min = (UINT16)(Rest / 60);
		
		TempPnt += sprintf(TempPnt, "%02u:%02u.%02u", Min, Sec, DSec);
		if (SMPS_LoopCntr == -1)
		{
			NewLineState = 0x00;
			TempPnt += sprintf(TempPnt, " %s", "finished");
		}
		else
		{
			NewLineState = PauseMode ? 0x02 : 0x01;
			TempPnt += sprintf(TempPnt, " %s", PauseMode ? "paused" : "playing");
			if (SMPS_LoopCntr > 0)
				TempPnt += sprintf(TempPnt, " L %d", SMPS_LoopCntr);
		}
		strcpy(TempPnt, ")");	TempPnt += 0x01;
	}
	if (LastLineState != NewLineState)
	{
		if (LastLineState != 0xFF)
			ClearLine();
		LastLineState = NewLineState;
	}
	
	// The console under Windows displays 80 chars in a line.
	// I calc with 78 chars per line and 4 chars for the track number.
	// So I limit the file name length to: 79 - 4 - Len(time + "playing" + etc.)
	printf("%2d %.*s%s\r", FileID + 1, 75 - strlen(TempBuf), FileList[FileID].Title, TempBuf);
	
	return;
}

static void WaitTimeForKey(unsigned int MSec)
{
	DWORD CurTime;
	
	CurTime = timeGetTime() + MSec;
	while(! ExitWait && timeGetTime() < CurTime)
	{
		Sleep(20);
		if (_kbhit())
			break;
	}
	// don't reset ExitWait to enforce skipping WaitForKey()
	
	return;
}

static void WaitForKey(void)
{
	while(! ExitWait)
	{
		Sleep(20);
		if (! PauseMode && cursor == smps_playing)
			ReDisplayFileID(cursor);
		if (_kbhit())
			break;
	}
	if (ExitWait)
		ExitWait --;
	
	return;
}

static void FinishedSongSignal(void)
{
	if (AutoProgress)
	{
		GoToNextSong = true;
		ExitWait ++;
	}
	
	return;
}

static void SmpsStopSignal(void)
{
	if (SMPS_StoppedTimer == -1 || smps_playing == -1)
		return;
	
	SMPS_CountdownTimer = 2 * SampleRate;
	return;
}

static void SmpsLoopSignal(void)
{
	if (SMPS_LoopCntr >= 2)
		FinishedSongSignal();
	return;
}

static void SmpsCountdownSignal(void)
{
	if (SMPS_StoppedTimer > 0)
		FinishedSongSignal();
	return;
}

static void CommVarChangeCallback(void)
{
	CondVarChg = true;
	ExitWait ++;
	
	return;
}
