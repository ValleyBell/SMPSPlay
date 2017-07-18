// Shared Routines for INI Loader
// ------------------------------
// Written by Valley Bell, 2014
#define _CRTDBG_MAP_ALLOC	// note: no effect in Release builds
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>	// for isspace()
#include <stdtype.h>
#include "ini_lib.h"

#ifdef _MSC_VER
#define stricmp		_stricmp
#else
#define stricmp		strcasecmp
#endif

UINT8 GetTextLine(UINT32 BufSize, char* Buffer, FILE* hFile)
{
	char* TempPtr;
	
	while(! feof(hFile))
	{
		TempPtr = fgets(Buffer, BufSize, hFile);
		if (TempPtr == NULL)
			break;
		if (Buffer[0x00] == '\n' || Buffer[0x00] == '\0')
			continue;
		
		if (Buffer[0x00] != ';')
		{
			// strip \n off of the current line
			TempPtr = Buffer + strlen(Buffer) - 1;
			while(TempPtr >= Buffer && *TempPtr < ' ')
			{
				*TempPtr = '\0';
				TempPtr --;
			}
			return 0x00;
		}
		
		// skip comment lines
		// fgets has set a null-terminator at char 0xFF
		while(Buffer[strlen(Buffer) - 1] != '\n')
		{
			TempPtr = fgets(Buffer, BufSize, hFile);
			if (TempPtr == NULL || Buffer[0x00] == '\0')
				break;
		}
	}
	
	return 0xFF;
}

UINT8 GetSectionToken(char* Buffer, char** TokenL, char** TokenR)
{
	char* TempPtr;
	
	if (Buffer[0x00] != '[')
		return 0xFF;
	
	// [Section]
	TempPtr = strchr(Buffer, ']');
	if (TempPtr == NULL)
		return 0x10;
	*TempPtr = '\0';
		
	*TokenL = Buffer;
	*TokenR = Buffer + 1;
	return 0x00;
}

UINT8 GetTokenPtrs(char* Buffer, char** TokenL, char** TokenR)
{
	UINT8 RetVal;
	char* SplitPnt;
	char* TempPtr;
	
	RetVal = GetSectionToken(Buffer, TokenL, TokenR);
	if (RetVal != 0xFF)
		return RetVal;
	
	// Option = value
	SplitPnt = strchr(Buffer, '=');
	if (SplitPnt == NULL || SplitPnt == Buffer)
		return 0x01;	// invalid line
	
	// split at '='
	*SplitPnt = '\0';
	
	// [right token] trim left spaces off
	TempPtr = SplitPnt + 1;
	while(*TempPtr != '\0' && isspace(*TempPtr))
		TempPtr ++;
	if (*TempPtr == ';')
		return 0x02;	// the value is a comment - invalid line
	*TokenR = TempPtr;
	
	// [left token] trim right spaces off
	TempPtr = SplitPnt - 1;
	while(TempPtr >= Buffer && isspace(*TempPtr))
		TempPtr --;
	TempPtr[1] = '\0';
	
	// [left token] now trim the left spaces off
	TempPtr = Buffer;
	while(*TempPtr != '\0' && isspace(*TempPtr))
		TempPtr ++;
	*TokenL = TempPtr;
	if (*TempPtr == '\0')
		return 0x03;	// left token empty - line invalid
	
	return 0x00;
}

UINT8 GetNextToken_Tab(char** Buffer_TokenL, char** TokenR)
{
	char* TempPtr;
	
	TempPtr = *Buffer_TokenL;
	while(*TempPtr == ' ')
		TempPtr ++;
	if (*TempPtr == ';')	// a comment char is equal to a line end
		*TempPtr = '\0';
	if (*TempPtr == '\0')
		return 0xFF;
	
	*Buffer_TokenL = TempPtr;
	while(*TempPtr != '\0' && *TempPtr != '\t')
		TempPtr ++;
	
	if (*TempPtr == '\0')	// line end reached
	{
		*TokenR = NULL;
	}
	else
	{
		*TempPtr = '\0';	// replace tab with string terminator
		*TokenR = TempPtr + 1;
		TrimToken(*Buffer_TokenL);
	}
	
	return 0x00;
}

