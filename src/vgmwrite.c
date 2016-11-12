/*
    vgmwrite.c

    VGM output module

  Ported from Gens/GS r7+ vgm mod.

*/

#include "vgmwrite.h"

// C includes.
#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include <common_def.h>


void ClearLine(void);			// from main.c
void RedrawStatusLine(void);	// from main.c


#define CLOCK_NTSC	53693175
#define CLOCK_PAL	53203424
#define CLOCK_PICOPCM	(1280000 | 0x80000000)
 

typedef struct _vgm_file_header VGM_HEADER;
struct _vgm_file_header
{
	UINT32 fccVGM;
	UINT32 lngEOFOffset;
	UINT32 lngVersion;
	UINT32 lngHzPSG;
	UINT32 lngHz2413;
	UINT32 lngGD3Offset;
	UINT32 lngTotalSamples;
	UINT32 lngLoopOffset;
	UINT32 lngLoopSamples;
	UINT32 lngRate;
	UINT16 shtPSG_Feedback;
	UINT8 bytPSG_STWidth;
	UINT8 bytPSG_Flags;
	UINT32 lngHz2612;
	UINT32 lngHz2151;
	UINT32 lngDataOffset;
	UINT32 lngHzSPCM;
	UINT32 lngSPCMIntf;
	// -> 0x40 Bytes
	UINT32 lngHzRF5C68;
	UINT32 lngHz2203;
	UINT32 lngHz2608;
	UINT32 lngHz2610;
	UINT32 lngHz3812;
	UINT32 lngHz3526;
	UINT32 lngHz8950;
	UINT32 lngHz262;
	UINT32 lngHz278B;
	UINT32 lngHz271;
	UINT32 lngHz280B;
	UINT32 lngHzRF5C164;
	UINT32 lngHzPWM;
	UINT32 lngHzAY8910;
	UINT8 bytModifiers[0x08];
	// -> 0x80 Bytes
	UINT32 lngHzGBDMG;
	UINT32 lngHzNESAPU;
	UINT32 lngHzMultiPCM;
	UINT32 lngHzUPD7759;
	UINT32 lngHzOKIM6258;
	UINT8 bytOKI6258Flags;
	UINT8 bytK054539Flags;
	UINT8 bytC140Type;
	UINT8 bytReservedFlags;
	UINT32 lngHzOKIM6295;
	UINT32 lngHzK051649;
	UINT32 lngHzK054539;
	UINT32 lngHzHuC6280;
	UINT32 lngHzC140;
	UINT32 lngHzK053260;
	UINT32 lngHzPokey;
	UINT32 lngHzQSound;
	UINT32 lngHzSCSP;
	UINT32 lngExtraOfs;
};	// -> 0xC0 Bytes
typedef struct _vgm_header_extra
{
	UINT32 DataSize;
	UINT32 Chp2ClkOffset;
	UINT32 ChpVolOffset;
} VGM_HDR_EXTRA;
#pragma pack(1)
typedef struct _vgm_extra_chip_data16
{
	UINT8 Type;
	UINT8 Flags;
	UINT16 Data;
} VGMX_CHIP_DATA16;
typedef struct _vgm_extra_chip_extra16
{
	UINT8 ChipCnt;
	VGMX_CHIP_DATA16 CCData[4];
} VGMX_CHP_EXTRA16;
#pragma pack()

typedef struct _vgm_gd3_tag GD3_TAG;
struct _vgm_gd3_tag
{
	UINT32 fccGD3;
	UINT32 lngVersion;
	UINT32 lngTagLength;
	wchar_t strTrackNameE[0x70];
	wchar_t strTrackNameJ[0x10];	// Japanese Names are not used
	wchar_t strGameNameE[0x70];
	wchar_t strGameNameJ[0x10];
	wchar_t strSystemNameE[0x30];
	wchar_t strSystemNameJ[0x10];
	wchar_t strAuthorNameE[0x30];
	wchar_t strAuthorNameJ[0x10];
	wchar_t strReleaseDate[0x10];
	wchar_t strCreator[0x20];
	wchar_t strNotes[0x50];
};	// -> 0x200 Bytes
typedef struct _vgm_file_inf VGM_INF;
struct _vgm_file_inf
{
	FILE* hFile;
	VGM_HEADER Header;
	VGM_HDR_EXTRA HdrExtra;
	VGMX_CHP_EXTRA16 HdrXCVol;
	UINT32 HeaderSize;
	UINT32 BytesWrt;
	UINT32 SmplsWrt;
	UINT32 EvtDelay;
};
typedef struct _vgm_chip VGM_CHIP;
struct _vgm_chip
{
	UINT8 ChipType;
	//UINT16 VgmID;
	UINT8 HadWrite;
};


