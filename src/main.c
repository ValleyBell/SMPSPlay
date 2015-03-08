// SMPS Player
// -----------
// Written by Valley Bell, 2014

#define SMPSPLAY_VER	"2.10"
//#define BETA

#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
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

#include "stdtype.h"
#include "stdbool.h"

#include "loader.h"
#include "ini_lib.h"
#include "Sound.h"
#include "Stream.h"
#include "Engine/smps_structs.h"
#include "Engine/smps.h"
#include "Engine/dac.h"
#include "Engine/necpcm.h"
#include "vgmwrite.h"


int main(int argc, char* argv[]);
static void InitSmpsFile(SMPS_SET* SmpsFile, UINT32 FileLen, UINT8* FileData, const SMPS_EXT_DEF* ExtDef);
static void GetFileList(const char* DirPath);
static void PrintFileList(void);
static UINT32 GetFileData(const char* FileName, UINT8** RetBuffer);

void ClearLine(void);
void DisplayFileID(int FileID);
void RedrawStatusLine(void);
void ReDisplayFileID(int FileID);
static void WaitTimeForKey(unsigned int MSec);
static void WaitForKey(void);
void FinishedSongSignal(void);


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
static bool GoToNextSong;

extern bool PauseThread;	// from Stream.c
extern volatile bool ThreadPauseConfrm;
extern char SoundLogFile[MAX_PATH];

extern UINT32 SampleRate;	// from Sound.c
extern UINT8 BitsPerSample;
extern UINT16 FrameDivider;
extern UINT32 PlayingTimer;
extern INT32 LoopCntr;
extern INT32 StoppedTimer;

bool PauseMode;
bool PALMode;
static bool AutoProgress;
UINT8 DebugMsgs;
UINT8 VGM_DataBlkCompress = 1;
static UINT8 LastLineState;

extern UINT16 AUDIOBUFFERU;
static UINT8* CondJumpVar;