char* TrimToken(char* Buffer)
{
	char* TempPtr;
	
	if (*Buffer == ';')
	{
		*Buffer = '\0';
		return NULL;
	}
	
	TempPtr = Buffer;
	while(*TempPtr != '\0' && ! isspace(*TempPtr))
		TempPtr ++;
	if (*TempPtr == '\0')	// reached line end
		return NULL;	// this is the last token on this line
	*TempPtr = '\0';
	
	return TempPtr + 1;
}

void RevertTokenTrim(char* TokenL, const char* TokenR)
{
	char* TempPtr;
	
	if (TokenR == NULL)
		return;
	
	TempPtr = TokenL;
	while(*TempPtr != '\0')
		TempPtr ++;
	while(*TempPtr == '\0' && TempPtr < TokenR)
	{
		*TempPtr = ' ';
		TempPtr ++;
	}
	
	return;
}

// This function splits the line into several columns. Columns are separated with whitespaces.
void GetColumns(char* TextStr, UINT8 MaxCols, char** ColumnPtrs)
{
	char* CurStr;
	UINT8 CurCol;
	
	CurStr = TextStr;
	for (CurCol = 0; CurCol < MaxCols; CurCol ++)
	{
		if (*CurStr == ';')
			*CurStr = '\0';
		if (*CurStr == '\0')
			break;
		
		ColumnPtrs[CurCol] = CurStr;
		while(*CurStr != '\0' && ! isspace(*CurStr))	// skip the word
			CurStr ++;
		while(isspace(*CurStr))		// skip the tab/space
			CurStr ++;
	}
	if (CurCol < MaxCols && *CurStr != '\0')
	{
		ColumnPtrs[CurCol] = CurStr;
		CurCol ++;
	}
	for (; CurCol < MaxCols; CurCol ++)
		ColumnPtrs[CurCol] = NULL;
	
	return;
}

// This function splits the line into several columns. This function splits only at tabs.
void GetColumns_Tab(char* TextStr, UINT8 MaxCols, char** ColumnPtrs)
{
	UINT8 CurCol;
	UINT8 RetVal;
	
	ColumnPtrs[0] = TextStr;
	for (CurCol = 1; CurCol < MaxCols; CurCol ++)
	{
		RetVal = GetNextToken_Tab(&ColumnPtrs[CurCol - 1], &ColumnPtrs[CurCol]);
		if (RetVal || ColumnPtrs[CurCol] == NULL)
			break;
	}
	if (CurCol < MaxCols)
	{
		for (; CurCol < MaxCols; CurCol ++)
			ColumnPtrs[CurCol] = NULL;
	}
	else
	{
		TrimToken(ColumnPtrs[CurCol - 1]);
	}
	
	return;
}

UINT32 ParseNumber(char* Token, char** NextToken, char** RetParseEnd)
{
	char* EndPtr;
	UINT8 Base;
	UINT8 Postfix;
	char* StartPtr;
	UINT32 ResVal;
	
	EndPtr = TrimToken(Token);
	if (NextToken != NULL)
		*NextToken = EndPtr;
	if (*Token == '\0')
	{
		if (*RetParseEnd != NULL)
			*RetParseEnd = Token;
		return 0;
	}
	EndPtr = Token + strlen(Token) - 1;
	
	Base = 10;
	Postfix = 0;
	if (*Token == '$')
	{
		Base = 0x10;
		StartPtr = Token + 1;
	}
	else if (Token[0] == '0' && tolower(Token[1]) == 'x')
	{
		Base = 0x10;
		StartPtr = Token + 2;
	}
	else if (tolower(*EndPtr) == 'h')
	{
		Base = 0x10;
		StartPtr = Token;
		Postfix = 1;
	}
	else
	{
		StartPtr = Token;
		Base = 0;
	}
	
	ResVal = strtoul(StartPtr, RetParseEnd, Base);
	if (RetParseEnd != NULL && Postfix && *RetParseEnd == EndPtr)	// catch 'h' postfix
		(*RetParseEnd) ++;
	return ResVal;
}