#define CHIP_COUNT	0x05
static const UINT8 CHIP_LIST[CHIP_COUNT] = {VGMC_YM2612, VGMC_SN76496, VGMC_RF5C164, VGMC_PWM, VGMC_UPD7759};

UINT8 Enable_VGMDumping;
static UINT8 VGM_Dumping;
UINT8 VGM_IgnoreWrt;
static VGM_INF VgmFile;
static VGM_CHIP VgmChip[CHIP_COUNT];
static GD3_TAG VgmTag;

// Function Prototypes
INLINE size_t atwcpy(wchar_t* dststr, const char* srcstr);
static UINT8 ChipType2ID(UINT8 chip_type);
static void vgm_header_clear(void);
static void vgm_close(void);
static void write_vgm_tag(const wchar_t* TagStr, FILE* hFile);
static void vgm_write_delay(void);
//static void vgm_flush_pcm(void);

/*static UINT32 PCMC_Start;
static UINT32 PCMC_Next;
static UINT32 PCMC_Pos;
static UINT8 PCMCache[0x400];*/
static UINT32 DACFreq;
static UINT8 WroteDAC;

extern UINT32 SampleRate;
static UINT32 CurPbkSample;
static UINT32 LastVGMSmpl;

static const char* MusFileName;
static char* VGMFileName;
static UINT8 SpcChipEnable;

// ASCII to Wide-Char String Copy
INLINE size_t atwcpy(wchar_t* dststr, const char* srcstr)
{
	return mbstowcs(dststr, srcstr, strlen(srcstr) + 0x01);
}

static UINT8 ChipType2ID(UINT8 chip_type)
{
	UINT8 curchip;
	UINT8 chip_id;
	
	chip_id = 0xFF;
	for (curchip = 0x00; curchip < CHIP_COUNT; curchip ++)
	{
		if (CHIP_LIST[curchip] == chip_type)
		{
			chip_id = curchip;
			break;
		}
	}
	
	return chip_id;
}

void vgm_init(void)
{
	Enable_VGMDumping = 0x00;
	VGM_IgnoreWrt = 0x00;
	
	MusFileName = NULL;
	VGMFileName = NULL;
	VGM_Dumping = 0x00;
	SpcChipEnable = 0x00;
	
	return;
}

void vgm_deinit(void)
{
	if (VGM_Dumping)
		vgm_dump_stop();
	
	if (VGMFileName != NULL)
	{
		free(VGMFileName);
		VGMFileName = NULL;
	}
	
	return;
}

void vgm_set_chip_enable(UINT8 Mask)
{
	SpcChipEnable = Mask;
	
	return;
}

void MakeVgmFileName(const char* FileName)
{
	size_t StrLen;
	char* TempPnt;
	
	TempPnt = strrchr(FileName, '\\');
	if (TempPnt == NULL)
		TempPnt = strrchr(FileName, '/');
	if (TempPnt == NULL)
		MusFileName = FileName;
	else
		MusFileName = TempPnt + 0x01;
	
	StrLen = 0x06 + strlen(MusFileName) + 0x05;
	VGMFileName = (char*)realloc(VGMFileName, StrLen);
	
	strcpy(VGMFileName, "dumps/");
	strcat(VGMFileName, MusFileName);
	TempPnt = strrchr(VGMFileName, '.');
	if (TempPnt == NULL)
		TempPnt = VGMFileName + strlen(VGMFileName);
	strcpy(TempPnt, ".vgm");
	
	return;
}