CONFIG_DATA Config;

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
	
	vgm_init();
	memset(&Config, 0x00, sizeof(CONFIG_DATA));
	Config.FM6DACOff = 0xFF;
	Config.ResmplForce = 0xFF;
	SampleRate = 44100;
	BitsPerSample = 16;
	AUDIOBUFFERU = 10;
	
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
	NECPCM_Reset();
	PALMode = false;
	FrameDivider = PALMode ? 50 : 60;
	
	_mkdir("dumps");
	strcpy(SoundLogFile, "dumps/out.wav");
	SoundLogging(Config.LogWave);
	
	if (Config.SamplePerSec)
		SampleRate = Config.SamplePerSec;
	if (Config.BitsPerSample)
		BitsPerSample = Config.BitsPerSample;
	if (Config.AudioBufs)
		AUDIOBUFFERU = Config.AudioBufs;
	StartAudioOutput();
	
	InitDriver();
	CondJumpVar = SmpsGetVariable(SMPSVAR_CONDIT_JUMP);
	PlayingTimer = 0;
	LoopCntr = -1;
	StoppedTimer = -1;
	
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
			printf("Channel %i of %s %s\r", NChannel + 1, ChipName, MuteToggleResult ? "enabled" : "disabled");
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
				ThreadPauseConfrm = false;
				PauseThread = true;
				while(! ThreadPauseConfrm)
					Sleep(1);
				
				vgm_set_loop(0x00);
				vgm_dump_stop();
				
				PlayingTimer = 0;
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
					MakeVgmFileName(FileList[cursor].Title);
					PlayMusic(&LastSmpsCfg);
					smps_playing = cursor;
				}
				else
				{
					StopAllSound();
				}
			}
			
			PauseMode = false;
			PauseThread = false;
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
			ThreadPauseConfrm = false;
			PauseThread = true;
			while(! ThreadPauseConfrm)
				Sleep(1);
			
			StopAllSound();
			PauseThread = false;
			break;
		case 'V':
			Enable_VGMDumping = ! Enable_VGMDumping;
			ClearLine();
			printf("VGM Logging %s.\r", Enable_VGMDumping ? "enabled" : "disabled");
			WaitTimeForKey(1000);
			DisplayFileID(cursor);
			break;
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
			/*PauseThread = true;
			FreeDACData();
			FreeFlutterData();
			FreeGlobalInsSet();
			
			LoadINIFile("data\\config.ini");
			LoadFlutterData(IniPath[0x00]);
			LoadDACData(IniPath[0x01]);
			LoadGlobalInsSet(IniPath[0x02]);
			PauseThread = false;
			
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
		if (GoToNextSong)
		{
			GoToNextSong = false;
			if (smps_playing < FileCount - 1)
			{
				inkey = 'N';
			}
			else
			{
				FadeOutMusic();
				inkey = 0x00;
			}
		}
		else
		{
			inkey = _getch();
			if (inkey == 0x00 || inkey == 0xE0)
				inkey = 0xE0;
			else
				inkey = toupper(inkey);
		}
	}
	
	ThreadPauseConfrm = false;
	PauseThread = true;
	while(! ThreadPauseConfrm)
		Sleep(1);
	vgm_set_loop(0x00);
	vgm_dump_stop();
	
	DeinitDriver();
	StopAudioOutput();
	
FinishProgram:
	vgm_deinit();
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
	
	SmpsFile->Seq.Len = (FileLen < 0x10000) ? FileLen : 0xFFFF;
	SmpsFile->Seq.Data = FileData;
	SmpsFile->SeqBase = 0x0000;
	SmpsFile->InsLib.InsCount = 0x00;
	SmpsFile->InsLib.InsPtrs = NULL;
	
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


/*
void LoadINIFile(const char* INIFileName)
{
	FILE* hFile;
	//char BasePath[0x100];
	char TempStr[0x100];
	size_t TempInt;
	char* TempPnt;
	
	strcpy(WorkDir[0x00], "music");
	strcpy(WorkDir[0x01], "data");
	strcpy(IniPath[0x00], "");
	strcpy(IniPath[0x01], "");
	strcpy(IniPath[0x02], "");
#ifndef _DEBUG
	DebugMsgs = 0x00;
#else
	DebugMsgs = 0xFF;
#endif
	VGM_DataBlkCompress = 0x00;
	
	hFile = fopen(INIFileName, "rt");
	if (hFile == NULL)
	{
		printf("Error opening %s\n", INIFileName);
		return;
	}
	
	while(! feof(hFile))
	{
		TempPnt = fgets(TempStr, 0x100, hFile);
		if (TempPnt == NULL)
			break;
		if (TempStr[0x00] == '\n' || TempStr[0x00] == '\0')
			continue;
		if (TempStr[0x00] == ';')
		{
			// skip comment lines
			// fgets has set a null-terminator at char 0x3F
			while(TempStr[strlen(TempStr) - 1] != '\n')
			{
				fgets(TempStr, 0x40, hFile);
				if (TempStr[0x00] == '\0')
					break;
			}
			continue;
		}
		
		TempPnt = strchr(TempStr, '=');
		if (TempPnt == NULL || TempPnt == TempStr)
			continue;	// invalid line
		
		TempInt = strlen(TempPnt) - 1;
		if (TempPnt[TempInt] == '\n')
			TempPnt[TempInt] = '\0';
		
		*TempPnt = '\0';
		TempPnt ++;
		while(*TempPnt == ' ')
			TempPnt ++;
		
		TempInt = strlen(TempStr) - 1;
		while(TempInt > 0 && TempStr[TempInt] == ' ')
		{
			TempStr[TempInt] = '\0';
			TempInt --;
		}
		
		if (! _stricmp(TempStr, "MusicDir"))
		{
			strcpy(WorkDir[0x00], TempPnt);
			TempPnt = WorkDir[0x00] + strlen(WorkDir[0x00]) - 0x01;
			while(TempPnt >= WorkDir[0x00] && (*TempPnt == '\\' || *TempPnt == '/'))
			{
				*TempPnt = '\0';
				TempPnt --;
			}
		}
		else if (! _stricmp(TempStr, "DataDir"))
		{
			strcpy(WorkDir[0x01], TempPnt);
			TempPnt = WorkDir[0x01] + strlen(WorkDir[0x01]) - 0x01;
			while(TempPnt >= WorkDir[0x01] && (*TempPnt == '\\' || *TempPnt == '/'))
			{
				*TempPnt = '\0';
				TempPnt --;
			}
		}
		else if (! _stricmp(TempStr, "PSGFlt"))
		{
			sprintf(IniPath[0x00], "%s\\%s", WorkDir[0x01], TempPnt);
		}
		else if (! _stricmp(TempStr, "DACSnd"))
		{
			sprintf(IniPath[0x01], "%s\\%s", WorkDir[0x01], TempPnt);
		}
		else if (! _stricmp(TempStr, "GblInsSet"))
		{
			sprintf(IniPath[0x02], "%s\\%s", WorkDir[0x01], TempPnt);
		}
		else if (! _stricmp(TempStr, "DebugMsgs"))
		{
			DebugMsgs = (UINT8)strtoul(TempPnt, NULL, 0);
		}
		else if (! _stricmp(TempStr, "CompressVGM"))
		{
			VGM_DataBlkCompress = (UINT8)strtoul(TempPnt, NULL, 0);
		}
		else if (! _stricmp(TempStr, "AudioBuffers"))
		{
			AudioBufs = (UINT8)strtoul(TempPnt, NULL, 0);
		}
	}
	
	fclose(hFile);
	
	return;
}*/

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

void ReDisplayFileID(int FileID)
{
	char TempBuf[0x20];	// maximum is 0x18 chars (for 2 loop digits) + '\0' (without Z80 offset)
	char* TempPnt;
	UINT8 NewLineState;
	//LastLineState
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
		
		PbTime = PlayingTimer;
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
		if (LoopCntr == -1)
		{
			NewLineState = 0x00;
			TempPnt += sprintf(TempPnt, " %s", "finished");
		}
		else
		{
			NewLineState = PauseMode ? 0x02 : 0x01;
			TempPnt += sprintf(TempPnt, " %s", PauseMode ? "paused" : "playing");
			if (LoopCntr > 0)
				TempPnt += sprintf(TempPnt, " L %d", LoopCntr);
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
	while(timeGetTime() < CurTime)
	{
		Sleep(20);
		if (_kbhit())
			break;
	}
	
	return;
}

static void WaitForKey(void)
{
	while(! GoToNextSong && ! PauseMode)
	{
		Sleep(20);
		if (cursor == smps_playing)
			ReDisplayFileID(cursor);
		if (_kbhit())
			break;
	}
	
	return;
}

void FinishedSongSignal(void)
{
	if (AutoProgress)
		GoToNextSong = true;
	
	return;
}