UINT8 GetBoolValue(const char* Token, const char* StrTrue, const char* StrFalse)
{
	if (! stricmp(Token, StrTrue))
		return 1;
	else if (! stricmp(Token, StrFalse))
		return 0;
	else
		return strtol(Token, NULL, 0) ? 1 : 0;
}


void StandardizePath(char* Path)
{
	char* CurChr;
	
	CurChr = Path;
	while(*CurChr != '\0')
	{
		if (*CurChr == '\\')
			*CurChr = '/';
		CurChr ++;
	}
	
	return;
}

UINT8 IsAbsolutePath(const char* Path)
{
	if (Path == NULL || Path[0] == '\0')
		return 0;
	
#ifdef WIN32
	if (Path[1] == ':')	// "C:\" (Note: At this point all \ should've been replaced with / already.)
		return 1;
	if (Path[0] == '/')
	{
		if (Path[1] == '/')
			return 1;	// Windows network path: "\\computer\folder\"
		else
			return 2;	// absolute path on same device
	}
#else
	if (Path[0] == '/')
		return 1;		// absolute Unix path: "/foo/bar/"
#endif
	
	return 0;
}

// Note:
//	- Pass DstBuf == NULL to get the necessary size of the buffer.
//  - You mustn't call it with DstBufSize != NULL and DstBuf == NULL.
UINT32 ConcatPath(UINT32* DstBufSize, char** DstBuf, const char* Path1, const char* Path2)
{
	size_t Path1Len;
	size_t Path2Len;
	size_t Path2Start;
	UINT8 Path2Abs;
	size_t FnlPathAlloc;
	
	Path1Len = strlen(Path1);
	Path2Len = strlen(Path2);
	Path2Abs = IsAbsolutePath(Path2);
	if (Path2Abs == 0)
	{
		Path2Start = Path1Len;
	}
	else if (Path2Abs == 1)
	{
		Path2Start = 0;
	}
	else
	{
		if (Path1[1] == ':')	// Path incl. device?
			Path2Start = 2;	// leave "C:" and add "\abc\"
		else
			Path2Start = 0;	// overwrite path completely
	}
	FnlPathAlloc = Path2Start + Path2Len + 1;
	
	// Note to myself: making the caller-function responsible would've reduced the complexity A LOT.
	if (DstBufSize != NULL)
	{
		if (*DstBuf == NULL || *DstBufSize < FnlPathAlloc)
		{
			*DstBuf = (char*)realloc(*DstBuf, FnlPathAlloc);
			*DstBufSize = (UINT32)FnlPathAlloc;
		}
	}
	if (DstBuf != NULL)
	{
		if (*DstBuf == NULL)	// If DstBufSize == NULL && *DstBuf == NULL, allocate some space.
			*DstBuf = (char*)malloc(FnlPathAlloc);
		strncpy(*DstBuf, Path1, Path2Start);
		strcpy(*DstBuf + Path2Start, Path2);
	}
	
	return (UINT32)(Path2Start + Path2Len);
}

// create path from folder name, returning the final path length
// Examples:
//	data -> data/
//	data/ -> data/
//	./ -> [empty string]
UINT32 CreatePath(UINT32* DstBufSize, char** DstBuf, const char* SrcPath)
{
	// Note:
	//	- *DstBuf == NULL -> buffer will be allocated
	//	- *DstBuf != NULL -> buffer must be large enough to contain the full path
	//	                     (up to strlen(SrcPath)+1 characters)
	char* PathStr;
	char* CurChr;
	size_t MinBufSize;
	
	if (SrcPath == NULL)
		SrcPath = "";
	
	MinBufSize = strlen(SrcPath) + 2;
	if (*DstBuf == NULL || (DstBufSize != NULL && *DstBufSize < MinBufSize))
	{
		*DstBuf = (char*)realloc(*DstBuf, MinBufSize * sizeof(char));
		if (DstBufSize != NULL)
			*DstBufSize = (UINT32)MinBufSize;
	}
	PathStr = *DstBuf;
	
	strcpy(PathStr, SrcPath);
	if (PathStr[0] == '\0')
		return 0;
	
	// standardize path separators (\ -> /)
	// Note: I don't use StandardizePath() because I can reuse CurChr this way.
	CurChr = PathStr;
	while(*CurChr != '\0')
	{
		if (*CurChr == '\\')
			*CurChr = '/';
		CurChr ++;
	}
	
	// check for / at the end and add it if necessary
	if (CurChr[-1] != '/')
	{
		*CurChr = '/';
		CurChr ++;
		*CurChr = '\0';
	}
	
	// Path "./" equals ""
	if (! strcmp(PathStr, "./"))
	{
		strcpy(PathStr, "");
		return 0;
	}
	
	return (UINT32)(CurChr - PathStr);	// quick version of strlen(PathStr)
}