int vgm_dump_start(void)
{
	UINT8 curchip;
	
	if (! Enable_VGMDumping)
		return 0;
	
	VgmFile.hFile = fopen(VGMFileName, "wb");
	if (VgmFile.hFile == NULL)
	{
		ClearLine();
		printf("Can't open file for VGM dumping!\n");
		RedrawStatusLine();
		return -3;
	}
	
	for (curchip = 0x00; curchip < CHIP_COUNT; curchip ++)
	{
		VgmChip[curchip].ChipType = 0xFF;
		VgmChip[curchip].HadWrite = 0x00;
	}
	
	//PCMC_Start = 0xFFFFFFFF;
	
	VgmFile.BytesWrt = 0;
	VgmFile.SmplsWrt = 0;
	VgmFile.EvtDelay = 0;
	vgm_header_clear();
	
	VgmTag.fccGD3 = 0x20336447;	// 'Gd3 '
	VgmTag.lngVersion = 0x0100;
	if (MusFileName != NULL)
		atwcpy(VgmTag.strTrackNameE, MusFileName);
	else
		wcscpy(VgmTag.strTrackNameE, L"");
	wcscpy(VgmTag.strTrackNameJ, L"");
	wcscpy(VgmTag.strGameNameE, L"");
	wcscpy(VgmTag.strGameNameJ, L"");
	//if (Genesis_Started)
		wcscpy(VgmTag.strSystemNameE, L"Sega Mega Drive / Genesis");
//	else if (SegaCD_Started)
//		wcscpy(VgmTag.strSystemNameE, L"Sega MegaCD / SegaCD");
//	else if (_32X_Started)
//		wcscpy(VgmTag.strSystemNameE, L"Sega 32X");
	wcscpy(VgmTag.strSystemNameJ, L"");
	wcscpy(VgmTag.strAuthorNameE, L"");
	wcscpy(VgmTag.strAuthorNameJ, L"");
	wcscpy(VgmTag.strReleaseDate, L"");
	wcscpy(VgmTag.strCreator, L"");
	
#if ! defined(_MSC_VER) || _MSC_VER >= 1400	// MS VC++ 2005 and higher
	swprintf(VgmTag.strNotes, 0x50, L"Generated by %s", L"SMPSPlay");
#else	// older MS VC++ versions like VC6
	swprintf(VgmTag.strNotes, L"Generated by %s", L"SMPSPlay");
#endif
	VgmTag.lngTagLength = (UINT32)(	wcslen(VgmTag.strTrackNameE) + 0x01 +
										wcslen(VgmTag.strTrackNameJ) + 0x01 +
										wcslen(VgmTag.strGameNameE) + 0x01 +
										wcslen(VgmTag.strGameNameJ) + 0x01 +
										wcslen(VgmTag.strSystemNameE) + 0x01 +
										wcslen(VgmTag.strSystemNameJ) + 0x01 +
										wcslen(VgmTag.strAuthorNameE) + 0x01 +
										wcslen(VgmTag.strAuthorNameJ) + 0x01 +
										wcslen(VgmTag.strReleaseDate) + 0x01 +
										wcslen(VgmTag.strCreator) + 0x01 +
										wcslen(VgmTag.strNotes) + 0x01);
	VgmTag.lngTagLength *= 0x02;	// String Length -> Byte Length
	
	WroteDAC = 0x00;
	DACFreq = 0xFFFFFFFF;
	
	CurPbkSample = 0;
	LastVGMSmpl = 0;
	
	VGM_Dumping = 0x01;
	
	return 0;
}

int vgm_dump_stop(void)
{
	UINT8 curchip;
	UINT8 chip_unused;
	
	if (! VGM_Dumping)
		return -1;
	
	chip_unused = 0x00;
	for (curchip = 0x00; curchip < CHIP_COUNT; curchip ++)
	{
		if (! VgmChip[curchip].HadWrite && VgmChip[curchip].ChipType != 0xFF)
		{
			chip_unused ++;
			switch(CHIP_LIST[curchip])
			{
			case VGMC_SN76496:
				VgmFile.Header.lngHzPSG = 0x00;
				VgmFile.Header.shtPSG_Feedback = 0x00;
				VgmFile.Header.bytPSG_STWidth = 0x00;
				VgmFile.Header.bytPSG_Flags = 0x00;
				break;
			case VGMC_YM2612:
				VgmFile.Header.lngHz2612 = 0x00;
				break;
			case VGMC_RF5C164:
				VgmFile.Header.lngHzRF5C164 = 0x00;
				break;
			case VGMC_PWM:
				VgmFile.Header.lngHzPWM = 0x00;
				break;
			case VGMC_UPD7759:
				VgmFile.Header.lngHzUPD7759 = 0x00;
				break;
			}
		}
	}
//	if (chip_unused)
//		printf("Header Data of %hu unused Chips removed.\n", chip_unused);
	
	vgm_close();
	
	VGM_Dumping = 0x00;
	
	return 0;
}

void vgm_update(UINT32 PbkSamples)
{
	if (! VGM_Dumping)
		return;
	
	if (SampleRate == 44100)
	{
		VgmFile.EvtDelay += PbkSamples;
	}
	else
	{
		UINT32 CurVGMSmpl;
		
		CurPbkSample += PbkSamples;
		CurVGMSmpl = CurPbkSample * 44100 / SampleRate;
		VgmFile.EvtDelay += CurVGMSmpl - LastVGMSmpl;
		LastVGMSmpl = CurVGMSmpl;
		
		if (CurPbkSample >= SampleRate)
		{
			CurPbkSample -= SampleRate;
			LastVGMSmpl -= 44100;
		}
	}
	
	return;
}

