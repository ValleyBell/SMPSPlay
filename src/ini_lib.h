#ifndef __INI_LIB_H__
#define __INI_LIB_H__

#include <stdio.h>	// for FILE
#include <stdtype.h>	// for UINT32

// Functions used to parse the actual lines in .ini files
UINT8 GetTextLine(UINT32 BufSize, char* Buffer, FILE* hFile);
UINT8 GetSectionToken(char* Buffer, char** TokenL, char** TokenR);
UINT8 GetTokenPtrs(char* Buffer, char** TokenL, char** TokenR);
UINT8 GetNextToken_Tab(char** Buffer_TokenL, char** TokenR);
char* TrimToken(char* Buffer);
void RevertTokenTrim(char* TokenL, const char* TokenR);

void GetColumns(char* TextStr, UINT8 MaxCols, char** ColumnPtrs);
void GetColumns_Tab(char* TextStr, UINT8 MaxCols, char** ColumnPtrs);
UINT32 ParseNumber(char* Token, char** NextToken, char** RetParseEnd);
UINT8 GetBoolValue(const char* Token, const char* StrTrue, const char* StrFalse);

// Functions to handle paths in .ini-files
#ifdef GetFileTitle	// prevent it from clashing with the Windows API (where it renames it to GetFileTitleA/GetFileTitleW)
#undef GetFileTitle
#endif
void StandardizePath(char* Path);
UINT8 IsAbsolutePath(const char* Path);
UINT32 ConcatPath(UINT32* DstBufSize, char** DstBuf, const char* Path1, const char* Path2);
UINT32 CreatePath(UINT32* DstBufSize, char** DstBuf, const char* SrcPath);
char* GetFileTitle(const char* Path);
char* GetFileExtention(const char* Path);

UINT32 ReadHexData(const char* ArrayStr, UINT8** RetBuffer);
UINT32 ReadArrayData(char* ArrayStr, void** RetBuffer, UINT8 ValType, char** RetParseEnd);
UINT32 ReadMultilineArrayData(FILE* hFile, char* ArrayStr, void** RetBuffer, UINT8 ValType);

#endif	// __INI_LIB_H__