char* GetFileTitle(const char* Path)
{
	// Note: Path separators MUST be '/', '\'
	char* LastSlash;
	
	LastSlash = strrchr(Path, '/');
	if (LastSlash == NULL)
		return (char*)Path;	// behave like the ISO C
	else
		return LastSlash + 1;
}

char* GetFileExtention(const char* Path)
{
	char* FileTitle;
	char* LastSlash;
	
	FileTitle = GetFileTitle(Path);	// ensure we don't find a dot in the path
	LastSlash = strrchr(FileTitle, '.');
	if (LastSlash == NULL)
		return (char*)Path + strlen(Path);
	else
		return LastSlash + 1;
}

UINT32 ReadHexData(const char* ArrayStr, UINT8** RetBuffer)
{
	const char* StrPos;
	char NumStr[3];
	UINT32 CurNum;
	UINT32 ArrAlloc;
	UINT8* DataArr;
	
	ArrAlloc = 0x10;
	DataArr = (UINT8*)malloc(ArrAlloc);
	
	NumStr[2] = '\0';
	StrPos = ArrayStr;
	CurNum = 0x00;
	while(*StrPos != '\0')
	{
		// skip spaces
		while(isspace(*StrPos))
			StrPos ++;
		if (*StrPos == '\0' || *StrPos == ';')
			break;
		
		NumStr[0] = *StrPos;	// first digit
		StrPos ++;
		if (! isspace(*StrPos) && *StrPos != '\0')	// if second character was no space, then it's a 2-digit number
		{
			NumStr[1] = *StrPos;	// copy second digit
			StrPos ++;
		}
		else
		{
			NumStr[1] = '\0';
		}
		
		if (CurNum >= ArrAlloc)
		{
			ArrAlloc *= 2;
			DataArr = (UINT8*)realloc(DataArr, ArrAlloc);
		}
		DataArr[CurNum] = (UINT8)strtoul(NumStr, NULL, 0x10);
		CurNum ++;
	}
	if (! CurNum)
	{
		free(DataArr);
		return 0x00;
	}
	
	if (CurNum < ArrAlloc)
		*RetBuffer = (UINT8*)realloc(DataArr, CurNum);	// realloc to save potentially wasted space
	else
		*RetBuffer = DataArr;
	return CurNum;
}