static void vgm_header_clear(void)
{
	UINT32 CUR_CLOCK;
	VGM_HEADER* Header;
	VGM_HDR_EXTRA* HdrX;
	VGMX_CHP_EXTRA16* HdrXVol;
	UINT8 Padding[0x10];
	UINT32 PaddBytes;
	
	if (VgmFile.hFile == NULL)
		return;
	
	Header = &VgmFile.Header;
	HdrX = &VgmFile.HdrExtra;
	HdrXVol = &VgmFile.HdrXCVol;
	memset(Header, 0x00, sizeof(VGM_HEADER));
	Header->fccVGM = 0x206D6756;	// 'Vgm '
	Header->lngEOFOffset = 0x00;
	Header->lngVersion = 0x0160;
	//Header->lngGD3Offset = 0x00;
	//Header->lngTotalSamples = 0;
	//Header->lngLoopOffset = 0x00;
	//Header->lngLoopSamples = 0;
	Header->lngRate = 60;
	Header->lngExtraOfs = 0x00;	// disabled by default
	
	HdrX->DataSize = sizeof(VGM_HDR_EXTRA);
	HdrX->ChpVolOffset = HdrX->DataSize - 0x08;
	HdrXVol->ChipCnt = 0x00;
	memset(Padding, 0x00, 0x10);
	
	CUR_CLOCK = CLOCK_NTSC;
	VgmChip[0x00].ChipType = CHIP_LIST[0x00];
	VgmFile.Header.lngHz2612 = (CUR_CLOCK + 3) / 7;
	VgmChip[0x01].ChipType = CHIP_LIST[0x01];
	VgmFile.Header.lngHzPSG = (CUR_CLOCK + 7) / 15;
	VgmFile.Header.shtPSG_Feedback = 0x09;
	VgmFile.Header.bytPSG_STWidth = 0x10;
	VgmFile.Header.bytPSG_Flags = 0x06;
	VgmFile.HeaderSize = 0x40;
	
	if (SpcChipEnable & VGM_CEN_SCD_PCM)
	{
		VgmChip[0x02].ChipType = CHIP_LIST[0x02];
		VgmFile.Header.lngHzRF5C164 = 12500000;	// verfied from MESS/MAME
		if (VgmFile.HeaderSize < 0x80)
			VgmFile.HeaderSize = 0x80;
	}
	if (SpcChipEnable & VGM_CEN_32X_PWM)
	{
		VgmChip[0x03].ChipType = CHIP_LIST[0x03];
		VgmFile.Header.lngHzPWM = (CUR_CLOCK * 3 + 3) / 7;
		if (VgmFile.HeaderSize < 0x80)
			VgmFile.HeaderSize = 0x80;
	}
	if (SpcChipEnable & VGM_CEN_PICOPCM)
	{
		VgmChip[0x04].ChipType = CHIP_LIST[0x04];
		if (Header->lngVersion < 0x0161)
			Header->lngVersion = 0x0161;
		VgmFile.Header.lngHzUPD7759 = CLOCK_PICOPCM;
		if (VgmFile.HeaderSize < 0xC0)
			VgmFile.HeaderSize = 0xC0;
		HdrXVol->ChipCnt = 0x01;
		HdrXVol->CCData[0].Type = 0x16;	// uPD7759
		HdrXVol->CCData[0].Flags = 0x00;
		HdrXVol->CCData[0].Data = 0x2B;	// ~0.33 * PSG (PSG is 0x80)
	}
	
	Header->lngDataOffset = VgmFile.HeaderSize;
	if (! Header->lngExtraOfs && HdrXVol->ChipCnt)
		Header->lngExtraOfs = 0x04;
	if (Header->lngExtraOfs)
	{
		Header->lngDataOffset += HdrX->DataSize;
		Header->lngDataOffset += 0x01 + sizeof(VGMX_CHIP_DATA16) * HdrXVol->ChipCnt;
	}
	PaddBytes = (0 - Header->lngDataOffset) & 0x0F;
	Header->lngDataOffset += PaddBytes;
	
	Header->lngDataOffset -= 0x34;	// moved here from vgm_close
	fwrite(Header, VgmFile.HeaderSize, 0x01, VgmFile.hFile);
	VgmFile.BytesWrt += VgmFile.HeaderSize;
	if (Header->lngExtraOfs)
	{
		fwrite(HdrX, 0x01, HdrX->DataSize, VgmFile.hFile);
		VgmFile.BytesWrt += HdrX->DataSize;
		
		fputc(HdrXVol->ChipCnt, VgmFile.hFile);
		fwrite(HdrXVol->CCData, sizeof(VGMX_CHIP_DATA16), HdrXVol->ChipCnt, VgmFile.hFile);
		VgmFile.BytesWrt += 0x01 + sizeof(VGMX_CHIP_DATA16) * HdrXVol->ChipCnt;
		
		fwrite(Padding, 0x01, PaddBytes, VgmFile.hFile);
		VgmFile.BytesWrt += PaddBytes;
	}
	
	return;
}

static void vgm_close(void)
{
	VGM_HEADER* Header;
	
	if (VgmFile.hFile == NULL)
		return;
	
	Header = &VgmFile.Header;
	Header->lngVersion = WroteDAC ? 0x0160 : 0x0150;	// write v150 for VGMs without DAC
	
	vgm_write_delay();
	fputc(0x66, VgmFile.hFile);	// Write EOF Command
	VgmFile.BytesWrt ++;
	
	// GD3 Tag
	Header->lngGD3Offset = VgmFile.BytesWrt - 0x14;
	fwrite(&VgmTag.fccGD3, 0x04, 0x01, VgmFile.hFile);
	fwrite(&VgmTag.lngVersion, 0x04, 0x01, VgmFile.hFile);
	fwrite(&VgmTag.lngTagLength, 0x04, 0x01, VgmFile.hFile);
	write_vgm_tag(VgmTag.strTrackNameE, VgmFile.hFile);
	write_vgm_tag(VgmTag.strTrackNameJ, VgmFile.hFile);
	write_vgm_tag(VgmTag.strGameNameE, VgmFile.hFile);
	write_vgm_tag(VgmTag.strGameNameJ, VgmFile.hFile);
	write_vgm_tag(VgmTag.strSystemNameE, VgmFile.hFile);
	write_vgm_tag(VgmTag.strSystemNameJ, VgmFile.hFile);
	write_vgm_tag(VgmTag.strAuthorNameE, VgmFile.hFile);
	write_vgm_tag(VgmTag.strAuthorNameJ, VgmFile.hFile);
	write_vgm_tag(VgmTag.strReleaseDate, VgmFile.hFile);
	write_vgm_tag(VgmTag.strCreator, VgmFile.hFile);
	write_vgm_tag(VgmTag.strNotes, VgmFile.hFile);
	VgmFile.BytesWrt += 0x0C + VgmTag.lngTagLength;
	
	// Rewrite Header
	Header->lngTotalSamples = VgmFile.SmplsWrt;
	if (Header->lngLoopOffset)
	{
		Header->lngLoopOffset -= 0x1C;
		Header->lngLoopSamples = VgmFile.SmplsWrt - Header->lngLoopSamples;
	}
	Header->lngEOFOffset = VgmFile.BytesWrt - 0x04;
	fseek(VgmFile.hFile, 0x00, SEEK_SET);
	fwrite(Header, VgmFile.HeaderSize, 0x01, VgmFile.hFile);
	
	fclose(VgmFile.hFile);
	VgmFile.hFile = NULL;
	
	//logerror("VGM %02hX closed.\t%lu Bytes, %lu Samples written\n", vgm_id, VgmFile.BytesWrt, VgmFile.SmplsWrt);
	
	return;
}

static void write_vgm_tag(const wchar_t* TagStr, FILE* hFile)
{
	const wchar_t* CurStr;
	UINT16 UnicodeChr;
	
	// under Windows it also would be possible to use this line
	//fwrite(TagStr, 0x02, wcslen(TagStr) + 0x01, hFile);
	
	CurStr = TagStr;
	// Write Tag-Text
	while(*CurStr)
	{
		UnicodeChr = (UINT16)*CurStr;
		fwrite(&UnicodeChr, 0x02, 0x01, hFile);
		CurStr ++;
	}
	// Write Null-Terminator
	UnicodeChr = (UINT16)*CurStr;
	fwrite(&UnicodeChr, 0x02, 0x01, hFile);
	
	return;
}