UINT32 ReadArrayData(char* ArrayStr, void** RetBuffer, UINT8 ValType, char** RetParseEnd)
{
	// ValType values:
	//	0x01 - UINT8
	//	0x02 - UINT16
	//	0x04 - UINT32
	char* CurToken;
	char* NxtToken;
	char* ParseEnd;
	UINT32 CurNum;
	UINT32 ArrSize;
	UINT32 ArrAlloc;
	UINT32* DataArr;
	UINT32 RetVal;
	
	CurToken = ArrayStr;
	while(*CurToken != '\0' && isspace(*CurToken))
		CurToken ++;
	if (*CurToken == '\0')
	{
		*RetBuffer = NULL;
		if (RetParseEnd != NULL)
			*RetParseEnd = CurToken;
		return 0x00;
	}
	
	ArrAlloc = 0x10;
	DataArr = (UINT32*)malloc(ArrAlloc * sizeof(UINT32));
	
	CurNum = 0x00;
	while(CurToken != NULL)
	{
		RetVal = ParseNumber(CurToken, &NxtToken, &ParseEnd);
		if (ParseEnd == CurToken)	// couldn't parse this number or reached end of line
			break;
		
		if (CurNum >= ArrAlloc)
		{
			ArrAlloc *= 2;
			DataArr = (UINT32*)realloc(DataArr, ArrAlloc * sizeof(UINT32));
		}
		DataArr[CurNum] = RetVal;
		CurNum ++;
		CurToken = NxtToken;
	}
	ArrSize = CurNum;
	if (RetParseEnd != NULL)
		*RetParseEnd = CurToken;
	if (! ArrSize)
	{
		free(DataArr);
		*RetBuffer = NULL;
		return 0x00;
	}
	
	switch(ValType)
	{
	case 0x01:
		{
			UINT8* RetData;
			
			RetData = (UINT8*)malloc(ArrSize * sizeof(UINT8));
			for (CurNum = 0x00; CurNum < ArrSize; CurNum ++)
				RetData[CurNum] = (UINT8)DataArr[CurNum];
			*RetBuffer = RetData;
		}
		free(DataArr);
		break;
	case 0x02:
		{
			UINT16* RetData;
			
			RetData = (UINT16*)malloc(ArrSize * sizeof(UINT16));
			for (CurNum = 0x00; CurNum < ArrSize; CurNum ++)
				RetData[CurNum] = (UINT16)DataArr[CurNum];
			*RetBuffer = RetData;
		}
		free(DataArr);
		break;
	case 0x04:
		if (CurNum < ArrAlloc)
			*RetBuffer = (UINT32*)realloc(DataArr, ArrSize * sizeof(UINT32));	// realloc to save potentially wasted space
		else
			*RetBuffer = DataArr;
		break;
	default:
		free(DataArr);
		*RetBuffer = NULL;
		return 0x00;
	}
	return ArrSize;
}


UINT32 ReadMultilineArrayData(FILE* hFile, char* ArrayStr, void** RetBuffer, UINT8 ValType)
{
	char LineStr[0x100];
	UINT32 ArrAlloc;
	UINT32 ArrCount;
	UINT32* ValCount;
	void** ValData;
	char* LToken;
	char* RToken;
	void* RetData;
	UINT32 RetSize;
	UINT32 TotalSize;
	UINT32 CurVal;
	UINT8 RetVal;
	
	LToken = ArrayStr;
	while(*LToken != '\0' && isspace(*LToken))
		LToken ++;
	if (*LToken == '\0')
	{
		*RetBuffer = NULL;
		return 0x00;
	}
	
	if (*LToken != '{')
		return ReadArrayData(ArrayStr, RetBuffer, ValType, NULL);
	LToken ++;
	
	ArrAlloc = 0x10;
	ValCount = (UINT32*)malloc(ArrAlloc * sizeof(UINT32));
	ValData = (void**)malloc(ArrAlloc * sizeof(void*));
	
	TotalSize = 0;
	ArrCount = 0;
	
	RetSize = ReadArrayData(LToken, &RetData, ValType, &RToken);
	if (RetSize)	// The rest of the line might be empty.
	{
		ValCount[ArrCount] = RetSize;
		ValData[ArrCount] = RetData;
		TotalSize += RetSize;
		ArrCount ++;
	}
	while(! (RToken != NULL && *RToken == '}') && ! feof(hFile))
	{
		RetVal = GetTextLine(0x100, LineStr, hFile);
		if (RetVal)
			break;
		RetSize = ReadArrayData(LineStr, &RetData, ValType, &RToken);
		if (! RetSize)
			break;
		
		if (ArrCount >= ArrAlloc)
		{
			ArrAlloc += 0x10;
			ValCount = (UINT32*)realloc(ValCount, ArrAlloc * sizeof(UINT32));
			ValData = (void**)realloc(ValData, ArrAlloc * sizeof(void*));
		}
		ValCount[ArrCount] = RetSize;
		ValData[ArrCount] = RetData;
		TotalSize += RetSize;
		ArrCount ++;
	}
	
	RetData = malloc(TotalSize * ValType);
	RetSize = 0;
	for (CurVal = 0; CurVal < ArrCount; CurVal ++)
	{
		memcpy((UINT8*)RetData + RetSize, ValData[CurVal], ValCount[CurVal] * ValType);
		RetSize += ValCount[CurVal] * ValType;
		free(ValData[CurVal]);
	}
	free(ValCount);
	free(ValData);
	
	*RetBuffer = RetData;
	return TotalSize;
}