static void vgm_write_delay(void)
{
	UINT16 delaywrite;
	
	//if (VgmFile.EvtDelay)
	//	vgm_flush_pcm();
	while(VgmFile.EvtDelay)
	{
		if (VgmFile.EvtDelay > 0x0000FFFF)
			delaywrite = 0xFFFF;
		else
			delaywrite = (UINT16)VgmFile.EvtDelay;
		
		if (delaywrite <= 0x0010)
		{
			fputc(0x6F + delaywrite, VgmFile.hFile);
			VgmFile.BytesWrt += 0x01;
		}
		else
		{
			if (delaywrite == 735)
			{
				fputc(0x62, VgmFile.hFile);
				VgmFile.BytesWrt += 0x01;
			}
			else if (delaywrite == 2 * 735)
			{
				fputc(0x62, VgmFile.hFile);
				fputc(0x62, VgmFile.hFile);
				VgmFile.BytesWrt += 0x02;
			}
			else
			{
				fputc(0x61, VgmFile.hFile);
				fwrite(&delaywrite, 0x02, 0x01, VgmFile.hFile);
				VgmFile.BytesWrt += 0x03;
			}
		}
		VgmFile.SmplsWrt += delaywrite;
		
		VgmFile.EvtDelay -= delaywrite;
	}
	
	return;
}

void vgm_write(UINT8 chip_type, UINT8 port, UINT16 r, UINT8 v)
{
	UINT8 chip_id;
	
	if (! VGM_Dumping || VGM_IgnoreWrt)
		return;
	chip_id = ChipType2ID(chip_type);
	if (chip_id == 0xFF)
		return;
	if (VgmFile.hFile == NULL)
		return;
	
	if (! VgmChip[chip_id].HadWrite)
		VgmChip[chip_id].HadWrite = 0x01;
	vgm_write_delay();
	
	//if (! (chip_type == VGMC_RF5C164 && port == 0x01))
	//	vgm_flush_pcm();
	
	switch(chip_type)	// Write the data
	{
	case VGMC_SN76496:
		switch(port)
		{
		case 0x00:	// standard PSG register
			fputc(0x50, VgmFile.hFile);
			fputc(r, VgmFile.hFile);
			VgmFile.BytesWrt += 0x02;
			break;
		case 0x01:	// GG Stereo
			fputc(0x4F, VgmFile.hFile);
			fputc(r, VgmFile.hFile);
			VgmFile.BytesWrt += 0x02;
			break;
		}
		break;
	case VGMC_YM2612:
		fputc(0x52 + (port & 0x01), VgmFile.hFile);
		fputc(r, VgmFile.hFile);
		fputc(v, VgmFile.hFile);
		VgmFile.BytesWrt += 0x03;
		break;
	/*case VGMC_RF5C164:	// Sega MegaCD PCM
		switch(port)
		{
		case 0x00:	// Write Register
			fputc(0xB1, VgmFile.hFile);	// Write Register
			fputc(r, VgmFile.hFile);	// Register
			fputc(v, VgmFile.hFile);	// Value
			VgmFile.BytesWrt += 0x03;
			break;
		case 0x01:	// Write Memory Byte
//			fputc(0xC2, VgmFile.hFile);		// Write Memory
//			fputc((r >> 0) & 0xFF, VgmFile.hFile);	// offset low
//			fputc((r >> 8) & 0xFF, VgmFile.hFile);	// offset high
//			fputc(v, VgmFile.hFile);		// Data
//			VgmFile.BytesWrt += 0x04;
			
			// optimize consecutive Memory Writes
			if (PCMC_Start == 0xFFFFFFFF || r != PCMC_Next)
			{
				// flush cache to file
				vgm_flush_pcm();
				PCMC_Start = r;
				PCMC_Next = PCMC_Start;
				PCMC_Pos = 0x00;
			}
			PCMCache[PCMC_Pos] = v;
			PCMC_Pos ++;
			PCMC_Next ++;
			if (PCMC_Pos >= 0x400)
				PCMC_Next = 0xFFFFFFFF;
			
			break;
		}
		break;
	case VGMC_PWM:
		fputc(0xB2, VgmFile.hFile);
		fputc((port << 4) | ((r & 0xF00) >> 8), VgmFile.hFile);
		fputc(r & 0xFF, VgmFile.hFile);
		VgmFile.BytesWrt += 0x03;
		break;*/
	case VGMC_UPD7759:
		fputc(0xB6, VgmFile.hFile);
		fputc(r, VgmFile.hFile);
		fputc(v, VgmFile.hFile);
		VgmFile.BytesWrt += 0x03;
		break;
	default:
		fputc(0x01, VgmFile.hFile);	// write invalid data - for debugging purposes
		break;
	}
	
	return;
}

void vgm_write_large_data(UINT8 chip_type, UINT8 type, UINT32 datasize, UINT32 value1, UINT32 value2, const void* data)
{
	UINT8 chip_id;
	UINT32 finalsize;
	UINT16 TempSht;
	char write_it;
	
	if (! VGM_Dumping)
		return;
	chip_id = ChipType2ID(chip_type);
	if (chip_id == 0xFF)
		return;
	
	if (VgmFile.hFile == NULL)
		return;
	
	vgm_write_delay();
	
	write_it = 0x00;
	switch(chip_type)	// Write the data
	{
	case VGMC_SN76496:
		break;
	case VGMC_YM2612:
		switch(type)
		{
		case 0x00:	// uncompressed
			type = 0x00;
			write_it = 0x01;
			break;
		case 0x01:	// comrpessed
			type = 0x40;
			write_it = 0x01;
			break;
		case 0xFF:	// Delta-Table
			type = 0x7F;
			write_it = 0x01;
			break;
		}
		break;
	/*case VGMC_RF5C164:
		switch(type)
		{
		case 0x00:
			break;
		case 0x01:	// RAM Data
			//vgm_flush_pcm();
			type = 0xC1;	// Type: SegaCD RAM Data
			write_it = 0x01;
			break;
		}
		break;
	case VGMC_PWM:
		break;*/
	}
	
	if (write_it)
	{
		fputc(0x67, VgmFile.hFile);
		fputc(0x66, VgmFile.hFile);
		fputc(type, VgmFile.hFile);
		switch(type & 0xC0)
		{
		case 0x80:
			// Value 1 & 2 are used to write parts of the image (and save space)
			if (! value2)
				value2 = datasize - value1;
			if (data == NULL)
			{
				value1 = 0x00;
				value2 = 0x00;
			}
			finalsize = 0x08 + value2;
			
			fwrite(&finalsize, 0x04, 0x01, VgmFile.hFile);	// Data Block Size
			fwrite(&datasize, 0x04, 0x01, VgmFile.hFile);	// ROM Size
			fwrite(&value1, 0x04, 0x01, VgmFile.hFile);		// Data Base Address
			// Data Length is useless - is equal to finalsize - 0x08
			//fwrite(&value2, 0x04, 0x01, VgmFile.hFile);	// Data Length
			fwrite(data, 0x01, value2, VgmFile.hFile);
			VgmFile.BytesWrt += 0x07 + finalsize;
			break;
		case 0xC0:
			finalsize = datasize + 0x02;
			fwrite(&finalsize, 0x04, 0x01, VgmFile.hFile);	// Data Block Size
			fwrite(&value1, 0x02, 0x01, VgmFile.hFile);		// Data Address
			fwrite(data, 0x01, datasize, VgmFile.hFile);
			VgmFile.BytesWrt += 0x07 + finalsize;
			break;
		case 0x00:
			fwrite(&datasize, 0x04, 0x01, VgmFile.hFile);	// Data Block Size
			fwrite(data, 0x01, datasize, VgmFile.hFile);
			VgmFile.BytesWrt += 0x07 + datasize;
			break;
		case 0x40:
			if (type < 0x7F)
			{
				finalsize = datasize + 0x0A;
				fwrite(&finalsize, 0x04, 0x01, VgmFile.hFile);	// Data Block Size
				
				fputc(0x01, VgmFile.hFile);						// Compression Type: DPCM
				fwrite(&value1, 0x04, 0x01, VgmFile.hFile);		// uncompressed size
				
				fputc(8, VgmFile.hFile);						// Bits decompressed
				fputc(4, VgmFile.hFile);						// Bits compressed
				fputc(0x00, VgmFile.hFile);						// reserved
				TempSht = (UINT16)value2;
				fwrite(&TempSht, 0x02, 0x01, VgmFile.hFile);	// start value
				fwrite(data, 0x01, datasize, VgmFile.hFile);
				VgmFile.BytesWrt += 0x07 + finalsize;
			}
			else
			{
				finalsize = datasize + 0x06;
				fwrite(&finalsize, 0x04, 0x01, VgmFile.hFile);	// Data Block Size
				fputc(0x01, VgmFile.hFile);						// Compression Type: DPCM
				fputc(0x00, VgmFile.hFile);						// reserved/unused
				fputc(8, VgmFile.hFile);						// Bits decompressed
				fputc(4, VgmFile.hFile);						// Bits compressed
				TempSht = (UINT16)datasize;			// 8-bit values
				fwrite(&TempSht, 0x02, 0x01, VgmFile.hFile);	// number of values
				fwrite(data, 0x01, datasize, VgmFile.hFile);
				VgmFile.BytesWrt += 0x07 + finalsize;
			}
			break;
		}
	}
	else
	{
		fputc(0x01, VgmFile.hFile);	// write invalid data
	}
	
	return;
}

/*static void vgm_flush_pcm(void)
{
	UINT32 finalsize;
	
	if (PCMC_Start == 0xFFFFFFFF || ! PCMC_Pos)
		return;
	
	if (PCMC_Pos == 0x01)
	{
		// it would be a waste of space to write a data block for 1 byte of data
		fputc(0xC2, VgmFile.hFile);		// Write Memory
		fputc((PCMC_Start >> 0) & 0xFF, VgmFile.hFile);	// offset low
		fputc((PCMC_Start >> 8) & 0xFF, VgmFile.hFile);	// offset high
		fputc(PCMCache[0x00], VgmFile.hFile);		// Data
		VgmFile.BytesWrt += 0x04;
	}
	else
	{
		// calling vgm_write_large_data doesn't work if vgm_flush_pcm is
		// called from vgm_write_delay
		fputc(0x67, VgmFile.hFile);
		fputc(0x66, VgmFile.hFile);
		fputc(0xC1, VgmFile.hFile);
		finalsize = PCMC_Pos + 0x02;
		fwrite(&finalsize, 0x04, 0x01, VgmFile.hFile);	// Data Block Size
		fwrite(&PCMC_Start, 0x02, 0x01, VgmFile.hFile);	// Data Address
		fwrite(PCMCache, 0x01, PCMC_Pos, VgmFile.hFile);
		VgmFile.BytesWrt += 0x07 + finalsize;
	}
	
	PCMC_Start = 0xFFFFFFFF;
	
	return;
}*/

void vgm_write_stream_data_command(UINT8 stream, UINT8 type, UINT32 data)
{
	if (! VGM_Dumping)
		return;
	
	vgm_write_delay();
	
	switch(type)
	{
	case 0x00:	// chip setup
		fputc(0x90, VgmFile.hFile);
		fputc(stream, VgmFile.hFile);
		fputc((data & 0x00FF0000) >> 16, VgmFile.hFile);
		fputc((data & 0x0000FF00) >>  8, VgmFile.hFile);
		fputc((data & 0x000000FF) >>  0, VgmFile.hFile);
		VgmFile.BytesWrt += 0x05;
		break;
	case 0x01:	// data block setup
		fputc(0x91, VgmFile.hFile);
		fputc(stream, VgmFile.hFile);
		fputc(data & 0xFF, VgmFile.hFile);
		fputc(0x01, VgmFile.hFile);
		fputc(0x00, VgmFile.hFile);
		VgmFile.BytesWrt += 0x05;
		break;
	case 0x02:	// frequency setup
		if (DACFreq != data)
		{
			fputc(0x92, VgmFile.hFile);
			fputc(stream, VgmFile.hFile);
			fwrite(&data, 0x04, 0x01, VgmFile.hFile);
			VgmFile.BytesWrt += 0x06;
			
			DACFreq = data;
		}
		break;
	case 0x04:	// stop sample
		fputc(0x94, VgmFile.hFile);
		fputc(stream, VgmFile.hFile);
		VgmFile.BytesWrt += 0x02;
		break;
	case 0x05:	// start sample
		fputc(0x95, VgmFile.hFile);
		fputc(stream, VgmFile.hFile);
		fputc((data & 0x000000FF) >> 0, VgmFile.hFile);
		fputc((data & 0x0000FF00) >> 8, VgmFile.hFile);
		fputc((data & 0x00FF0000) >> 16, VgmFile.hFile);
		VgmFile.BytesWrt += 0x05;
		break;
	}
	WroteDAC = 0x01;
	
	return;
}

void vgm_set_loop(UINT8 SetLoop)
{
	if (! VGM_Dumping || VgmFile.hFile == NULL)
		return;
	
	if (SetLoop)
	{
		vgm_write_delay();
		
		VgmFile.Header.lngLoopOffset = VgmFile.BytesWrt;
		VgmFile.Header.lngLoopSamples = VgmFile.SmplsWrt;
		DACFreq = 0xFFFFFFFF;
	}
	else
	{
		VgmFile.Header.lngLoopOffset = 0x00;
		VgmFile.Header.lngLoopSamples = 0;
	}
	
	return;
}
